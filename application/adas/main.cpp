#include "SdvAppFrame.h"

// 피처는 각 .cpp 파일에서 SDV_FEATURE 매크로로 등록됨.
// SdvAppFrame이 FeatureRegistry를 통해 자동으로 dispatch.
int main(int argc, char* argv[]) {
    SdvAppFrame app(50);
    return app.run(argc, argv);
}
