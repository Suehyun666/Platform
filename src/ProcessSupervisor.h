#pragma once
#include "FeatureProfile.h"
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include <thread>
#include <atomic>
#include <mutex>

enum class SupervisorError {
    Ok,
    AlreadyRunning,
    ForkFailed,
    ExecFailed,
    NotFound,
    NotRunning,
};

class ProcessSupervisor {
public:
    ProcessSupervisor();
    ~ProcessSupervisor();

    SupervisorError launch(const FeatureProfile& profile);
    SupervisorError gracefulKill(const std::string& feature_id, int timeout_ms = 2000);
    SupervisorError hardKill(const std::string& feature_id);
    bool            isAlive(const std::string& feature_id) const;
    void            listAll() const;

private:
    struct ProcessRecord {
        pid_t          pid         = -1;
        FeatureProfile profile;
        int            retry_count = 0;
        bool           disabled    = false; // true면 watchdog가 재시작 안 함
    };

    mutable std::mutex                             mutex_;
    std::unordered_map<std::string, ProcessRecord> registry_;

    std::thread       watchdog_thread_;
    std::atomic<bool> running_{true};

    SupervisorError launchLocked(const FeatureProfile& profile);
    void            watchdogLoop();
};
