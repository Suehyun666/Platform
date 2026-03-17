# 빌드 및 실행 가이드

## 요구사항

- Linux 환경 (또는 WSL)
- CMake 3.14+
- g++ (C++20 지원)
- 인터넷 연결 (nlohmann/json FetchContent 최초 다운로드 시)

---

## WSL 설정 (Windows)

### 1. WSL + Ubuntu 설치

PowerShell (관리자 권한):
```powershell
wsl --install -d Ubuntu
```
재시작 후 Ubuntu 창에서 username/password 설정.

### 2. 빌드 도구 설치

```bash
sudo apt update && sudo apt install -y cmake g++ build-essential
```

### 3. CLion WSL 툴체인 설정

```
File → Settings → Build, Execution, Deployment → Toolchains
  → + → WSL → Distribution: Ubuntu
File → Settings → Build, Execution, Deployment → CMake
  → Debug 프로필의 Toolchain을 WSL로 변경
```

---

## 빌드

```bash
cd /path/to/Platform

cmake -S . -B build
cmake --build build
```

빌드 결과물:
```
build/Platform              ← Supervisor 실행파일
application/bin/adas        ← adas 앱
application/bin/navigation  ← navigation 앱
```

---

## 실행

반드시 프로젝트 루트에서 실행 (manifest.json과 binary_path 상대경로 기준):

```bash
./build/Platform
```

**정상 시작 로그:**
```
[SHM] 생성: /platform_collision_avoidance
[SHM] 생성: /platform_blind_spot_detection
[SHM] 생성: /platform_route_guidance
[SHM] 생성: /platform_map_rendering
[Supervisor] 실행: adas (pid=1234)
  활성 피처: collision_avoidance blind_spot_detection
[Supervisor] 실행: navigation (pid=1235)
  활성 피처: route_guidance
[Platform] 대기: navigation (모든 피처 OFF, 피처 ON 시 자동 기동)

명령어: kill <id> | hkill <id> | list | set <id> <feature> on|off | quit
```

---

## manifest.json 구조

```json
[
  {
    "process_id": "navigation",
    "binary_path": "application/bin/navigation",
    "loop_interval_ms": 500,
    "restart_policy": {
      "max_retries": 3,
      "retry_delay_ms": 1000,
      "action_on_failure": "DISABLE_FLAG"
    },
    "features": [
      { "feature_id": "route_guidance", "flag": true  },
      { "feature_id": "map_rendering",  "flag": false }
    ]
  }
]
```

| 필드 | 설명 |
|------|------|
| `process_id` | CLI 명령어의 `<id>`. 반드시 유일해야 함 |
| `binary_path` | 실행 바이너리 경로 (프로젝트 루트 기준 상대경로) |
| `loop_interval_ms` | SDK 메인 루프 주기 (ms). 생략 시 앱 생성자 기본값 사용 |
| `features[].flag` | `true`인 피처가 하나라도 있으면 부팅 시 프로세스 기동 |
| `max_retries` | Watchdog 자동 재시작 최대 횟수 |
| `retry_delay_ms` | 재시작 전 대기 시간 (ms) |
| `action_on_failure` | `max_retries` 초과 시 동작: `DISABLE_FLAG`(기본) 또는 `RESTART`(무한 재시도) |

---

## Watchdog 재시작 정책

| `action_on_failure` | 동작 |
|---------------------|------|
| `DISABLE_FLAG` | max_retries 초과 시 `state = Disabled`, 재시작 없음 |
| `RESTART` | max_retries 초과해도 `retry_count = 1` 리셋 후 무한 재시도 (크리티컬 프로세스용) |

**retry_count 초기화 규칙:** 프로세스가 30초 이상 안정적으로 실행된 뒤 crash하면 retry_count를 0으로 초기화합니다. "일시적 오류"로 인해 영구 Disabled 되는 상황을 방지합니다.

---

## 새 앱 추가하는 법

1. `application/my_app.cpp` 작성 (`SdvAppFrame` 상속)
2. `CMakeLists.txt`에 추가:
   ```cmake
   add_executable(my_app application/my_app.cpp)
   target_link_libraries(my_app PRIVATE sdv_sdk)
   set_target_properties(my_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})
   ```
3. `manifest.json`에 항목 추가
4. 빌드 후 실행
