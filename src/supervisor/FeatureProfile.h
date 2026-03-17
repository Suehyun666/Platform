#pragma once
#include <string>
#include <vector>
#include <algorithm>

struct RestartPolicy {
    int max_retries     = 3;
    int retry_delay_ms  = 1000;
    std::string action_on_failure = "DISABLE_FLAG";
};

// 프로세스 내부의 개별 피처
struct FeatureFlag {
    std::string feature_id;
    bool        flag = false;
};

// 하나의 프로세스(바이너리) + 그 안에 속한 피처 목록
struct ProcessProfile {
    std::string process_id;
    std::string binary_path;
    int         loop_interval_ms = 100; // SDK 메인 루프 주기 (manifest로 주입, 앱 기본값 override)
    RestartPolicy restart_policy;
    std::vector<FeatureFlag> features;

    // 피처 중 하나라도 켜져 있으면 프로세스를 띄운다
    bool hasAnyEnabledFeature() const {
        return std::ranges::any_of(features, [](const auto& f) { return f.flag; });
    }
};
