# SDK 라이브러리 사용 가이드

`sdv_sdk`는 SDV 앱 개발자가 시스템 인프라(SHM 연결, 시그널 처리, 메인 루프)를 직접 구현하지 않고 비즈니스 로직에만 집중할 수 있도록 제공되는 정적 라이브러리입니다.

---

## 라이브러리 구성

```
src/
├── SdvAppFrame.h/.cpp   # SDK 베이스 클래스 (앱이 상속)
└── ShmManager.h/.cpp    # SHM 생성/연결 유틸리티 (SDK 내부 사용)
```

---

## 빠른 시작

### 1. SdvAppFrame 상속

```cpp
#include "SdvAppFrame.h"
#include <iostream>

class MyApp : public SdvAppFrame {
public:
    // 앱 기본 루프 주기 설정 (ms). manifest로 override 가능.
    MyApp() : SdvAppFrame(100) {}

    void onStart(const std::vector<std::string>& features) override {
        // 초기화: 카메라 오픈, 메모리 할당 등
        std::cout << "[MyApp] 시작: " << features.size() << "개 피처\n";
    }

    void onUpdate(const std::string& feature_id) override {
        // 매 루프마다 호출. feature_id별로 로직 분기.
        std::cout << "[MyApp/" << feature_id << "] 연산 중\n";
    }

    void onStop() override {
        // 종료 시: 리소스 해제, 하드웨어 안전 상태 전환
        std::cout << "[MyApp] 정상 종료\n";
    }
};

int main(int argc, char* argv[]) {
    MyApp app;
    return app.run(argc, argv); // SDK가 나머지를 처리
}
```

### 2. CMakeLists.txt 설정

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
    { "feature_id": "feature_a", "flag": true },
    { "feature_id": "feature_b", "flag": false }
  ]
}
```

---

## 라이프사이클 훅

| 훅 | 호출 시점 | 용도 |
|---|---|---|
| `onStart(features)` | 루프 진입 전 1회 | 리소스 초기화, 통신 채널 오픈 |
| `onUpdate(feature_id)` | 매 루프 주기마다, 피처가 enabled일 때 | 비즈니스 로직 |
| `onStop()` | SIGTERM 수신 또는 Kill Switch 감지 시 | 리소스 해제, 안전 상태 전환 |

### onUpdate 호출 조건

```
매 루프 주기마다:
  for each feature:
    if is_killed  → onStop() 호출 후 종료  ← Kill Switch (최우선)
    if is_enabled → onUpdate(feature_id)   ← Feature Flag
    else          → skip
```

---

## Loop Interval 설정

루프 주기는 두 곳에서 설정할 수 있으며 **manifest가 앱 기본값을 override**합니다.

### 앱 생성자에서 기본값 설정

```cpp
class MyApp : public SdvAppFrame {
public:
    MyApp() : SdvAppFrame(50) {}  // 기본값: 50ms
    ...
};
```

### manifest.json으로 override

```json
{ "loop_interval_ms": 200 }
```

Supervisor가 `--loop-ms=200`을 argv로 전달. 앱의 50ms 기본값보다 우선 적용.

### 우선순위 요약

```
manifest loop_interval_ms  ──►  --loop-ms=N (argv)  ──►  loop_interval_ms_ (runtime)
앱 생성자 기본값           ──►  SdvAppFrame(N)       ──►  loop_interval_ms_ (초기값)

최종 적용: manifest 값이 있으면 override, 없으면 앱 기본값 사용
```

| 상황 | 결과 |
|------|------|
| manifest `loop_interval_ms: 200`, 앱 기본값 50 | **200ms** 적용 |
| manifest에 없음, 앱 기본값 50 | **50ms** 적용 |
| 둘 다 없음 | **100ms** (SdvAppFrame 기본값) |

---

## SHM 동작 방식

앱 개발자는 SHM을 직접 다룰 필요 없습니다. SDK가 자동으로 처리합니다.

```
Supervisor                 SHM (/platform_<feature_id>)        App (SDK)
────────────               ────────────────────────────        ──────────
launch() 시:               FeatureControlState {
  ShmManager 생성   ──►      atomic<bool> is_enabled            run() 에서
  is_enabled 초기화 ──►      atomic<bool> is_killed      ◄───   openForRead()
                             atomic<uint64_t> heartbeat         루프마다 읽기
set <feature> on:  ──►      is_enabled = true
set <feature> off: ──►      is_enabled = false
kill <process>:    ──►      is_killed = true (예정)
```

- `is_killed` 감지 시 → `onStop()` 즉시 호출 후 프로세스 정상 종료
- `is_enabled = false` 시 → 해당 피처의 `onUpdate` 호출 skip (프로세스 유지)
- `heartbeat` → 앱이 `onUpdate` 호출마다 1 증가 (생존 확인용)

---

## 참고: SHM 연결 타임아웃

Supervisor가 SHM을 생성한 직후 fork하므로 타이밍 이슈가 없지만, SDK는 50ms 간격으로 최대 10회 재시도합니다 (최대 500ms 대기).

```cpp
// SdvAppFrame.cpp 내부
for (int r = 0; r < kShmRetries && !ptr; ++r) {
    ptr = ShmManager::openForRead(feature_id);
    if (!ptr) sleep(50ms);
}
```
