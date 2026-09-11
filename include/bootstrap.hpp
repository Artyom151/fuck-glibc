#pragma once

#include <string>
#include "config.hpp"

namespace fglibc {

class BootstrapManager {
public:
    static bool ensure_initialized(const Config &cfg);
    static bool init_prefix(const Config &cfg, bool install_gaming_libs = false);
    static bool install_gaming_libraries(const Config &cfg);
    static bool install_custom_archive(const Config &cfg, const std::string &archive_path);
    static bool rebuild_ld_cache(const Config &cfg);
    static void print_status(const Config &cfg);
};

} // namespace fglibc
