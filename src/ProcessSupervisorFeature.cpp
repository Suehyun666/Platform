#include "ProcessSupervisor.h"
#include <iostream>
#include <csignal>

SupervisorError ProcessSupervisor::setFeatureFlag(const std::string& process_id,
                                                   const std::string& feature_id, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 등록된 프로필에서 프로세스 찾기
    auto p_it = all_profiles_.find(process_id);
    if (p_it == all_profiles_.end()) return SupervisorError::NotFound;

    // 2. all_profiles_의 flag 업데이트
    bool found = false;
    for (auto& f : p_it->second.features) {
        if (f.feature_id == feature_id) { f.flag = value; found = true; break; }
    }
    if (!found) return SupervisorError::NotFound;

    // 3. registry_ 내 profile도 동기화 (재시작 시 최신 flag 반영)
    auto r_it = registry_.find(process_id);
    if (r_it != registry_.end()) {
        for (auto& f : r_it->second.profile.features)
            if (f.feature_id == feature_id) { f.flag = value; break; }
    }

    // 4. 실행 중이면 SHM으로 즉시 전달
    auto s_it = shm_slots_.find(feature_id);
    if (s_it != shm_slots_.end()) s_it->second->setEnabled(value);

    std::cout << "[Supervisor] 피처 " << feature_id << " → " << (value ? "ON" : "OFF") << "\n";

    // 5. 피처를 켰는데 프로세스가 없거나 죽어있으면 자동 기동
    if (value && !isAliveLocked(process_id)) {
        // Disabled 상태면 retry_count 초기화 후 재기동
        if (r_it != registry_.end()) r_it->second.retry_count = 0;
        std::cout << "[Supervisor] 프로세스 자동 기동: " << process_id << "\n";
        launchLocked(p_it->second);
    }

    return SupervisorError::Ok;
}

bool ProcessSupervisor::isAlive(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isAliveLocked(process_id);
}

void ProcessSupervisor::listAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (all_profiles_.empty()) { std::cout << "[Supervisor] 등록된 프로세스 없음\n"; return; }

    std::cout << "[Supervisor] 프로세스 목록:\n";
    for (const auto& [id, profile] : all_profiles_) {
        auto r_it = registry_.find(id);
        bool alive = isAliveLocked(id);

        const char* s = "NotStarted";
        int pid = -1, retries = 0;
        if (r_it != registry_.end()) {
            s = r_it->second.state == ProcessState::Running  ? "Running"  :
                r_it->second.state == ProcessState::Stopping ? "Stopping" : "Disabled";
            pid     = r_it->second.pid;
            retries = r_it->second.retry_count;
        }

        std::cout << "  [" << s << "] " << id
                  << "  pid=" << pid
                  << "  " << (alive ? "alive" : "dead")
                  << "  retries=" << retries << "/" << profile.restart_policy.max_retries << "\n"
                  << "    features: ";
        for (const auto& f : profile.features)
            std::cout << f.feature_id << "=" << (f.flag ? "ON" : "OFF") << " ";
        std::cout << "\n";
    }
}
