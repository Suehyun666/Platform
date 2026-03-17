#pragma once
#include "ProcessRecord.h"
#include "FeatureProfile.h"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>

class Watchdog {
public:
    using RestartFn = std::function<void(const ProcessProfile&)>;

    Watchdog(std::unordered_map<std::string, ProcessRecord>& registry,
             std::unordered_map<std::string, ProcessProfile>& all_profiles,
             std::mutex& mutex,
             RestartFn restart_cb);
    ~Watchdog();

    Watchdog(const Watchdog&)            = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    void loop();

    std::unordered_map<std::string, ProcessRecord>&  registry_;
    std::unordered_map<std::string, ProcessProfile>& all_profiles_;
    std::mutex& mutex_;
    RestartFn   restart_cb_;
    std::thread thread_;
    std::atomic<bool> running_{true};

    static constexpr int kIntervalMs    = 500;
    static constexpr int kStableSeconds = 30; // 이 시간 이상 실행 시 retry_count 초기화
};
