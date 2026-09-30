#!/bin/sh
set -u

RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
BOLD='\033[1m'
RESET='\033[0m'

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
BIN="${PROJECT_ROOT}/bin/fglibc"
PASS=0
FAIL=0
SKIP=0
TOTAL=0
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/fglibc-stress.XXXXXX") || exit 1
trap 'rm -rf "$TMP_ROOT"' EXIT HUP INT TERM

if [ ! -x "$BIN" ]; then
    printf '%bBinary missing: %s (run make first)%b\n' "$RED" "$BIN" "$RESET"
    exit 2
fi

export FGLIBC_PREFIX="$TMP_ROOT/prefix"
export FGLIBC_ENGINE=namespace
SOURCE_SYSROOT=${FGLIBC_SOURCE_SYSROOT:-"$HOME/.fglibc/sysroot"}
if [ -x "$SOURCE_SYSROOT/usr/bin/bash" ] && [ -f "$SOURCE_SYSROOT/usr/lib/libc.so.6" ] && [ -f "$SOURCE_SYSROOT/usr/lib/ld-linux-x86-64.so.2" ]; then
    mkdir -p "$FGLIBC_PREFIX/sysroot/usr/bin" "$FGLIBC_PREFIX/sysroot/usr/lib"
    cp "$SOURCE_SYSROOT/usr/bin/bash" "$FGLIBC_PREFIX/sysroot/usr/bin/bash"
    for library in ld-linux-x86-64.so.2 libc.so.6 libreadline.so.8 libncursesw.so.6; do
        if [ -e "$SOURCE_SYSROOT/usr/lib/$library" ]; then
            cp -L "$SOURCE_SYSROOT/usr/lib/$library" "$FGLIBC_PREFIX/sysroot/usr/lib/$library"
        fi
    done
    RUNTIME_STAGED=1
else
    RUNTIME_STAGED=0
fi

pass() { TOTAL=$((TOTAL + 1)); PASS=$((PASS + 1)); printf '%b[PASS]%b %s\n' "$GREEN" "$RESET" "$1"; }
fail() { TOTAL=$((TOTAL + 1)); FAIL=$((FAIL + 1)); printf '%b[FAIL]%b %s\n' "$RED" "$RESET" "$1"; [ -z "${2:-}" ] || printf '       %s\n' "$2"; }
skip() { TOTAL=$((TOTAL + 1)); SKIP=$((SKIP + 1)); printf '%b[SKIP]%b %s — %s\n' "$YELLOW" "$RESET" "$1" "$2"; }

check_output() {
    name=$1 expected=$2
    shift 2
    output=$("$@" 2>&1)
    result=$?
    if [ "$result" -eq 0 ] && printf '%s' "$output" | grep -Fq "$expected"; then pass "$name"; else fail "$name" "exit=$result; expected output: $expected"; fi
}

check_exit() {
    name=$1 expected=$2
    shift 2
    "$@" >"$TMP_ROOT/stdout" 2>"$TMP_ROOT/stderr"
    result=$?
    if [ "$result" -eq "$expected" ]; then pass "$name"; else fail "$name" "expected exit=$expected, got $result: $(cat "$TMP_ROOT/stderr")"; fi
}

printf '\n%b============================================================%b\n' "$CYAN$BOLD" "$RESET"
printf '%b          fglibc isolated stress and regression checks%b\n' "$BOLD" "$RESET"
printf '%bTemporary prefix: %s%b\n\n' "$CYAN" "$FGLIBC_PREFIX" "$RESET"

check_output 'version command' 'version' "$BIN" version
check_output 'help command' 'USAGE:' "$BIN" --help
empty_status=$(FGLIBC_PREFIX="$TMP_ROOT/empty-prefix" "$BIN" status 2>&1)
if printf '%s' "$empty_status" | grep -q 'Not Initialized'; then pass 'status reports an empty prefix'; else fail 'status reports an empty prefix'; fi
check_output 'config uses FGLIBC_PREFIX' "$FGLIBC_PREFIX" "$BIN" status
check_exit 'setup requires explicit component' 2 "$BIN" setup
check_exit 'install requires package names' 2 "$BIN" install
check_exit 'check requires executable path' 1 "$BIN" check
check_output 'setup status component' 'Setup & Integration Status' "$BIN" setup status
check_output 'package list command' 'Installed Packages' "$BIN" list
WINEPREFIX="$TMP_ROOT/wine" XDG_CACHE_HOME="$TMP_ROOT/cache" \
    check_output 'bundled win-setup script resolution' 'Microsoft Edge WebView2' "$BIN" win-setup status

mkdir -p "$FGLIBC_PREFIX/cache"
printf archive > "$FGLIBC_PREFIX/cache/stress.pkg.tar.zst"
printf keep > "$FGLIBC_PREFIX/cache/keep.txt"
if "$BIN" clean >/dev/null 2>&1 && [ ! -e "$FGLIBC_PREFIX/cache/stress.pkg.tar.zst" ] && [ -f "$FGLIBC_PREFIX/cache/keep.txt" ]; then
    pass 'cache cleanup removes package archives only'
else
    fail 'cache cleanup removes package archives only'
fi

check_output 'ELF parser recognizes host shell and musl interpreter' 'MUSL - Native Alpine binary' "$BIN" check /bin/sh
printf 'int main(void) { return 0; }\n' > "$TMP_ROOT/elf_fixture.c"
if command -v cc >/dev/null 2>&1 && cc "$TMP_ROOT/elf_fixture.c" -o "$TMP_ROOT/elf_fixture" >/dev/null 2>&1; then
    check_output 'ELF parser handles freshly compiled fixture' 'ELF Inspection Report' "$BIN" check "$TMP_ROOT/elf_fixture"
else
    skip 'ELF parser compiled fixture' 'host C compiler is unavailable'
fi
printf 'not an executable\n' > "$TMP_ROOT/not-elf"
check_exit 'ELF parser rejects invalid input' 1 "$BIN" check "$TMP_ROOT/not-elf"

if [ "$RUNTIME_STAGED" -eq 1 ]; then
    check_exit 'unknown executable returns failure' 127 "$BIN" "$TMP_ROOT/no-such-program"
    check_output 'shell -c forwards arguments' 'shell-args-ok' "$BIN" sh -c 'printf shell-args-ok'
    check_output 'shell preserves spaces and quotes' 'two words' "$BIN" sh -c 'printf "%s" "two words"'
    check_exit 'shell propagates child exit status' 42 "$BIN" sh -c 'exit 42'
else
    skip 'unknown executable returns failure' 'no ready host prefix to clone into isolated test prefix'
    skip 'shell argument forwarding' 'no ready host prefix to clone into isolated test prefix'
fi

if [ -x "$PROJECT_ROOT/examples/glibc_test" ]; then
    check_output 'ELF parser recognizes C example' 'ELF Inspection Report' "$BIN" check "$PROJECT_ROOT/examples/glibc_test"
else
    skip 'ELF parser example' 'examples/glibc_test is not built; examples build may require initialized sysroot'
fi

if [ -x "$PROJECT_ROOT/examples/glibc_cpp_test" ]; then
    check_output 'ELF parser recognizes C++ example' 'ELF Inspection Report' "$BIN" check "$PROJECT_ROOT/examples/glibc_cpp_test"
else
    skip 'ELF parser C++ example' 'examples/glibc_cpp_test is not built'
fi

if [ -x "$FGLIBC_PREFIX/sysroot/usr/lib/ld-linux-x86-64.so.2" ]; then
    check_output 'initialized glibc prefix status' 'Ready' "$BIN" status
    if [ -x "$PROJECT_ROOT/examples/glibc_test" ]; then
        check_output 'glibc C binary execution' 'SUCCESS' "$BIN" "$PROJECT_ROOT/examples/glibc_test"
    else
        skip 'glibc C binary execution' 'example binary is not built'
    fi
else
    skip 'glibc runtime execution' 'isolated test prefix is not initialized; network/bootstrap intentionally not started'
fi

parallel_failed=0
parallel_pids=
i=1
while [ "$i" -le 8 ]; do
    (
        expected=$((i * i))
        result=$("$BIN" sh -c "printf %s $expected" 2>/dev/null) || exit 1
        [ "$result" = "$expected" ]
    ) &
    parallel_pids="$parallel_pids $!"
    i=$((i + 1))
done
for pid in $parallel_pids; do
    wait "$pid" || parallel_failed=$((parallel_failed + 1))
done
if [ "$parallel_failed" -eq 0 ]; then pass 'parallel shell invocations (8 processes)'; else fail 'parallel shell invocations (8 processes)' "$parallel_failed child process(es) failed"; fi

if [ -x "$PROJECT_ROOT/examples/glibc_sdl2_test" ]; then
    skip 'SDL/OpenGL runtime' 'requires initialized graphical libraries and host display/GPU validation'
else
    skip 'SDL/OpenGL runtime' 'example binary is not built'
fi
skip 'package install/uninstall lifecycle' 'requires network and mutates package state; use an explicitly provisioned disposable prefix'
skip 'Spotify/Discord launch' 'external applications and graphical session are not guaranteed in CI'
skip 'Wine/Proton services' 'host Wine/Proton installation and Windows prefixes are external state'

printf '\n%b============================================================%b\n' "$CYAN$BOLD" "$RESET"
printf 'Results: %b%d passed%b, %b%d failed%b, %b%d skipped%b (%d checks)\n' "$GREEN$BOLD" "$PASS" "$RESET" "$RED$BOLD" "$FAIL" "$RESET" "$YELLOW$BOLD" "$SKIP" "$RESET" "$TOTAL"
printf 'Disposable prefix removed on exit: %s\n\n' "$FGLIBC_PREFIX"
[ "$FAIL" -eq 0 ]
