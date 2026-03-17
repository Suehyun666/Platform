#include "ShmManager.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <new>

static std::string shmName(const std::string& feature_id) {
    return "/platform_" + feature_id;
}

ShmManager::ShmManager(const std::string& feature_id, bool initial_enabled)
    : feature_id_(feature_id)
{
    const std::string name = shmName(feature_id);
    shm_unlink(name.c_str());  // 좀비 SHM 정리
    fd_ = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) { std::cerr << "[SHM] shm_open 실패: " << name << "\n"; return; }

    if (ftruncate(fd_, sizeof(FeatureControlState)) < 0) {
        std::cerr << "[SHM] ftruncate 실패: " << name << "\n"; return;
    }

    void* addr = mmap(nullptr, sizeof(FeatureControlState),
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) { std::cerr << "[SHM] mmap 실패: " << name << "\n"; return; }

    ptr_ = new (addr) FeatureControlState{};
    ptr_->is_enabled.store(initial_enabled, std::memory_order_relaxed);
    std::cout << "[SHM] 생성: " << name << "\n";
}

ShmManager::~ShmManager() {
    if (ptr_) munmap(ptr_, sizeof(FeatureControlState));
    if (fd_ >= 0) close(fd_);
    shm_unlink(shmName(feature_id_).c_str());
}

void ShmManager::setEnabled(bool value) {
    if (ptr_) ptr_->is_enabled.store(value, std::memory_order_relaxed);
}

void ShmManager::setKilled(bool value) {
    if (ptr_) ptr_->is_killed.store(value, std::memory_order_relaxed);
}

FeatureControlState* ShmManager::connect(const std::string& feature_id) {
    const std::string name = shmName(feature_id);
    int fd = shm_open(name.c_str(), O_RDWR, 0666);
    if (fd < 0) return nullptr;

    void* addr = mmap(nullptr, sizeof(FeatureControlState),
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (addr == MAP_FAILED) ? nullptr : static_cast<FeatureControlState*>(addr);
}
