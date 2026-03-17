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

SupervisorError ProcessSupervisor::launch(const FeatureProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    return launchLocked(profile);
}

// 이미 mutex를 잡은 상태에서 호출 (watchdog 내부 재시작용)
SupervisorError ProcessSupervisor::launchLocked(const FeatureProfile& profile) {
    auto it = registry_.find(profile.feature_id);
    if (it != registry_.end()) {
        const pid_t pid = it->second.pid;
        if (pid > 0 && ::kill(pid, 0) == 0) {
            std::cerr << "[Supervisor] 이미 실행 중: " << profile.feature_id << "\n";
            return SupervisorError::AlreadyRunning;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[Supervisor] fork 실패: " << profile.feature_id << "\n";
        return SupervisorError::ForkFailed;
    }

    if (pid == 0) {
        // 자식 stdout/stderr을 로그 파일로 리다이렉트
        std::string log_path = profile.feature_id + ".log";
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        const char* argv[] = { profile.binary_path.c_str(), nullptr };
        execvp(profile.binary_path.c_str(), const_cast<char**>(argv));
        _exit(EXIT_FAILURE);
    }

    // 부모 프로세스
    if (it != registry_.end()) {
        it->second.pid = pid; // 재시작 시 pid만 갱신
    } else {
        registry_[profile.feature_id] = ProcessRecord{ pid, profile, 0, false };
    }
    std::cout << "[Supervisor] 실행: " << profile.feature_id
              << " (pid=" << pid << ")\n";
    return SupervisorError::Ok;
}

// ── gracefulKill ─────────────────────────────────────────────────────────────
// 설계: supervisor가 SIGTERM을 보내면, 자식 프로세스가 스스로 SIGTERM 핸들러에서
//       cleanup 후 exit. supervisor는 그것을 기다리고, timeout 초과 시 SIGKILL.

SupervisorError ProcessSupervisor::gracefulKill(const std::string& feature_id, int timeout_ms) {
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(feature_id);
        if (it == registry_.end()) return SupervisorError::NotFound;

        auto& rec = it->second;
        if (rec.pid <= 0 || ::kill(rec.pid, 0) != 0) {
            registry_.erase(it);
            return SupervisorError::NotRunning;
        }
        rec.disabled = true; // watchdog가 이 프로세스를 재시작하지 않도록
        pid = rec.pid;
    }

    std::cout << "[Supervisor] SIGTERM → " << feature_id << " (pid=" << pid << ")\n";
    ::kill(pid, SIGTERM);

    // 자식이 SIGTERM 핸들러에서 cleanup 후 스스로 종료하기를 기다림
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        if (waitpid(pid, &status, WNOHANG) > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            registry_.erase(feature_id);
            std::cout << "[Supervisor] 정상 종료: " << feature_id << "\n";
            return SupervisorError::Ok;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cerr << "[Supervisor] timeout → SIGKILL: " << feature_id << "\n";
    return hardKill(feature_id);
}

// ── hardKill ─────────────────────────────────────────────────────────────────

SupervisorError ProcessSupervisor::hardKill(const std::string& feature_id) {
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(feature_id);
        if (it == registry_.end()) return SupervisorError::NotFound;
        it->second.disabled = true;
        pid = it->second.pid;
    }

    ::kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_.erase(feature_id);
    }
    std::cout << "[Supervisor] SIGKILL 완료: " << feature_id << "\n";
    return SupervisorError::Ok;
}

// ── isAlive / listAll ─────────────────────────────────────────────────────────

bool ProcessSupervisor::isAlive(const std::string& feature_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(feature_id);
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
        std::cout << "  " << id
                  << "  pid=" << rec.pid
                  << "  " << (alive ? "alive" : "dead")
                  << (rec.disabled ? "  [disabled]" : "")
                  << "  retries=" << rec.retry_count
                  << "/" << rec.profile.restart_policy.max_retries
                  << "\n";
    }
}

// ── watchdogLoop ─────────────────────────────────────────────────────────────
// 500ms마다 각 자식 프로세스를 확인하고, 죽어있으면 restart_policy에 따라 재시작.

void ProcessSupervisor::watchdogLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::vector<std::pair<FeatureProfile, int>> toRestart; // {profile, delay_ms}

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, rec] : registry_) {
                if (rec.pid <= 0 || rec.disabled) continue;

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
                        rec.disabled = true;
                        std::cerr << " → DISABLE_FLAG";
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
