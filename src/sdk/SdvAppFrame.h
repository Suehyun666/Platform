#pragma once
#include "ShmManager.h"
#include <vector>
#include <string>
#include <csignal>

// 앱 개발자가 상속하여 구현하는 SDK 베이스 클래스.
// argv 파싱, SHM 연결, 시그널 처리, 메인 루프를 모두 담당한다.
//
// loop interval 우선순위: manifest(--loop-ms=N) > 앱 생성자 기본값
class SdvAppFrame {
public:
    // default_loop_ms: 앱에서 설정하는 기본 루프 주기 (manifest로 override 가능)
    explicit SdvAppFrame(int default_loop_ms = 100);
    virtual ~SdvAppFrame() = default;

    // 앱 개발자가 구현할 Hook
    virtual void onStart(const std::vector<std::string>& features) = 0;
    virtual void onUpdate(const std::string& feature_id) = 0;
    virtual void onStop() = 0;

    // argv 파싱 → SHM 연결 → 루프 실행 → 정리
    int run(int argc, char* argv[]);

protected:
    static constexpr int kShmRetries = 10;

private:
    int loop_interval_ms_;
    std::vector<std::pair<std::string, FeatureControlState*>> slots_;

    static void signalHandler(int);
    static volatile sig_atomic_t running_;
};
