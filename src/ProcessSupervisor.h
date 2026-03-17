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

// 프로세스의 현재 상태
enum class ProcessState {
    Running,   // 정상 실행 중, watchdog 재시작 대상
    Stopping,  // gracefulKill 요청됨, SIGTERM 후 종료 대기 중
    Disabled,  // max_retries 초과 또는 kill 완료, 재시작 안 함
};

class ProcessSupervisor {
public:
    ProcessSupervisor();
    ~ProcessSupervisor();

    SupervisorError launch(const ProcessProfile& profile);
    SupervisorError gracefulKill(const std::string& process_id, int timeout_ms = 2000);
    SupervisorError hardKill(const std::string& process_id);
    bool            isAlive(const std::string& process_id) const;
    void            listAll() const;

private:
    struct ProcessRecord {
        pid_t          pid         = -1;
        ProcessProfile profile;
        int            retry_count = 0;
        ProcessState   state       = ProcessState::Running;
    };

    mutable std::mutex                             mutex_;
    std::unordered_map<std::string, ProcessRecord> registry_;

    std::thread       watchdog_thread_;
    std::atomic<bool> running_{true};

    SupervisorError launchLocked(const ProcessProfile& profile);
    void            watchdogLoop();
};
