#include "ProcessSupervisor.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <chrono>
#include <vector>

ProcessSupervisor::ProcessSupervisor()
    : watchdog_(registry_, mutex_, [this](const ProcessProfile& p) { launch(p); })
{}

ProcessSupervisor::~ProcessSupervisor() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, rec] : registry_) {
        if (rec.pid > 0 && ::kill(rec.pid, 0) == 0) {
            ::kill(rec.pid, SIGTERM);
            waitpid(rec.pid, nullptr, 0);
        }
    }
}

SupervisorError ProcessSupervisor::launch(const ProcessProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    return launchLocked(profile);
}

SupervisorError ProcessSupervisor::launchLocked(const ProcessProfile& profile) {
    auto it = registry_.find(profile.process_id);
    if (it != registry_.end() && it->second.pid > 0 && ::kill(it->second.pid, 0) == 0) {
        std::cerr << "[Supervisor] 이미 실행 중: " << profile.process_id << "\n";
        return SupervisorError::AlreadyRunning;
    }

    // SHM은 fork 전에 생성해야 자식 프로세스가 즉시 열 수 있다
    for (const auto& f : profile.features)
        shm_slots_[f.feature_id] = std::make_unique<ShmManager>(f.feature_id, f.flag);

    pid_t pid = fork();
    if (pid < 0) {
        for (const auto& f : profile.features) shm_slots_.erase(f.feature_id);
        std::cerr << "[Supervisor] fork 실패\n";
        return SupervisorError::ForkFailed;
    }

    if (pid == 0) {
        std::string log = profile.process_id + ".log";
        int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }

        // manifest의 loop_interval_ms를 --loop-ms=N 형태로 전달 (앱 기본값 override)
        std::string loop_arg = "--loop-ms=" + std::to_string(profile.loop_interval_ms);
        std::vector<const char*> argv = {profile.binary_path.c_str(), loop_arg.c_str()};
        for (const auto& f : profile.features) if (f.flag) argv.push_back(f.feature_id.c_str());
        argv.push_back(nullptr);
        execvp(argv[0], const_cast<char**>(argv.data()));
        _exit(EXIT_FAILURE);
    }

    auto& rec           = registry_[profile.process_id];
    rec.pid             = pid;
    rec.profile         = profile;
    rec.state           = ProcessState::Running;
    rec.last_started_at = std::chrono::steady_clock::now();

    std::cout << "[Supervisor] 실행: " << profile.process_id << " (pid=" << pid << ")\n  활성 피처: ";
    for (const auto& f : profile.features) if (f.flag) std::cout << f.feature_id << " ";
    std::cout << "\n";
    return SupervisorError::Ok;
}

SupervisorError ProcessSupervisor::setFeatureFlag(const std::string& /*process_id*/,
                                                   const std::string& feature_id, bool value) {
    auto it = shm_slots_.find(feature_id);
    if (it == shm_slots_.end()) return SupervisorError::NotFound;
    it->second->setEnabled(value);
    std::cout << "[Supervisor] 피처 " << feature_id << " → " << (value ? "ON" : "OFF") << "\n";
    return SupervisorError::Ok;
}

bool ProcessSupervisor::isAlive(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(process_id);
    return it != registry_.end() && it->second.pid > 0 && ::kill(it->second.pid, 0) == 0;
}

void ProcessSupervisor::listAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registry_.empty()) { std::cout << "[Supervisor] 실행 중인 프로세스 없음\n"; return; }
    std::cout << "[Supervisor] 실행 목록:\n";
    for (const auto& [id, rec] : registry_) {
        const char* s = rec.state == ProcessState::Running  ? "Running"  :
                        rec.state == ProcessState::Stopping ? "Stopping" : "Disabled";
        bool alive = rec.pid > 0 && ::kill(rec.pid, 0) == 0;
        std::cout << "  [" << s << "] " << id << "  pid=" << rec.pid
                  << "  " << (alive ? "alive" : "dead")
                  << "  retries=" << rec.retry_count << "/" << rec.profile.restart_policy.max_retries << "\n"
                  << "    features: ";
        for (const auto& f : rec.profile.features)
            std::cout << f.feature_id << "=" << (f.flag ? "ON" : "OFF") << " ";
        std::cout << "\n";
    }
}
