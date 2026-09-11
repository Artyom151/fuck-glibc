#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include "config.hpp"
#include "runner.hpp"
#include "bootstrap.hpp"
#include "elf_parser.hpp"
#include "installer_ui.hpp"
#include "utils.hpp"

using namespace fglibc;

static void print_version() {
    std::cout << utils::colors::CYAN << utils::colors::BOLD
              << "fglibc" << utils::colors::RESET
              << " version 1.2.0 (Fast Glibc Runner, Wine/Proton & Windows Runtime Layer for Musl/Alpine)\n"
              << "Compiled for Linux x86_64\n";
}

static void print_help(const char *prog_name) {
    using namespace utils::colors;
    std::cout << BOLD << "\nfglibc - Wine-like Glibc & Windows Compatibility Layer for Alpine / Musl Linux" << RESET << "\n\n"
              << BOLD << "USAGE:" << RESET << "\n"
              << "  " << CYAN << prog_name << " <executable> [args...]" << RESET << "                 Run a Linux binary or Windows .exe directly\n"
              << "  " << CYAN << prog_name << " run <executable> [args...]" << RESET << "             Run a program in glibc environment\n"
              << "  " << CYAN << prog_name << " win-setup <component>" << RESET << "                  Install Windows runtimes (webview2, vcrun, dxvk, fonts)\n"
              << "  " << CYAN << prog_name << " winetricks [verb...]" << RESET << "                   Run winetricks to install Windows libraries/fonts\n"
              << "  " << CYAN << prog_name << " shell" << RESET << "                                  Enter an interactive glibc shell\n"
              << "  " << CYAN << prog_name << " check <executable>" << RESET << "                     Inspect ELF or PE binary and check dependencies\n"
              << "  " << CYAN << prog_name << " setup [component]" << RESET << "                    Interactive package managers, mirrors & desktop setup\n"
              << "  " << CYAN << prog_name << " sync-desktop" << RESET << "                          Export apps & icons to KDE, GNOME, Hyprland, Rofi\n"
              << "  " << CYAN << prog_name << " mirrors" << RESET << "                               Benchmark & select fastest Arch Linux mirrors\n"
              << "  " << CYAN << prog_name << " init [--gaming]" << RESET << "                        Initialize or reset the glibc runtime prefix\n"
              << "  " << CYAN << prog_name << " install" << RESET << "                                Interactive package manager & library installer\n"
              << "  " << CYAN << prog_name << " install [--no-cache] <pkg...>" << RESET << "          Install packages from Arch Linux repositories\n"
              << "  " << CYAN << prog_name << " install gaming" << RESET << "                         Install complete gaming stack (Mesa, Vulkan, SDL)\n"
              << "  " << CYAN << prog_name << " install <archive.pkg.tar.zst>" << RESET << "          Install a local package archive into prefix\n"
              << "  " << CYAN << prog_name << " uninstall <pkg1> [pkg2...]" << RESET << "             Uninstall package(s) from prefix using manifests\n"
              << "  " << CYAN << prog_name << " clean | --no-cache" << RESET << "                     Clean downloaded package archives from cache\n"
              << "  " << CYAN << prog_name << " list" << RESET << "                                   List all installed packages\n"
              << "  " << CYAN << prog_name << " test" << RESET << "                                   Run comprehensive stress test suite\n"
              << "  " << CYAN << prog_name << " status" << RESET << "                                 Show prefix info, glibc version & hardware status\n"
              << "  " << CYAN << prog_name << " help" << RESET << "                                   Display this help message\n"
              << "  " << CYAN << prog_name << " version" << RESET << "                                Display version information\n\n"
              << BOLD << "PACKAGE MANAGERS & SETUP (setup):" << RESET << "\n"
              << "  " << CYAN << "all" << RESET << "       Run comprehensive setup (mirrors, pacman, yay, paru, devel, desktop)\n"
              << "  " << CYAN << "mirrors" << RESET << "   Benchmark & rank fastest mirrors (latency test)\n"
              << "  " << CYAN << "desktop" << RESET << "   Export and sync desktop shortcuts & icons (KDE, GNOME, Hyprland, Rofi)\n"
              << "  " << CYAN << "pacman" << RESET << "    Configure Pacman package manager & local ALPM database\n"
              << "  " << CYAN << "yay" << RESET << "       Install and configure Yay (Arch User Repository helper)\n"
              << "  " << CYAN << "paru" << RESET << "      Install and configure Paru (Rust-based AUR helper)\n"
              << "  " << CYAN << "apt" << RESET << "       Install and configure Debian APT & DPKG tools\n"
              << "  " << CYAN << "apk" << RESET << "       Install and configure Alpine APK Tools in glibc prefix\n"
              << "  " << CYAN << "devel" << RESET << "     Install base build stack (fakeroot, gcc, make, patch, git)\n"
              << "  " << CYAN << "status" << RESET << "    Check package managers and setup status\n\n"
              << BOLD << "WINDOWS SERVICES & RUNTIMES (win-setup):" << RESET << "\n"
              << "  " << CYAN << "webview2" << RESET << "  Microsoft Edge WebView2 Runtime & registry configuration\n"
              << "  " << CYAN << "vcrun" << RESET << "     Visual C++ 2015-2022 Redistributables (x86 & x64)\n"
              << "  " << CYAN << "fonts" << RESET << "     TrueType Core Fonts (Arial, Times, Tahoma, Segoe UI)\n"
              << "  " << CYAN << "dxvk" << RESET << "      DirectX 9/10/11 -> Vulkan translation layer\n"
              << "  " << CYAN << "vkd3d" << RESET << "     DirectX 12 -> Vulkan translation layer\n"
              << "  " << CYAN << "all" << RESET << "       Install all recommended runtimes (fonts, vcrun, webview2, dxvk)\n"
              << "  " << CYAN << "status" << RESET << "    Check Windows services and runtimes status\n\n"
              << BOLD << "EXAMPLES:" << RESET << "\n"
              << "  # Run a Linux or Windows application:\n"
              << "  fglibc discord\n"
              << "  fglibc launcher.exe\n\n"
              << "  # Setup package managers & desktop integration:\n"
              << "  fglibc setup\n"
              << "  fglibc setup mirrors\n"
              << "  fglibc sync-desktop\n\n"
              << "  # Setup Windows runtimes:\n"
              << "  fglibc win-setup webview2\n"
              << "  fglibc win-setup vcrun\n"
              << "  fglibc win-setup all\n"
              << "  fglibc winetricks dotnet48\n\n"
              << "  # Install / Uninstall Linux packages:\n"
              << "  fglibc install fastfetch\n"
              << "  fglibc uninstall fastfetch\n\n"
              << "  # Clean cache & run stress tests:\n"
              << "  fglibc clean\n"
              << "  fglibc test\n\n";
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    Config cfg = Config::load_from_env();
    std::string cmd = argv[1];

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_help(argv[0]);
        return 0;
    }

    if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        print_version();
        return 0;
    }

    if (cmd == "status" || cmd == "info") {
        BootstrapManager::print_status(cfg);
        return 0;
    }

    if (cmd == "setup") {
        std::vector<std::string> setup_args;
        for (int i = 2; i < argc; ++i) {
            setup_args.push_back(argv[i]);
        }
        return InstallerUI::run_setup(cfg, setup_args);
    }

    if (cmd == "sync-desktop" || cmd == "desktop" || cmd == "export-desktop") {
        return InstallerUI::sync_desktop_entries(cfg, true);
    }

    if (cmd == "mirrors" || cmd == "rank-mirrors" || cmd == "mirror") {
        return InstallerUI::rank_mirrors(cfg, true);
    }

    if (cmd == "init") {
        bool gaming = false;
        if (argc >= 3 && std::string(argv[2]) == "--gaming") {
            gaming = true;
        }
        return BootstrapManager::init_prefix(cfg, gaming) ? 0 : 1;
    }

    if (cmd == "clean" || cmd == "clean-cache" || cmd == "cache-clean" || cmd == "--clean-cache") {
        return InstallerUI::clean_cache(cfg);
    }

    if (cmd == "uninstall" || cmd == "remove") {
        std::vector<std::string> pkgs;
        for (int i = 2; i < argc; ++i) {
            pkgs.push_back(argv[i]);
        }
        return InstallerUI::uninstall_packages(cfg, pkgs);
    }

    if (cmd == "list" || cmd == "packages") {
        InstallerUI::list_installed_packages(cfg);
        return 0;
    }

    if (cmd == "test" || cmd == "stress-test") {
        std::string test_script = "/home/runtime/coding/fuck-glibc/scripts/stress_test.sh";
        if (!utils::file_exists(test_script)) {
            test_script = "./scripts/stress_test.sh";
        }
        std::string cmd_run = "sh \"" + test_script + "\"";
        return utils::run_command(cmd_run);
    }

    if (cmd == "win-setup" || cmd == "windows-setup" || cmd == "win" || cmd == "windows") {
        std::string script_path = "/home/runtime/coding/fuck-glibc/scripts/win_setup.sh";
        if (!utils::file_exists(script_path)) {
            script_path = "./scripts/win_setup.sh";
        }
        std::string args_str;
        for (int i = 2; i < argc; ++i) {
            args_str += " \"" + std::string(argv[i]) + "\"";
        }
        std::string cmd_run = "sh \"" + script_path + "\"" + args_str;
        return utils::run_command(cmd_run);
    }

    if (cmd == "winetricks" || cmd == "tricks") {
        std::string script_path = "/home/runtime/coding/fuck-glibc/scripts/winetricks.sh";
        if (!utils::file_exists(script_path)) {
            script_path = "./scripts/winetricks.sh";
        }
        std::string args_str;
        for (int i = 2; i < argc; ++i) {
            args_str += " \"" + std::string(argv[i]) + "\"";
        }
        std::string cmd_run = "sh \"" + script_path + "\"" + args_str;
        return utils::run_command(cmd_run);
    }

    bool no_cache = false;
    int arg_offset = 2;
    if (cmd == "--no-cache") {
        no_cache = true;
        if (argc < 3) {
            return InstallerUI::clean_cache(cfg);
        }
        cmd = argv[2];
        arg_offset = 3;
    }

    if (cmd == "install") {
        std::vector<std::string> pkgs;
        for (int i = arg_offset; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--no-cache") {
                no_cache = true;
            } else {
                pkgs.push_back(a);
            }
        }
        if (pkgs.empty()) {
            return InstallerUI::run_interactive(cfg);
        }
        std::string target = pkgs[0];
        if (target == "gaming" && pkgs.size() == 1) {
            return BootstrapManager::install_gaming_libraries(cfg) ? 0 : 1;
        } else if ((utils::file_exists(target) || target.find(".tar.") != std::string::npos || target.find(".pkg.tar.") != std::string::npos) && pkgs.size() == 1) {
            return BootstrapManager::install_custom_archive(cfg, target) ? 0 : 1;
        } else {
            return InstallerUI::install_packages(cfg, pkgs, no_cache);
        }
    }

    if (cmd == "check") {
        if (argc < 3) {
            utils::log_error("Usage: fglibc check <path_to_executable>");
            return 1;
        }
        std::string target = argv[2];
        if (utils::is_windows_pe_binary(target)) {
            std::cout << utils::colors::YELLOW << utils::colors::BOLD
                      << "\n[i] File is a Windows PE Executable (.exe)!" << utils::colors::RESET << "\n"
                      << "Windows executables require Wine or Proton to run on Linux.\n";
            std::string proton = utils::find_proton_script();
            if (!proton.empty()) {
                std::cout << utils::colors::GREEN << "✔ Detected Steam Proton at: " << proton << utils::colors::RESET << "\n"
                          << "You can launch it automatically by simply running:\n  "
                          << utils::colors::CYAN << "fglibc \"" << target << "\"" << utils::colors::RESET << "\n\n";
            } else {
                std::cout << utils::colors::YELLOW << "To run this game, please install Wine or Steam Proton." << utils::colors::RESET << "\n\n";
            }
            return 0;
        }

        ElfInfo info = ElfParser::parse(target, cfg);
        if (!info.valid) {
            utils::log_error("File is not a valid ELF binary: " + target);
            return 1;
        }
        info.print_summary(cfg);
        return 0;
    }

    if (cmd == "shell" || cmd == "sh") {
        return Runner::run_shell(cfg);
    }

    // Prepare run arguments
    std::vector<std::string> run_args;
    if (cmd == "run") {
        if (argc < 3) {
            utils::log_error("Usage: fglibc run <executable> [args...]");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            run_args.push_back(argv[i]);
        }
    } else {
        for (int i = 1; i < argc; i++) {
            run_args.push_back(argv[i]);
        }
    }

    // Auto-detect Windows PE (.exe) executable or sysroot executable
    std::string target_bin = run_args[0];

    // If target does not exist directly, look in fglibc installation directories
    if (!utils::file_exists(target_bin)) {
        std::vector<std::string> candidate_paths;
        if (!target_bin.empty() && target_bin[0] == '/') {
            candidate_paths.push_back(cfg.sysroot_dir + target_bin);
        } else {
            candidate_paths.push_back(cfg.sysroot_dir + "/usr/bin/" + target_bin);
            candidate_paths.push_back(cfg.sysroot_dir + "/bin/" + target_bin);
            candidate_paths.push_back(cfg.sysroot_dir + "/usr/local/bin/" + target_bin);
            candidate_paths.push_back(cfg.sysroot_dir + "/opt/" + target_bin + "/" + target_bin);
            candidate_paths.push_back(cfg.sysroot_dir + "/opt/" + target_bin + "/bin/" + target_bin);
            candidate_paths.push_back(cfg.sysroot_dir + "/opt/" + target_bin);
        }

        for (const auto &cand : candidate_paths) {
            if (utils::file_exists(cand)) {
                utils::log_info("Resolved '" + run_args[0] + "' -> " + cand);
                target_bin = cand;
                run_args[0] = cand;
                break;
            }
        }
    }

    if (utils::is_windows_pe_binary(target_bin)) {
        utils::log_info("Detected Windows executable (.exe): " + target_bin);

        // Auto-configure Chromium arguments for Microsoft Edge WebView2 under Wine to prevent DirectComposition crash loop
        const char *cur_wv_args = getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");
        std::string wv_flags = "--no-sandbox --disable-gpu --disable-gpu-compositing --disable-direct-composition";
        std::string wv_args;
        if (cur_wv_args && strlen(cur_wv_args) > 0) {
            wv_args = cur_wv_args;
            if (wv_args.find("--no-sandbox") == std::string::npos) wv_args += " --no-sandbox";
            if (wv_args.find("--disable-gpu") == std::string::npos) wv_args += " --disable-gpu";
            if (wv_args.find("--disable-gpu-compositing") == std::string::npos) wv_args += " --disable-gpu-compositing";
            if (wv_args.find("--disable-direct-composition") == std::string::npos) wv_args += " --disable-direct-composition";
        } else {
            wv_args = wv_flags;
        }
        setenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", wv_args.c_str(), 1);

        // Check if the binary is part of a Steam game library
        std::string steam_root = utils::find_steam_root_for_game(target_bin);
        std::string proton = utils::find_proton_script();

        if (!steam_root.empty() && !proton.empty()) {
            std::string compat_data = utils::find_compat_data_for_game(target_bin);

            setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steam_root.c_str(), 1);
            if (!compat_data.empty()) {
                setenv("STEAM_COMPAT_DATA_PATH", compat_data.c_str(), 1);

                // Extract Steam AppID from compatdata path
                size_t slash = compat_data.rfind('/');
                if (slash != std::string::npos) {
                    std::string appid = compat_data.substr(slash + 1);
                    setenv("SteamAppId", appid.c_str(), 1);
                    setenv("SteamGameId", appid.c_str(), 1);
                    setenv("STEAM_COMPAT_APP_ID", appid.c_str(), 1);

                    // Write steam_appid.txt in game directory if missing
                    size_t last_slash = target_bin.rfind('/');
                    if (last_slash != std::string::npos) {
                        std::string game_dir = target_bin.substr(0, last_slash);
                        std::string appid_file = game_dir + "/steam_appid.txt";
                        if (!utils::file_exists(appid_file)) {
                            utils::write_text_file(appid_file, appid + "\n");
                        }
                    }
                }
            }

            // Ensure ~/.steam symlink exists to prevent lsteamclient assertion failure
            std::string home_steam = utils::get_home_dir() + "//.steam";
            if (!utils::dir_exists(home_steam) && !utils::file_exists(home_steam)) {
                std::string target_steam = utils::get_home_dir() + "/.var/app/com.valvesoftware.Steam/.steam";
                if (utils::dir_exists(target_steam)) {
                    symlink(target_steam.c_str(), home_steam.c_str());
                }
            }

            // Disable Xalia accessibility bridge which causes "No displays available" crashes
            setenv("PROTON_USE_XALIA", "0", 1);

            utils::log_info("Auto-routing through Steam Proton: " + proton);
            std::vector<std::string> proton_args = { proton, "run", target_bin };
            for (size_t i = 1; i < run_args.size(); i++) {
                proton_args.push_back(run_args[i]);
            }
            return Runner::run(cfg, proton_args);
        }

        // Standalone Windows executable: Prefer Wine if available, else Proton with fallback prefix
        if (utils::file_exists("/usr/bin/wine") || utils::file_exists("/usr/local/bin/wine") || utils::file_exists("/bin/wine")) {
            utils::log_info("Auto-routing through system Wine on host...");
            std::vector<char*> c_args;
            std::string wine_cmd = "wine";
            c_args.push_back(const_cast<char*>(wine_cmd.c_str()));
            for (size_t i = 0; i < run_args.size(); i++) {
                c_args.push_back(const_cast<char*>(run_args[i].c_str()));
            }
            c_args.push_back(nullptr);
            execvp(wine_cmd.c_str(), c_args.data());
            perror("execvp(wine)");
            return 1;
        } else if (!proton.empty()) {
            std::string fallback_prefix = utils::get_home_dir() + "/.fglibc/proton_prefix";
            utils::create_dirs(fallback_prefix);
            setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", utils::get_home_dir().c_str(), 1);
            setenv("STEAM_COMPAT_DATA_PATH", fallback_prefix.c_str(), 1);
            setenv("PROTON_USE_XALIA", "0", 1);
            utils::log_info("Routing through Steam Proton with dedicated prefix: " + fallback_prefix);
            std::vector<std::string> proton_args = { proton, "run", target_bin };
            for (size_t i = 1; i < run_args.size(); i++) {
                proton_args.push_back(run_args[i]);
            }
            return Runner::run(cfg, proton_args);
        }

        utils::log_error("Target is a Windows executable (.exe), but neither Wine nor Proton was found!");
        return 1;
    }

    // For Electron/Chromium apps (Yandex Music, Discord, Spotify, VS Code), auto-append --no-sandbox
    // to allow smooth execution in unprivileged user namespaces
    std::string bname = target_bin;
    size_t last_slash = bname.rfind('/');
    if (last_slash != std::string::npos) bname = bname.substr(last_slash + 1);
    if (bname == "yandex-music" || bname == "yandexmusic" || bname == "discord" || bname == "spotify" || bname == "code" || bname == "slack" || bname == "obsidian") {
        bool has_sandbox_arg = false;
        for (const auto &arg : run_args) {
            if (arg.find("--no-sandbox") != std::string::npos) {
                has_sandbox_arg = true;
                break;
            }
        }
        if (!has_sandbox_arg) {
            run_args.push_back("--no-sandbox");
        }
    }

    return Runner::run(cfg, run_args);
}
