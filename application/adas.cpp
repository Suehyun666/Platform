#include "SdvAppFrame.h"
#include <iostream>

class AdasApp : public SdvAppFrame {
public:
    // 앱 기본 루프 주기: 50ms. manifest의 loop_interval_ms가 있으면 override됨.
    AdasApp() : SdvAppFrame(50) {}

    void onStart(const std::vector<std::string>& features) override {
        for (const auto& f : features) {
            if (f == "collision_avoidance")  std::cout << "[ADAS] 전방 충돌 감지 센서 초기화\n";
            else if (f == "blind_spot_detection") std::cout << "[ADAS] 사각지대 레이더 초기화\n";
        }
    }

    void onUpdate(const std::string& feature_id) override {
        if (feature_id == "collision_avoidance") {
            // 전방 장애물 거리 계산 (실제 구현 시 센서 데이터 읽기)
            std::cout << "[ADAS] collision_avoidance: 전방 거리 측정 중...\n";
        } else if (feature_id == "blind_spot_detection") {
            // 측면 차량 감지 (실제 구현 시 레이더 데이터 읽기)
            std::cout << "[ADAS] blind_spot_detection: 측면 감지 중...\n";
        }
    }

    void onStop() override {
        std::cout << "[ADAS] 센서 전원 차단, 안전 종료 완료\n";
    }
};

int main(int argc, char* argv[]) {
    AdasApp app;
    return app.run(argc, argv);
}
