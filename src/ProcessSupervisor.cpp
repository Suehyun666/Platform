#include "ProcessSupervisor.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <chrono>
#include <vector>

ProcessSupervisor::ProcessSupervisor()
    : watchdog_(registry_, all_profiles_, mutex_, [this](const ProcessProfile& p) { launch(p); })
{}

void ProcessSupervisor::registerProfile(const ProcessProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    all_profiles_[profile.process_id] = profile;
    // SHM 수명 = feature 등록 수명. 프로세스 재시작과 무관하게 유지.
    for (const auto& f : profile.features)
        shm_slots_[f.feature_id] = std::make_unique<ShmManager>(f.feature_id, f.flag);
}

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

    pid_t pid = fork();
    if (pid < 0) {
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
        // flag 여부와 무관하게 모든 피처 ID 전달 → 앱이 SHM을 처음부터 연결해둠
        for (const auto& f : profile.features) argv.push_back(f.feature_id.c_str());
        argv.push_back(nullptr);
        execvp(argv[0], const_cast<char**>(argv.data()));
        _exit(EXIT_FAILURE);
    }

    auto& rec           = registry_[profile.process_id];
    rec.pid             = pid;
    rec.state           = ProcessState::Running;
    rec.retry_count     = 0;
    rec.last_started_at = std::chrono::steady_clock::now();

    std::cout << "[Supervisor] 실행: " << profile.process_id << " (pid=" << pid << ")\n  활성 피처: ";
    for (const auto& f : profile.features) if (f.flag) std::cout << f.feature_id << " ";
    std::cout << "\n";
    return SupervisorError::Ok;
}

bool ProcessSupervisor::isAliveLocked(const std::string& process_id) const {
    auto it = registry_.find(process_id);
    return it != registry_.end() && it->second.pid > 0 && ::kill(it->second.pid, 0) == 0;
}
