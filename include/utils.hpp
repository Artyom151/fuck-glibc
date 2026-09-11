#pragma once

#include <string>
#include <vector>
#include <iostream>

namespace fglibc {
namespace utils {

// ANSI Colors
namespace colors {
    const std::string RESET   = "\033[0m";
    const std::string BOLD    = "\033[1m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string WHITE   = "\033[37m";
}

void log_info(const std::string &msg);
void log_success(const std::string &msg);
void log_warn(const std::string &msg);
void log_error(const std::string &msg);
void log_debug(const std::string &msg);

bool file_exists(const std::string &path);
bool dir_exists(const std::string &path);
bool create_dirs(const std::string &path);
std::string get_home_dir();
std::string get_cwd();
std::string resolve_path(const std::string &path);

int run_command(const std::string &cmd, bool print_output = true);
std::string run_command_output(const std::string &cmd);

bool download_file(const std::string &url, const std::string &dest_path);
bool extract_archive(const std::string &archive_path, const std::string &dest_dir, const std::string &subpath = "");

int write_text_file(const std::string &path, const std::string &content);
std::string read_text_file(const std::string &path);

bool is_windows_pe_binary(const std::string &path);
std::string find_proton_script();
std::string find_steam_root_for_game(const std::string &game_path);
std::string find_compat_data_for_game(const std::string &game_path);

} // namespace utils
} // namespace fglibc
