#include "ManifestLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static ProcessProfile parseProfile(const json& item, bool& ok) {
    ok = false;
    ProcessProfile p;

    if (!item.contains("process_id") || !item.contains("binary_path")) {
        std::cerr << "[ManifestLoader] process_id 또는 binary_path 누락\n";
        return p;
    }

    p.process_id       = item["process_id"].get<std::string>();
    p.binary_path      = item["binary_path"].get<std::string>();
    p.loop_interval_ms = item.value("loop_interval_ms", 100);

    if (item.contains("restart_policy")) {
        const auto& rp             = item["restart_policy"];
        p.restart_policy.max_retries    = rp.value("max_retries", 3);
        p.restart_policy.retry_delay_ms = rp.value("retry_delay_ms", 1000);
        p.restart_policy.action_on_failure =
            rp.value("action_on_failure", std::string("DISABLE_FLAG")) == "RESTART"
                ? FailureAction::Restart
                : FailureAction::DisableFlag;
    }

    if (item.contains("features") && item["features"].is_array()) {
        for (const auto& f : item["features"]) {
            if (!f.contains("feature_id")) continue;
            FeatureFlag ff;
            ff.feature_id = f["feature_id"].get<std::string>();
            ff.flag       = f.value("flag", false);
            p.features.push_back(ff);
        }
    }

    ok = true;
    return p;
}

std::optional<std::vector<ProcessProfile>> ManifestLoader::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[ManifestLoader] 파일 열기 실패: " << path << "\n";
        return std::nullopt;
    }

    json root;
    if (!json::accept(file)) {
        std::cerr << "[ManifestLoader] JSON 형식 오류: " << path << "\n";
        return std::nullopt;
    }
    file.seekg(0);
    file >> root;

    if (!root.is_array()) {
        std::cerr << "[ManifestLoader] 최상위가 배열이 아닙니다\n";
        return std::nullopt;
    }

    std::vector<ProcessProfile> profiles;
    for (const auto& item : root) {
        bool ok = false;
        auto p  = parseProfile(item, ok);
        if (ok) {
            profiles.push_back(std::move(p));
        }
        // 파싱 실패한 항목은 건너뛰고 나머지 계속 로드 (fail-safe)
    }

    return profiles;
}
