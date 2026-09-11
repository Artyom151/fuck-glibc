#pragma once

#include <string>
#include <vector>
#include <map>
#include "config.hpp"

namespace fglibc {

struct ElfLibDependency {
    std::string name;
    bool found_in_prefix = false;
    std::string resolved_path;
};

struct ElfInfo {
    std::string filepath;
    bool valid = false;
    bool is_64bit = false;
    std::string arch;
    std::string elf_type;
    std::string interpreter; // PT_INTERP
    bool is_glibc = false;
    bool is_musl = false;
    bool is_static = false;

    std::vector<std::string> rpaths;
    std::vector<std::string> runpaths;
    std::vector<std::string> needed_libs;
    std::vector<std::string> glibc_versions_required;
    std::vector<ElfLibDependency> resolved_dependencies;

    void print_summary(const Config &cfg) const;
};

class ElfParser {
public:
    static ElfInfo parse(const std::string &filepath, const Config &cfg);
};

} // namespace fglibc
