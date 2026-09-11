#!/bin/sh
# fglibc - Winetricks wrapper for Wine / Proton environments

set -e

CYAN="$(printf '\033[0;36m')"
GREEN="$(printf '\033[0;32m')"
YELLOW="$(printf '\033[1;33m')"
RED="$(printf '\033[0;31m')"
BOLD="$(printf '\033[1m')"
RESET="$(printf '\033[0m')"

WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export WINEPREFIX

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/fglibc/bin"
mkdir -p "$CACHE_DIR"

WINETRICKS_BIN="$CACHE_DIR/winetricks"

if [ ! -f "$WINETRICKS_BIN" ]; then
    printf "${CYAN}[i]${RESET} Downloading latest winetricks from upstream...\n"
    if command -v curl >/dev/null 2>&1; then
        curl -fL "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks" -o "$WINETRICKS_BIN"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$WINETRICKS_BIN" "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks"
    else
        printf "${RED}[✗]${RESET} Neither curl nor wget is available to download winetricks!\n" >&2
        exit 1
    fi
    chmod +x "$WINETRICKS_BIN"
    printf "${GREEN}[✓]${RESET} Winetricks installed to %s\n" "$WINETRICKS_BIN"
fi

# Ensure wine is available
if ! command -v wine >/dev/null 2>&1; then
    printf "${RED}[✗]${RESET} Wine binary was not found in PATH!\n" >&2
    exit 1
fi

exec "$WINETRICKS_BIN" "$@"
