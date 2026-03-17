#include "SdvAppFrame.h"
#include <iostream>

class NavigationApp : public SdvAppFrame {
public:
    // 앱 기본 루프 주기: 200ms. manifest의 loop_interval_ms가 있으면 override됨.
    NavigationApp() : SdvAppFrame(200) {}

    void onStart(const std::vector<std::string>& features) override {
        std::cout << "[Navigation] 초기화: " << features.size() << "개 피처 로드\n";
    }

    void onUpdate(const std::string& feature_id) override {
        std::cout << "[Navigation/" << feature_id << "] 동작 중...\n";
    }

    void onStop() override {
        std::cout << "[Navigation] 안전 종료 완료\n";
    }
};

int main(int argc, char* argv[]) {
    NavigationApp app;
    return app.run(argc, argv);
}
