#include "bootstrap.hpp"
#include "utils.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>

namespace fglibc {

static const std::string CORE_MIRROR  = "https://geo.mirror.pkgbuild.com/core/os/x86_64/";
static const std::string EXTRA_MIRROR = "https://geo.mirror.pkgbuild.com/extra/os/x86_64/";

static std::string fetch_pkg_filename(const std::string &mirror, const std::string &prefix) {
    std::string cmd = "curl -sL \"" + mirror + "\" | grep -oE 'href=\"" + prefix + "-[0-9][^\"]*\\.pkg\\.tar\\.zst\"' | head -n 1 | sed -E 's/href=\"([^\"]+)\"/\\1/'";
    std::string out = utils::run_command_output(cmd);
    return out;
}

static bool download_and_extract_arch_pkg(const Config &cfg, const std::string &mirror, const std::string &pkg_name) {
    utils::log_info("Resolving package " + pkg_name + "...");
    std::string filename = fetch_pkg_filename(mirror, pkg_name);
    if (filename.empty()) {
        utils::log_error("Could not find package " + pkg_name + " on " + mirror);
        return false;
    }

    std::string url = mirror + filename;
    std::string local_path = cfg.cache_dir + "/" + filename;

    if (!utils::file_exists(local_path)) {
        if (!utils::download_file(url, local_path)) {
            return false;
        }
    } else {
        utils::log_info("Using cached package: " + filename);
    }

    if (!utils::extract_archive(local_path, cfg.sysroot_dir)) {
        utils::log_error("Failed to extract package " + filename);
        return false;
    }

    return true;
}

bool BootstrapManager::ensure_initialized(const Config &cfg) {
    if (cfg.is_prefix_ready()) {
        return true;
    }

    utils::log_warn("Prefix at " + cfg.prefix_dir + " is not initialized or incomplete.");
    std::cout << utils::colors::BOLD << "Would you like to initialize the glibc runtime now? [Y/n]: " << utils::colors::RESET;
    std::string ans;
    std::getline(std::cin, ans);
    if (!ans.empty() && ans != "y" && ans != "Y" && ans != "yes" && ans != "Yes") {
        utils::log_error("Aborted by user.");
        return false;
    }

    return init_prefix(cfg, false);
}

bool BootstrapManager::init_prefix(const Config &cfg, bool install_gaming_libs) {
    using namespace utils::colors;

    std::cout << BOLD << "\n============================================\n"
              << "       fglibc Runtime Initializer           \n"
              << "============================================\n" << RESET;

    utils::log_info("Initializing glibc prefix at: " + cfg.prefix_dir);
    cfg.ensure_prefix_dirs();

    // 1. Download and extract glibc
    utils::log_info("Step 1/3: Installing GNU C Library (glibc)...");
    if (!download_and_extract_arch_pkg(cfg, CORE_MIRROR, "glibc")) {
        utils::log_error("Failed to install glibc!");
        return false;
    }

    // 2. Download and extract libgcc
    utils::log_info("Step 2/3: Installing GCC runtime (libgcc)...");
    if (!download_and_extract_arch_pkg(cfg, CORE_MIRROR, "libgcc")) {
        utils::log_error("Failed to install libgcc!");
        return false;
    }

    // 3. Download and extract libstdc++
    utils::log_info("Step 3/3: Installing C++ Standard Library (libstdc++)...");
    if (!download_and_extract_arch_pkg(cfg, CORE_MIRROR, "libstdc(%2B%2B|\\+\\+)")) {
        utils::log_error("Failed to install libstdc++!");
        return false;
    }

    // Setup symlinks
    utils::log_info("Configuring library paths and symlinks...");
    std::string s_lib64 = cfg.sysroot_dir + "/lib64";
    std::string s_lib   = cfg.sysroot_dir + "/lib";
    std::string s_ulib64= cfg.sysroot_dir + "/usr/lib64";

    symlink("usr/lib", s_lib64.c_str());
    symlink("usr/lib", s_lib.c_str());
    symlink("lib", s_ulib64.c_str());

    // Setup /etc/ld.so.conf
    std::string ld_conf = cfg.sysroot_dir + "/etc/ld.so.conf";
    std::string conf_content =
        "/usr/lib\n"
        "/lib64\n"
        "/lib\n"
        "/usr/local/lib\n";
    utils::write_text_file(ld_conf, conf_content);

    // Rebuild ld.so.cache
    rebuild_ld_cache(cfg);

    if (install_gaming_libs) {
        install_gaming_libraries(cfg);
    }

    std::string ver = cfg.get_glibc_version();
    utils::log_success("Prefix initialized successfully!");
    utils::log_success("Installed GLIBC: " + ver);

    return true;
}

bool BootstrapManager::install_gaming_libraries(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n=== Installing Gaming & Multimedia Libraries ===" << RESET << "\n";
    utils::log_info("Installing Vulkan, OpenGL (libglvnd), X11, and SDL2/3...");

    bool ok = true;
    // Core dependencies
    ok &= download_and_extract_arch_pkg(cfg, CORE_MIRROR, "libffi");
    ok &= download_and_extract_arch_pkg(cfg, CORE_MIRROR, "libedit");
    ok &= download_and_extract_arch_pkg(cfg, CORE_MIRROR, "ncurses");
    ok &= download_and_extract_arch_pkg(cfg, CORE_MIRROR, "icu");
    ok &= download_and_extract_arch_pkg(cfg, CORE_MIRROR, "libxml2");

    // X11 stack
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxau");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxdmcp");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxcb");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libx11");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxext");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxfixes");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxrender");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxrandr");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxinerama");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxcursor");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxi");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxxf86vm");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxshmfence");

    // Wayland & input
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "wayland");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libxkbcommon");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libdecor");

    // Hardware graphics, Mesa, and Vulkan
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libpciaccess");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "mesa");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "libglvnd");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "vulkan-icd-loader");

    // SDL
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "sdl3");
    ok &= download_and_extract_arch_pkg(cfg, EXTRA_MIRROR, "sdl2-compat");

    rebuild_ld_cache(cfg);

    if (ok) {
        utils::log_success("Gaming libraries installed successfully!");
    } else {
        utils::log_warn("Some gaming packages encountered errors during installation.");
    }
    return ok;
}

bool BootstrapManager::install_custom_archive(const Config &cfg, const std::string &archive_path) {
    if (!utils::file_exists(archive_path)) {
        utils::log_error("Archive file not found: " + archive_path);
        return false;
    }
    utils::log_info("Installing custom archive: " + archive_path);
    bool ok = utils::extract_archive(archive_path, cfg.sysroot_dir);
    if (ok) {
        rebuild_ld_cache(cfg);
        utils::log_success("Archive installed successfully!");
    }
    return ok;
}

bool BootstrapManager::rebuild_ld_cache(const Config &cfg) {
    std::string ld_conf = cfg.sysroot_dir + "/etc/ld.so.conf";
    std::string existing = utils::read_text_file(ld_conf);
    if (existing.find("/usr/lib32") == std::string::npos) {
        std::ofstream ofs(ld_conf, std::ios::app);
        if (ofs.is_open()) {
            ofs << "\n/usr/lib32\n/lib32\n";
        }
    }
    if (existing.find("/usr/lib/pulseaudio") == std::string::npos) {
        std::ofstream ofs(ld_conf, std::ios::app);
        if (ofs.is_open()) {
            ofs << "/usr/lib/pulseaudio\n";
        }
    }

    std::string ldconfig_path = cfg.sysroot_dir + "/usr/bin/ldconfig";
    std::string ld_so = cfg.get_ld_linux_path();
    if (utils::file_exists(ldconfig_path) && utils::file_exists(ld_so)) {
        std::string cmd = "\"" + ld_so + "\" --library-path \"" + cfg.sysroot_dir + "/usr/lib\" \"" + ldconfig_path + "\" -r \"" + cfg.sysroot_dir + "\" 2>&1";
        utils::run_command(cmd, false);
        return true;
    }
    return false;
}

void BootstrapManager::print_status(const Config &cfg) {
    using namespace utils::colors;

    std::cout << BOLD << "\n============================================\n"
              << "             fglibc Status Report           \n"
              << "============================================\n" << RESET;

    std::cout << "Prefix Location:  " << CYAN << cfg.prefix_dir << RESET << "\n";
    std::cout << "Sysroot Status:   ";
    if (cfg.is_prefix_ready()) {
        std::cout << GREEN << BOLD << "Ready" << RESET << "\n";
        std::cout << "GLIBC Version:    " << GREEN << cfg.get_glibc_version() << RESET << "\n";
        std::cout << "Dynamic Linker:   " << cfg.get_ld_linux_path() << "\n";
    } else {
        std::cout << RED << BOLD << "Not Initialized (Run 'fglibc init')" << RESET << "\n";
    }

    std::cout << "\nHost Subsystems Passthrough:\n";

    // Graphics / DRI
    std::cout << "  - GPU / DRI Acceleration: ";
    if (utils::dir_exists("/dev/dri")) {
        std::cout << GREEN << "Available (/dev/dri found: ";
        if (utils::file_exists("/dev/dri/renderD128")) std::cout << "renderD128 ";
        if (utils::file_exists("/dev/dri/card0")) std::cout << "card0";
        std::cout << ")" << RESET << "\n";
    } else {
        std::cout << YELLOW << "No /dev/dri found (Software rendering)" << RESET << "\n";
    }

    // Display
    std::cout << "  - X11 Display Server:     ";
    const char *disp = getenv("DISPLAY");
    if (disp && utils::file_exists("/tmp/.X11-unix")) {
        std::cout << GREEN << "Connected (" << disp << ")" << RESET << "\n";
    } else {
        std::cout << YELLOW << "Not active" << RESET << "\n";
    }

    std::cout << "  - Wayland Display Server: ";
    const char *wdisp = getenv("WAYLAND_DISPLAY");
    if (wdisp) {
        std::cout << GREEN << "Connected (" << wdisp << ")" << RESET << "\n";
    } else {
        std::cout << YELLOW << "Not active" << RESET << "\n";
    }

    // Audio
    std::cout << "  - Sound / Audio:          ";
    const char *pulse = getenv("PULSE_SERVER");
    if (pulse || utils::dir_exists("/dev/snd")) {
        std::cout << GREEN << "Available (";
        if (pulse) std::cout << "PulseAudio/PipeWire: " << pulse << " ";
        if (utils::dir_exists("/dev/snd")) std::cout << "ALSA devices";
        std::cout << ")" << RESET << "\n";
    } else {
        std::cout << YELLOW << "No audio devices detected" << RESET << "\n";
    }

    // Gamepads / Input
    std::cout << "  - Input Controllers:      ";
    if (utils::dir_exists("/dev/input")) {
        std::cout << GREEN << "Available (/dev/input)" << RESET << "\n";
    } else {
        std::cout << YELLOW << "No /dev/input" << RESET << "\n";
    }

    std::cout << "\nEngine Selected: " << CYAN;
    switch (cfg.engine) {
        case EngineType::NAMESPACE: std::cout << "Namespace (PivotRoot + OverlayFS) [Default]"; break;
        case EngineType::BWRAP:     std::cout << "Bubblewrap (bwrap)"; break;
        case EngineType::DIRECT:    std::cout << "Direct Linker (ld-linux)"; break;
    }
    std::cout << RESET << "\n\n";
}

} // namespace fglibc
