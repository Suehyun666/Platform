#pragma once
#include <string>
#include <unordered_map>
#include <memory>

// 피처 하나의 인터페이스. 각 피처 파일에서 상속해 구현.
class IFeature {
public:
    virtual void onStart()  {}
    virtual void onUpdate() {}
    virtual void onStop()   {}
    virtual ~IFeature() = default;
};

// 피처 ID → IFeature 인스턴스 매핑 (싱글톤)
class FeatureRegistry {
public:
    static FeatureRegistry& instance();
    void      add(const std::string& id, std::unique_ptr<IFeature> f);
    IFeature* get(const std::string& id) const;

private:
    std::unordered_map<std::string, std::unique_ptr<IFeature>> map_;
};

// 정적 초기화로 피처를 자동 등록하는 헬퍼
struct FeatureRegistrar {
    FeatureRegistrar(const std::string& id, std::unique_ptr<IFeature> f) {
        FeatureRegistry::instance().add(id, std::move(f));
    }
};

// @Service 역할: 파일 스코프에 선언하면 main() 전에 자동 등록됨
//   SDV_FEATURE("collision_avoidance", CollisionAvoidance);
#define SDV_FEATURE(feature_id, cls) \
    static FeatureRegistrar _sdv_reg_##cls(feature_id, std::make_unique<cls>())
