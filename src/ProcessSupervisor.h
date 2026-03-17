#pragma once
#include "ProcessRecord.h"
#include "ShmManager.h"
#include "Watchdog.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

enum class SupervisorError {
    Ok, AlreadyRunning, ForkFailed, NotFound, NotRunning,
};

class ProcessSupervisor {
public:
    ProcessSupervisor();
    ~ProcessSupervisor();

    SupervisorError launch(const ProcessProfile& profile);
    SupervisorError gracefulKill(const std::string& process_id, int timeout_ms = 2000);
    SupervisorError hardKill(const std::string& process_id);
    SupervisorError setFeatureFlag(const std::string& process_id,
                                   const std::string& feature_id, bool value);
    bool isAlive(const std::string& process_id) const;
    void listAll() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ProcessRecord>               registry_;
    std::unordered_map<std::string, std::unique_ptr<ShmManager>> shm_slots_;
    Watchdog watchdog_;

    SupervisorError launchLocked(const ProcessProfile& profile);
};
