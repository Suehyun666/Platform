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
cd /mnt/c/Users/psj03/Desktop/Platform

# 앱 바이너리 먼저 빌드 (Platform이 실행할 대상)
cmake -B build && cmake --build build

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
[Platform] skip: navigation (flag=OFF)

명령어: kill <id> | hkill <id> | list | quit
```

> manifest.json의 `binary_path`는 실행 위치 기준 상대경로입니다.
> 반드시 프로젝트 루트(`Platform/`)에서 실행하세요.

---

## manifest.json 수정으로 피처 ON/OFF

```json
{
  "feature_id": "navigation",
  "binary_path": "application/bin/navigation",
  "flag": true,          ← false → true 로 변경하면 부팅 시 실행됨
  "restart_policy": {
    "max_retries": 3,
    "retry_delay_ms": 1000,
    "action_on_failure": "DISABLE_FLAG"
  }
}
```
