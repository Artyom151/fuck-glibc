#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$DIR/bin/fglibc"

if [ ! -f "$BIN" ]; then
    echo "[!] Building fglibc..."
    make -C "$DIR"
fi

TARGET_DIR="$HOME/.local/bin"
if [ "$(id -u)" -eq 0 ]; then
    TARGET_DIR="/usr/local/bin"
fi

mkdir -p "$TARGET_DIR"
cp -f "$BIN" "$TARGET_DIR/fglibc"
chmod +x "$TARGET_DIR/fglibc"

echo "[✓] fglibc installed successfully to $TARGET_DIR/fglibc"

if ! echo "$PATH" | grep -q "$TARGET_DIR"; then
    echo "[i] Note: Please ensure $TARGET_DIR is in your PATH."
    echo "    export PATH=\"$TARGET_DIR:\$PATH\""
fi
