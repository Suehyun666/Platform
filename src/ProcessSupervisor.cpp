#include "ProcessSupervisor.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>
#include <vector>

// ── 생성자 / 소멸자 ──────────────────────────────────────────────────────────

ProcessSupervisor::ProcessSupervisor() {
    watchdog_thread_ = std::thread(&ProcessSupervisor::watchdogLoop, this);
}

ProcessSupervisor::~ProcessSupervisor() {
    running_ = false;
    if (watchdog_thread_.joinable())
        watchdog_thread_.join();

    // 남아있는 자식 프로세스 전부 정리
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, rec] : registry_) {
        if (rec.pid > 0 && ::kill(rec.pid, 0) == 0) {
            ::kill(rec.pid, SIGTERM);
            waitpid(rec.pid, nullptr, 0);
        }
    }
}

// ── launch ───────────────────────────────────────────────────────────────────

SupervisorError ProcessSupervisor::launch(const ProcessProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    return launchLocked(profile);
}

// 이미 mutex를 잡은 상태에서 호출 (watchdog 내부 재시작용)
SupervisorError ProcessSupervisor::launchLocked(const ProcessProfile& profile) {
    auto it = registry_.find(profile.process_id);
    if (it != registry_.end()) {
        const pid_t pid = it->second.pid;
        if (pid > 0 && ::kill(pid, 0) == 0) {
            std::cerr << "[Supervisor] 이미 실행 중: " << profile.process_id << "\n";
            return SupervisorError::AlreadyRunning;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[Supervisor] fork 실패: " << profile.process_id << "\n";
        return SupervisorError::ForkFailed;
    }

    if (pid == 0) {
        // 자식 stdout/stderr을 로그 파일로 리다이렉트
        std::string log_path = profile.process_id + ".log";
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        // 활성화된 피처 ID를 argv로 전달: binary feature1 feature2 ...
        std::vector<const char*> argv;
        argv.push_back(profile.binary_path.c_str());
        for (const auto& f : profile.features) {
            if (f.flag) argv.push_back(f.feature_id.c_str());
        }
        argv.push_back(nullptr);

        execvp(profile.binary_path.c_str(), const_cast<char**>(argv.data()));
        _exit(EXIT_FAILURE);
    }

    // 부모 프로세스
    if (it != registry_.end()) {
        it->second.pid   = pid;                    // 재시작 시 pid만 갱신
        it->second.state = ProcessState::Running;  // 상태 초기화
    } else {
        registry_[profile.process_id] = ProcessRecord{ pid, profile, 0, ProcessState::Running };
    }
    std::cout << "[Supervisor] 실행: " << profile.process_id
              << " (pid=" << pid << ")\n";

    // 활성 피처 목록 출력
    std::cout << "  활성 피처: ";
    for (const auto& f : profile.features) {
        if (f.flag) std::cout << f.feature_id << " ";
    }
    std::cout << "\n";

    return SupervisorError::Ok;
}

// ── gracefulKill ─────────────────────────────────────────────────────────────
// 1단계: SIGTERM → 앱이 스스로 cleanup 후 종료
// 2단계: timeout 초과 시 hardKill (SIGKILL)

SupervisorError ProcessSupervisor::gracefulKill(const std::string& process_id, int timeout_ms) {
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(process_id);
        if (it == registry_.end()) return SupervisorError::NotFound;

        auto& rec = it->second;
        if (rec.pid <= 0 || ::kill(rec.pid, 0) != 0) {
            registry_.erase(it);
            return SupervisorError::NotRunning;
        }
        rec.state = ProcessState::Stopping; // watchdog가 재시작 시도하지 않음
        pid = rec.pid;
    }

    std::cout << "[Supervisor] SIGTERM → " << process_id << " (pid=" << pid << ")\n";
    ::kill(pid, SIGTERM);

    // 앱이 SIGTERM 핸들러에서 cleanup 후 스스로 종료하기를 기다림
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        if (waitpid(pid, &status, WNOHANG) > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            registry_.erase(process_id);
            std::cout << "[Supervisor] 정상 종료: " << process_id << "\n";
            return SupervisorError::Ok;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cerr << "[Supervisor] timeout → SIGKILL: " << process_id << "\n";
    return hardKill(process_id);
}

// ── hardKill ─────────────────────────────────────────────────────────────────

SupervisorError ProcessSupervisor::hardKill(const std::string& process_id) {
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(process_id);
        if (it == registry_.end()) return SupervisorError::NotFound;
        it->second.state = ProcessState::Disabled;
        pid = it->second.pid;
    }

    ::kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_.erase(process_id);
    }
    std::cout << "[Supervisor] SIGKILL 완료: " << process_id << "\n";
    return SupervisorError::Ok;
}

// ── isAlive / listAll ─────────────────────────────────────────────────────────

bool ProcessSupervisor::isAlive(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(process_id);
    if (it == registry_.end()) return false;
    return it->second.pid > 0 && ::kill(it->second.pid, 0) == 0;
}

void ProcessSupervisor::listAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registry_.empty()) {
        std::cout << "[Supervisor] 실행 중인 프로세스 없음\n";
        return;
    }
    std::cout << "[Supervisor] 실행 목록:\n";
    for (const auto& [id, rec] : registry_) {
        bool alive = rec.pid > 0 && ::kill(rec.pid, 0) == 0;

        const char* state_str = "Running";
        if (rec.state == ProcessState::Stopping) state_str = "Stopping";
        if (rec.state == ProcessState::Disabled)  state_str = "Disabled";

        std::cout << "  [" << state_str << "] " << id
                  << "  pid=" << rec.pid
                  << "  " << (alive ? "alive" : "dead")
                  << "  retries=" << rec.retry_count
                  << "/" << rec.profile.restart_policy.max_retries
                  << "\n";

        // 피처 목록 출력
        std::cout << "    features: ";
        for (const auto& f : rec.profile.features) {
            std::cout << f.feature_id << "=" << (f.flag ? "ON" : "OFF") << " ";
        }
        std::cout << "\n";
    }
}

// ── watchdogLoop ─────────────────────────────────────────────────────────────
// 500ms마다 각 자식 프로세스를 확인하고, 죽어있으면 restart_policy에 따라 재시작.
// Running 상태인 프로세스만 재시작 대상.

void ProcessSupervisor::watchdogLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::vector<std::pair<ProcessProfile, int>> toRestart; // {profile, delay_ms}

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, rec] : registry_) {
                // Running 상태가 아니면 watchdog 관리 대상에서 제외
                if (rec.pid <= 0 || rec.state != ProcessState::Running) continue;

                int status;
                if (waitpid(rec.pid, &status, WNOHANG) <= 0) continue;

                // 자식 프로세스 종료 감지
                std::cout << "[Watchdog] " << id
                          << " 종료 감지 (pid=" << rec.pid << ")\n";
                rec.pid = -1;

                const auto& policy = rec.profile.restart_policy;
                if (rec.retry_count < policy.max_retries) {
                    rec.retry_count++;
                    std::cout << "[Watchdog] " << id << " 재시작 예약 ("
                              << rec.retry_count << "/" << policy.max_retries << ")\n";
                    toRestart.push_back({ rec.profile, policy.retry_delay_ms });
                } else {
                    std::cerr << "[Watchdog] " << id << " 최대 재시도 초과";
                    if (policy.action_on_failure == "DISABLE_FLAG") {
                        rec.state = ProcessState::Disabled;
                        std::cerr << " → Disabled";
                    }
                    std::cerr << "\n";
                }
            }
        }

        // 락 밖에서 delay 후 재시작 (락을 장시간 잡지 않도록)
        for (const auto& [profile, delay_ms] : toRestart) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            launch(profile); // 내부에서 mutex 획득
        }
    }
}
