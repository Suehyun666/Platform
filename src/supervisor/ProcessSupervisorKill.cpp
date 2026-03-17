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

        setAllShmKilled(process_id, true);  // SHM kill switch + SIGTERM 동시 전달
    }
    std::cout << "[Supervisor] SIGTERM → " << process_id << " (pid=" << pid << ")\n";
    ::kill(pid, SIGTERM);

    if (waitForExit(pid, timeout_ms)) {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_.erase(process_id);
        std::cout << "[Supervisor] 정상 종료: " << process_id << "\n";
        return SupervisorError::Ok;
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

        setAllShmKilled(process_id, true);  // SHM kill switch → 앱이 onStop() 후 자발적 종료 기회
    }

    if (waitForExit(pid, 200)) {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_.erase(process_id);
        std::cout << "[Supervisor] Kill Switch 종료: " << process_id << "\n";
        return SupervisorError::Ok;
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
