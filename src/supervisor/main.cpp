#include <iostream>
#include <string>
#include "ManifestLoader.h"
#include "ProcessSupervisor.h"

int main() {
    auto profiles = ManifestLoader::load("manifest.json");
    if (!profiles) {
        std::cerr << "[Platform] manifest 로드 실패. 부팅 중단\n";
        return EXIT_FAILURE;
    }

    ProcessSupervisor supervisor;
    for (const auto& p : *profiles) {
        supervisor.registerProfile(p);  // 항상 등록 → set 명령어로 자동 기동 가능
        if (p.hasAnyEnabledFeature()) {
            supervisor.launch(p);
        } else {
            std::cout << "[Platform] 대기: " << p.process_id << " (모든 피처 OFF, 피처 ON 시 자동 기동)\n";
        }
    }

    std::cout << "\n명령어: kill <id> | hkill <id> | list | set <id> <feature> on|off | quit\n";
    std::string cmd;
    while (std::cin >> cmd) {
        if (cmd == "quit") {
            break;
        } else if (cmd == "list") {
            supervisor.listAll();
        } else if (cmd == "kill") {
            std::string id; std::cin >> id;
            supervisor.gracefulKill(id);
        } else if (cmd == "hkill") {
            std::string id; std::cin >> id;
            supervisor.hardKill(id);
        } else if (cmd == "set") {
            std::string proc_id, feat_id, state_str;
            std::cin >> proc_id >> feat_id >> state_str;
            supervisor.setFeatureFlag(proc_id, feat_id, state_str == "on");
        } else {
            std::cout << "알 수 없는 명령어\n";
        }
    }

    return EXIT_SUCCESS;
}
