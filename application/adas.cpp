#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <vector>
#include <string>

static volatile sig_atomic_t g_running = 1;

static void onSignal(int) {
    g_running = 0;
}

int main(int argc, char* argv[]) {
    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);

    // argv[1..] 에 활성화된 피처 ID가 전달됨
    std::vector<std::string> features;
    for (int i = 1; i < argc; ++i) {
        features.emplace_back(argv[i]);
    }

    std::cout << "[ADAS] 시작 (pid=" << getpid() << ")\n";
    std::cout << "[ADAS] 활성 피처: ";
    for (const auto& f : features) std::cout << f << " ";
    std::cout << "\n";

    while (g_running) {
        for (const auto& f : features) {
            std::cout << "[ADAS/" << f << "] 동작 중...\n";
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "[ADAS] SIGTERM 수신 → 정상 종료\n";
    return 0;
}
