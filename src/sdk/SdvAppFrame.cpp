#include "SdvAppFrame.h"
#include <sys/mman.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <string_view>

volatile sig_atomic_t SdvAppFrame::running_ = 1;

SdvAppFrame::SdvAppFrame(int default_loop_ms) : loop_interval_ms_(default_loop_ms) {}

void SdvAppFrame::signalHandler(int) { running_ = 0; }

int SdvAppFrame::run(int argc, char* argv[]) {
    signal(SIGTERM, signalHandler);
    signal(SIGINT,  signalHandler);

    // argv 파싱:
    //   --loop-ms=N  → loop_interval_ms_ override (manifest에서 주입)
    //   그 외        → feature ID (SHM 연결 대상)
    std::vector<std::string> features;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--loop-ms=")) {
            try {
                loop_interval_ms_ = std::stoi(std::string(arg.substr(10)));
            } catch (...) {
                std::cerr << "[SDK] 잘못된 --loop-ms 값: " << arg << "\n";
            }
            continue;
        }

        features.emplace_back(argv[i]);

        FeatureControlState* ptr = nullptr;
        for (int r = 0; r < kShmRetries && !ptr; ++r) {
            ptr = ShmManager::connect(argv[i]);
            if (!ptr) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (ptr) {
            slots_.push_back({argv[i], ptr});
        } else {
            std::cerr << "[SDK] SHM 연결 실패: " << argv[i] << "\n";
        }
    }

    onStart(features);

    bool killed = false;
    while (running_ && !killed) {
        bool any_enabled = false;
        for (auto& [name, shm] : slots_) {
            if (shm->is_killed.load(std::memory_order_relaxed)) {
                std::cout << "[SDK] Kill Switch: " << name << "\n";
                killed = true;
                break;
            }
            if (shm->is_enabled.load(std::memory_order_relaxed)) {
                any_enabled = true;
                shm->heartbeat.fetch_add(1, std::memory_order_relaxed);
                onUpdate(name);
            }
        }
        if (!killed) {
            // 모든 피처가 OFF되면 스스로 종료 (Supervisor가 자동 재기동)
            if (!any_enabled && !slots_.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(loop_interval_ms_));
        }
    }

    onStop();
    for (auto& [name, ptr] : slots_)
        munmap(ptr, sizeof(FeatureControlState));
    return 0;
}
