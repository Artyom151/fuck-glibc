#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <numeric>

int main(int argc, char **argv) {
    std::cout << "\n============================================\n";
    std::cout << "  GLIBC C++ STL & Threading Test on Alpine  \n";
    std::cout << "============================================\n";

    std::vector<std::string> games = {
        "Doom", "Half-Life", "Cyberpunk 2077", "Minecraft", "Terraria"
    };

    std::cout << "[+] Loaded " << games.size() << " games in C++ std::vector:\n";
    for (const auto &g : games) {
        std::cout << "    * " << g << "\n";
    }

    std::cout << "\n[+] Testing C++11 std::thread (POSIX pthreads under glibc):\n";
    std::thread worker([]() {
        std::cout << "    [Worker Thread] Running inside glibc thread context!\n";
        double sum = 0.0;
        for (int i = 0; i < 100000; i++) {
            sum += std::sin(i) * std::cos(i);
        }
        std::cout << "    [Worker Thread] Math calculation finished: " << sum << "\n";
    });

    worker.join();
    std::cout << "[+] Worker thread joined successfully!\n";
    std::cout << "[+] Arguments passed to binary: " << argc << "\n";
    for (int i = 0; i < argc; i++) {
        std::cout << "    argv[" << i << "] = " << argv[i] << "\n";
    }
    std::cout << "\n[SUCCESS] Everything runs with full glibc C++ ABI compatibility!\n\n";
    return 0;
}
