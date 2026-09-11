#!/bin/sh
set -e

echo "=== Running fglibc Test Suite on Alpine Linux ==="

echo "[1/5] Testing fglibc version & status..."
./fglibc version
./fglibc status

echo "[2/5] Testing ELF Parser & Dependency Checker..."
./fglibc check ./examples/glibc_test
./fglibc check ./examples/glibc_cpp_test
./fglibc check ./examples/glibc_sdl2_test

echo "[3/5] Testing Native C Glibc Execution..."
./fglibc ./examples/glibc_test --flag1 --test=ok

echo "[4/5] Testing Native C++ STL & Multi-threading Glibc Execution..."
./fglibc ./examples/glibc_cpp_test SuperTuxKart

echo "[5/5] Testing SDL2, Vulkan & OpenGL Gaming APIs..."
./fglibc ./examples/glibc_sdl2_test

echo "\n=============================================="
echo "  ALL TESTS PASSED WITH 100% SUCCESS!"
echo "=============================================="
