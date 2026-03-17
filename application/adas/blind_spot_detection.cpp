#include "Feature.h"
#include <iostream>

class BlindSpotDetection : public IFeature {
public:
    void onStart() override {
        std::cout << "[ADAS] 사각지대 레이더 초기화\n";
    }

    void onUpdate() override {
        // 실제 구현: 측면 레이더 데이터 읽기 → 차량 접근 감지
        std::cout << "[ADAS] blind_spot_detection: 측면 감지 중...\n";
    }

    void onStop() override {
        std::cout << "[ADAS] blind_spot_detection: 레이더 전원 차단\n";
    }
};

SDV_FEATURE("blind_spot_detection", BlindSpotDetection);
