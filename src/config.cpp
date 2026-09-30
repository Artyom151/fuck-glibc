#include "config.hpp"
#include "utils.hpp"
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

namespace fglibc {

Config Config::load_from_env() {
    Config cfg;

    const char *p = getenv("FGLIBC_PREFIX");
    if (p && *p) {
        cfg.prefix_dir = utils::resolve_path(p);
    } else {
        cfg.prefix_dir = utils::get_home_dir() + "/.fglibc";
    }

    cfg.sysroot_dir        = cfg.prefix_dir + "/sysroot";
    cfg.overlay_upper_dir  = cfg.prefix_dir + "/upper";
    cfg.overlay_work_dir   = cfg.prefix_dir + "/work";
    cfg.cache_dir          = cfg.prefix_dir + "/cache";

    const char *eng = getenv("FGLIBC_ENGINE");
    if (eng) {
        std::string s(eng);
        if (s == "bwrap") {
            cfg.engine = EngineType::BWRAP;
        } else if (s == "direct") {
            cfg.engine = EngineType::DIRECT;
        } else {
            cfg.engine = EngineType::NAMESPACE;
        }
    }

    if (getenv("FGLIBC_DEBUG") != nullptr) {
        cfg.debug = true;
    }

    if (getenv("FGLIBC_NO_X11") != nullptr) {
        cfg.passthrough_x11 = false;
    }
    if (getenv("FGLIBC_NO_WAYLAND") != nullptr) {
        cfg.passthrough_wayland = false;
    }
    if (getenv("FGLIBC_NO_AUDIO") != nullptr) {
        cfg.passthrough_audio = false;
    }
    if (getenv("FGLIBC_NO_DRI") != nullptr) {
        cfg.passthrough_dri = false;
    }

    const char *lp = getenv("FGLIBC_LIBRARY_PATH");
    if (lp) {
        cfg.custom_library_path = lp;
    }

    return cfg;
}

std::string Config::get_ld_linux_path() const {
    // Standard x86_64 glibc dynamic linkers
    std::string path1 = sysroot_dir + "/usr/lib/ld-linux-x86-64.so.2";
    if (utils::file_exists(path1)) return path1;
    std::string path2 = sysroot_dir + "/lib64/ld-linux-x86-64.so.2";
    if (utils::file_exists(path2)) return path2;
    std::string path3 = sysroot_dir + "/lib/ld-linux-x86-64.so.2";
    if (utils::file_exists(path3)) return path3;
    // 32-bit glibc
    std::string path4 = sysroot_dir + "/lib/ld-linux.so.2";
    if (utils::file_exists(path4)) return path4;
    return "";
}

bool Config::is_prefix_ready() const {
    std::string ld = get_ld_linux_path();
    if (ld.empty()) return false;
    // Check libc.so.6
    std::string libc1 = sysroot_dir + "/usr/lib/libc.so.6";
    std::string libc2 = sysroot_dir + "/lib64/libc.so.6";
    std::string libc3 = sysroot_dir + "/lib/libc.so.6";
    return (utils::file_exists(libc1) || utils::file_exists(libc2) || utils::file_exists(libc3));
}

std::string Config::get_glibc_version() const {
    std::string ld = get_ld_linux_path();
    if (ld.empty()) return "None";
    std::string out = utils::run_command_output("\"" + ld + "\" --version 2>&1 | head -n 1");
    if (!out.empty()) return out;
    return "Unknown";
}

void Config::ensure_prefix_dirs() const {
    utils::create_dirs(prefix_dir);
    utils::create_dirs(sysroot_dir);
    utils::create_dirs(sysroot_dir + "/usr/lib");
    utils::create_dirs(sysroot_dir + "/usr/bin");
    utils::create_dirs(sysroot_dir + "/usr/local/bin");
    utils::create_dirs(sysroot_dir + "/bin");
    utils::create_dirs(sysroot_dir + "/opt");
    utils::create_dirs(sysroot_dir + "/lib64");
    utils::create_dirs(sysroot_dir + "/lib");
    utils::create_dirs(sysroot_dir + "/etc");
    utils::create_dirs(overlay_upper_dir);
    utils::create_dirs(overlay_work_dir);
    utils::create_dirs(cache_dir);
}

} // namespace fglibc
