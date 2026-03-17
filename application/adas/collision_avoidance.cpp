#include "Feature.h"
#include <iostream>

class CollisionAvoidance : public IFeature {
public:
    void onStart() override {
        std::cout << "[ADAS] 전방 충돌 감지 센서 초기화\n";
    }

    void onUpdate() override {
        // 실제 구현: 전방 카메라/라이다 데이터 읽기 → 장애물 거리 계산
        std::cout << "[ADAS] collision_avoidance: 전방 거리 측정 중...\n";
    }

    void onStop() override {
        std::cout << "[ADAS] collision_avoidance: 센서 전원 차단\n";
    }
};

SDV_FEATURE("collision_avoidance", CollisionAvoidance);
