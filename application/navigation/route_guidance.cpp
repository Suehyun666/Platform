#include "Feature.h"
#include <iostream>

class RouteGuidance : public IFeature {
public:
    void onStart() override {
        std::cout << "[Navigation] GPS 모듈 초기화, 경로 탐색 준비\n";
    }

    void onUpdate() override {
        // 실제 구현: GPS 위치 읽기 → 경로 재계산 → 다음 안내 지점 출력
        std::cout << "[Navigation] route_guidance: 경로 안내 갱신 중...\n";
    }

    void onStop() override {
        std::cout << "[Navigation] route_guidance: GPS 연결 종료\n";
    }
};

SDV_FEATURE("route_guidance", RouteGuidance);
