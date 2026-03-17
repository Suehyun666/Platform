#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void onSignal(int) {
    g_running = 0;
}

int main() {
    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);

    std::cout << "[Navigation] 시작 (pid=" << getpid() << ")\n";
    while (g_running) {
        std::cout << "[Navigation] 동작 중...\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "[Navigation] SIGTERM 수신 → 정상 종료\n";
    return 0;
}
