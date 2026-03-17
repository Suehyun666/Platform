#pragma once
#include "ShmManager.h"
#include "Feature.h"
#include <vector>
#include <string>
#include <csignal>

// 앱 개발자가 상속하거나, SDV_FEATURE 매크로로 피처를 등록해 사용하는 SDK 베이스 클래스.
// argv 파싱, SHM 연결, 시그널 처리, 메인 루프를 모두 담당한다.
//
// 피처 dispatch 우선순위:
//   1. FeatureRegistry에 등록된 피처 → IFeature::onUpdate() 호출
//   2. 미등록 피처 → onUpdate(feature_id) 가상 함수 호출 (fallback)
//
// loop interval 우선순위: manifest(--loop-ms=N) > 앱 생성자 기본값
class SdvAppFrame {
public:
    explicit SdvAppFrame(int default_loop_ms = 100);
    virtual ~SdvAppFrame() = default;

    // Fallback Hook: SDV_FEATURE 미사용 시 오버라이드해 사용.
    // SDV_FEATURE로 등록된 피처는 IFeature 쪽으로 dispatch되므로 여기서 처리하지 않아도 됨.
    virtual void onStart(const std::vector<std::string>& features) {}
    virtual void onUpdate(const std::string& feature_id) {}
    virtual void onStop() {}

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
