#pragma once
#include <string>

struct RestartPolicy {
    int max_retries     = 3;
    int retry_delay_ms  = 1000;
    std::string action_on_failure = "DISABLE_FLAG";
};

struct FeatureProfile {
    std::string feature_id;
    std::string binary_path;
    bool        flag = false;
    RestartPolicy restart_policy;
};
