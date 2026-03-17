#pragma once
#include "FeatureProfile.h"
#include <optional>
#include <vector>
#include <string>

class ManifestLoader {
public:
    // 성공: profiles, 파일/형식 오류: nullopt, 개별 항목 오류: 해당 항목 skip
    static std::optional<std::vector<FeatureProfile>> load(const std::string& path);
};
