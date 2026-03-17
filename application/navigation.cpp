#include "SdvAppFrame.h"
#include <iostream>

class NavigationApp : public SdvAppFrame {
public:
    // 앱 기본 루프 주기: 200ms. manifest의 loop_interval_ms가 있으면 override됨.
    NavigationApp() : SdvAppFrame(200) {}

    void onStart(const std::vector<std::string>& features) override {
        for (const auto& f : features) {
            if (f == "route_guidance")  std::cout << "[Navigation] GPS 모듈 초기화, 경로 탐색 준비\n";
            else if (f == "map_rendering") std::cout << "[Navigation] 지도 렌더러 초기화\n";
        }
    }

    void onUpdate(const std::string& feature_id) override {
        if (feature_id == "route_guidance") {
            // 현재 위치 기반 경로 재계산 (실제 구현 시 GPS 데이터 읽기)
            std::cout << "[Navigation] route_guidance: 경로 안내 갱신 중...\n";
        } else if (feature_id == "map_rendering") {
            // 화면에 지도 타일 렌더링 (실제 구현 시 GPU 호출)
            std::cout << "[Navigation] map_rendering: 지도 타일 렌더링 중...\n";
        }
    }

    void onStop() override {
        std::cout << "[Navigation] GPS 연결 종료, 안전 종료 완료\n";
    }
};

int main(int argc, char* argv[]) {
    NavigationApp app;
    return app.run(argc, argv);
}
