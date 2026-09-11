#include "utils.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <limits.h>

namespace fglibc {
namespace utils {

void log_info(const std::string &msg) {
    std::cerr << colors::CYAN << "[fglibc:info] " << colors::RESET << msg << std::endl;
}

void log_success(const std::string &msg) {
    std::cerr << colors::GREEN << colors::BOLD << "[fglibc:ok] " << colors::RESET << msg << std::endl;
}

void log_warn(const std::string &msg) {
    std::cerr << colors::YELLOW << "[fglibc:warn] " << colors::RESET << msg << std::endl;
}

void log_error(const std::string &msg) {
    std::cerr << colors::RED << colors::BOLD << "[fglibc:error] " << colors::RESET << msg << std::endl;
}

void log_debug(const std::string &msg) {
    std::cerr << colors::MAGENTA << "[fglibc:debug] " << colors::RESET << msg << std::endl;
}

bool file_exists(const std::string &path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode));
}

bool dir_exists(const std::string &path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

bool create_dirs(const std::string &path) {
    if (path.empty()) return false;
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path.c_str());
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

std::string get_home_dir() {
    const char *h = getenv("HOME");
    if (h) return std::string(h);
    return "/tmp";
}

std::string get_cwd() {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) {
        return std::string(buf);
    }
    return ".";
}

std::string resolve_path(const std::string &path) {
    char buf[PATH_MAX];
    if (realpath(path.c_str(), buf)) {
        return std::string(buf);
    }
    return path;
}

int run_command(const std::string &cmd, bool print_output) {
    std::string full_cmd = cmd;
    if (!print_output) {
        full_cmd += " >/dev/null 2>&1";
    }
    return system(full_cmd.c_str());
}

std::string run_command_output(const std::string &cmd) {
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    char buf[512];
    std::string result;
    while (fgets(buf, sizeof(buf), fp)) {
        result += buf;
    }
    pclose(fp);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

bool download_file(const std::string &url, const std::string &dest_path) {
    std::string cmd;
    if (file_exists("/usr/bin/curl") || file_exists("/bin/curl") || file_exists("/usr/local/bin/curl")) {
        cmd = "curl -fL --progress-bar \"" + url + "\" -o \"" + dest_path + "\"";
    } else if (file_exists("/usr/bin/wget") || file_exists("/bin/wget")) {
        cmd = "wget --show-progress -q -O \"" + dest_path + "\" \"" + url + "\"";
    } else {
        log_error("Neither curl nor wget found in system!");
        return false;
    }

    log_info("Downloading: " + url);
    int ret = run_command(cmd, true);
    if (ret != 0) {
        log_error("Failed to download from " + url);
        unlink(dest_path.c_str());
        return false;
    }
    return true;
}

bool extract_archive(const std::string &archive_path, const std::string &dest_dir, const std::string &subpath) {
    create_dirs(dest_dir);
    std::string cmd;

    if (archive_path.find(".tar.zst") != std::string::npos || archive_path.find(".pkg.tar.zst") != std::string::npos) {
        cmd = "tar -I zstd -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
    } else if (archive_path.find(".tar.xz") != std::string::npos) {
        cmd = "tar -xJf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
    } else if (archive_path.find(".tar.gz") != std::string::npos || archive_path.find(".tgz") != std::string::npos) {
        cmd = "tar -xzf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
    } else if (archive_path.find(".deb") != std::string::npos) {
        cmd = "mkdir -p /tmp/fglibc_deb && cd /tmp/fglibc_deb && ar -x \"" + archive_path + "\" && "
              "tar -xf data.tar.* -C \"" + dest_dir + "\" && cd - && rm -rf /tmp/fglibc_deb";
    } else {
        cmd = "tar -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
    }

    if (!subpath.empty()) {
        cmd += " " + subpath;
    }

    log_info("Extracting to " + dest_dir + "...");
    int ret = run_command(cmd + " 2>&1", false);
    return (ret == 0);
}

int write_text_file(const std::string &path, const std::string &content) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, content.data(), content.size());
    close(fd);
    return (written == (ssize_t)content.size()) ? 0 : -1;
}

std::string read_text_file(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

bool is_windows_pe_binary(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    char magic[2];
    if (f.read(magic, 2) && magic[0] == 'M' && magic[1] == 'Z') {
        return true;
    }
    return false;
}

std::string find_proton_script() {
    std::vector<std::string> candidates = {
        get_home_dir() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Proton - Experimental/proton",
        get_home_dir() + "/.local/share/Steam/steamapps/common/Proton - Experimental/proton",
        get_home_dir() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Proton 9.0/proton",
        get_home_dir() + "/.local/share/Steam/steamapps/common/Proton 9.0/proton",
        get_home_dir() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Proton 8.0/proton",
        get_home_dir() + "/.local/share/Steam/steamapps/common/Proton 8.0/proton"
    };

    for (const auto &c : candidates) {
        if (file_exists(c)) return c;
    }
    return "";
}

std::string find_steam_root_for_game(const std::string &game_path) {
    size_t pos = game_path.find("/steamapps/");
    if (pos != std::string::npos) {
        return game_path.substr(0, pos);
    }
    return "";
}

std::string find_compat_data_for_game(const std::string &game_path) {
    std::string steam_root = find_steam_root_for_game(game_path);
    if (steam_root.empty()) return "";

    std::string compat_dir = steam_root + "/steamapps/compatdata";
    std::string steamapps_dir = steam_root + "/steamapps";

    DIR *d = opendir(steamapps_dir.c_str());
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name.rfind("appmanifest_", 0) == 0 && name.find(".acf") != std::string::npos) {
                std::string content = read_text_file(steamapps_dir + "/" + name);
                std::string appid;
                size_t id_pos = content.find("\"appid\"");
                if (id_pos != std::string::npos) {
                    size_t start = content.find("\"", id_pos + 7);
                    size_t end = content.find("\"", start + 1);
                    if (start != std::string::npos && end != std::string::npos) {
                        appid = content.substr(start + 1, end - start - 1);
                    }
                }

                size_t dir_pos = content.find("\"installdir\"");
                if (dir_pos != std::string::npos && !appid.empty()) {
                    size_t start = content.find("\"", dir_pos + 12);
                    size_t end = content.find("\"", start + 1);
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string dir_name = content.substr(start + 1, end - start - 1);
                        if (game_path.find("/" + dir_name + "/") != std::string::npos ||
                            game_path.rfind("/" + dir_name) == game_path.length() - dir_name.length() - 1) {
                            closedir(d);
                            return compat_dir + "/" + appid;
                        }
                    }
                }
            }
        }
        closedir(d);
    }

    return "";
}

} // namespace utils
} // namespace fglibc
