#include "installer_ui.hpp"
#include "bootstrap.hpp"
#include "elf_parser.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <csignal>
#include <future>
#include <thread>
#include <chrono>
#include "runner.hpp"

namespace fglibc {

static std::string CORE_MIRROR     = "https://geo.mirror.pkgbuild.com/core/os/x86_64/";
static std::string EXTRA_MIRROR    = "https://geo.mirror.pkgbuild.com/extra/os/x86_64/";
static std::string MULTILIB_MIRROR = "https://geo.mirror.pkgbuild.com/multilib/os/x86_64/";
static const std::string CHAOTIC_MIRROR  = "https://geo-mirror.chaotic.cx/chaotic-aur/x86_64/";

static void update_active_mirrors(const std::string &base_url) {
    if (base_url.empty()) return;
    std::string b = base_url;
    while (!b.empty() && b.back() == '/') b.pop_back();
    CORE_MIRROR     = b + "/core/os/x86_64/";
    EXTRA_MIRROR    = b + "/extra/os/x86_64/";
    MULTILIB_MIRROR = b + "/multilib/os/x86_64/";
}

static std::string to_lower(const std::string &s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
    return res;
}

static std::string trim_string(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Meta-packages / Bundles (like electron, gaming, media) that group multiple packages
static const std::vector<PackageBundle> BUNDLES = {
    {
        "electron",
        "Full Electron & Chromium runtime stack (Discord, VS Code, Slack, Obsidian, Spotify)",
        {
            "glib2", "gtk3", "cairo", "pango", "at-spi2-core", "nss", "nspr", "dbus",
            "alsa-lib", "systemd-libs", "libxcomposite", "libxdamage", "libxrandr",
            "libxfixes", "libxcursor", "libxi", "libxtst", "libxss",
            "libcloudproviders", "libnotify", "pcre2", "util-linux-libs",
            "libpng", "freetype2", "fontconfig", "harfbuzz", "pixman",
            "fribidi", "gdk-pixbuf2", "libepoxy", "libthai", "libdatrie",
            "tinysparql", "graphite", "libjpeg-turbo", "libtiff", "libcups",
            "brotli", "bzip2", "sqlite", "glycin", "json-glib", "lcms2", "libseccomp",
            "libasyncns"
        }
    },
    {
        "gaming",
        "Full 3D graphics, Vulkan, OpenGL & game engine stack",
        {
            "mesa", "libglvnd", "vulkan-icd-loader", "vulkan-radeon", "vulkan-intel",
            "vulkan-swrast", "sdl3", "sdl2-compat", "openal", "wayland", "libxkbcommon",
            "libdecor", "libx11", "libxext", "libxfixes", "libxrender", "libxrandr",
            "libxinerama", "libxcursor", "libxi", "libxxf86vm", "libxshmfence",
            "libpciaccess", "xcb-util-keysyms", "libdisplay-info"
        }
    },
    {
        "media",
        "Audio and video codecs, frameworks & PipeWire/ALSA sound stack",
        {
            "ffmpeg", "alsa-lib", "libpulse", "pipewire", "openal", "libvorbis",
            "libogg", "flac", "opus", "mpg123", "libsndfile", "gstreamer",
            "gst-plugins-base", "gst-plugins-good"
        }
    },
    {
        "desktop",
        "Core GTK, font rendering & graphical desktop environment stack",
        {
            "gtk3", "gtk4", "glib2", "cairo", "pango", "fontconfig", "freetype2",
            "dbus", "at-spi2-core", "libnotify"
        }
    },
    {
        "wine-deps",
        "Essential Linux libraries for Wine and Proton Windows games",
        {
            "mesa", "vulkan-icd-loader", "vulkan-radeon", "alsa-lib", "libpulse",
            "openal", "libx11", "libxrandr", "libxinerama", "libxcursor", "libxi",
            "libxxf86vm", "gnutls", "libpng", "libjpeg-turbo", "lib32-glibc",
            "lib32-gcc-libs", "lib32-mesa", "lib32-vulkan-icd-loader", "lib32-alsa-lib",
            "lib32-libx11"
        }
    },
    {
        "steam",
        "Full Steam runtime stack with 32-bit & 64-bit graphics, audio & system libraries",
        {
            "steam", "vulkan-icd-loader", "vulkan-radeon", "mesa", "alsa-lib",
            "libpulse", "zenity", "curl", "dbus", "nss", "libx11", "libxcursor",
            "libxi", "libxrandr", "libxinerama", "libxss", "libglvnd", "openal",
            "systemd-libs", "ttf-dejavu", "lib32-glibc", "lib32-gcc-libs",
            "lib32-mesa", "lib32-vulkan-icd-loader", "lib32-vulkan-radeon",
            "lib32-alsa-lib", "lib32-libx11", "lib32-libxss", "lib32-libglvnd",
            "lib32-systemd", "lib32-nss", "lib32-pipewire", "lib32-libva"
        }
    }
};

// Aliases from common library names to actual Arch package names
static const std::unordered_map<std::string, std::string> LIBRARY_ALIASES = {
    { "libglib", "glib2" },
    { "libglib-2.0", "glib2" },
    { "libglib2", "glib2" },
    { "glib", "glib2" },
    { "libgobject", "glib2" },
    { "libgio", "glib2" },
    { "libnss", "nss" },
    { "libnss3", "nss" },
    { "libnspr", "nspr" },
    { "libnspr4", "nspr" },
    { "libdbus", "dbus" },
    { "libdbus-1", "dbus" },
    { "libasound", "alsa-lib" },
    { "libasound2", "alsa-lib" },
    { "alsa", "alsa-lib" },
    { "libasyncns", "libasyncns" },
    { "libasyncns.so", "libasyncns" },
    { "libudev", "systemd-libs" },
    { "udev", "systemd-libs" },
    { "libgtk", "gtk3" },
    { "libgtk-3", "gtk3" },
    { "gtk", "gtk3" },
    { "libatk", "at-spi2-core" },
    { "libatspi", "at-spi2-core" },
    { "at-spi", "at-spi2-core" },
    { "libvulkan", "vulkan-icd-loader" },
    { "vulkan", "vulkan-icd-loader" },
    { "libgl", "libglvnd" },
    { "libegl", "libglvnd" },
    { "libglx", "libglvnd" },
    { "opengl", "libglvnd" },
    { "libssl", "openssl" },
    { "libcrypto", "openssl" },
    { "libcurl", "curl" },
    { "libz", "zlib" },
    { "libzstd", "zstd" },
    { "liblzma", "xz" },
    { "libsqlite3", "sqlite" },
    { "libpng", "libpng" },
    { "libjpeg", "libjpeg-turbo" },
    { "libfreetype", "freetype2" },
    { "libfontconfig", "fontconfig" },
    { "libopenal", "openal" },
    { "libpulse", "libpulse" },
    { "pulseaudio", "libpulse" },
    { "libpipewire", "pipewire" },
    { "libsdl2", "sdl2-compat" },
    { "libsdl3", "sdl3" },
    { "libreadline.so", "readline" },
    { "libreadline", "readline" },
    { "libhistory.so", "readline" },
    { "libhistory", "readline" },
    { "libncursesw.so", "ncurses" },
    { "libncurses.so", "ncurses" },
    { "libncursesw", "ncurses" },
    { "libncurses", "ncurses" },
    { "libformw.so", "ncurses" },
    { "libmenuw.so", "ncurses" },
    { "libpanelw.so", "ncurses" },
    { "libasound.so", "alsa-lib" },
    { "libssl.so", "openssl" },
    { "libcrypto.so", "openssl" },
    { "libcurl.so", "curl" },
    { "libz.so", "zlib" },
    { "libzstd.so", "zstd" },
    { "liblzma.so", "xz" },
    { "libsqlite3.so", "sqlite" },
    { "libxml2.so", "libxml2" },
    { "libffi.so", "libffi" },
    { "libudev.so", "systemd-libs" },
    { "libdbus-1.so", "dbus" },
    { "libglib-2.0.so", "glib2" },
    { "libgobject-2.0.so", "glib2" },
    { "libgio-2.0.so", "glib2" },
    { "libgmodule-2.0.so", "glib2" },
    { "libfuse3.so", "fuse3" },
    { "libfuse.so", "fuse2" },
    { "libseccomp.so", "libseccomp" },
    { "libvulkan.so", "vulkan-icd-loader" },
    { "libgl.so", "libglvnd" },
    { "libegl.so", "libglvnd" },
    { "libx11.so", "libx11" },
    { "libxcb.so", "libxcb" },
    { "libpipewire-0.3.so", "pipewire" },
    { "libpulse.so", "libpulse" },
    { "libopenal.so", "openal" },
    { "libpng16.so", "libpng" },
    { "libjpeg.so", "libjpeg-turbo" },
    { "libfreetype.so", "freetype2" },
    { "libfontconfig.so", "fontconfig" },
    { "libharfbuzz.so", "harfbuzz" },
    { "libpcre2-8.so", "pcre2" },
    { "libbrotlidec.so", "brotli" },
    { "libbz2.so", "bzip2" },
    { "at-spi2-atk", "at-spi2-core" },
    { "libgssapi_krb5", "krb5" },
    { "libngtcp2_crypto_gnutls", "libngtcp2" },
    { "libcom_err", "e2fsprogs" },
    { "libss", "e2fsprogs" },
    { "libcrypt", "libxcrypt" },
    { "libverto-module-base", "libverto" },
    { "libatomic.so", "libatomic" },
    { "libatomic", "libatomic" },
    { "libmp3lame", "lame" },
    { "libmp3lame.so", "lame" },
    { "mp3lame", "lame" },
    { "lame", "lame" },
    { "libasyncns", "libasyncns" },
    { "libasyncns.so", "libasyncns" },
    { "asyncns", "libasyncns" },
    { "libminizip", "minizip" },
    { "libminizip.so", "minizip" },
    { "minizip", "minizip" },
    { "libxslt", "libxslt" },
    { "libxslt.so", "libxslt" },
    { "libexslt", "libxslt" },
    { "libexslt.so", "libxslt" },
    { "xslt", "libxslt" },
    { "libdrm", "libdrm" },
    { "libdrm.so", "libdrm" },
    { "drm", "libdrm" },
    { "libgbm", "mesa" },
    { "libgbm.so", "mesa" },
    { "gbm", "mesa" },
    { "libflac", "flac" },
    { "libflac.so", "flac" },
    { "flac", "flac" },
    { "libogg", "libogg" },
    { "libogg.so", "libogg" },
    { "ogg", "libogg" },
    { "libvorbis", "libvorbis" },
    { "libvorbis.so", "libvorbis" },
    { "libvorbisenc", "libvorbis" },
    { "libvorbisenc.so", "libvorbis" },
    { "libvorbisfile", "libvorbis" },
    { "libvorbisfile.so", "libvorbis" },
    { "vorbis", "libvorbis" },
    { "libopus", "opus" },
    { "libopus.so", "opus" },
    { "opus", "opus" },
    { "libmpg123", "mpg123" },
    { "libmpg123.so", "mpg123" },
    { "mpg123", "mpg123" },
    { "libsndfile", "libsndfile" },
    { "libsndfile.so", "libsndfile" },
    { "sndfile", "libsndfile" },
    { "libcups", "libcups" },
    { "libcups.so", "libcups" },
    { "cups", "libcups" },
    { "libxkbcommon", "libxkbcommon" },
    { "libxkbcommon.so", "libxkbcommon" },
    { "libxkbcommon-x11", "libxkbcommon" },
    { "libxkbcommon-x11.so", "libxkbcommon" },
    { "xkbcommon", "libxkbcommon" },
    { "libgpg-error", "libgpg-error" },
    { "libgpg-error.so", "libgpg-error" },
    { "libgcrypt", "libgcrypt" },
    { "libgcrypt.so", "libgcrypt" },
    { "libxcb-keysyms", "xcb-util-keysyms" },
    { "libxcb-keysyms.so", "xcb-util-keysyms" },
    { "xcb-util-keysyms", "xcb-util-keysyms" },
    { "libdisplay-info", "libdisplay-info" },
    { "libdisplay-info.so", "libdisplay-info" },
    { "libdb-6.2", "db" },
    { "libdb-6.2.so", "db" },
    { "libxxhash", "xxhash" },
    { "libxxhash.so", "xxhash" },
    { "liblua5.3", "lua53" },
    { "liblua5.3.so", "lua53" },
    { "lua5.3", "lua53" },
    { "paru-bin", "paru" },
    { "libgc", "gc" },
    { "libgc.so", "gc" },
    { "libguile-3.0", "guile" },
    { "libguile-3.0.so", "guile" },
    { "libc.musl-x86_64", "" },
    { "libc.musl-x86_64.so.1", "" }
};

struct RepoPackage {
    std::string repo;
    std::string name;
    std::string filename;
};

// RAII Terminal Raw Mode Guard
class RawTerminalGuard {
private:
    struct termios orig_termios;
    bool active = false;

public:
    RawTerminalGuard() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON | ISIG);
            raw.c_iflag &= ~(IXON | ICRNL);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
                active = true;
                // Switch to alternate screen buffer and hide cursor
                write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14);
            }
        }
    }

    ~RawTerminalGuard() {
        restore();
    }

    void restore() {
        if (active) {
            // Restore cursor and main screen buffer
            write(STDOUT_FILENO, "\033[?25h\033[?1049l", 14);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            active = false;
        }
    }
};

static RawTerminalGuard *g_guard = nullptr;

static void sigint_handler(int) {
    if (g_guard) {
        g_guard->restore();
    }
    _exit(130);
}

std::vector<std::string> InstallerUI::expand_bundles_and_aliases(const std::vector<std::string> &pkgs) {
    std::vector<std::string> expanded;
    std::unordered_set<std::string> seen;

    for (const auto &raw_p : pkgs) {
        std::string p = raw_p;
        std::string lower_p = to_lower(p);

        // Check if it's an alias
        auto it = LIBRARY_ALIASES.find(lower_p);
        if (it != LIBRARY_ALIASES.end()) {
            p = it->second;
            lower_p = to_lower(p);
        }

        // Check if it's a bundle
        bool is_bundle = false;
        for (const auto &b : BUNDLES) {
            if (to_lower(b.name) == lower_p) {
                is_bundle = true;
                for (const auto &sub : b.packages) {
                    if (seen.insert(sub).second) {
                        expanded.push_back(sub);
                    }
                }
                break;
            }
        }

        if (!is_bundle) {
            // Auto-include dependencies for known complex packages
            if (lower_p == "discord" || lower_p == "code" || lower_p == "visual-studio-code-bin" ||
                lower_p == "obsidian" || lower_p == "slack-desktop" || lower_p == "spotify" ||
                lower_p == "telegram-desktop" || lower_p == "postman-bin" || lower_p == "insomnia") {
                for (const auto &b : BUNDLES) {
                    if (b.name == "electron") {
                        for (const auto &sub : b.packages) {
                            if (seen.insert(sub).second) {
                                expanded.push_back(sub);
                            }
                        }
                        break;
                    }
                }
            } else if (lower_p == "steam") {
                for (const auto &b : BUNDLES) {
                    if (b.name == "steam") {
                        for (const auto &sub : b.packages) {
                            if (seen.insert(sub).second) {
                                expanded.push_back(sub);
                            }
                        }
                        break;
                    }
                }
            } else if (lower_p == "prismlauncher" || lower_p == "heroic-games-launcher-bin" ||
                       lower_p == "lutris" || lower_p == "minecraft") {
                for (const auto &b : BUNDLES) {
                    if (b.name == "gaming") {
                        for (const auto &sub : b.packages) {
                            if (seen.insert(sub).second) {
                                expanded.push_back(sub);
                            }
                        }
                        break;
                    }
                }
            } else if (lower_p == "mpv" || lower_p == "vlc" || lower_p == "obs-studio" || lower_p == "kdenlive") {
                for (const auto &b : BUNDLES) {
                    if (b.name == "media") {
                        for (const auto &sub : b.packages) {
                            if (seen.insert(sub).second) {
                                expanded.push_back(sub);
                            }
                        }
                        break;
                    }
                }
            } else if (lower_p == "wine" || lower_p == "wine-staging") {
                for (const auto &b : BUNDLES) {
                    if (b.name == "wine-deps") {
                        for (const auto &sub : b.packages) {
                            if (seen.insert(sub).second) {
                                expanded.push_back(sub);
                            }
                        }
                        break;
                    }
                }
            }

            if (seen.insert(p).second) {
                expanded.push_back(p);
            }
        }
    }

    return expanded;
}

std::vector<PackageInfo> InstallerUI::get_default_catalog() {
    std::vector<PackageInfo> cat;

    // Bundles / Meta-packages first
    for (const auto &b : BUNDLES) {
        cat.push_back({ b.name, "Bundle", b.description });
    }

    // Curated packages
    std::vector<PackageInfo> individual = {
        // Common apps
        { "steam", "App", "Valve Steam gaming platform (auto-installs 32-bit & 64-bit gaming stack)" },
        { "discord", "App", "All-in-one voice and text chat for gamers (auto-installs Electron stack)" },
        { "prismlauncher", "App", "Custom Minecraft launcher (auto-installs Java & gaming stack)" },
        { "obsidian", "App", "Markdown-based note-taking application (auto-installs Electron)" },
        { "spotify", "App", "Digital music streaming service (auto-installs Electron & codecs)" },
        { "mpv", "App", "Fast, lightweight video player (auto-installs media stack)" },
        { "vlc", "App", "Multi-format media player and framework (auto-installs media stack)" },

        // Graphics & Drivers
        { "mesa", "Graphics", "Open-source OpenGL & Vulkan drivers (Radeon, Intel, Zink, Lavapipe)" },
        { "libglvnd", "Graphics", "GL Vendor-Neutral Dispatch (OpenGL, EGL, GLX runtime dispatch)" },
        { "vulkan-icd-loader", "Graphics", "Khronos Vulkan Installable Client Driver (ICD) Loader" },
        { "vulkan-radeon", "Graphics", "AMD Radeon Vulkan graphics driver (RADV)" },
        { "vulkan-intel", "Graphics", "Intel Vulkan graphics driver (ANV)" },
        { "vulkan-swrast", "Graphics", "Software Vulkan CPU rasterizer (Lavapipe)" },
        { "glfw", "Graphics", "Multi-platform library for OpenGL/Vulkan window creation" },
        { "freeglut", "Graphics", "OpenGL Utility Toolkit (GLUT) library" },
        { "glew", "Graphics", "OpenGL Extension Wrangler Library" },

        // Display Servers & Input (Wayland / X11)
        { "wayland", "Display", "Core Wayland display server and client libraries" },
        { "wayland-protocols", "Display", "Extended specifications for Wayland protocol" },
        { "libxkbcommon", "Display", "Keyboard keymap compiler and handling library" },
        { "libdecor", "Display", "Client-side window decorations for Wayland" },
        { "libx11", "Display", "Core X11 client library" },
        { "libxext", "Display", "X11 miscellaneous extensions library" },
        { "libxfixes", "Display", "X11 miscellaneous fixes extension" },
        { "libxrender", "Display", "X11 Rendering Extension library" },
        { "libxrandr", "Display", "X11 Resize, Rotate and Reflection extension" },
        { "libxinerama", "Display", "X11 Xinerama multi-monitor extension" },
        { "libxcursor", "Display", "X11 cursor management library" },
        { "libxi", "Display", "X11 Input Extension library" },
        { "libxxf86vm", "Display", "X11 XFree86 video mode extension" },
        { "libxshmfence", "Display", "Shared memory sync fences for DRI3" },
        { "libxcb", "Display", "X C Binding protocol library" },
        { "libxau", "Display", "X11 Authorization Protocol library" },
        { "libxdmcp", "Display", "X Display Manager Control Protocol library" },
        { "libxcomposite", "Display", "X11 Composite Extension library" },
        { "libxdamage", "Display", "X11 Damage Extension library" },
        { "libxtst", "Display", "X11 Testing Extension library" },

        // Audio & Multimedia
        { "openal", "Audio", "OpenAL 3D Audio API (Minecraft, game audio)" },
        { "alsa-lib", "Audio", "Advanced Linux Sound Architecture client library" },
        { "libpulse", "Audio", "PulseAudio / PipeWire sound server client library" },
        { "pipewire", "Audio", "Next-gen multimedia processing graph and server" },
        { "ffmpeg", "Media", "Complete cross-platform audio and video codecs" },
        { "gstreamer", "Media", "Multimedia framework and pipeline engine" },
        { "gst-plugins-base", "Media", "GStreamer base multimedia plugins" },
        { "gst-plugins-good", "Media", "GStreamer good multimedia plugins" },
        { "libvorbis", "Audio", "Ogg Vorbis lossy audio compression codec" },
        { "libogg", "Audio", "Ogg bitstream container format library" },
        { "flac", "Audio", "Free Lossless Audio Codec library" },
        { "opus", "Audio", "Opus low-delay interactive audio codec" },
        { "mpg123", "Audio", "Fast console MPEG Audio Player and decoder" },
        { "libsndfile", "Audio", "C library for reading and writing audio files" },

        // Game Engines & Steam
        { "sdl3", "Gaming", "Simple DirectMedia Layer 3 (modern games)" },
        { "sdl2-compat", "Gaming", "SDL 2.0 compatibility layer using SDL 3" },
        { "sdl2", "Gaming", "Simple DirectMedia Layer 2.0 runtime" },
        { "sdl2_image", "Gaming", "SDL2 image file loading library" },
        { "sdl2_mixer", "Gaming", "SDL2 multi-channel audio sound mixer" },
        { "sdl2_ttf", "Gaming", "SDL2 TrueType font rendering engine" },
        { "gamemode", "Gaming", "Optimise Linux system performance on demand" },
        { "mangohud", "Gaming", "Vulkan and OpenGL HUD for monitoring FPS & temps" },

        // Java Runtimes (OpenJDK)
        { "jdk-openjdk", "Java", "Latest OpenJDK Development Kit (Java 25+)" },
        { "jre-openjdk", "Java", "Latest OpenJDK Java Runtime Environment" },
        { "jdk21-openjdk", "Java", "OpenJDK 21 LTS Development Kit" },
        { "jre21-openjdk", "Java", "OpenJDK 21 LTS Java Runtime Environment" },
        { "jdk17-openjdk", "Java", "OpenJDK 17 LTS Development Kit" },
        { "jre17-openjdk", "Java", "OpenJDK 17 LTS Java Runtime Environment" },
        { "jdk8-openjdk", "Java", "OpenJDK 8 Legacy Runtime (Minecraft 1.12.2 / old mods)" },

        // Programming Languages & Tools
        { "python", "Dev", "Python 3 programming language and runtime" },
        { "nodejs", "Dev", "Node.js JavaScript runtime engine" },
        { "gcc", "Dev", "GNU Compiler Collection (C and C++)" },
        { "clang", "Dev", "LLVM C/C++/Objective-C compiler" },
        { "gdb", "Dev", "The GNU Project Debugger" },
        { "cmake", "Dev", "Cross-platform open-source build system" },
        { "make", "Dev", "GNU Make utility" },
        { "rust", "Dev", "Rust systems programming language compiler" },

        // System Libraries & Compression
        { "glib2", "System", "Common C routines & data structures (libglib-2.0)" },
        { "systemd-libs", "System", "Systemd and udev client libraries (libudev.so.1)" },
        { "libpciaccess", "System", "Generic PCI access library for hardware detection" },
        { "libffi", "System", "Foreign Function Interface library" },
        { "libedit", "System", "NetBSD Editline library (BSD readline alternative)" },
        { "ncurses", "System", "Terminal handling & curses library (libncursesw)" },
        { "icu", "System", "International Components for Unicode library" },
        { "libxml2", "System", "XML parsing and manipulation library" },
        { "expat", "System", "Fast XML parser library in C" },
        { "zlib", "System", "General purpose compression library" },
        { "zstd", "System", "Zstandard fast real-time compression algorithm" },
        { "xz", "System", "POSIX-like compression utilities (liblzma)" },
        { "bzip2", "System", "High-quality block-sorting data compressor" },
        { "freetype2", "System", "Font engine and rasterizer library" },
        { "fontconfig", "System", "Font configuration and customization library" },
        { "cairo", "System", "2D vector graphics library with multi-backend output" },
        { "pango", "System", "Text layout and internationalization library" },
        { "gtk3", "System", "GTK 3 Graphical User Interface toolkit" },
        { "gtk4", "System", "GTK 4 Graphical User Interface toolkit" }
    };

    cat.insert(cat.end(), individual.begin(), individual.end());
    return cat;
}

// Read raw key sequence from stdin
enum class KeyAction {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME,
    END,
    SPACE,
    ENTER,
    CTRL_F,
    BACKSPACE,
    ESC,
    SELECT_ALL,
    DESELECT_ALL,
    QUIT,
    CHAR,
    UNKNOWN
};

static KeyAction read_key(char &out_char) {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) {
        return KeyAction::UNKNOWN;
    }

    if (c == 3) { // Ctrl+C
        return KeyAction::QUIT;
    }
    if (c == 6) { // Ctrl+F
        return KeyAction::CTRL_F;
    }
    if (c == 13 || c == 10) { // Enter
        return KeyAction::ENTER;
    }
    if (c == ' ') {
        return KeyAction::SPACE;
    }
    if (c == 127 || c == 8) { // Backspace
        return KeyAction::BACKSPACE;
    }

    if (c == 27) { // Escape sequence
        char seq[4] = {0};
        struct timeval tv = {0, 30000}; // 30ms timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            if (read(STDIN_FILENO, &seq[0], 1) > 0) {
                if (seq[0] == '[') {
                    if (read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if (seq[1] == 'A') return KeyAction::UP;
                        if (seq[1] == 'B') return KeyAction::DOWN;
                        if (seq[1] == 'C') return KeyAction::RIGHT;
                        if (seq[1] == 'D') return KeyAction::LEFT;
                        if (seq[1] == 'H') return KeyAction::HOME;
                        if (seq[1] == 'F') return KeyAction::END;
                        if (seq[1] >= '1' && seq[1] <= '9') {
                            char seq2 = 0;
                            if (read(STDIN_FILENO, &seq2, 1) > 0 && seq2 == '~') {
                                if (seq[1] == '5') return KeyAction::PAGE_UP;
                                if (seq[1] == '6') return KeyAction::PAGE_DOWN;
                                if (seq[1] == '1' || seq[1] == '7') return KeyAction::HOME;
                                if (seq[1] == '4' || seq[1] == '8') return KeyAction::END;
                            }
                        }
                    }
                }
            }
        }
        return KeyAction::ESC;
    }

    out_char = c;
    return KeyAction::CHAR;
}

static std::vector<RepoPackage> load_all_repository_packages(const Config &cfg) {
    std::string path = cfg.cache_dir + "/all_packages.txt";
    std::vector<RepoPackage> res;
    std::ifstream f(path);
    if (!f.is_open()) {
        return res;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        RepoPackage p;
        if (iss >> p.repo >> p.name >> p.filename) {
            res.push_back(p);
        }
    }
    return res;
}

int InstallerUI::run_interactive(const Config &cfg) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        utils::log_error("Interactive installer requires an interactive TTY terminal.");
        return 1;
    }

    std::vector<PackageInfo> catalog = get_default_catalog();
    std::vector<RepoPackage> all_repo_packages = load_all_repository_packages(cfg);

    std::unordered_set<std::string> selected_packages;

    RawTerminalGuard guard;
    g_guard = &guard;
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    size_t cursor = 0;
    size_t scroll_offset = 0;
    std::string search_query = "";
    bool search_mode = false;

    struct DisplayItem {
        std::string name;
        std::string category;
        std::string description;
    };

    auto get_current_items = [&]() -> std::vector<DisplayItem> {
        std::vector<DisplayItem> items;
        std::string q = to_lower(search_query);

        if (q.empty()) {
            for (size_t i = 0; i < catalog.size(); ++i) {
                items.push_back({ catalog[i].name, catalog[i].category, catalog[i].description });
            }
            return items;
        }

        std::unordered_set<std::string> added;

        // 1. Check aliases first (e.g. searching 'libglib' -> suggest 'glib2' and 'electron' bundle)
        for (const auto &alias : LIBRARY_ALIASES) {
            if (alias.first.find(q) != std::string::npos) {
                if (added.insert(alias.second).second) {
                    items.push_back({ alias.second, "Alias (" + alias.first + ")", "Package providing " + alias.first });
                }
            }
        }

        // 2. Curated catalog matches (including bundles)
        for (size_t i = 0; i < catalog.size(); ++i) {
            std::string name = to_lower(catalog[i].name);
            std::string desc = to_lower(catalog[i].description);
            std::string cat  = to_lower(catalog[i].category);
            if (name.find(q) != std::string::npos || desc.find(q) != std::string::npos || cat.find(q) != std::string::npos) {
                if (added.insert(catalog[i].name).second) {
                    items.push_back({ catalog[i].name, catalog[i].category, catalog[i].description });
                }
            }
        }

        // 3. Global repo matches (Arch Linux Core, Extra, Multilib, Chaotic-AUR)
        size_t limit = 150;
        for (const auto &p : all_repo_packages) {
            if (added.count(p.name)) continue;
            std::string name = to_lower(p.name);
            if (name.find(q) != std::string::npos) {
                items.push_back({ p.name, p.repo, "Pre-built binary package from " + p.repo });
                added.insert(p.name);
                if (items.size() >= limit) break;
            }
        }

        // 4. Fallback search entry
        if (!added.count(search_query)) {
            items.push_back({ search_query, "AUR/Arch", "Search and install package '" + search_query + "'" });
        }

        return items;
    };

    while (true) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0 || ws.ws_row == 0) {
            ws.ws_col = 80;
            ws.ws_row = 24;
        }

        std::vector<DisplayItem> items = get_current_items();
        size_t total_display_items = items.size();

        if (cursor >= total_display_items && total_display_items > 0) {
            cursor = total_display_items - 1;
        }

        size_t header_lines = 5;
        size_t footer_lines = 3;
        size_t visible_capacity = (ws.ws_row > (header_lines + footer_lines)) ? (ws.ws_row - header_lines - footer_lines) : 5;

        if (cursor < scroll_offset) {
            scroll_offset = cursor;
        } else if (cursor >= scroll_offset + visible_capacity) {
            scroll_offset = cursor - visible_capacity + 1;
        }

        std::string frame;
        frame.reserve(4096);
        frame += "\033[H";

        // Header
        frame += "\033[1;36m";
        frame += "================================================================================\r\n";
        frame += "      fglibc Package Manager - Bundles (Electron/Gaming) & 18,600+ Packages     \r\n";
        frame += "================================================================================\033[0m\r\n";

        // Search Bar
        if (search_mode) {
            frame += " \033[1;33m[Search - Ctrl+F / Esc to exit]\033[0m \033[1;37m" + search_query + "\033[7m \033[0m\033[K\r\n";
        } else {
            if (!search_query.empty()) {
                frame += " \033[33mFiltered by: \"" + search_query + "\" (" + std::to_string(total_display_items) + " matches, ^F to edit, Esc to clear)\033[0m\033[K\r\n";
            } else {
                frame += " \033[90mSelect bundle (e.g. electron, gaming) or press [^F] to search | [Space] Toggle\033[0m\033[K\r\n";
            }
        }
        frame += "────────────────────────────────────────────────────────────────────────────────\r\n";

        for (size_t row = 0; row < visible_capacity; ++row) {
            size_t item_idx = scroll_offset + row;
            if (item_idx >= total_display_items) {
                frame += "\033[K\r\n";
                continue;
            }

            const auto &it = items[item_idx];
            bool is_cursor = (item_idx == cursor);
            bool is_checked = selected_packages.count(it.name) > 0;

            std::string line = "";
            if (is_cursor) {
                line += "\033[1;36m> \033[0m";
            } else {
                line += "  ";
            }

            if (is_checked) {
                line += "\033[1;32m[x]\033[0m ";
            } else {
                line += "\033[90m[ ]\033[0m ";
            }

            if (is_cursor) {
                line += "\033[1;37m" + it.name + "\033[0m";
            } else {
                line += "\033[37m" + it.name + "\033[0m";
            }

            int pad = 24 - (int)it.name.length();
            if (pad < 1) pad = 1;
            line += std::string(pad, ' ');

            if (it.category == "Bundle") {
                line += "\033[1;33m[" + it.category + "]\033[0m ";
            } else {
                line += "\033[35m[" + it.category + "]\033[0m ";
            }

            line += "\033[90m- " + it.description + "\033[0m";

            line += "\033[K\r\n";
            frame += line;
        }

        // Footer
        frame += "\033[36m────────────────────────────────────────────────────────────────────────────────\033[0m\r\n";
        std::string footer = " Selected: " + std::to_string(selected_packages.size()) + " package(s) | ";
        footer += "[Space] Toggle | [^F] Search | [Enter] Install | [a] All | [u] None | [q] Exit";
        frame += "\033[1;37m" + footer + "\033[0m\033[K\r\n";

        std::string pos_info = " Showing ";
        if (total_display_items > 0) {
            pos_info += std::to_string(scroll_offset + 1) + "-" + 
                        std::to_string(std::min(scroll_offset + visible_capacity, total_display_items)) + 
                        " of " + std::to_string(total_display_items);
        } else {
            pos_info += "0 of 0";
        }
        frame += "\033[90m" + pos_info + "\033[0m\033[K";

        write(STDOUT_FILENO, frame.data(), frame.size());

        char in_char = 0;
        KeyAction act = read_key(in_char);

        if (search_mode) {
            if (act == KeyAction::ESC || act == KeyAction::CTRL_F) {
                search_mode = false;
            } else if (act == KeyAction::ENTER) {
                if (!selected_packages.empty()) {
                    break;
                }
                search_mode = false;
            } else if (act == KeyAction::BACKSPACE) {
                if (!search_query.empty()) {
                    search_query.pop_back();
                    cursor = 0;
                    scroll_offset = 0;
                }
            } else if (act == KeyAction::UP) {
                if (cursor > 0) cursor--;
            } else if (act == KeyAction::DOWN) {
                if (cursor + 1 < total_display_items) cursor++;
            } else if (act == KeyAction::SPACE) {
                if (cursor < total_display_items) {
                    const std::string &sel_name = items[cursor].name;
                    if (selected_packages.count(sel_name)) {
                        selected_packages.erase(sel_name);
                    } else {
                        selected_packages.insert(sel_name);
                    }
                }
            } else if (act == KeyAction::CHAR) {
                if (in_char >= 32 && in_char <= 126) {
                    search_query += in_char;
                    cursor = 0;
                    scroll_offset = 0;
                }
            } else if (act == KeyAction::QUIT) {
                guard.restore();
                return 0;
            }
        } else {
            if (act == KeyAction::QUIT || in_char == 'q' || in_char == 'Q') {
                guard.restore();
                return 0;
            } else if (act == KeyAction::CTRL_F || in_char == '/') {
                search_mode = true;
            } else if (act == KeyAction::UP || in_char == 'k' || in_char == 'K') {
                if (cursor > 0) cursor--;
            } else if (act == KeyAction::DOWN || in_char == 'j' || in_char == 'J') {
                if (cursor + 1 < total_display_items) cursor++;
            } else if (act == KeyAction::PAGE_UP) {
                if (cursor >= 10) cursor -= 10; else cursor = 0;
            } else if (act == KeyAction::PAGE_DOWN) {
                if (cursor + 10 < total_display_items) cursor += 10; else if (total_display_items > 0) cursor = total_display_items - 1;
            } else if (act == KeyAction::HOME) {
                cursor = 0;
            } else if (act == KeyAction::END) {
                if (total_display_items > 0) cursor = total_display_items - 1;
            } else if (act == KeyAction::SPACE) {
                if (cursor < total_display_items) {
                    const std::string &sel_name = items[cursor].name;
                    if (selected_packages.count(sel_name)) {
                        selected_packages.erase(sel_name);
                    } else {
                        selected_packages.insert(sel_name);
                    }
                }
            } else if (act == KeyAction::SELECT_ALL || in_char == 'a' || in_char == 'A') {
                for (const auto &it : items) {
                    selected_packages.insert(it.name);
                }
            } else if (act == KeyAction::DESELECT_ALL || in_char == 'u' || in_char == 'U') {
                selected_packages.clear();
            } else if (act == KeyAction::ESC) {
                if (!search_query.empty()) {
                    search_query.clear();
                    cursor = 0;
                    scroll_offset = 0;
                }
            } else if (act == KeyAction::ENTER) {
                if (!selected_packages.empty()) {
                    break;
                }
            }
        }
    }

    guard.restore();

    std::vector<std::string> to_install(selected_packages.begin(), selected_packages.end());
    if (to_install.empty()) {
        std::cout << utils::colors::YELLOW << "No packages selected for installation." << utils::colors::RESET << "\n";
        return 0;
    }

    return install_packages(cfg, to_install);
}

static bool is_soname_satisfied(const Config &cfg, const std::string &soname) {
    std::string base = soname;
    if (utils::file_exists(cfg.sysroot_dir + "/usr/lib/" + base) ||
        utils::file_exists(cfg.sysroot_dir + "/lib/" + base) ||
        utils::file_exists(cfg.sysroot_dir + "/usr/lib32/" + base) ||
        utils::file_exists(cfg.sysroot_dir + "/lib32/" + base)) {
        return true;
    }
    std::string cmd = "ls \"" + cfg.sysroot_dir + "/usr/lib/" + base + "\"* \"" + cfg.sysroot_dir + "/usr/lib32/" + base + "\"* 2>/dev/null";
    std::string out = utils::run_command_output(cmd);
    return !out.empty();
}

static std::string resolve_dependency_name(const Config &cfg, const std::string &raw_dep) {
    std::string dep = raw_dep;
    size_t op_pos = dep.find_first_of("<>=");
    if (op_pos != std::string::npos) {
        dep = dep.substr(0, op_pos);
    }
    dep = trim_string(dep);
    if (dep.empty()) return "";

    std::string lower = to_lower(dep);

    // Skip self and base glibc / kernel headers / core glibc libraries
    if (lower == "glibc" || lower == "linux-api-headers" || lower == "filesystem" ||
        lower == "libc.so" || lower == "libm.so" || lower == "libpthread.so" ||
        lower == "libdl.so" || lower == "librt.so" || lower == "ld-linux-x86-64.so" ||
        lower == "ld-linux.so" || lower == "libresolv.so" || lower == "libutil.so") {
        return "";
    }

    // GCC runtime libraries (provided by gcc-libs)
    if (lower == "libgcc" || lower == "libstdc++" || lower == "libstdc++.so" ||
        lower == "libstdc++6" || lower == "libgcc_s" || lower == "libgcc_s.so") {
        return "gcc-libs";
    }
    if (lower == "lib32-gcc" || lower == "lib32-libgcc" || lower == "lib32-libstdc++" ||
        lower == "lib32-libstdc++.so" || lower == "lib32-libgcc_s") {
        return "lib32-gcc-libs";
    }

    // Virtual package / provider mappings
    if (lower == "sh") return "bash";
    if (lower == "vulkan-driver") return "vulkan-radeon";
    if (lower == "lib32-vulkan-driver") return "lib32-vulkan-radeon";
    if (lower == "libgl" || lower == "libgles" || lower == "libegl") return "libglvnd";
    if (lower == "lib32-libgl" || lower == "lib32-libgles" || lower == "lib32-libegl") return "lib32-libglvnd";
    if (lower == "ttf-font") return "ttf-dejavu";
    if (lower == "java-runtime") return "jre-openjdk";
    if (lower == "java-environment") return "jdk-openjdk";

    // If it's a soname (e.g. libreadline.so, libncursesw.so, libmp3lame.so.0), check and resolve
    size_t so_idx = lower.find(".so");
    if (so_idx != std::string::npos) {
        std::string base_soname = lower.substr(0, so_idx + 3);
        std::string stripped = lower.substr(0, so_idx);
        std::string no_lib = (stripped.rfind("lib", 0) == 0 && stripped.length() > 3) ? stripped.substr(3) : stripped;

        if (is_soname_satisfied(cfg, lower) || is_soname_satisfied(cfg, base_soname)) {
            return ""; // Already installed and available on disk!
        }

        // 1. Direct aliases check
        auto it = LIBRARY_ALIASES.find(lower);
        if (it != LIBRARY_ALIASES.end()) return it->second;
        it = LIBRARY_ALIASES.find(base_soname);
        if (it != LIBRARY_ALIASES.end()) return it->second;
        it = LIBRARY_ALIASES.find(stripped);
        if (it != LIBRARY_ALIASES.end()) return it->second;
        it = LIBRARY_ALIASES.find(no_lib);
        if (it != LIBRARY_ALIASES.end()) return it->second;

        // 2. Package database match
        std::string all_pkgs = cfg.cache_dir + "/all_packages.txt";
        if (utils::file_exists(all_pkgs)) {
            std::string cmd = "grep -m 1 -E '^[^ ]+ " + stripped + " ' \"" + all_pkgs + "\" 2>/dev/null";
            if (!utils::run_command_output(cmd).empty()) return stripped;
            std::string cmd_nolib = "grep -m 1 -E '^[^ ]+ " + no_lib + " ' \"" + all_pkgs + "\" 2>/dev/null";
            if (!utils::run_command_output(cmd_nolib).empty()) return no_lib;
        }

        return stripped;
    }

    // Check aliases
    auto it = LIBRARY_ALIASES.find(lower);
    if (it != LIBRARY_ALIASES.end()) {
        return it->second;
    }

    return dep;
}

static std::unordered_set<std::string> get_installed_packages(const Config &cfg) {
    std::unordered_set<std::string> installed = {
        "glibc", "gcc-libs", "linux-api-headers", "filesystem"
    };
    std::string tracker_file = cfg.sysroot_dir + "/etc/fglibc-installed-packages.txt";
    if (!utils::file_exists(tracker_file)) {
        // Initialize with packages present in sysroot / cache
        std::string cmd = "ls \"" + cfg.cache_dir + "\"/*.pkg.tar.zst 2>/dev/null | sed -E 's/.*\\/([^-]+)-.*\\.pkg\\.tar\\.zst/\\1/'";
        std::string out = utils::run_command_output(cmd);
        std::istringstream iss(out);
        std::string name;
        std::ofstream ofs(tracker_file);
        if (ofs.is_open()) {
            ofs << "glibc\ngcc-libs\nlinux-api-headers\nfilesystem\n";
        }
        while (iss >> name) {
            installed.insert(to_lower(name));
            if (ofs.is_open()) ofs << name << "\n";
        }
    } else {
        std::ifstream ifs(tracker_file);
        std::string line;
        while (std::getline(ifs, line)) {
            line = trim_string(line);
            if (!line.empty() && line[0] != '#') {
                installed.insert(to_lower(line));
            }
        }
    }
    return installed;
}

static void mark_package_installed(const Config &cfg, const std::string &pkg_name) {
    std::string tracker_file = cfg.sysroot_dir + "/etc/fglibc-installed-packages.txt";
    std::unordered_set<std::string> current = get_installed_packages(cfg);
    if (current.count(to_lower(pkg_name))) {
        return;
    }
    std::ofstream ofs(tracker_file, std::ios::app);
    if (ofs.is_open()) {
        ofs << pkg_name << "\n";
    }
}

// Fallback installer for Arch User Repository (AUR) packages
static bool install_aur_package(const Config &cfg, const std::string &pkg_name, size_t index, size_t total, std::vector<std::string> &discovered_deps) {
    using namespace utils::colors;

    std::string aur_url = "https://aur.archlinux.org/rpc/v5/info?arg[]=" + pkg_name;
    std::string aur_json = utils::run_command_output("curl -sL \"" + aur_url + "\" 2>/dev/null");

    if (aur_json.find("\"resultcount\":1") == std::string::npos && aur_json.find("\"resultcount\": 1") == std::string::npos) {
        return false;
    }

    std::cout << GREEN << BOLD << "[fglibc:info] Found '" << pkg_name << "' in Arch User Repository (AUR)!" << RESET << "\n";

    // Parse dependencies from AUR JSON
    auto parse_json_deps = [&](const std::string &key) {
        size_t pos = aur_json.find("\"" + key + "\":[");
        if (pos != std::string::npos) {
            size_t end_pos = aur_json.find("]", pos);
            if (end_pos != std::string::npos) {
                std::string sub = aur_json.substr(pos + key.length() + 4, end_pos - (pos + key.length() + 4));
                size_t p = 0;
                while (p < sub.length()) {
                    size_t q1 = sub.find('"', p);
                    if (q1 == std::string::npos) break;
                    size_t q2 = sub.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    std::string d = sub.substr(q1 + 1, q2 - q1 - 1);
                    size_t op = d.find_first_of("<>=");
                    if (op != std::string::npos) d = d.substr(0, op);
                    d = trim_string(d);
                    if (!d.empty()) {
                        discovered_deps.push_back(d);
                    }
                    p = q2 + 1;
                }
            }
        }
    };
    parse_json_deps("Depends");
    parse_json_deps("MakeDepends");

    // Check if yay or paru is available in prefix
    std::string yay_bin = cfg.sysroot_dir + "/usr/bin/yay";
    std::string paru_bin = cfg.sysroot_dir + "/usr/bin/paru";

    if (utils::file_exists(yay_bin)) {
        std::cout << CYAN << "Building and installing '" << pkg_name << "' with Yay..." << RESET << "\n";
        int res = Runner::run(cfg, {"yay", "-S", "--noconfirm", pkg_name});
        if (res == 0) {
            mark_package_installed(cfg, pkg_name);
            std::cout << GREEN << BOLD << "✔ [" << index << "/" << total << "] " << pkg_name << " installed from AUR successfully!\n" << RESET;
            return true;
        }
    } else if (utils::file_exists(paru_bin)) {
        std::cout << CYAN << "Building and installing '" << pkg_name << "' with Paru..." << RESET << "\n";
        int res = Runner::run(cfg, {"paru", "-S", "--noconfirm", pkg_name});
        if (res == 0) {
            mark_package_installed(cfg, pkg_name);
            std::cout << GREEN << BOLD << "✔ [" << index << "/" << total << "] " << pkg_name << " installed from AUR successfully!\n" << RESET;
            return true;
        }
    }

    // Direct snapshot build fallback
    std::string aur_dir = cfg.cache_dir + "/aur";
    utils::create_dirs(aur_dir);
    std::string snap_file = aur_dir + "/" + pkg_name + ".tar.gz";
    std::string snap_url = "https://aur.archlinux.org/cgit/aur.git/snapshot/" + pkg_name + ".tar.gz";

    std::cout << BOLD << "[" << index << "/" << total << "] Downloading AUR snapshot: " << RESET << snap_url << "\n";
    if (!utils::download_file(snap_url, snap_file)) {
        utils::log_error("Failed to download AUR snapshot for " + pkg_name);
        return false;
    }

    std::cout << BOLD << "[" << index << "/" << total << "] Extracting AUR build sources..." << RESET << "\n";
    utils::extract_archive(snap_file, aur_dir);
    std::string build_dir = aur_dir + "/" + pkg_name;

    std::cout << BOLD << "[" << index << "/" << total << "] Building package with makepkg..." << RESET << "\n";
    int res = Runner::run(cfg, {"makepkg", "-d", "--noconfirm"}, build_dir);
    if (res != 0) {
        res = Runner::run(cfg, {"fakeroot", "makepkg", "-d", "--noconfirm"}, build_dir);
    }

    std::string cmd_find = "ls \"" + build_dir + "\"/" + pkg_name + "-*.pkg.tar.zst 2>/dev/null | head -n 1";
    std::string pkg_archive = trim_string(utils::run_command_output(cmd_find));
    if (pkg_archive.empty() || !utils::file_exists(pkg_archive)) {
        cmd_find = "ls \"" + build_dir + "\"/*.pkg.tar.zst 2>/dev/null | head -n 1";
        pkg_archive = trim_string(utils::run_command_output(cmd_find));
    }

    if (!pkg_archive.empty() && utils::file_exists(pkg_archive)) {
        std::cout << BOLD << "[" << index << "/" << total << "] Installing built package into sysroot..." << RESET << "\n";
        utils::extract_archive(pkg_archive, cfg.sysroot_dir);

        std::string manifest_dir = cfg.sysroot_dir + "/var/lib/fglibc/manifests";
        utils::create_dirs(manifest_dir);
        std::string manifest_file = manifest_dir + "/" + to_lower(pkg_name) + ".list";
        std::string cmd_list = "tar --zstd -tf \"" + pkg_archive + "\" 2>/dev/null > \"" + manifest_file + "\"";
        utils::run_command(cmd_list);

        mark_package_installed(cfg, pkg_name);
        std::cout << GREEN << BOLD << "✔ [" << index << "/" << total << "] " << pkg_name << " built and installed successfully!\n" << RESET;
        return true;
    }

    utils::log_error("Could not build AUR package: " + pkg_name);
    return false;
}

// Download and install a package by name with a live progress bar
static bool install_single_package(const Config &cfg, const std::string &pkg_name, size_t index, size_t total, std::vector<std::string> &discovered_deps, bool no_cache = false) {
    using namespace utils::colors;

    std::cout << BOLD << CYAN << "\n[" << index << "/" << total << "] Resolving package: " << RESET << BOLD << pkg_name << RESET << "...\n";

    std::string all_pkgs_cache = cfg.cache_dir + "/all_packages.txt";
    std::string filename = "";
    std::string mirror = "";

    // 1. Check all_packages.txt index
    if (utils::file_exists(all_pkgs_cache)) {
        std::string cmd = "grep -m 1 -E '^[^ ]+ " + pkg_name + " ' \"" + all_pkgs_cache + "\" 2>/dev/null";
        std::string line = utils::run_command_output(cmd);
        if (!line.empty()) {
            std::istringstream iss(line);
            std::string repo, name, fn;
            if (iss >> repo >> name >> fn) {
                filename = fn;
                if (repo == "core") mirror = CORE_MIRROR;
                else if (repo == "extra") mirror = EXTRA_MIRROR;
                else if (repo == "multilib") mirror = MULTILIB_MIRROR;
                else if (repo == "chaotic-aur") mirror = CHAOTIC_MIRROR;
            }
        }
    }

    // 2. Check individual cached indices
    if (filename.empty()) {
        std::string core_cache = cfg.cache_dir + "/core_index.html";
        std::string extra_cache = cfg.cache_dir + "/extra_index.html";
        std::string multilib_cache = cfg.cache_dir + "/multilib_index.html";
        std::string chaotic_cache = cfg.cache_dir + "/chaotic_index.html";

        std::string cmd_core = "grep -m 1 -oE 'href=\"" + pkg_name + "-([0-9]+%3A|[0-9])[^\"]*\\.pkg\\.tar\\.zst\"' \"" + core_cache + "\" 2>/dev/null | head -n 1 | sed -E 's/.*href=\"([^\"]+)\"/\\1/'";
        filename = utils::run_command_output(cmd_core);
        if (!filename.empty()) {
            mirror = CORE_MIRROR;
        } else {
            std::string cmd_extra = "grep -m 1 -oE 'href=\"" + pkg_name + "-([0-9]+%3A|[0-9])[^\"]*\\.pkg\\.tar\\.zst\"' \"" + extra_cache + "\" 2>/dev/null | head -n 1 | sed -E 's/.*href=\"([^\"]+)\"/\\1/'";
            filename = utils::run_command_output(cmd_extra);
            if (!filename.empty()) {
                mirror = EXTRA_MIRROR;
            } else {
                std::string cmd_multi = "grep -m 1 -oE 'href=\"" + pkg_name + "-([0-9]+%3A|[0-9])[^\"]*\\.pkg\\.tar\\.zst\"' \"" + multilib_cache + "\" 2>/dev/null | head -n 1 | sed -E 's/.*href=\"([^\"]+)\"/\\1/'";
                filename = utils::run_command_output(cmd_multi);
                if (!filename.empty()) {
                    mirror = MULTILIB_MIRROR;
                } else {
                    std::string cmd_ch = "grep -m 1 -oE 'href=\"" + pkg_name + "-([0-9]+%3A|[0-9])[^\"]*\\.pkg\\.tar\\.zst\"' \"" + chaotic_cache + "\" 2>/dev/null | head -n 1 | sed -E 's/.*href=\"([^\"]+)\"/\\1/'";
                    filename = utils::run_command_output(cmd_ch);
                    if (!filename.empty()) {
                        mirror = CHAOTIC_MIRROR;
                    }
                }
            }
        }
    }

    // 3. Fallback to live mirror lookup
    if (filename.empty()) {
        const std::pair<std::string, std::string> live_mirrors[] = {
            { CORE_MIRROR, "Core" },
            { EXTRA_MIRROR, "Extra" },
            { MULTILIB_MIRROR, "Multilib" },
            { CHAOTIC_MIRROR, "Chaotic-AUR" }
        };
        for (const auto &m : live_mirrors) {
            std::string cmd_live = "curl --connect-timeout 4 --max-time 15 -sL \"" + m.first + "\" | grep -m 1 -oE 'href=\"" + pkg_name + "-([0-9]+%3A|[0-9])[^\"]*\\.pkg\\.tar\\.zst\"' | head -n 1 | sed -E 's/href=\"([^\"]+)\"/\\1/'";
            filename = utils::run_command_output(cmd_live);
            if (!filename.empty()) {
                mirror = m.first;
                break;
            }
        }
    }

    if (filename.empty()) {
        // Fallback to Arch User Repository (AUR)
        if (install_aur_package(cfg, pkg_name, index, total, discovered_deps)) {
            return true;
        }

        utils::log_error("Could not find package '" + pkg_name + "' in Arch Linux, Chaotic-AUR, or AUR repositories.");
        return false;
    }

    std::string url = mirror + filename;
    std::string local_path = cfg.cache_dir + "/" + filename;

    std::cout << BOLD << "[" << index << "/" << total << "] Downloading: " << RESET << filename << "\n";
    std::cout << utils::colors::CYAN << "From: " << mirror << utils::colors::RESET << "\n";

    if (no_cache && utils::file_exists(local_path)) {
        unlink(local_path.c_str());
    }

    if (!utils::file_exists(local_path)) {
        if (!utils::download_file(url, local_path)) {
            utils::log_error("Failed to download " + filename);
            return false;
        }
    } else {
        utils::log_info("Using cached package file: " + filename);
    }

    std::cout << BOLD << "[" << index << "/" << total << "] Extracting into glibc sysroot..." << RESET << "\n";
    if (!utils::extract_archive(local_path, cfg.sysroot_dir)) {
        utils::log_error("Failed to extract package " + filename);
        return false;
    }

    // Save package manifest for clean uninstallation
    std::string manifest_dir = cfg.sysroot_dir + "/var/lib/fglibc/manifests";
    utils::create_dirs(manifest_dir);
    std::string manifest_file = manifest_dir + "/" + to_lower(pkg_name) + ".list";
    std::string cmd_list = "tar --zstd -tf \"" + local_path + "\" 2>/dev/null > \"" + manifest_file + "\"";
    utils::run_command(cmd_list);

    // Inspect package provides from .PKGINFO and mark them satisfied
    std::string cmd_prov = "tar --zstd -xOf \"" + local_path + "\" .PKGINFO 2>/dev/null | grep -E '^provides = ' | sed 's/^provides = //'";
    std::string prov_output = utils::run_command_output(cmd_prov);
    if (!prov_output.empty()) {
        std::istringstream iss(prov_output);
        std::string prov_line;
        while (std::getline(iss, prov_line)) {
            size_t op_pos = prov_line.find_first_of("<>=");
            if (op_pos != std::string::npos) {
                prov_line = prov_line.substr(0, op_pos);
            }
            prov_line = trim_string(prov_line);
            if (!prov_line.empty()) {
                mark_package_installed(cfg, prov_line);
            }
        }
    }

    // Inspect package dependencies from .PKGINFO
    std::string cmd_deps = "tar --zstd -xOf \"" + local_path + "\" .PKGINFO 2>/dev/null | grep -E '^depend = ' | sed 's/^depend = //'";
    std::string deps_output = utils::run_command_output(cmd_deps);
    if (!deps_output.empty()) {
        std::istringstream iss(deps_output);
        std::string dep_line;
        while (std::getline(iss, dep_line)) {
            std::string resolved = resolve_dependency_name(cfg, dep_line);
            if (!resolved.empty()) {
                discovered_deps.push_back(resolved);
            }
        }
    }

    // 4. Binary ELF Scan: inspect every extracted ELF binary/library for missing DT_NEEDED
    if (utils::file_exists(manifest_file)) {
        std::ifstream mifs(manifest_file);
        std::string mentry;
        while (std::getline(mifs, mentry)) {
            mentry = trim_string(mentry);
            if (mentry.empty()) continue;
            while (!mentry.empty() && (mentry[0] == '/' || mentry[0] == '.')) {
                if (mentry.length() >= 2 && mentry[0] == '.' && mentry[1] == '/') mentry = mentry.substr(2);
                else if (mentry[0] == '/') mentry = mentry.substr(1);
                else break;
            }
            if (mentry.empty()) continue;
            std::string fpath = cfg.sysroot_dir + "/" + mentry;
            int fd = open(fpath.c_str(), O_RDONLY);
            if (fd >= 0) {
                char magic[4] = {0};
                if (read(fd, magic, 4) == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                    close(fd);
                    ElfInfo info = ElfParser::parse(fpath, cfg);
                    if (info.valid) {
                        for (const auto &dep : info.resolved_dependencies) {
                            if (!dep.found_in_prefix) {
                                std::string resolved = resolve_dependency_name(cfg, dep.name);
                                if (!resolved.empty()) {
                                    discovered_deps.push_back(resolved);
                                }
                            }
                        }
                    }
                } else {
                    close(fd);
                }
            }
        }
    }

    std::cout << GREEN << BOLD << "✔ [" << index << "/" << total << "] " << pkg_name << " installed successfully!\n" << RESET;
    return true;
}

int InstallerUI::install_packages(const Config &cfg, const std::vector<std::string> &pkgs, bool no_cache) {
    using namespace utils::colors;

    if (no_cache) {
        std::cout << YELLOW << "[fglibc:info] --no-cache enabled: bypassing cache for downloaded packages." << RESET << "\n";
    }

    // Expand meta-packages / bundles (like electron, gaming, steam, etc.) and aliases
    std::vector<std::string> initial_pkgs = expand_bundles_and_aliases(pkgs);
    std::unordered_set<std::string> installed = get_installed_packages(cfg);

    std::vector<std::string> queue = initial_pkgs;
    std::unordered_set<std::string> queued;
    for (const auto &p : queue) {
        queued.insert(to_lower(p));
    }

    std::cout << BOLD << "\n======================================================================\n"
              << "                      fglibc Package Installer                        \n"
              << "======================================================================\n" << RESET;

    if (queue.size() > pkgs.size()) {
        std::cout << YELLOW << "Expanded app presets: " << queue.size() << " packages queued." << RESET << "\n";
    }

    std::cout << BOLD << "Target Packages: " << RESET;
    for (size_t i = 0; i < queue.size(); ++i) {
        std::cout << CYAN << queue[i] << RESET << (i + 1 < queue.size() ? ", " : "\n\n");
    }

    size_t successful = 0;
    size_t current_idx = 0;

    while (current_idx < queue.size()) {
        std::string current_pkg = queue[current_idx];
        current_idx++;

        std::string lower_pkg = to_lower(current_pkg);

        // If it was an auto-discovered dependency and is already installed, skip
        if (installed.count(lower_pkg) && current_idx > initial_pkgs.size()) {
            successful++;
            continue;
        }

        std::vector<std::string> discovered_deps;
        if (install_single_package(cfg, current_pkg, current_idx, queue.size(), discovered_deps, no_cache)) {
            successful++;
            installed.insert(lower_pkg);
            mark_package_installed(cfg, current_pkg);

            size_t added = 0;
            for (const auto &dep : discovered_deps) {
                std::string lower_dep = to_lower(dep);
                if (!installed.count(lower_dep) && !queued.count(lower_dep)) {
                    queued.insert(lower_dep);
                    queue.push_back(dep);
                    added++;
                }
            }
            if (added > 0) {
                std::cout << GREEN << "  ✔ Auto-detected " << added << " new dependenc"
                          << (added == 1 ? "y" : "ies") << " for " << current_pkg << RESET << "\n";
            }
        }
    }

    std::cout << BOLD << "\nRebuilding glibc dynamic linker cache (64-bit & 32-bit)..." << RESET << "\n";
    BootstrapManager::rebuild_ld_cache(cfg);

    // Automatically sync and export desktop shortcuts & icons for newly installed apps
    sync_desktop_entries(cfg, false);

    std::cout << BOLD << "\n======================================================================\n";
    if (successful == queue.size()) {
        std::cout << GREEN << "✔ Successfully installed all " << successful << " package(s) and dependencies!" << RESET << "\n";
    } else {
        std::cout << YELLOW << "Installed " << successful << " of " << queue.size() << " package(s) with some errors." << RESET << "\n";
    }
    std::cout << BOLD << "======================================================================\n" << RESET;

    return (successful == queue.size()) ? 0 : 1;
}

int InstallerUI::uninstall_packages(const Config &cfg, const std::vector<std::string> &pkgs) {
    using namespace utils::colors;
    if (pkgs.empty()) {
        list_installed_packages(cfg);
        std::cout << "\nUsage: fglibc uninstall <pkg1> [pkg2...]\n\n";
        return 0;
    }

    std::cout << BOLD << "\n======================================================================\n"
              << "                     fglibc Package Uninstaller                       \n"
              << "======================================================================\n" << RESET;

    std::string manifest_dir = cfg.sysroot_dir + "/var/lib/fglibc/manifests";
    std::string tracker_file = cfg.sysroot_dir + "/etc/fglibc-installed-packages.txt";

    // Base protected packages that must never be uninstalled
    static const std::unordered_set<std::string> protected_pkgs = {
        "glibc", "gcc-libs", "linux-api-headers", "filesystem", "base"
    };

    static const std::unordered_set<std::string> protected_dirs = {
        "usr", "usr/bin", "usr/lib", "usr/lib32", "usr/lib64", "usr/share",
        "usr/share/applications", "usr/share/icons", "usr/share/man", "usr/share/doc",
        "usr/include", "usr/local", "usr/local/bin", "bin", "lib", "lib32", "lib64",
        "sbin", "etc", "var", "var/lib", "var/lib/fglibc", "var/lib/fglibc/manifests",
        "tmp", "opt", "dev", "proc", "sys", "home", "root"
    };

    std::unordered_set<std::string> removed_pkgs;
    size_t total_files_removed = 0;

    for (const auto &raw_pkg : pkgs) {
        std::string pkg = to_lower(trim_string(raw_pkg));
        if (pkg.empty()) continue;

        if (protected_pkgs.count(pkg)) {
            std::cout << YELLOW << "[!] Skipping protected base system package: " << pkg << RESET << "\n";
            continue;
        }

        std::string manifest_file = manifest_dir + "/" + pkg + ".list";
        if (!utils::file_exists(manifest_file)) {
            // Try looking in cache archives to construct file list
            std::string cmd_find_tar = "ls \"" + cfg.cache_dir + "\"/" + pkg + "-*.pkg.tar.zst 2>/dev/null | head -n 1";
            std::string archive = trim_string(utils::run_command_output(cmd_find_tar));
            if (!archive.empty() && utils::file_exists(archive)) {
                utils::create_dirs(manifest_dir);
                std::string cmd_gen = "tar --zstd -tf \"" + archive + "\" 2>/dev/null > \"" + manifest_file + "\"";
                utils::run_command(cmd_gen);
            }
        }

        size_t pkg_files_removed = 0;
        std::vector<std::string> candidate_dirs;

        if (utils::file_exists(manifest_file)) {
            std::ifstream ifs(manifest_file);
            std::string entry;
            while (std::getline(ifs, entry)) {
                entry = trim_string(entry);
                if (entry.empty()) continue;
                if (entry[0] == '.' && entry.find('/') == std::string::npos) continue;
                if (entry.find(".PKGINFO") != std::string::npos ||
                    entry.find(".BUILDINFO") != std::string::npos ||
                    entry.find(".MTREE") != std::string::npos ||
                    entry.find(".INSTALL") != std::string::npos) {
                    continue;
                }
                while (!entry.empty() && (entry[0] == '/' || entry[0] == '.')) {
                    if (entry.length() >= 2 && entry[0] == '.' && entry[1] == '/') entry = entry.substr(2);
                    else if (entry[0] == '/') entry = entry.substr(1);
                    else break;
                }
                if (entry.empty()) continue;

                std::string full_path = cfg.sysroot_dir + "/" + entry;
                struct stat st;
                if (lstat(full_path.c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        if (!protected_dirs.count(entry)) {
                            candidate_dirs.push_back(entry);
                        }
                    } else {
                        if (unlink(full_path.c_str()) == 0) {
                            pkg_files_removed++;
                        }
                    }
                }
            }
            ifs.close();
            unlink(manifest_file.c_str());
        } else {
            // Fallback: check typical locations
            std::vector<std::string> fallbacks = {
                cfg.sysroot_dir + "/usr/bin/" + pkg,
                cfg.sysroot_dir + "/bin/" + pkg,
                cfg.sysroot_dir + "/opt/" + pkg
            };
            for (const auto &f : fallbacks) {
                if (utils::file_exists(f)) {
                    unlink(f.c_str());
                    pkg_files_removed++;
                }
                if (utils::dir_exists(f)) {
                    std::string rm_cmd = "rm -rf \"" + f + "\"";
                    utils::run_command(rm_cmd);
                    pkg_files_removed++;
                }
            }
        }

        // Clean up empty directories from deepest to shallowest
        std::sort(candidate_dirs.begin(), candidate_dirs.end(), [](const std::string &a, const std::string &b) {
            return a.length() > b.length();
        });
        for (const auto &d : candidate_dirs) {
            if (!protected_dirs.count(d)) {
                std::string full_dir = cfg.sysroot_dir + "/" + d;
                rmdir(full_dir.c_str());
            }
        }

        removed_pkgs.insert(pkg);
        total_files_removed += pkg_files_removed;
        std::cout << GREEN << "✔ Uninstalled " << BOLD << pkg << RESET 
                  << " (removed " << pkg_files_removed << " files)\n";
    }

    // Update etc/fglibc-installed-packages.txt
    if (!removed_pkgs.empty() && utils::file_exists(tracker_file)) {
        std::ifstream ifs(tracker_file);
        std::vector<std::string> remaining;
        std::string line;
        while (std::getline(ifs, line)) {
            std::string trimmed = trim_string(line);
            if (trimmed.empty()) continue;
            if (!removed_pkgs.count(to_lower(trimmed))) {
                remaining.push_back(line);
            }
        }
        ifs.close();
        std::ofstream ofs(tracker_file);
        for (const auto &rem : remaining) {
            ofs << rem << "\n";
        }
    }

    std::cout << BOLD << "\nRebuilding glibc dynamic linker cache..." << RESET << "\n";
    BootstrapManager::rebuild_ld_cache(cfg);

    std::cout << BOLD << "======================================================================\n";
    std::cout << GREEN << "✔ Uninstallation complete! Total removed files: " << total_files_removed << RESET << "\n";
    std::cout << BOLD << "======================================================================\n" << RESET;

    return 0;
}

int InstallerUI::clean_cache(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\nCleaning package download cache in: " << CYAN << cfg.cache_dir << RESET << "...\n";
    if (!utils::dir_exists(cfg.cache_dir)) {
        std::cout << GREEN << "Cache directory is already empty." << RESET << "\n";
        return 0;
    }

    std::string size_cmd = "du -sh \"" + cfg.cache_dir + "\" 2>/dev/null | cut -f1";
    std::string size_before = trim_string(utils::run_command_output(size_cmd));
    if (size_before.empty()) size_before = "0B";

    std::string rm_cmd = "rm -f \"" + cfg.cache_dir + "\"/*.pkg.tar.zst 2>/dev/null";
    utils::run_command(rm_cmd);

    std::cout << GREEN << "✔ Package cache successfully cleaned! Space freed: " << BOLD << size_before << RESET << "\n\n";
    return 0;
}

void InstallerUI::list_installed_packages(const Config &cfg) {
    using namespace utils::colors;
    std::unordered_set<std::string> pkgs = get_installed_packages(cfg);
    std::vector<std::string> sorted(pkgs.begin(), pkgs.end());
    std::sort(sorted.begin(), sorted.end());

    std::cout << BOLD << "\nInstalled Packages in fglibc sysroot (" << sorted.size() << "):\n" << RESET;
    int col = 0;
    for (const auto &p : sorted) {
        std::cout << "  " << CYAN << std::left << std::setw(24) << p << RESET;
        col++;
        if (col % 3 == 0) std::cout << "\n";
    }
    if (col % 3 != 0) std::cout << "\n";
}

int InstallerUI::rank_mirrors(const Config &cfg, bool verbose) {
    using namespace utils::colors;
    if (verbose) {
        std::cout << BOLD << "\n======================================================================\n"
                  << "               fglibc Fastest Mirror Benchmark & Ranking              \n"
                  << "======================================================================\n" << RESET;
        std::cout << CYAN << "[i] Testing latency to global Arch Linux mirrors in parallel...\n\n" << RESET;
    }

    struct Candidate {
        std::string name;
        std::string base_url;
        int latency_ms = 99999;
        bool ok = false;
    };

    std::vector<Candidate> candidates = {
        { "Fastly Worldwide CDN", "https://fastly.mirror.pkgbuild.com" },
        { "Geo-DNS Worldwide", "https://geo.mirror.pkgbuild.com" },
        { "Yandex (RU / EU)", "https://mirror.yandex.ru/archlinux" },
        { "TrueNetwork (RU / Global)", "https://mirror.truenetwork.ru/archlinux" },
        { "Kernel.org Mirror", "https://mirrors.kernel.org/archlinux" },
        { "Leaseweb (EU / US)", "https://mirror.leaseweb.net/archlinux" },
        { "Rackspace (US / Global)", "https://mirror.rackspace.com/archlinux" },
        { "Thaller (EU)", "https://archlinux.thaller.ws" },
        { "F4st Host", "https://mirror.f4st.host/archlinux" },
        { "Osbeck Mirror", "https://mirror.osbeck.com/archlinux" }
    };

    // Benchmark in parallel
    std::vector<std::future<void>> futures;
    for (auto &c : candidates) {
        futures.push_back(std::async(std::launch::async, [&c]() {
            std::string test_url = c.base_url + "/core/os/x86_64/core.db";
            std::string cmd = "curl -s -o /dev/null -w \"%{http_code} %{time_total}\" --connect-timeout 2 -m 4 \"" + test_url + "\" 2>/dev/null";
            std::string out = utils::run_command_output(cmd);
            std::istringstream iss(out);
            int code = 0;
            double sec = 99.0;
            if (iss >> code >> sec) {
                if (code == 200 && sec > 0.001) {
                    c.latency_ms = static_cast<int>(sec * 1000.0);
                    c.ok = true;
                }
            }
        }));
    }

    for (auto &f : futures) {
        f.get();
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.latency_ms < b.latency_ms;
    });

    if (verbose) {
        std::cout << BOLD << std::left << std::setw(32) << "Mirror Name" 
                  << std::setw(42) << "Base URL" 
                  << "Latency" << RESET << "\n";
        std::cout << std::string(82, '-') << "\n";

        for (const auto &c : candidates) {
            std::cout << std::left << std::setw(32) << c.name
                      << std::setw(42) << c.base_url;
            if (c.ok) {
                if (c.latency_ms < 60) {
                    std::cout << GREEN << BOLD << c.latency_ms << " ms" << RESET << "\n";
                } else if (c.latency_ms < 150) {
                    std::cout << YELLOW << c.latency_ms << " ms" << RESET << "\n";
                } else {
                    std::cout << c.latency_ms << " ms\n";
                }
            } else {
                std::cout << RED << "Timeout / Error" << RESET << "\n";
            }
        }
    }

    // Write ranked mirrors to sysroot/etc/pacman.d/mirrorlist
    std::string mlist = cfg.sysroot_dir + "/etc/pacman.d/mirrorlist";
    utils::create_dirs(cfg.sysroot_dir + "/etc/pacman.d");

    std::ostringstream oss;
    oss << "## Arch Linux mirrorlist ranked by fglibc setup\n";
    oss << "## Generated automatically by fglibc fastest mirror benchmark\n\n";

    std::string fastest_url = "https://geo.mirror.pkgbuild.com";
    bool found_fastest = false;

    for (const auto &c : candidates) {
        if (c.ok) {
            if (!found_fastest) {
                fastest_url = c.base_url;
                found_fastest = true;
            }
            oss << "Server = " << c.base_url << "/$repo/os/$arch\n";
        }
    }

    if (!found_fastest) {
        oss << "Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\n";
        oss << "Server = https://fastly.mirror.pkgbuild.com/$repo/os/$arch\n";
    }

    utils::write_text_file(mlist, oss.str());
    update_active_mirrors(fastest_url);

    if (verbose) {
        std::cout << "\n" << GREEN << BOLD << "✔ Fastest mirror selected: " << CYAN << fastest_url << RESET << "\n";
        std::cout << GREEN << "✔ Ranked mirrorlist saved to: " << mlist << RESET << "\n";
        std::cout << BOLD << "======================================================================\n\n" << RESET;
    }

    return 0;
}

int InstallerUI::sync_desktop_entries(const Config &cfg, bool verbose) {
    using namespace utils::colors;
    if (verbose) {
        std::cout << BOLD << "\n======================================================================\n"
                  << "     fglibc Desktop & Launcher Integration (KDE, GNOME, Hyprland, Rofi) \n"
                  << "======================================================================\n" << RESET;
        std::cout << CYAN << "[i] Synchronizing applications and icons from fglibc sysroot...\n" << RESET;
    }

    std::string host_apps = utils::get_home_dir() + "/.local/share/applications";
    std::string host_icons = utils::get_home_dir() + "/.local/share/icons";
    std::string host_pixmaps = utils::get_home_dir() + "/.local/share/pixmaps";

    utils::create_dirs(host_apps);
    utils::create_dirs(host_icons);
    utils::create_dirs(host_pixmaps);

    // 1. Synchronize icons from sysroot to host
    std::string sys_icons = cfg.sysroot_dir + "/usr/share/icons";
    std::string sys_pixmaps = cfg.sysroot_dir + "/usr/share/pixmaps";
    if (utils::dir_exists(sys_icons)) {
        utils::run_command("cp -rn \"" + sys_icons + "/\"* \"" + host_icons + "/\" 2>/dev/null || true", false);
    }
    if (utils::dir_exists(sys_pixmaps)) {
        utils::run_command("cp -rn \"" + sys_pixmaps + "/\"* \"" + host_pixmaps + "/\" 2>/dev/null || true", false);
    }

    // 2. Scan opt for standalone app icons
    std::string opt_icon_scan = 
        "find \"" + cfg.sysroot_dir + "/opt\" -type f \\( -name \"*.png\" -o -name \"*.svg\" \\) 2>/dev/null | while read -r icon; do "
        "b=$(basename \"$icon\"); "
        "case \"$b\" in "
        "*icon*|*app*|*logo*|*main*) cp -n \"$icon\" \"" + host_pixmaps + "/\" 2>/dev/null || true ;; "
        "esac; "
        "done";
    utils::run_command(opt_icon_scan, false);

    // 3. Process desktop files in sysroot
    std::string cmd_find = "find \"" + cfg.sysroot_dir + "/usr/share/applications\" \"" + cfg.sysroot_dir + "/opt\" -name \"*.desktop\" 2>/dev/null";
    std::string out = utils::run_command_output(cmd_find);
    std::istringstream iss(out);
    std::string dfile;
    size_t exported = 0;

    while (std::getline(iss, dfile)) {
        dfile = trim_string(dfile);
        if (dfile.empty() || !utils::file_exists(dfile)) continue;

        std::string bname = dfile;
        size_t sl = bname.rfind('/');
        if (sl != std::string::npos) bname = bname.substr(sl + 1);

        if (bname.find("avahi") != std::string::npos ||
            bname.find("bssh") != std::string::npos ||
            bname.find("bvnc") != std::string::npos ||
            bname.find("pinentry") != std::string::npos ||
            bname.find("mimeinfo") != std::string::npos) {
            continue;
        }

        std::ifstream ifs(dfile);
        if (!ifs.is_open()) continue;

        std::ostringstream oss;
        std::string line;
        bool has_exec = false;
        bool has_terminal = false;

        while (std::getline(ifs, line)) {
            if (line.rfind("Exec=", 0) == 0) {
                has_exec = true;
                std::string exec_cmd = trim_string(line.substr(5));
                if (!exec_cmd.empty() && exec_cmd[0] == '"') {
                    size_t qend = exec_cmd.find('"', 1);
                    if (qend != std::string::npos) {
                        exec_cmd = exec_cmd.substr(1, qend - 1) + exec_cmd.substr(qend + 1);
                    }
                }
                if (exec_cmd.rfind("fglibc ", 0) != 0 && exec_cmd.rfind("/usr/local/bin/fglibc ", 0) != 0) {
                    oss << "Exec=fglibc " << exec_cmd << "\n";
                } else {
                    oss << "Exec=" << exec_cmd << "\n";
                }
            } else if (line.rfind("TryExec=", 0) == 0) {
                oss << "TryExec=fglibc\n";
            } else if (line.rfind("Terminal=", 0) == 0) {
                has_terminal = true;
                oss << "Terminal=false\n";
            } else {
                oss << line << "\n";
            }
        }
        ifs.close();
        if (!has_exec) continue;

        if (!has_terminal) {
            oss << "Terminal=false\n";
        }

        std::string target = host_apps + "/" + bname;
        utils::write_text_file(target, oss.str());
        chmod(target.c_str(), 0755);
        exported++;
        if (verbose) {
            std::cout << GREEN << "  ✔ Exported shortcut: " << CYAN << bname << RESET << "\n";
        }
    }

    // 4. Update desktop caches
    utils::run_command("command -v update-desktop-database >/dev/null && update-desktop-database \"" + host_apps + "\" 2>/dev/null || true", false);
    utils::run_command("command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 2>/dev/null || true", false);
    utils::run_command("command -v kbuildsycoca5 >/dev/null && kbuildsycoca5 2>/dev/null || true", false);
    utils::run_command("command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t \"" + host_icons + "/hicolor\" 2>/dev/null || true", false);

    if (verbose) {
        std::cout << "\n" << GREEN << BOLD << "✔ Successfully exported " << exported 
                  << " desktop application(s) to KDE Plasma, GNOME, Hyprland, and Rofi!" << RESET << "\n";
        std::cout << BOLD << "======================================================================\n\n" << RESET;
    }

    return 0;
}

bool InstallerUI::setup_pacman(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[1/6] Configuring Pacman package manager & ALPM database..." << RESET << "\n";

    std::string pacman_bin = cfg.sysroot_dir + "/usr/bin/pacman";
    if (!utils::file_exists(pacman_bin)) {
        std::cout << CYAN << "Pacman binary missing. Installing pacman..." << RESET << "\n";
        install_packages(cfg, {"pacman"});
    }

    // 1. Initialize ALPM database directories and version
    std::string alpm_dir = cfg.sysroot_dir + "/var/lib/pacman/local";
    utils::create_dirs(alpm_dir);
    utils::create_dirs(cfg.sysroot_dir + "/var/lib/pacman/sync");
    utils::create_dirs(cfg.sysroot_dir + "/var/cache/pacman/pkg");

    std::string db_ver = alpm_dir + "/ALPM_DB_VERSION";
    if (!utils::file_exists(db_ver)) {
        utils::write_text_file(db_ver, "9\n");
    }

    // 2. Configure pacman.conf
    std::string pconf = cfg.sysroot_dir + "/etc/pacman.conf";
    if (utils::file_exists(pconf)) {
        std::string content = utils::read_text_file(pconf);
        bool changed = false;
        size_t pos = content.find("DownloadUser = alpm");
        if (pos != std::string::npos && content.find("#DownloadUser = alpm") == std::string::npos) {
            content.replace(pos, std::string("DownloadUser = alpm").length(), "#DownloadUser = alpm");
            changed = true;
        }
        pos = content.find("SigLevel    = Required");
        if (pos != std::string::npos) {
            size_t eol = content.find('\n', pos);
            content.replace(pos, (eol != std::string::npos ? eol - pos : content.length() - pos), "SigLevel = Never");
            changed = true;
        }
        if (content.find("[chaotic-aur]") == std::string::npos) {
            content += "\n[chaotic-aur]\nSigLevel = Never\nServer = https://geo-mirror.chaotic.cx/chaotic-aur/x86_64/\n";
            changed = true;
        }
        if (changed) {
            utils::write_text_file(pconf, content);
        }
    }

    // 3. Ensure mirrorlist has active mirrors
    std::string mlist = cfg.sysroot_dir + "/etc/pacman.d/mirrorlist";
    if (!utils::file_exists(mlist) || utils::read_text_file(mlist).empty()) {
        std::string default_mirrors = 
            "Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\n"
            "Server = https://fastly.mirror.pkgbuild.com/$repo/os/$arch\n"
            "Server = https://mirror.yandex.ru/archlinux/$repo/os/$arch\n";
        utils::create_dirs(cfg.sysroot_dir + "/etc/pacman.d");
        utils::write_text_file(mlist, default_mirrors);
    }

    // 4. Download sync databases if missing
    std::string sync_dir = cfg.sysroot_dir + "/var/lib/pacman/sync";
    std::vector<std::pair<std::string, std::string>> dbs = {
        { "core.db", "https://geo.mirror.pkgbuild.com/core/os/x86_64/core.db" },
        { "extra.db", "https://geo.mirror.pkgbuild.com/extra/os/x86_64/extra.db" },
        { "multilib.db", "https://geo.mirror.pkgbuild.com/multilib/os/x86_64/multilib.db" },
        { "chaotic-aur.db", "https://geo-mirror.chaotic.cx/chaotic-aur/x86_64/chaotic-aur.db" }
    };
    for (const auto &d : dbs) {
        std::string target = sync_dir + "/" + d.first;
        if (!utils::file_exists(target)) {
            std::cout << CYAN << "  Downloading ALPM database " << d.first << "..." << RESET << "\n";
            utils::download_file(d.second, target);
        }
    }

    std::cout << GREEN << "✔ Pacman package manager and ALPM database configured successfully!" << RESET << "\n";
    return true;
}

bool InstallerUI::setup_yay(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[2/6] Configuring Yay (Arch User Repository Helper)..." << RESET << "\n";
    std::string yay_bin = cfg.sysroot_dir + "/usr/bin/yay";
    if (utils::file_exists(yay_bin)) {
        std::cout << GREEN << "✔ Yay is already installed at: " << yay_bin << RESET << "\n";
        return true;
    }
    setup_pacman(cfg);
    install_packages(cfg, {"yay", "git", "fakeroot"});
    if (utils::file_exists(yay_bin)) {
        std::cout << GREEN << "✔ Yay installed and verified successfully!" << RESET << "\n";
        return true;
    }
    std::cout << YELLOW << "[!] Could not install yay from repository." << RESET << "\n";
    return false;
}

bool InstallerUI::setup_paru(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[3/6] Configuring Paru (Rust-based AUR Helper)..." << RESET << "\n";
    std::string paru_bin = cfg.sysroot_dir + "/usr/bin/paru";
    if (utils::file_exists(paru_bin)) {
        std::string check = utils::run_command_output("fglibc paru --version 2>&1");
        if (check.find("paru v") != std::string::npos) {
            std::cout << GREEN << "✔ Paru is already installed: " << trim_string(check) << RESET << "\n";
            return true;
        }
    }

    setup_pacman(cfg);
    std::string paru_url = "https://geo-mirror.chaotic.cx/chaotic-aur/x86_64/paru-2.1.0-2.1-x86_64.pkg.tar.zst";
    std::string paru_archive = cfg.cache_dir + "/paru-2.1.0-2.1-x86_64.pkg.tar.zst";
    std::cout << CYAN << "Downloading pre-compiled Paru (libalpm v16 compatible)..." << RESET << "\n";
    if (utils::download_file(paru_url, paru_archive)) {
        utils::extract_archive(paru_archive, cfg.sysroot_dir);
        chmod(paru_bin.c_str(), 0755);
        std::string check = utils::run_command_output("fglibc paru --version 2>&1");
        if (check.find("paru v") != std::string::npos) {
            std::cout << GREEN << "✔ Paru installed successfully: " << trim_string(check) << RESET << "\n";
            return true;
        }
    }
    std::cout << YELLOW << "[!] Paru installation failed." << RESET << "\n";
    return false;
}

bool InstallerUI::setup_apt(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[4/6] Configuring APT & DPKG (Debian/Ubuntu Tools)..." << RESET << "\n";
    std::string apt_bin = cfg.sysroot_dir + "/usr/bin/apt";
    std::string dpkg_bin = cfg.sysroot_dir + "/usr/bin/dpkg";
    if (!utils::file_exists(apt_bin) || !utils::file_exists(dpkg_bin)) {
        install_packages(cfg, {"apt", "dpkg"});
    }

    utils::create_dirs(cfg.sysroot_dir + "/var/lib/dpkg/updates");
    utils::create_dirs(cfg.sysroot_dir + "/var/lib/dpkg/info");
    std::string status_file = cfg.sysroot_dir + "/var/lib/dpkg/status";
    if (!utils::file_exists(status_file)) {
        utils::write_text_file(status_file, "");
    }

    utils::create_dirs(cfg.sysroot_dir + "/etc/apt");
    std::string slist = cfg.sysroot_dir + "/etc/apt/sources.list";
    if (!utils::file_exists(slist)) {
        std::string sample = 
            "# Debian / Ubuntu compatibility sources for fglibc\n"
            "# deb http://deb.debian.org/debian bookworm main contrib non-free\n";
        utils::write_text_file(slist, sample);
    }

    std::cout << GREEN << "✔ APT and DPKG configured successfully! Usage: fglibc apt, fglibc dpkg" << RESET << "\n";
    return true;
}

bool InstallerUI::setup_apk(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[5/6] Configuring APK Tools (Alpine Linux Manager)..." << RESET << "\n";
    std::string apk_bin = cfg.sysroot_dir + "/usr/bin/apk";
    if (!utils::file_exists(apk_bin)) {
        install_packages(cfg, {"apk-tools", "lua53"});
    }

    utils::create_dirs(cfg.sysroot_dir + "/etc/apk");
    std::string repos = cfg.sysroot_dir + "/etc/apk/repositories";
    if (!utils::file_exists(repos)) {
        std::string content = 
            "https://dl-cdn.alpinelinux.org/alpine/v3.21/main\n"
            "https://dl-cdn.alpinelinux.org/alpine/v3.21/community\n";
        utils::write_text_file(repos, content);
    }

    std::cout << GREEN << "✔ APK tools configured successfully! Usage: fglibc apk <command>" << RESET << "\n";
    return true;
}

bool InstallerUI::setup_devel(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n[6/6] Configuring Base Development Stack (gcc, make, fakeroot, git)..." << RESET << "\n";
    std::vector<std::string> tools = {"fakeroot", "patch", "make", "binutils", "gcc", "git", "pkgconf"};
    return (install_packages(cfg, tools) == 0);
}

void InstallerUI::print_setup_status(const Config &cfg) {
    using namespace utils::colors;
    std::cout << BOLD << "\n======================================================================\n"
              << "               fglibc Setup & Integration Status                      \n"
              << "======================================================================\n" << RESET;
    std::cout << "  Sysroot Prefix:  " << CYAN << cfg.sysroot_dir << RESET << "\n";

    std::string mlist = cfg.sysroot_dir + "/etc/pacman.d/mirrorlist";
    if (utils::file_exists(mlist)) {
        std::string cur_m = utils::run_command_output("grep -E '^Server =' \"" + mlist + "\" 2>/dev/null | head -n 1 | awk '{print $3}'");
        std::cout << "  Active Mirror:   " << GREEN << trim_string(cur_m) << RESET << "\n";
    } else {
        std::cout << "  Active Mirror:   " << YELLOW << "Default (geo.mirror.pkgbuild.com)" << RESET << "\n";
    }

    auto check_tool = [&](const std::string &name, const std::string &rel_bin) {
        std::string full = cfg.sysroot_dir + rel_bin;
        if (utils::file_exists(full)) {
            std::cout << "  " << std::left << std::setw(17) << (name + ":") << GREEN << "✔ Installed" << RESET << " (" << full << ")\n";
        } else {
            std::cout << "  " << std::left << std::setw(17) << (name + ":") << YELLOW << "✗ Not Installed" << RESET << "\n";
        }
    };

    check_tool("Pacman", "/usr/bin/pacman");
    check_tool("Yay (AUR)", "/usr/bin/yay");
    check_tool("Paru (AUR)", "/usr/bin/paru");
    check_tool("APT", "/usr/bin/apt");
    check_tool("DPKG", "/usr/bin/dpkg");
    check_tool("APK Tools", "/usr/bin/apk");
    check_tool("Fakeroot", "/usr/bin/fakeroot");
    check_tool("GCC / G++", "/usr/bin/gcc");
    check_tool("Make", "/usr/bin/make");
    check_tool("Git", "/usr/bin/git");

    std::string host_apps = utils::get_home_dir() + "/.local/share/applications";
    std::string count_out = utils::run_command_output("find \"" + host_apps + "\" -name \"*.desktop\" 2>/dev/null | wc -l");
    std::cout << "  Desktop Apps:    " << CYAN << trim_string(count_out) << " exported shortcuts in KDE / GNOME / Hyprland / Rofi" << RESET << "\n";
    std::cout << BOLD << "======================================================================\n\n" << RESET;
}

int InstallerUI::run_setup(const Config &cfg, const std::vector<std::string> &args) {
    using namespace utils::colors;

    struct SetupTUIItem {
        std::string id;
        std::string name;
        std::string category;
        std::string description;
        bool selected;
        bool is_installed;
    };

    std::vector<SetupTUIItem> items = {
        { "mirrors", "Rank Fastest Mirrors", "Network", "Benchmark & select lowest latency Arch Linux mirrors", true, false },
        { "pacman",  "Pacman & ALPM DB",      "PkgMgr",  "Arch Linux package manager & database sync",          true, utils::file_exists(cfg.sysroot_dir + "/usr/bin/pacman") },
        { "yay",     "Yay (AUR Helper)",      "PkgMgr",  "Go-based Arch User Repository helper (auto-builds)",  true, utils::file_exists(cfg.sysroot_dir + "/usr/bin/yay") },
        { "paru",    "Paru (AUR Helper)",     "PkgMgr",  "Rust-based Arch User Repository helper (pacman-like)",true, utils::file_exists(cfg.sysroot_dir + "/usr/bin/paru") },
        { "apt",     "APT & DPKG",            "PkgMgr",  "Debian/Ubuntu package management tools & database",   false, utils::file_exists(cfg.sysroot_dir + "/usr/bin/apt") },
        { "apk",     "APK Tools",             "PkgMgr",  "Alpine Linux package manager in glibc prefix",        false, utils::file_exists(cfg.sysroot_dir + "/usr/bin/apk") },
        { "devel",   "Base Devel Stack",      "Build",   "fakeroot, gcc, make, patch, git, binutils",           true, utils::file_exists(cfg.sysroot_dir + "/usr/bin/fakeroot") },
        { "desktop", "Desktop Integration",   "System",  "Export apps & icons to KDE, GNOME, Hyprland, Rofi",   true, false }
    };

    // CLI Dispatcher Mode
    if (!args.empty()) {
        for (const auto &arg_raw : args) {
            std::string arg = to_lower(trim_string(arg_raw));
            if (arg == "all") {
                rank_mirrors(cfg, true);
                setup_pacman(cfg);
                setup_devel(cfg);
                setup_yay(cfg);
                setup_paru(cfg);
                sync_desktop_entries(cfg, true);
                print_setup_status(cfg);
                return 0;
            } else if (arg == "mirrors" || arg == "mirror" || arg == "rank-mirrors") {
                rank_mirrors(cfg, true);
            } else if (arg == "desktop" || arg == "sync-desktop" || arg == "export-desktop") {
                sync_desktop_entries(cfg, true);
            } else if (arg == "pacman") {
                setup_pacman(cfg);
            } else if (arg == "yay") {
                setup_yay(cfg);
            } else if (arg == "paru") {
                setup_paru(cfg);
            } else if (arg == "apt" || arg == "dpkg") {
                setup_apt(cfg);
            } else if (arg == "apk") {
                setup_apk(cfg);
            } else if (arg == "devel" || arg == "build-tools") {
                setup_devel(cfg);
            } else if (arg == "status" || arg == "info") {
                print_setup_status(cfg);
            } else {
                utils::log_error("Unknown setup option: " + arg_raw);
                std::cout << "Available: all, mirrors, desktop, pacman, yay, paru, apt, apk, devel, status\n";
                return 1;
            }
        }
        return 0;
    }

    // Non-interactive fallback
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        rank_mirrors(cfg, true);
        setup_pacman(cfg);
        setup_devel(cfg);
        setup_yay(cfg);
        setup_paru(cfg);
        sync_desktop_entries(cfg, true);
        print_setup_status(cfg);
        return 0;
    }

    // Interactive Checkbox TUI Mode
    RawTerminalGuard guard;
    g_guard = &guard;
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    size_t cursor = 0;

    while (true) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0 || ws.ws_row == 0) {
            ws.ws_col = 80;
            ws.ws_row = 24;
        }

        std::string frame;
        frame.reserve(4096);
        frame += "\033[H";

        // Header
        frame += "\033[1;36m";
        frame += "================================================================================\r\n";
        frame += "         fglibc Setup - Package Managers, Fastest Mirrors & Desktop Sync        \r\n";
        frame += "================================================================================\033[0m\r\n";
        frame += " \033[90mUse [Up/Down] to navigate | [Space] to toggle | [Enter] to run selected setup\033[0m\033[K\r\n";
        frame += "────────────────────────────────────────────────────────────────────────────────\r\n";

        for (size_t i = 0; i < items.size(); ++i) {
            const auto &it = items[i];
            bool is_cursor = (i == cursor);

            std::string line = "";
            if (is_cursor) {
                line += "\033[1;36m> \033[0m";
            } else {
                line += "  ";
            }

            if (it.selected) {
                line += "\033[1;32m[x]\033[0m ";
            } else {
                line += "\033[90m[ ]\033[0m ";
            }

            if (is_cursor) {
                line += "\033[1;37m" + it.name + "\033[0m";
            } else {
                line += "\033[37m" + it.name + "\033[0m";
            }

            int pad = 24 - (int)it.name.length();
            if (pad < 1) pad = 1;
            line += std::string(pad, ' ');

            if (it.category == "Network") {
                line += "\033[1;33m[" + it.category + "]\033[0m ";
            } else if (it.category == "PkgMgr") {
                line += "\033[1;34m[" + it.category + "]\033[0m ";
            } else if (it.category == "Build") {
                line += "\033[1;35m[" + it.category + "]\033[0m ";
            } else {
                line += "\033[1;32m[" + it.category + "]\033[0m ";
            }

            line += "\033[90m- " + it.description;
            if (it.is_installed) {
                line += " \033[32m(Installed)\033[90m";
            }
            line += "\033[0m";

            line += "\033[K\r\n";
            frame += line;
        }

        // Footer
        frame += "\033[36m────────────────────────────────────────────────────────────────────────────────\033[0m\r\n";
        size_t count_selected = 0;
        for (const auto &it : items) if (it.selected) count_selected++;

        std::string footer = " Selected: " + std::to_string(count_selected) + " action(s) | ";
        footer += "[Space] Toggle | [Enter] Run Setup | [a] All | [u] None | [q] Exit";
        frame += "\033[1;37m" + footer + "\033[0m\033[K\r\n";

        write(STDOUT_FILENO, frame.data(), frame.size());

        char in_char = 0;
        KeyAction act = read_key(in_char);

        if (act == KeyAction::QUIT || act == KeyAction::ESC) {
            guard.restore();
            std::cout << "\nSetup cancelled by user.\n\n";
            return 0;
        } else if (act == KeyAction::UP) {
            if (cursor > 0) cursor--;
            else cursor = items.size() - 1;
        } else if (act == KeyAction::DOWN) {
            if (cursor + 1 < items.size()) cursor++;
            else cursor = 0;
        } else if (act == KeyAction::SPACE) {
            items[cursor].selected = !items[cursor].selected;
        } else if (act == KeyAction::CHAR) {
            if (in_char == 'a' || in_char == 'A') {
                for (auto &it : items) it.selected = true;
            } else if (in_char == 'u' || in_char == 'U') {
                for (auto &it : items) it.selected = false;
            } else if (in_char == 'q' || in_char == 'Q') {
                guard.restore();
                std::cout << "\nSetup cancelled by user.\n\n";
                return 0;
            }
        } else if (act == KeyAction::ENTER) {
            break;
        }
    }

    guard.restore();

    std::cout << BOLD << "\n======================================================================\n"
              << "                      Running fglibc Setup                            \n"
              << "======================================================================\n" << RESET;

    for (const auto &it : items) {
        if (!it.selected) continue;

        if (it.id == "mirrors") {
            rank_mirrors(cfg, true);
        } else if (it.id == "pacman") {
            setup_pacman(cfg);
        } else if (it.id == "yay") {
            setup_yay(cfg);
        } else if (it.id == "paru") {
            setup_paru(cfg);
        } else if (it.id == "apt") {
            setup_apt(cfg);
        } else if (it.id == "apk") {
            setup_apk(cfg);
        } else if (it.id == "devel") {
            setup_devel(cfg);
        } else if (it.id == "desktop") {
            sync_desktop_entries(cfg, true);
        }
    }

    std::cout << GREEN << BOLD << "✔ fglibc Setup completed successfully!" << RESET << "\n";
    print_setup_status(cfg);
    return 0;
}

} // namespace fglibc
