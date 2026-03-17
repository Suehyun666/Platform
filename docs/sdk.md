# SDK 라이브러리 사용 가이드

`sdv_sdk`는 SDV 앱 개발자가 SHM 연결, 시그널 처리, 메인 루프를 직접 구현하지 않고 비즈니스 로직에만 집중하도록 제공되는 정적 라이브러리입니다.

---

## 라이브러리 구성

```
src/sdk/
├── SdvAppFrame.h/cpp   앱 베이스 클래스 (상속해서 사용)
└── ShmManager.h/cpp    SHM 연결 유틸리티 (SDK 내부 사용, 앱이 직접 호출 불필요)
```

---

## 빠른 시작

### 1. SdvAppFrame 상속

```cpp
#include "SdvAppFrame.h"
#include <iostream>

class MyApp : public SdvAppFrame {
public:
    // 앱 기본 루프 주기 (ms). manifest의 loop_interval_ms가 있으면 override됨.
    MyApp() : SdvAppFrame(100) {}

    void onStart(const std::vector<std::string>& features) override {
        // 초기화: 카메라 오픈, 메모리 할당 등. 딱 1회 호출.
        std::cout << "[MyApp] 시작: " << features.size() << "개 피처\n";
    }

    void onUpdate(const std::string& feature_id) override {
        // 매 루프 주기마다, 피처가 enabled일 때만 호출.
        std::cout << "[MyApp/" << feature_id << "] 연산 중\n";
    }

    void onStop() override {
        // 종료 시: 리소스 해제, 하드웨어 안전 상태 전환.
        // SIGTERM 또는 Kill Switch 감지 시 호출.
        std::cout << "[MyApp] 정상 종료\n";
    }
};

int main(int argc, char* argv[]) {
    MyApp app;
    return app.run(argc, argv);  // SDK가 나머지를 처리
}
```

### 2. CMakeLists.txt

```cmake
add_executable(my_app application/my_app.cpp)
target_link_libraries(my_app PRIVATE sdv_sdk)
set_target_properties(my_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})
```

### 3. manifest.json 등록

```json
{
  "process_id": "my_app",
  "binary_path": "application/bin/my_app",
  "loop_interval_ms": 50,
  "restart_policy": {
    "max_retries": 3,
    "retry_delay_ms": 1000,
    "action_on_failure": "DISABLE_FLAG"
  },
  "features": [
    { "feature_id": "feature_a", "flag": true  },
    { "feature_id": "feature_b", "flag": false }
  ]
}
```

---

## 라이프사이클 훅

| 훅 | 호출 시점 | 용도 |
|---|---|---|
| `onStart(features)` | 루프 진입 전 1회 | 리소스 초기화, 통신 채널 오픈 |
| `onUpdate(feature_id)` | 매 루프 주기, 피처가 enabled일 때 | 비즈니스 로직 |
| `onStop()` | SIGTERM 수신 또는 Kill Switch 감지 시 | 리소스 해제, 안전 상태 전환 |

### 루프 내 호출 흐름

```
매 루프 주기:
  for each feature in slots_:
    if is_killed  → killed = true → 루프 즉시 탈출
    if is_enabled → heartbeat++ + onUpdate(feature_id)

  루프 탈출 후:
    onStop() 호출
    SHM munmap
    exit
```

### 자율 종료 조건

모든 피처가 `is_enabled = false`가 되면 앱이 스스로 종료합니다.

```
!any_enabled && !slots_.empty()  →  루프 탈출 → onStop() → exit
```

Watchdog은 이 경우를 `hasAnyEnabledFeature() == false`로 판단하여 **재시작하지 않습니다**.

---

## Loop Interval 설정

루프 주기는 **manifest가 앱 생성자 기본값을 override**합니다.

```
앱 생성자: SdvAppFrame(50)           → loop_interval_ms_ = 50ms (기본)
manifest:  "loop_interval_ms": 200   → Supervisor가 --loop-ms=200 argv로 전달
                                     → loop_interval_ms_ = 200ms (override)
```

| 상황 | 결과 |
|------|------|
| manifest `loop_interval_ms: 200`, 앱 기본값 50 | **200ms** |
| manifest에 없음, 앱 기본값 50 | **50ms** |
| 둘 다 없음 | **100ms** (SdvAppFrame 하드코딩 기본값) |

---

## SHM 연결 방식

앱 개발자는 SHM을 직접 다룰 필요 없습니다.

```
Supervisor (shm_slots_)            /dev/shm/platform_<feature_id>        App (SDK)
───────────────────────            ──────────────────────────────        ──────────
registerProfile() 시:              FeatureControlState {
  ShmManager 생성       ──────►      atomic<bool>     is_enabled    ◄──── run() 에서
  is_enabled 초기화     ──────►      atomic<bool>     is_killed           ShmManager::connect()
                                     atomic<uint64_t> heartbeat           루프마다 읽기
                                   }
set <feature> on/off:   ──────►  is_enabled = true/false
kill/hkill:             ──────►  is_killed = true
재기동 시:              ──────►  is_killed = false (초기화)
```

---

## SHM 연결 재시도

Supervisor가 `registerProfile()`에서 SHM을 먼저 생성한 뒤 `launch()`로 프로세스를 fork하므로 타이밍 이슈가 거의 없습니다. 그러나 SDK는 안전을 위해 50ms 간격으로 최대 10회 재시도합니다 (최대 500ms 대기).

```cpp
for (int r = 0; r < 10 && !ptr; ++r) {
    ptr = ShmManager::connect(feature_id);
    if (!ptr) sleep(50ms);
}
// 연결 실패 시: 해당 피처는 slots_에서 제외, 에러 로그 출력
```
