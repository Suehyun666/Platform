#pragma once
#include "FeatureProfile.h"
#include "ProcessRecord.h"
#include "ShmManager.h"
#include "Watchdog.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <sys/types.h>

enum class SupervisorError {
    Ok, AlreadyRunning, ForkFailed, NotFound, NotRunning,
};

class ProcessSupervisor {
public:
    ProcessSupervisor();
    ~ProcessSupervisor();

    // manifest의 모든 프로필을 등록 (launch 여부와 무관)
    void registerProfile(const ProcessProfile& profile);

    SupervisorError launch(const ProcessProfile& profile);
    SupervisorError gracefulKill(const std::string& process_id, int timeout_ms = 2000);
    SupervisorError hardKill(const std::string& process_id);
    SupervisorError setFeatureFlag(const std::string& process_id,
                                   const std::string& feature_id, bool value);
    bool isAlive(const std::string& process_id) const;
    void listAll() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ProcessProfile>              all_profiles_;  // 등록된 전체 프로필
    std::unordered_map<std::string, ProcessRecord>               registry_;
    std::unordered_map<std::string, std::unique_ptr<ShmManager>> shm_slots_;
    Watchdog watchdog_;

    SupervisorError launchLocked(const ProcessProfile& profile);
    bool isAliveLocked(const std::string& process_id) const;
    // 프로세스의 모든 피처 SHM에 is_killed 플래그 설정 (mutex 보유 상태에서 호출)
    void setAllShmKilled(const std::string& process_id, bool value);
    // pid가 종료될 때까지 timeout_ms 동안 폴링. 종료됐으면 true 반환.
    static bool waitForExit(pid_t pid, int timeout_ms);
};
