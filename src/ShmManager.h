#pragma once
#include <atomic>
#include <string>

// SHM에 배치될 피처 상태 구조체 (lock-free 읽기/쓰기)
// 주의: std::atomic이 lock-free임을 가정 (x86_64 기준)
struct FeatureControlState {
    std::atomic<bool>     is_enabled{false};
    std::atomic<bool>     is_killed{false};
    std::atomic<uint64_t> heartbeat{0};
};

// Supervisor가 생성/소유. 소멸 시 SHM 자동 해제.
class ShmManager {
public:
    ShmManager(const std::string& feature_id, bool initial_enabled);
    ~ShmManager();

    ShmManager(const ShmManager&)            = delete;
    ShmManager& operator=(const ShmManager&) = delete;

    void setEnabled(bool value);
    void setKilled(bool value);

    // App 쪽에서 호출. 반환된 포인터는 호출자가 munmap 해야 함.
    static FeatureControlState* connect(const std::string& feature_id);

private:
    std::string          feature_id_;
    int                  fd_  = -1;
    FeatureControlState* ptr_ = nullptr;
};
