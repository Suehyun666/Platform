#include "Watchdog.h"
#include <sys/wait.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

Watchdog::Watchdog(std::unordered_map<std::string, ProcessRecord>& registry,
                   std::unordered_map<std::string, ProcessProfile>& all_profiles,
                   std::mutex& mutex, RestartFn restart_cb)
    : registry_(registry), all_profiles_(all_profiles), mutex_(mutex), restart_cb_(std::move(restart_cb))
{
    thread_ = std::thread(&Watchdog::loop, this);
}

void Watchdog::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

Watchdog::~Watchdog() { stop(); }

void Watchdog::loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kIntervalMs));

        std::vector<std::pair<ProcessProfile, int>> to_restart;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, rec] : registry_) {
                if (rec.pid <= 0 || rec.state != ProcessState::Running) continue;

                int status;
                if (waitpid(rec.pid, &status, WNOHANG) <= 0) continue;

                std::cout << "[Watchdog] " << id << " 종료 감지 (pid=" << rec.pid << ")\n";
                rec.pid = -1;

                // 안정적으로 실행됐으면 retry_count 초기화
                auto uptime = std::chrono::steady_clock::now() - rec.last_started_at;
                if (uptime >= std::chrono::seconds(kStableSeconds)) {
                    rec.retry_count = 0;
                    std::cout << "[Watchdog] " << id << " 안정 실행 감지 → retry_count 초기화\n";
                }

                // 모든 피처가 OFF면 자율 종료가 의도된 동작 → 재시작하지 않음
                const auto& profile = all_profiles_.at(id);
                if (!profile.hasAnyEnabledFeature()) {
                    std::cout << "[Watchdog] " << id << " 피처 전부 OFF → 재시작 안 함\n";
                    rec.state = ProcessState::Disabled;
                    continue;
                }

                const auto& policy = profile.restart_policy;
                if (rec.retry_count < policy.max_retries) {
                    rec.retry_count++;
                    std::cout << "[Watchdog] " << id << " 재시작 예약 ("
                              << rec.retry_count << "/" << policy.max_retries << ")\n";
                    to_restart.push_back({profile, policy.retry_delay_ms});
                } else {
                    std::cerr << "[Watchdog] " << id << " 최대 재시도 초과 → Disabled\n";
                    rec.state = ProcessState::Disabled;
                }
            }
        }

        for (const auto& [profile, delay_ms] : to_restart) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            restart_cb_(profile);
        }
    }
}
