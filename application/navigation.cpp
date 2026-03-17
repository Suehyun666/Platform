#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <sys/mman.h>
#include <vector>
#include <string>
#include "ShmManager.h"

static volatile sig_atomic_t g_running = 1;
static void onSignal(int) { g_running = 0; }

int main(int argc, char* argv[]) {
    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);

    // SHM 연결: argv[1..] 에 활성 피처 ID가 전달됨
    std::vector<std::pair<std::string, FeatureControlState*>> features;
    for (int i = 1; i < argc; ++i) {
        auto* ptr = ShmManager::openForRead(argv[i]);
        if (ptr) {
            features.push_back({argv[i], ptr});
        } else {
            std::cerr << "[Navigation] SHM 연결 실패: " << argv[i] << "\n";
        }
    }

    std::cout << "[Navigation] 시작 (pid=" << getpid() << ")\n";

    while (g_running) {
        for (auto& [name, shm] : features) {
            // Kill Switch 최우선
            if (shm->is_killed.load(std::memory_order_relaxed)) {
                std::cout << "[Navigation/" << name << "] Kill Switch 활성 → 중단\n";
                continue;
            }
            if (!shm->is_enabled.load(std::memory_order_relaxed)) continue;

            std::cout << "[Navigation/" << name << "] 동작 중...\n";
            shm->heartbeat.fetch_add(1, std::memory_order_relaxed);
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto& [name, ptr] : features)
        munmap(ptr, sizeof(FeatureControlState));

    std::cout << "[Navigation] SIGTERM 수신 → 정상 종료\n";
    return 0;
}
