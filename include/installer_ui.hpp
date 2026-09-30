#pragma once

#include <string>
#include <vector>
#include "config.hpp"

namespace fglibc {

struct PackageInfo {
    std::string name;
    std::string category;
    std::string description;
    bool selected = false;
    bool is_installed = false;
};

struct PackageBundle {
    std::string name;
    std::string description;
    std::vector<std::string> packages;
};

class InstallerUI {
public:
    // Run setup for explicitly selected components.
    static int run_setup(const Config &cfg, const std::vector<std::string> &args = {});

    // Benchmark and rank fastest mirrors, writing to pacman.d/mirrorlist
    static int rank_mirrors(const Config &cfg, bool verbose = true);

    // Synchronize applications (.desktop) and icons to KDE, GNOME, Hyprland, Rofi
    static int sync_desktop_entries(const Config &cfg, bool verbose = true);

    // Setup specific package managers & components
    static bool setup_pacman(const Config &cfg);
    static bool setup_yay(const Config &cfg);
    static bool setup_paru(const Config &cfg);
    static bool setup_apt(const Config &cfg);
    static bool setup_apk(const Config &cfg);
    static bool setup_devel(const Config &cfg);
    static void print_setup_status(const Config &cfg);

    // Install a list of packages directly (expanding bundles and aliases)
    static int install_packages(const Config &cfg, const std::vector<std::string> &pkgs, bool no_cache = false);

    // Uninstall a list of packages from sysroot using manifests
    static int uninstall_packages(const Config &cfg, const std::vector<std::string> &pkgs);

    // Clean package download cache in ~/.fglibc/cache
    static int clean_cache(const Config &cfg);

    // Print all currently installed packages
    static void list_installed_packages(const Config &cfg);

    // Expand meta-packages and library aliases into individual packages
    static std::vector<std::string> expand_bundles_and_aliases(const std::vector<std::string> &pkgs);

    // Get the default curated list of packages & bundles
    static std::vector<PackageInfo> get_default_catalog();
};

} // namespace fglibc
