#pragma once

#include <string>
#include <vector>

namespace fglibc {

enum class EngineType {
    NAMESPACE, // Unprivileged user namespace + pivot_root + overlay (default & most compatible)
    BWRAP,     // Bubblewrap sandbox engine
    DIRECT     // Direct dynamic linker execution fallback (ld-linux --library-path ...)
};

struct Config {
    std::string prefix_dir;
    std::string sysroot_dir;
    std::string overlay_upper_dir;
    std::string overlay_work_dir;
    std::string cache_dir;
    std::string custom_library_path;

    EngineType engine = EngineType::NAMESPACE;
    bool debug = false;
    bool passthrough_x11 = true;
    bool passthrough_wayland = true;
    bool passthrough_audio = true;
    bool passthrough_dri = true;
    bool passthrough_input = true;

    static Config load_from_env();
    bool is_prefix_ready() const;
    std::string get_glibc_version() const;
    std::string get_ld_linux_path() const;
    void ensure_prefix_dirs() const;
};

} // namespace fglibc
