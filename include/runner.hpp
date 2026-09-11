#pragma once

#include <string>
#include <vector>
#include "config.hpp"

namespace fglibc {

class Runner {
public:
    static int run(const Config &cfg, const std::vector<std::string> &args, const std::string &custom_cwd = "");
    static int run_shell(const Config &cfg);

private:
    static int run_namespace_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd);
    static int run_bwrap_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd);
    static int run_direct_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd);

    static std::vector<std::string> build_common_environment(const Config &cfg);
};

} // namespace fglibc
