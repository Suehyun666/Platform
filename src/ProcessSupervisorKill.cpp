#include "ProcessSupervisor.h"
#include <iostream>
#include <csignal>
#include <sys/wait.h>
#include <chrono>
#include <thread>

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
        rec.state = ProcessState::Stopping;
        pid = rec.pid;
    }

    std::cout << "[Supervisor] SIGTERM → " << process_id << " (pid=" << pid << ")\n";
    ::kill(pid, SIGTERM);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(pid, nullptr, WNOHANG) > 0) {
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

SupervisorError ProcessSupervisor::hardKill(const std::string& process_id) {
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(process_id);
        if (it == registry_.end()) return SupervisorError::NotFound;
        it->second.state = ProcessState::Disabled;
        pid = it->second.pid;

        // SHM kill switch → 앱이 onStop() 후 자발적 종료 기회
        auto p_it = all_profiles_.find(process_id);
        if (p_it != all_profiles_.end()) {
            for (const auto& f : p_it->second.features) {
                auto s_it = shm_slots_.find(f.feature_id);
                if (s_it != shm_slots_.end()) s_it->second->setKilled(true);
            }
        }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(pid, nullptr, WNOHANG) > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            registry_.erase(process_id);
            std::cout << "[Supervisor] Kill Switch 종료: " << process_id << "\n";
            return SupervisorError::Ok;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
