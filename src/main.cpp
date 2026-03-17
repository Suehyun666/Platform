#include <iostream>
#include <string>
#include "ManifestLoader.h"
#include "ProcessSupervisor.h"

int main() {
    // 1. manifest 로드
    auto profiles = ManifestLoader::load("manifest.json");
    if (!profiles) {
        std::cerr << "[Platform] manifest 로드 실패. 부팅 중단\n";
        return EXIT_FAILURE;
    }

    // 2. 활성 피처가 하나라도 있는 프로세스만 실행
    ProcessSupervisor supervisor;
    for (const auto& p : *profiles) {
        if (p.hasAnyEnabledFeature()) {
            supervisor.launch(p);
        } else {
            std::cout << "[Platform] skip: " << p.process_id << " (모든 피처 OFF)\n";
        }
    }

    // 3. CLI 루프
    std::cout << "\n명령어: kill <id> | hkill <id> | list | quit\n";
    std::string cmd, id;
    while (std::cin >> cmd) {
        if (cmd == "quit") {
            break;
        } else if (cmd == "list") {
            supervisor.listAll();
        } else if (cmd == "kill") {
            std::cin >> id;
            supervisor.gracefulKill(id);
        } else if (cmd == "hkill") {
            std::cin >> id;
            supervisor.hardKill(id);
        } else {
            std::cout << "알 수 없는 명령어. kill <id> | hkill <id> | list | quit\n";
        }
    }

    return EXIT_SUCCESS;
}
