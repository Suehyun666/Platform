#include "Feature.h"
#include <iostream>

class MapRendering : public IFeature {
public:
    void onStart() override {
        std::cout << "[Navigation] 지도 렌더러 초기화\n";
    }

    void onUpdate() override {
        // 실제 구현: 현재 위치 주변 지도 타일 로드 → GPU 렌더링
        std::cout << "[Navigation] map_rendering: 지도 타일 렌더링 중...\n";
    }

    void onStop() override {
        std::cout << "[Navigation] map_rendering: 렌더러 종료\n";
    }
};

SDV_FEATURE("map_rendering", MapRendering);
