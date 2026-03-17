#include "Feature.h"

FeatureRegistry& FeatureRegistry::instance() {
    static FeatureRegistry reg;
    return reg;
}

void FeatureRegistry::add(const std::string& id, std::unique_ptr<IFeature> f) {
    map_[id] = std::move(f);
}

IFeature* FeatureRegistry::get(const std::string& id) const {
    auto it = map_.find(id);
    return (it != map_.end()) ? it->second.get() : nullptr;
}
