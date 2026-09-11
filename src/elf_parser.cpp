#include "elf_parser.hpp"
#include "utils.hpp"
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <iomanip>
#include <algorithm>

namespace fglibc {

ElfInfo ElfParser::parse(const std::string &filepath, const Config &cfg) {
    ElfInfo info;
    info.filepath = filepath;
    info.valid = false;

    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        return info;
    }

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(Elf64_Ehdr)) {
        close(fd);
        return info;
    }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        return info;
    }

    const uint8_t *bytes = static_cast<const uint8_t *>(mapped);
    if (bytes[EI_MAG0] != ELFMAG0 || bytes[EI_MAG1] != ELFMAG1 ||
        bytes[EI_MAG2] != ELFMAG2 || bytes[EI_MAG3] != ELFMAG3) {
        munmap(mapped, st.st_size);
        return info;
    }

    info.valid = true;
    info.is_64bit = (bytes[EI_CLASS] == ELFCLASS64);

    if (info.is_64bit) {
        const Elf64_Ehdr *ehdr = reinterpret_cast<const Elf64_Ehdr *>(bytes);
        switch (ehdr->e_machine) {
            case EM_X86_64:  info.arch = "x86_64"; break;
            case EM_AARCH64: info.arch = "aarch64"; break;
            case EM_386:     info.arch = "x86 (32-bit)"; break;
            case EM_ARM:     info.arch = "arm (32-bit)"; break;
            case EM_RISCV:   info.arch = "riscv64"; break;
            default:         info.arch = "machine_" + std::to_string(ehdr->e_machine); break;
        }

        switch (ehdr->e_type) {
            case ET_EXEC: info.elf_type = "Executable (ET_EXEC)"; break;
            case ET_DYN:  info.elf_type = "Shared Object / PIE Executable (ET_DYN)"; break;
            case ET_REL:  info.elf_type = "Relocatable Object (ET_REL)"; break;
            default:      info.elf_type = "Type " + std::to_string(ehdr->e_type); break;
        }

        // Program headers
        const Elf64_Phdr *phdr = reinterpret_cast<const Elf64_Phdr *>(bytes + ehdr->e_phoff);
        const Elf64_Phdr *dynamic_phdr = nullptr;

        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_INTERP) {
                if (phdr[i].p_offset + phdr[i].p_filesz <= (size_t)st.st_size) {
                    info.interpreter = std::string(reinterpret_cast<const char *>(bytes + phdr[i].p_offset));
                }
            } else if (phdr[i].p_type == PT_DYNAMIC) {
                dynamic_phdr = &phdr[i];
            }
        }

        // Check interpreter type
        if (info.interpreter.empty()) {
            info.is_static = true;
        } else if (info.interpreter.find("ld-linux") != std::string::npos) {
            info.is_glibc = true;
        } else if (info.interpreter.find("ld-musl") != std::string::npos) {
            info.is_musl = true;
        }

        // Parse PT_DYNAMIC to find DT_NEEDED and DT_STRTAB
        if (dynamic_phdr && dynamic_phdr->p_offset < (size_t)st.st_size) {
            const Elf64_Dyn *dyn = reinterpret_cast<const Elf64_Dyn *>(bytes + dynamic_phdr->p_offset);
            uint64_t strtab_offset = 0;
            std::vector<uint64_t> needed_offsets;
            std::vector<uint64_t> rpath_offsets;
            std::vector<uint64_t> runpath_offsets;

            // Section headers might give direct file offset of .dynstr
            if (ehdr->e_shoff > 0 && ehdr->e_shnum > 0) {
                const Elf64_Shdr *shdr = reinterpret_cast<const Elf64_Shdr *>(bytes + ehdr->e_shoff);
                for (int i = 0; i < ehdr->e_shnum; i++) {
                    if (shdr[i].sh_type == SHT_STRTAB && i != ehdr->e_shstrndx) {
                        // Check if this string table corresponds to dynamic symbols
                        // Usually named .dynstr
                        strtab_offset = shdr[i].sh_offset;
                        break;
                    }
                }
            }

            for (size_t i = 0; dyn[i].d_tag != DT_NULL; i++) {
                if (dyn[i].d_tag == DT_NEEDED) {
                    needed_offsets.push_back(dyn[i].d_un.d_val);
                } else if (dyn[i].d_tag == DT_RPATH) {
                    rpath_offsets.push_back(dyn[i].d_un.d_val);
                } else if (dyn[i].d_tag == DT_RUNPATH) {
                    runpath_offsets.push_back(dyn[i].d_un.d_val);
                }
            }

            if (strtab_offset > 0 && strtab_offset < (size_t)st.st_size) {
                const char *strtab = reinterpret_cast<const char *>(bytes + strtab_offset);
                for (uint64_t off : needed_offsets) {
                    if (strtab_offset + off < (size_t)st.st_size) {
                        info.needed_libs.push_back(strtab + off);
                    }
                }
                for (uint64_t off : rpath_offsets) {
                    if (strtab_offset + off < (size_t)st.st_size) {
                        info.rpaths.push_back(strtab + off);
                    }
                }
                for (uint64_t off : runpath_offsets) {
                    if (strtab_offset + off < (size_t)st.st_size) {
                        info.runpaths.push_back(strtab + off);
                    }
                }
            }
        }

        // Check for GLIBC version strings in .gnu.version_r or string table
        if (ehdr->e_shoff > 0 && ehdr->e_shnum > 0) {
            const Elf64_Shdr *shdr = reinterpret_cast<const Elf64_Shdr *>(bytes + ehdr->e_shoff);
            for (int i = 0; i < ehdr->e_shnum; i++) {
                if (shdr[i].sh_type == SHT_GNU_verneed) {
                    const Elf64_Verneed *vn = reinterpret_cast<const Elf64_Verneed *>(bytes + shdr[i].sh_offset);
                    const Elf64_Shdr *strtab_sh = &shdr[shdr[i].sh_link];
                    const char *vstrings = reinterpret_cast<const char *>(bytes + strtab_sh->sh_offset);

                    const uint8_t *curr = reinterpret_cast<const uint8_t *>(vn);
                    while (curr < bytes + shdr[i].sh_offset + shdr[i].sh_size) {
                        const Elf64_Verneed *v = reinterpret_cast<const Elf64_Verneed *>(curr);
                        const uint8_t *aux_curr = curr + v->vn_aux;
                        for (int j = 0; j < v->vn_cnt; j++) {
                            const Elf64_Vernaux *aux = reinterpret_cast<const Elf64_Vernaux *>(aux_curr);
                            std::string vname = vstrings + aux->vna_name;
                            if (std::find(info.glibc_versions_required.begin(),
                                          info.glibc_versions_required.end(),
                                          vname) == info.glibc_versions_required.end()) {
                                info.glibc_versions_required.push_back(vname);
                            }
                            if (aux->vna_next == 0) break;
                            aux_curr += aux->vna_next;
                        }
                        if (v->vn_next == 0) break;
                        curr += v->vn_next;
                    }
                }
            }
        }
    }

    munmap(mapped, st.st_size);

    // Resolve dependencies in glibc sysroot
    std::vector<std::string> search_dirs = {
        cfg.sysroot_dir + "/usr/lib",
        cfg.sysroot_dir + "/lib64",
        cfg.sysroot_dir + "/lib",
        cfg.sysroot_dir + "/usr/local/lib",
        cfg.sysroot_dir + "/usr/lib32",
        cfg.sysroot_dir + "/lib32",
        cfg.sysroot_dir + "/usr/lib/pulseaudio"
    };

    // Also include the binary's directory itself (for $ORIGIN libraries like electron/libffmpeg.so)
    size_t last_slash = filepath.rfind('/');
    if (last_slash != std::string::npos) {
        std::string bin_dir = filepath.substr(0, last_slash);
        search_dirs.push_back(bin_dir);
    }

    for (const auto &lib : info.needed_libs) {
        ElfLibDependency dep;
        dep.name = lib;
        dep.found_in_prefix = false;

        for (const auto &dir : search_dirs) {
            std::string candidate = dir + "/" + lib;
            if (utils::file_exists(candidate)) {
                dep.found_in_prefix = true;
                dep.resolved_path = candidate;
                break;
            }
        }
        info.resolved_dependencies.push_back(dep);
    }

    return info;
}

void ElfInfo::print_summary([[maybe_unused]] const Config &cfg) const {
    using namespace utils::colors;

    std::cout << BOLD << "\n=== fglibc ELF Inspection Report ===" << RESET << "\n";
    std::cout << "File: " << CYAN << filepath << RESET << "\n";
    std::cout << "Arch: " << WHITE << arch << (is_64bit ? " (64-bit)" : " (32-bit)") << RESET << "\n";
    std::cout << "Type: " << elf_type << "\n";

    std::cout << "Interpreter (PT_INTERP): ";
    if (interpreter.empty()) {
        std::cout << YELLOW << "None (Statically Linked Binary)" << RESET << "\n";
    } else {
        std::cout << CYAN << interpreter << RESET;
        if (is_glibc) {
            std::cout << GREEN << " [GLIBC - Target binary requires fglibc!]" << RESET << "\n";
        } else if (is_musl) {
            std::cout << BLUE << " [MUSL - Native Alpine binary]" << RESET << "\n";
        } else {
            std::cout << YELLOW << " [Custom]" << RESET << "\n";
        }
    }

    if (!glibc_versions_required.empty()) {
        std::cout << "\nRequired Symbol Versions:\n";
        for (const auto &v : glibc_versions_required) {
            std::cout << "  - " << MAGENTA << v << RESET << "\n";
        }
    }

    std::cout << "\nShared Library Dependencies (" << needed_libs.size() << " total):\n";
    size_t missing_count = 0;
    for (const auto &dep : resolved_dependencies) {
        if (dep.found_in_prefix) {
            std::cout << "  " << GREEN << "✔ " << dep.name << RESET << " -> " << dep.resolved_path << "\n";
        } else {
            missing_count++;
            std::cout << "  " << RED << "✘ " << dep.name << RESET << " -> " << YELLOW << "MISSING in fglibc prefix" << RESET << "\n";
        }
    }

    std::cout << "\nStatus: ";
    if (missing_count == 0) {
        std::cout << GREEN << BOLD << "All dependencies satisfied in fglibc prefix! Ready to run." << RESET << "\n\n";
    } else {
        std::cout << RED << BOLD << missing_count << " dependencies missing in prefix!" << RESET << "\n";
        std::cout << "Tip: If this is a game, run: " << CYAN << "fglibc install gaming" << RESET
                  << " to install graphics & audio runtimes.\n\n";
    }
}

} // namespace fglibc
