# 빌드 및 실행 가이드

## 요구사항

- Linux 환경 (또는 WSL)
- CMake 3.14+
- g++ (C++20 지원)
- 인터넷 연결 (nlohmann/json FetchContent 다운로드)

---

## WSL 설정 (Windows)

### 1. WSL + Ubuntu 설치

PowerShell (관리자 권한):
```powershell
wsl --install -d Ubuntu
```
재시작 후 Ubuntu 창에서 username/password 설정.

### 2. 빌드 도구 설치

Ubuntu 터미널:
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
cd /home/suehyun/Platform

cmake -S . -B build && cmake --build build

# application/bin/ 에 adas, navigation 생성 확인
ls application/bin/
```

---

## 실행

```bash
./build/Platform
```

**정상 시작 로그:**
```
[Supervisor] 실행: adas (pid=1234)
  활성 피처: collision_avoidance blind_spot_detection
[Supervisor] 실행: navigation (pid=1235)
  활성 피처: route_guidance
[Platform] skip: navigation (모든 피처 OFF)  ← 피처가 모두 OFF인 경우

명령어: kill <id> | hkill <id> | list | quit
```

> `manifest.json`의 `binary_path`는 실행 위치 기준 상대경로입니다.
> 반드시 프로젝트 루트(`Platform/`)에서 실행하세요.

---

## manifest.json 구조

프로세스 단위로 정의하고, 각 프로세스가 소유할 피처를 `features` 배열로 선언합니다.

```json
{
  "process_id": "navigation",
  "binary_path": "application/bin/navigation",
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
```

| 필드 | 설명 |
|------|------|
| `process_id` | 프로세스 식별자. CLI 명령어에서 `<id>`로 사용 |
| `binary_path` | 실행할 바이너리 경로 (프로젝트 루트 기준 상대경로) |
| `features[].flag` | `true`인 피처가 하나라도 있으면 프로세스 실행 |
| `max_retries` | watchdog 자동 재시작 최대 횟수 |
| `retry_delay_ms` | 재시작 전 대기 시간 (ms) |
| `action_on_failure` | `max_retries` 초과 시 동작. 현재: `DISABLE_FLAG` |

### 피처 ON/OFF

`features[].flag`를 수정하고 Platform을 재시작하면 반영됩니다.

```json
{ "feature_id": "map_rendering", "flag": true }
```

> Phase 2 (SHM 연동) 후에는 재시작 없이 `set navigation map_rendering on` 명령으로 런타임 변경 가능.

---

## retry_count 초기화 동작 (watchdog)

프로세스가 **30초 이상 안정적으로 실행**된 뒤 crash하면 watchdog이 `retry_count`를 0으로 초기화합니다.

```
crash 발생
  └─ 가동 시간 >= 30s? → retry_count = 0 (초기화 후 재시작)
  └─ 가동 시간 < 30s?  → retry_count++ (누적)
       └─ max_retries 초과 → state = Disabled
```

이를 통해 "가끔 발생하는 일시적 오류"로 프로세스가 영구 Disabled 되는 상황을 방지합니다.
