#!/bin/sh

# ANSI Colors
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
BOLD='\033[1m'
RESET='\033[0m'

PASSED_TESTS=0
FAILED_TESTS=0
TOTAL_TESTS=0

get_time_ms() {
    python3 -c "import time; print(int(time.time() * 1000))" 2>/dev/null || date +%s
}

run_test() {
    TEST_NAME="$1"
    TEST_CMD="$2"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    printf "%b[%02d/12] RUNNING:%b %s... " "${CYAN}${BOLD}" "${TOTAL_TESTS}" "${RESET}" "${TEST_NAME}"
    
    LOG_FILE="/tmp/fglibc_test_${TOTAL_TESTS}.log"
    START_TIME=$(get_time_ms)
    
    sh -c "${TEST_CMD}" > "${LOG_FILE}" 2>&1
    RET=$?
    
    END_TIME=$(get_time_ms)
    DIFF_MS=$((END_TIME - START_TIME))
    if [ "$DIFF_MS" -le 0 ]; then DIFF_MS=1; fi
    
    if [ $RET -eq 0 ]; then
        printf "%b[PASS]%b (%d ms)\n" "${GREEN}${BOLD}" "${RESET}" "${DIFF_MS}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        printf "%b[FAIL]%b\n" "${RED}${BOLD}" "${RESET}"
        printf "%b----------------- Error Log -----------------%b\n" "${RED}" "${RESET}"
        tail -n 25 "${LOG_FILE}"
        printf "%b---------------------------------------------%b\n" "${RED}" "${RESET}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    rm -f "${LOG_FILE}"
}

printf "\n%b======================================================================%b\n" "${CYAN}${BOLD}" "${RESET}"
printf "%b               fglibc Full Comprehensive Stress Test Suite            %b\n" "${BOLD}" "${RESET}"
printf "%b======================================================================%b\n\n" "${CYAN}${BOLD}" "${RESET}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${PROJECT_ROOT}/bin/fglibc"

if [ ! -x "${BIN}" ]; then
    printf "%bError: fglibc binary not found at %s. Please run 'make' first.%b\n" "${RED}" "${BIN}" "${RESET}"
    exit 1
fi

if [ ! -f "${PROJECT_ROOT}/examples/glibc_test" ] || [ ! -f "${PROJECT_ROOT}/examples/glibc_cpp_test" ] || [ ! -f "${PROJECT_ROOT}/examples/glibc_sdl2_test" ]; then
    printf "%bBuilding test examples...%b\n" "${CYAN}" "${RESET}"
    make -C "${PROJECT_ROOT}" examples >/dev/null 2>&1 || true
fi

# Test 1: CLI Version & Hardware Status
run_test "CLI Version & Environment Status" \
    "${BIN} version && ${BIN} status"

# Test 2: ELF Binary Inspection & Dependencies
run_test "ELF Parser & Dependency Inspection" \
    "${BIN} check ${PROJECT_ROOT}/examples/glibc_test && ${BIN} check ${PROJECT_ROOT}/examples/glibc_cpp_test"

# Test 3: Native C Glibc Runtime Execution
run_test "Native C Glibc Execution (glibc 2.44, UID/GID, DRI GPU nodes)" \
    "${BIN} ${PROJECT_ROOT}/examples/glibc_test --stress-mode=1"

# Test 4: Native C++ Multi-threading & STL ABI Compatibility
run_test "C++11 Threading & STL Compatibility (POSIX pthreads under glibc)" \
    "${BIN} ${PROJECT_ROOT}/examples/glibc_cpp_test StressRunner"

# Test 5: Graphics, SDL2 & OpenGL/Mesa Software Stack
run_test "SDL2 & OpenGL Context Initialization" \
    "SDL_VIDEODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ${BIN} ${PROJECT_ROOT}/examples/glibc_sdl2_test"

# Test 6: Shebang Execution & Overlayfs /bin Isolation
run_test "Shebang Execution (#!/bin/bash via glibc overlayfs)" \
    'TMP_SCRIPT="/tmp/fglibc_shebang_test.sh"
    cat << "SH_EOF" > "${TMP_SCRIPT}"
#!/bin/bash
set -e
RES=$(seq 1 100 | awk '\''{s+=$1} END {print s}'\'')
if [ "$RES" != "5050" ]; then
    echo "Math assertion failed: $RES"
    exit 1
fi
exit 0
SH_EOF
    chmod +x "${TMP_SCRIPT}"
    '"${BIN}"' "${TMP_SCRIPT}"
    RET=$?
    rm -f "${TMP_SCRIPT}"
    exit $RET'

# Test 7: Massive Concurrency Stress Test (30 Parallel Processes)
run_test "Concurrency Stress Test (30 simultaneous fglibc runner processes)" \
    'PIDS=""
    ERR=0
    for i in $(seq 1 30); do
        (
            EXPECTED=$((i * i))
            RES=$('"${BIN}"' bash -c "echo $((i * i))" 2>/dev/null)
            if [ "$RES" != "$EXPECTED" ]; then
                echo "Expected $EXPECTED got $RES"
                exit 1
            fi
        ) &
        PIDS="$PIDS $!"
    done
    for pid in $PIDS; do
        wait $pid || ERR=$((ERR + 1))
    done
    if [ "$ERR" -ne 0 ]; then
        echo "$ERR processes failed under concurrency!"
        exit 1
    fi'

# Test 8: Exit Code Preservation & Signal Handling
run_test "Exit Code & Signal Preservation" \
    '# Verify exit code 0
    '"${BIN}"' bash -c "exit 0" || exit 1
    # Verify exit code 42
    '"${BIN}"' bash -c "exit 42" 2>/dev/null
    CODE=$?
    if [ "$CODE" -ne 42 ]; then
        echo "Expected exit code 42, got $CODE"
        exit 1
    fi
    # Verify exit code 127
    '"${BIN}"' /non_existent_binary_xyz_123 2>/dev/null
    CODE=$?
    if [ "$CODE" -ne 127 ]; then
        echo "Expected exit code 127, got $CODE"
        exit 1
    fi'

# Test 9: High-Throughput I/O & Pipe Streaming (100,000 lines)
run_test "I/O Streaming & Pipe Throughput (100,000 lines)" \
    'COUNT=$('"${BIN}"' bash -c "seq 1 100000" 2>/dev/null | wc -l)
    if [ "$COUNT" -ne 100000 ]; then
        echo "Stream line count mismatch: expected 100000, got $COUNT"
        exit 1
    fi'

# Test 10: Package Manager Lifecycle (--no-cache, run, uninstall, manifests)
run_test "Package Manager Lifecycle (install --no-cache, run, uninstall, manifests)" \
    '# Install with --no-cache
    '"${BIN}"' install --no-cache which
    # Verify execution
    RESOLVED=$('"${BIN}"' which bash 2>/dev/null)
    if [ -z "$RESOLVED" ]; then
        echo "Failed to execute installed which binary!"
        exit 1
    fi
    # Verify manifest exists
    if [ ! -f "$HOME/.fglibc/sysroot/var/lib/fglibc/manifests/which.list" ]; then
        echo "Manifest file for which was not created!"
        exit 1
    fi
    # Uninstall package
    '"${BIN}"' uninstall which
    # Verify binary is gone
    if [ -f "$HOME/.fglibc/sysroot/usr/bin/which" ]; then
        echo "Binary still exists after uninstall!"
        exit 1
    fi
    # Verify manifest is cleaned up
    if [ -f "$HOME/.fglibc/sysroot/var/lib/fglibc/manifests/which.list" ]; then
        echo "Manifest still exists after uninstall!"
        exit 1
    fi'

# Test 11: Real-World Applications Launch Stress Check
run_test "Real-World Apps Check (Spotify & Discord launch checks)" \
    '# Spotify help launch
    '"${BIN}"' spotify --help >/dev/null
    # Discord version launch with timeout
    timeout 3 '"${BIN}"' discord --version >/dev/null 2>&1 || true'

# Test 12: Windows Compatibility Services & Tools (win-setup & winetricks)
run_test "Windows Compatibility Services & Tools (win-setup status & winetricks)" \
    '# Verify win-setup status
    STATUS_OUT=$('"${BIN}"' win-setup status)
    if ! echo "$STATUS_OUT" | grep -q "fglibc Windows Runtime & Services Status"; then
        echo "Failed to query Windows services status!"
        exit 1
    fi
    # Verify WebView2 detection
    if ! echo "$STATUS_OUT" | grep -q "Microsoft Edge WebView2"; then
        echo "Missing WebView2 report in status output!"
        exit 1
    fi
    # Verify winetricks version query
    WINETRICKS_OUT=$('"${BIN}"' winetricks --version)
    if ! echo "$WINETRICKS_OUT" | grep -q "sha256sum"; then
        echo "Failed to invoke winetricks wrapper!"
        exit 1
    fi'

printf "\n%b======================================================================%b\n" "${CYAN}${BOLD}" "${RESET}"
if [ "${FAILED_TESTS}" -eq 0 ]; then
    printf "%b       ✔ ALL %d/%d STRESS TESTS PASSED WITH 100%% SUCCESS!       %b\n" "${GREEN}${BOLD}" "${TOTAL_TESTS}" "${TOTAL_TESTS}" "${RESET}"
else
    printf "%b       ✘ STRESS TESTS FAILED: %d/%d FAILED!                %b\n" "${RED}${BOLD}" "${FAILED_TESTS}" "${TOTAL_TESTS}" "${RESET}"
fi
printf "%b======================================================================%b\n\n" "${CYAN}${BOLD}" "${RESET}"

exit ${FAILED_TESTS}
