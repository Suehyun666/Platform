#pragma once
#include <chrono>
#include <sys/types.h>

enum class ProcessState { Running, Stopping, Disabled };

struct ProcessRecord {
    pid_t        pid         = -1;
    int          retry_count = 0;
    ProcessState state       = ProcessState::Disabled;  // 실행 전까지는 Disabled
    std::chrono::steady_clock::time_point last_started_at;
};
