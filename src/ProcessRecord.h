#pragma once
#include "FeatureProfile.h"
#include <chrono>
#include <sys/types.h>

enum class ProcessState { Running, Stopping, Disabled };

struct ProcessRecord {
    pid_t          pid             = -1;
    ProcessProfile profile;
    int            retry_count     = 0;
    ProcessState   state           = ProcessState::Running;
    std::chrono::steady_clock::time_point last_started_at;
};
