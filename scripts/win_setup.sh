#!/bin/sh
# fglibc - Windows Runtime & Wine/Proton Components Manager
# Supports Microsoft Edge WebView2, Visual C++ 2015-2022, DXVK, VKD3D, Core Fonts, etc.

set -e

CYAN="$(printf '\033[0;36m')"
GREEN="$(printf '\033[0;32m')"
YELLOW="$(printf '\033[1;33m')"
RED="$(printf '\033[0;31m')"
BOLD="$(printf '\033[1m')"
RESET="$(printf '\033[0m')"

log_info() {
    printf "${CYAN}[i]${RESET} %s\n" "$1"
}

log_ok() {
    printf "${GREEN}[✓]${RESET} %s\n" "$1"
}

log_warn() {
    printf "${YELLOW}[!]${RESET} %s\n" "$1"
}

log_error() {
    printf "${RED}[✗]${RESET} %s\n" "$1" >&2
}

# Determine Wine Prefix
WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export WINEPREFIX

WINE_BIN="$(which wine 2>/dev/null || echo "")"
if [ -z "$WINE_BIN" ]; then
    log_error "Wine was not found in PATH! Please install Wine or configure Proton."
    exit 1
fi

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/fglibc/win"
mkdir -p "$CACHE_DIR"

download_file() {
    url="$1"
    dest="$2"
    if [ -f "$dest" ]; then
        log_info "Using cached: $dest"
        return 0
    fi
    log_info "Downloading $url -> $dest"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --progress-bar "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$dest" "$url"
    else
        log_error "Neither curl nor wget is available!"
        return 1
    fi
}

init_wineprefix() {
    if [ ! -d "$WINEPREFIX/drive_c" ]; then
        log_info "Initializing Wine prefix at $WINEPREFIX..."
        wineboot -u >/dev/null 2>&1 || true
    fi
}

install_webview2() {
    init_wineprefix
    log_info "Setting up Microsoft Edge WebView2 Runtime in $WINEPREFIX..."

    # Check if already installed
    WV_DIR_X86="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application"
    WV_DIR_X64="$WINEPREFIX/drive_c/Program Files/Microsoft/EdgeWebView/Application"

    INSTALLED_VER=""
    for d in "$WV_DIR_X86"/* "$WV_DIR_X64"/*; do
        if [ -f "$d/msedgewebview2.exe" ]; then
            INSTALLED_VER="$(basename "$d")"
            break
        fi
    done

    if [ -n "$INSTALLED_VER" ]; then
        log_ok "Microsoft Edge WebView2 is already installed (version: $INSTALLED_VER)"
    else
        SETUP_EXE="$CACHE_DIR/MicrosoftEdgeWebView2Setup.exe"
        # Check if setup was already placed by an app in drive_c
        EXISTING_SETUP="$(find "$WINEPREFIX/drive_c" -maxdepth 3 -name "MicrosoftEdgeWebView2Setup.exe" 2>/dev/null | head -n 1 || true)"
        if [ -n "$EXISTING_SETUP" ] && [ -f "$EXISTING_SETUP" ]; then
            SETUP_EXE="$EXISTING_SETUP"
        else
            WV_BOOTSTRAP_URL="https://go.microsoft.com/fwlink/p/?LinkId=2124703"
            download_file "$WV_BOOTSTRAP_URL" "$SETUP_EXE"
        fi

        log_info "Running Microsoft Edge WebView2 installer (silent mode)..."
        WINEDEBUG=-all wine "$SETUP_EXE" /silent /install || true

        # Wait for extraction/installation
        WAIT_COUNT=0
        while [ $WAIT_COUNT -lt 60 ]; do
            for d in "$WV_DIR_X86"/* "$WV_DIR_X64"/*; do
                if [ -f "$d/msedgewebview2.exe" ]; then
                    INSTALLED_VER="$(basename "$d")"
                    break 2
                fi
            done
            sleep 2
            WAIT_COUNT=$((WAIT_COUNT + 1))
        done
    fi

    if [ -z "$INSTALLED_VER" ]; then
        log_error "WebView2 installation failed or timed out!"
        return 1
    fi

    log_info "Configuring WebView2 registry entries in Wine prefix..."
    W_LOCATION="C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application"
    if [ ! -d "$WV_DIR_X86" ] && [ -d "$WV_DIR_X64" ]; then
        W_LOCATION="C:\\Program Files\\Microsoft\\EdgeWebView\\Application"
    fi

    wine reg add "HKLM\\Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv /t REG_SZ /d "$INSTALLED_VER" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v location /t REG_SZ /d "$W_LOCATION" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v name /t REG_SZ /d "Microsoft Edge WebView2 Runtime" /f >/dev/null 2>&1 || true

    wine reg add "HKLM\\Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv /t REG_SZ /d "$INSTALLED_VER" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v location /t REG_SZ /d "$W_LOCATION" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v name /t REG_SZ /d "Microsoft Edge WebView2 Runtime" /f >/dev/null 2>&1 || true

    # Configure AdditionalBrowserArguments policy in Wine registry to prevent DirectComposition GPU crash loop
    WV_FLAGS="--no-sandbox --disable-gpu --disable-gpu-compositing --disable-direct-composition"
    wine reg add "HKLM\\Software\\Policies\\Microsoft\\Edge\\WebView2\\AdditionalBrowserArguments" /v "*" /t REG_SZ /d "$WV_FLAGS" /f >/dev/null 2>&1 || true
    wine reg add "HKCU\\Software\\Policies\\Microsoft\\Edge\\WebView2\\AdditionalBrowserArguments" /v "*" /t REG_SZ /d "$WV_FLAGS" /f >/dev/null 2>&1 || true
    wine reg add "HKCU\\Environment" /v "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS" /t REG_SZ /d "$WV_FLAGS" /f >/dev/null 2>&1 || true

    log_ok "Microsoft Edge WebView2 ($INSTALLED_VER) successfully configured!"
    log_info "Note: fglibc automatically applies Wine-compatible flags ($WV_FLAGS) for WebView2."
}

install_vcrun() {
    init_wineprefix
    log_info "Setting up Microsoft Visual C++ 2015-2022 Redistributable (x64 & x86)..."

    VC_X64="$CACHE_DIR/vc_redist.x64.exe"
    VC_X86="$CACHE_DIR/vc_redist.x86.exe"

    download_file "https://aka.ms/vs/17/release/vc_redist.x64.exe" "$VC_X64"
    download_file "https://aka.ms/vs/17/release/vc_redist.x86.exe" "$VC_X86"

    log_info "Installing VC++ Redistributable (x64)..."
    WINEDEBUG=-all wine "$VC_X64" /install /quiet /norestart || true

    log_info "Installing VC++ Redistributable (x86)..."
    WINEDEBUG=-all wine "$VC_X86" /install /quiet /norestart || true

    log_ok "Visual C++ 2015-2022 Redistributables installed successfully!"
}

install_fonts() {
    init_wineprefix
    log_info "Configuring Windows Core Fonts in $WINEPREFIX..."
    FONT_DIR="$WINEPREFIX/drive_c/windows/Fonts"
    mkdir -p "$FONT_DIR"

    # Symlink / copy system truetype fonts into Wine Fonts dir
    find /usr/share/fonts -type f \( -name "*.ttf" -o -name "*.otf" \) 2>/dev/null | while read -r f; do
        b="$(basename "$f")"
        if [ ! -f "$FONT_DIR/$b" ]; then
            ln -sf "$f" "$FONT_DIR/$b" 2>/dev/null || cp -f "$f" "$FONT_DIR/$b" 2>/dev/null || true
        fi
    done

    # Setup essential metric-compatible aliases in Wine registry
    log_info "Setting font substitutes in Wine registry..."
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Arial" /t REG_SZ /d "DejaVu Sans" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Times New Roman" /t REG_SZ /d "DejaVu Serif" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Courier New" /t REG_SZ /d "DejaVu Sans Mono" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Tahoma" /t REG_SZ /d "DejaVu Sans" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Segoe UI" /t REG_SZ /d "DejaVu Sans" /f >/dev/null 2>&1 || true
    wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes" /v "Verdana" /t REG_SZ /d "DejaVu Sans" /f >/dev/null 2>&1 || true

    log_ok "Windows Core Fonts successfully configured ($(ls -1 "$FONT_DIR" | wc -l) font files available)!"
}

install_dxvk() {
    init_wineprefix
    log_info "Setting up DXVK (DirectX 9/10/11 -> Vulkan) in $WINEPREFIX..."

    # Query latest release
    LATEST_URL="$(curl -s https://api.github.com/repos/doitsujin/dxvk/releases/latest 2>/dev/null | grep '"browser_download_url":' | grep '\.tar\.gz"' | head -n 1 | cut -d '"' -f 4 || true)"
    if [ -z "$LATEST_URL" ]; then
        LATEST_URL="https://github.com/doitsujin/dxvk/releases/download/v2.4.1/dxvk-2.4.1.tar.gz"
    fi

    TAR_NAME="$(basename "$LATEST_URL")"
    DXVK_TAR="$CACHE_DIR/$TAR_NAME"
    download_file "$LATEST_URL" "$DXVK_TAR"

    EXTRACT_DIR="$CACHE_DIR/dxvk_extracted"
    rm -rf "$EXTRACT_DIR"
    mkdir -p "$EXTRACT_DIR"
    tar -xzf "$DXVK_TAR" -C "$EXTRACT_DIR"

    DXVK_DIR="$(find "$EXTRACT_DIR" -maxdepth 1 -mindepth 1 -type d | head -n 1)"
    if [ -d "$DXVK_DIR" ]; then
        SYS32="$WINEPREFIX/drive_c/windows/system32"
        SYSWOW64="$WINEPREFIX/drive_c/windows/syswow64"
        mkdir -p "$SYS32" "$SYSWOW64"

        if [ -d "$DXVK_DIR/x64" ]; then
            cp -f "$DXVK_DIR/x64"/*.dll "$SYS32/"
        fi
        if [ -d "$DXVK_DIR/x32" ]; then
            cp -f "$DXVK_DIR/x32"/*.dll "$SYSWOW64/"
        fi

        # Register DLL overrides
        log_info "Configuring Wine DLL overrides for DXVK (native)..."
        for dll in d3d9 d3d10core d3d11 dxgi; do
            wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v "$dll" /t REG_SZ /d "native" /f >/dev/null 2>&1 || true
        done
        log_ok "DXVK successfully installed into $WINEPREFIX!"
    else
        log_error "Failed to extract DXVK package!"
        return 1
    fi
}

install_vkd3d() {
    init_wineprefix
    log_info "Setting up VKD3D-Proton (DirectX 12 -> Vulkan) in $WINEPREFIX..."

    LATEST_URL="$(curl -s https://api.github.com/repos/HansKristian-Work/vkd3d-proton/releases/latest 2>/dev/null | grep '"browser_download_url":' | grep '\.tar\.zst"' | head -n 1 | cut -d '"' -f 4 || true)"
    if [ -z "$LATEST_URL" ]; then
        LATEST_URL="https://github.com/HansKristian-Work/vkd3d-proton/releases/download/v3.0.1/vkd3d-proton-3.0.1.tar.zst"
    fi

    TAR_NAME="$(basename "$LATEST_URL")"
    VKD3D_TAR="$CACHE_DIR/$TAR_NAME"
    download_file "$LATEST_URL" "$VKD3D_TAR"

    EXTRACT_DIR="$CACHE_DIR/vkd3d_extracted"
    rm -rf "$EXTRACT_DIR"
    mkdir -p "$EXTRACT_DIR"
    tar -I zstd -xf "$VKD3D_TAR" -C "$EXTRACT_DIR" 2>/dev/null || tar -xf "$VKD3D_TAR" -C "$EXTRACT_DIR"

    VKD3D_DIR="$(find "$EXTRACT_DIR" -maxdepth 1 -mindepth 1 -type d | head -n 1)"
    if [ -d "$VKD3D_DIR" ]; then
        SYS32="$WINEPREFIX/drive_c/windows/system32"
        SYSWOW64="$WINEPREFIX/drive_c/windows/syswow64"
        mkdir -p "$SYS32" "$SYSWOW64"

        if [ -d "$VKD3D_DIR/x64" ]; then
            cp -f "$VKD3D_DIR/x64"/*.dll "$SYS32/"
        fi
        if [ -d "$VKD3D_DIR/x86" ]; then
            cp -f "$VKD3D_DIR/x86"/*.dll "$SYSWOW64/"
        fi

        log_info "Configuring Wine DLL overrides for VKD3D (native)..."
        for dll in d3d12 d3d12core; do
            wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v "$dll" /t REG_SZ /d "native" /f >/dev/null 2>&1 || true
        done
        log_ok "VKD3D-Proton successfully installed into $WINEPREFIX!"
    else
        log_error "Failed to extract VKD3D package!"
        return 1
    fi
}

install_all() {
    log_info "Starting full setup of all recommended Windows runtimes..."
    install_fonts
    install_vcrun
    install_webview2
    install_dxvk
    log_ok "All Windows runtimes configured successfully in $WINEPREFIX!"
}

show_status() {
    init_wineprefix
    printf "${BOLD}fglibc Windows Runtime & Services Status:${RESET}\n"
    printf "  WINEPREFIX: %s\n" "$WINEPREFIX"
    printf "  Wine Binary: %s (%s)\n" "$WINE_BIN" "$($WINE_BIN --version 2>/dev/null || echo 'unknown')"

    # WebView2
    WV_DIR_X86="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application"
    WV_DIR_X64="$WINEPREFIX/drive_c/Program Files/Microsoft/EdgeWebView/Application"
    WV_FOUND=""
    for d in "$WV_DIR_X86"/* "$WV_DIR_X64"/*; do
        if [ -f "$d/msedgewebview2.exe" ]; then
            WV_FOUND="$(basename "$d")"
            break
        fi
    done
    if [ -n "$WV_FOUND" ]; then
        printf "  Microsoft Edge WebView2: %sInstalled (v%s)%s\n" "$GREEN" "$WV_FOUND" "$RESET"
    else
        printf "  Microsoft Edge WebView2: %sNot installed%s\n" "$YELLOW" "$RESET"
    fi

    # VC++
    if [ -f "$WINEPREFIX/drive_c/windows/system32/vcruntime140.dll" ] || [ -f "$WINEPREFIX/drive_c/windows/syswow64/vcruntime140.dll" ]; then
        printf "  Visual C++ 2015-2022:    %sInstalled%s\n" "$GREEN" "$RESET"
    else
        printf "  Visual C++ 2015-2022:    %sNot installed%s\n" "$YELLOW" "$RESET"
    fi

    # Fonts
    FONT_COUNT=$(ls -1 "$WINEPREFIX/drive_c/windows/Fonts" 2>/dev/null | wc -l || echo 0)
    printf "  Windows Fonts:           %s%d font files%s\n" "$GREEN" "$FONT_COUNT" "$RESET"

    # DXVK
    if [ -f "$WINEPREFIX/drive_c/windows/system32/d3d11.dll" ]; then
        printf "  DXVK (DirectX 9-11):     %sInstalled%s\n" "$GREEN" "$RESET"
    else
        printf "  DXVK (DirectX 9-11):     %sWine default / Not configured%s\n" "$YELLOW" "$RESET"
    fi

    # VKD3D
    if [ -f "$WINEPREFIX/drive_c/windows/system32/d3d12.dll" ]; then
        printf "  VKD3D (DirectX 12):      %sInstalled%s\n" "$GREEN" "$RESET"
    else
        printf "  VKD3D (DirectX 12):      %sWine default / Not configured%s\n" "$YELLOW" "$RESET"
    fi
}

cmd="${1:-status}"
case "$cmd" in
    webview2|edge|webview)
        install_webview2
        ;;
    vcrun|vcrun2022|vcredist)
        install_vcrun
        ;;
    fonts|corefonts)
        install_fonts
        ;;
    dxvk)
        install_dxvk
        ;;
    vkd3d|d3d12)
        install_vkd3d
        ;;
    all)
        install_all
        ;;
    status)
        show_status
        ;;
    help|--help|-h)
        printf "${BOLD}Usage:${RESET} fglibc win-setup <component>\n\n"
        printf "Components:\n"
        printf "  webview2    Install Microsoft Edge WebView2 Runtime & registry configuration\n"
        printf "  vcrun       Install Microsoft Visual C++ 2015-2022 Redistributables (x86 & x64)\n"
        printf "  fonts       Configure TrueType core fonts (Arial, Times, Tahoma, Segoe UI)\n"
        printf "  dxvk        Install DXVK (DirectX 9/10/11 -> Vulkan) and configure DLL overrides\n"
        printf "  vkd3d       Install VKD3D-Proton (DirectX 12 -> Vulkan)\n"
        printf "  all         Install all recommended runtimes (fonts, vcrun, webview2, dxvk)\n"
        printf "  status      Check installed Windows runtimes and services status\n"
        ;;
    *)
        log_error "Unknown component: $cmd"
        printf "Run 'fglibc win-setup help' for available components.\n"
        exit 1
        ;;
esac
