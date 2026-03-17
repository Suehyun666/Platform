# 아키텍처

---

## 디렉토리 구조

```
Platform/
├── src/
│   ├── supervisor/          ← Platform 프로세스 전용
│   │   ├── main.cpp             진입점, CLI 루프
│   │   ├── ManifestLoader.h/cpp manifest.json 파싱
│   │   ├── FeatureProfile.h     ProcessProfile, FeatureFlag, RestartPolicy 타입
│   │   ├── ProcessRecord.h      런타임 프로세스 상태 (pid, retry_count, state)
│   │   ├── ProcessSupervisor.h/cpp   launch / registerProfile / isAlive / listAll
│   │   ├── ProcessSupervisorKill.cpp gracefulKill / hardKill
│   │   ├── ProcessSupervisorFeature.cpp setFeatureFlag
│   │   └── Watchdog.h/cpp       크래시 감지 및 자동 재시작 스레드
│   └── sdk/                 ← sdv_sdk 정적 라이브러리 (앱이 링크)
│       ├── ShmManager.h/cpp     POSIX SHM 생성·연결 (supervisor·SDK 공유)
│       └── SdvAppFrame.h/cpp    앱 베이스 클래스 (onStart / onUpdate / onStop)
├── application/             ← 앱 바이너리 소스
│   ├── adas.cpp
│   ├── navigation.cpp
│   └── bin/                 빌드 결과물 출력 디렉토리
├── docs/                    문서
├── manifest.json            프로세스·피처 선언
└── CMakeLists.txt
```

---

## 컴포넌트 구조

```
┌──────────────────────────────────────────────────────────────────┐
│                        Platform (Supervisor)                     │
│                                                                  │
│  manifest.json ─► ManifestLoader ─► ProcessProfile              │
│                                           │                      │
│                                    ProcessSupervisor             │
│                                    ├── all_profiles_  (등록 전체) │
│                                    ├── registry_      (런타임 상태)│
│                                    ├── shm_slots_     (SHM 소유)  │
│                                    └── Watchdog       500ms 주기  │
└──────────────────────────────────────────────────────────────────┘
         │ fork/execvp                  │ SHM write (setFeatureFlag/kill)
         │ argv: feature_ids +          │
         │       --loop-ms=N            │
         ▼                              ▼
  [adas 프로세스]              /dev/shm/platform_<feature_id>
  sdv_sdk 링크                 FeatureControlState {
  SdvAppFrame::run()             atomic<bool> is_enabled
  ├── ShmManager::connect()      atomic<bool> is_killed
  └── 루프: SHM 읽기            atomic<uint64_t> heartbeat
                               }
```

---

## 핵심 타입

### ProcessProfile (`FeatureProfile.h`)

```
ProcessProfile
├── process_id       : "adas", "navigation" 등
├── binary_path      : 실행 바이너리 경로
├── loop_interval_ms : SDK 메인 루프 주기 (manifest → argv로 주입)
├── restart_policy
│   ├── max_retries       : 최대 자동 재시작 횟수
│   ├── retry_delay_ms    : 재시작 전 대기 시간
│   └── action_on_failure : "DISABLE_FLAG" | "RESTART"
└── features[]
        └── FeatureFlag
            ├── feature_id : "collision_avoidance" 등
            └── flag       : 부팅 시 초기 활성 여부
```

### ProcessState (`ProcessRecord.h`)

| 상태 | 의미 | Watchdog 재시작 |
|------|------|----------------|
| `Running` | 정상 실행 중 | 대상 |
| `Stopping` | gracefulKill 요청, SIGTERM 대기 중 | 차단 |
| `Disabled` | kill 완료 또는 max_retries 초과 | 차단 |

### FeatureControlState (`ShmManager.h`)

```
/dev/shm/platform_<feature_id>  (피처 1개당 SHM 1개)

is_killed  > is_enabled  (우선순위)
is_killed  = true  → onStop() 즉시 호출 후 종료
is_enabled = false → 해당 피처 onUpdate() 실행 안 함 (프로세스는 유지)
heartbeat         → onUpdate() 마다 +1 (생존 확인용)
```

---

## 부트 시퀀스

```
main()
  │
  ├─ ManifestLoader::load("manifest.json")
  │     └─ ProcessProfile 배열 반환 (실패 시 EXIT_FAILURE)
  │
  ├─ ProcessSupervisor 생성
  │     └─ Watchdog 스레드 시작 (500ms 주기)
  │
  ├─ 각 ProcessProfile 순회
  │     ├─ registerProfile(p)        ← 항상 등록 (SHM 생성 포함)
  │     └─ hasAnyEnabledFeature()?
  │           true  → launch(p)      ← fork/execvp
  │           false → 대기 로그 출력  (set 명령으로 나중에 기동 가능)
  │
  └─ CLI 루프 (stdin 대기)
```

---

## 시퀀스 다이어그램

### 부트 / launch

```mermaid
sequenceDiagram
    participant M  as main()
    participant ML as ManifestLoader
    participant PS as ProcessSupervisor
    participant OS as OS (fork/exec)
    participant AP as App

    M->>ML: load("manifest.json")
    ML-->>M: vector<ProcessProfile>

    M->>PS: new ProcessSupervisor()
    PS->>PS: Watchdog 스레드 시작

    loop 각 ProcessProfile
        M->>PS: registerProfile(p)
        PS->>PS: all_profiles_ 저장
        PS->>PS: ShmManager 생성 (피처별 SHM)

        opt hasAnyEnabledFeature()
            M->>PS: launch(p)
            PS->>OS: fork()
            OS-->>AP: execvp(binary, [feat1, feat2, --loop-ms=N])
            PS->>PS: registry_[id] = {pid, Running}
        end
    end

    M->>M: CLI 루프 진입
```

---

### setFeatureFlag (런타임 ON/OFF)

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant PS as ProcessSupervisor
    participant SHM as SHM
    participant AP as App

    U->>PS: setFeatureFlag("navigation", "map_rendering", true)
    PS->>PS: all_profiles_ 플래그 업데이트
    PS->>SHM: is_enabled = true
    opt 프로세스가 죽어있으면
        PS->>PS: launchLocked(profile)
    end

    Note over AP: 다음 루프 주기에 SHM 읽기
    AP->>SHM: is_enabled?
    SHM-->>AP: true
    AP->>AP: onUpdate("map_rendering") 실행

    U->>PS: setFeatureFlag("navigation", "map_rendering", false)
    PS->>SHM: is_enabled = false
    Note over AP: 다음 루프 주기부터 onUpdate skip
    Note over AP: 모든 피처 OFF → 자율 종료
    Note over PS: Watchdog이 종료 감지, hasAnyEnabledFeature() = false → 재시작 안 함
```

---

### gracefulKill

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant PS as ProcessSupervisor
    participant AP as App

    U->>PS: gracefulKill("adas")
    PS->>PS: state = Stopping (Watchdog 재시작 차단)
    PS->>AP: SHM is_killed = true  (모든 피처)
    PS->>AP: SIGTERM 전송 (동시)

    Note over AP: is_killed 감지 → onStop() → exit
    PS->>PS: waitpid(WNOHANG) 폴링 50ms 간격
    PS->>PS: registry_.erase("adas")
    PS-->>U: Ok

    Note over PS: timeout 2000ms 초과 시 → hardKill
```

---

### hardKill

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant PS as ProcessSupervisor
    participant AP as App

    U->>PS: hardKill("adas")
    PS->>PS: state = Disabled
    PS->>AP: SHM is_killed = true  (모든 피처)
    Note over AP: 최대 200ms 안에 자발적 종료 기회
    PS->>PS: waitpid(WNOHANG) 폴링 (200ms 타임아웃)

    alt 200ms 내 자발적 종료
        PS->>PS: registry_.erase("adas")
        PS-->>U: Ok (Kill Switch 종료)
    else 200ms 초과
        PS->>AP: SIGKILL
        PS->>PS: waitpid() 블로킹
        PS->>PS: registry_.erase("adas")
        PS-->>U: Ok (SIGKILL 완료)
    end
```

---

### Watchdog 자동 재시작

```mermaid
sequenceDiagram
    participant WD as Watchdog 스레드
    participant PS as ProcessSupervisor
    participant OS as OS

    loop 500ms마다
        WD->>PS: registry_ 순회 (state == Running 만)
        PS->>OS: waitpid(pid, WNOHANG)

        alt 살아있음
            OS-->>WD: 0 → skip
        else 종료 감지
            OS-->>WD: pid 반환
            WD->>WD: 가동시간 >= 30s? → retry_count = 0
            WD->>WD: hasAnyEnabledFeature()?

            alt 모든 피처 OFF (자율 종료)
                WD->>WD: state = Disabled, 재시작 안 함
            else 피처 활성 중
                alt retry_count < max_retries
                    WD->>WD: retry_count++
                    WD->>PS: launchLocked(profile)
                else action_on_failure == "RESTART"
                    WD->>WD: retry_count = 1 (무한 재시도)
                    WD->>PS: launchLocked(profile)
                else DISABLE_FLAG
                    WD->>WD: state = Disabled
                end
            end
        end
    end
```

---

### 프로세스 상태 전이

```
         registerProfile() + launch()
  [없음] ────────────────────────────► [Running]
                                           │
                              gracefulKill()│        Watchdog 크래시 감지
                                           │     ┌──────────────────────────┐
                                     [Stopping]  │  retry < max → 재시작    │
                                           │     │  retry >= max            │
                                    waitpid()    │    DISABLE_FLAG → Disabled│
                                    or SIGKILL   │    RESTART → 무한 재시도  │
                                           │     └──────────────────────────┘
                                           ▼
                                      [Disabled]
                              (registry 유지, 재시작 없음)
```

---

## SHM 수명 관리

SHM은 **프로세스 수명이 아닌 피처 등록 수명**을 따릅니다.

```
registerProfile()  → ShmManager 생성 (SHM /dev/shm/platform_<id> 생성)
launch()           → 프로세스 기동, SHM is_killed 초기화
프로세스 종료      → SHM 유지 (Supervisor가 계속 소유)
프로세스 재기동    → 기존 SHM 재사용 (is_killed = false 초기화)
~ProcessSupervisor → ShmManager 소멸 → shm_unlink
```

이렇게 설계한 이유:
- **BUG-1 방지**: 이전 설계에서는 launchLocked()가 SHM을 재생성할 때 기존 ShmManager가 소멸되며 shm_unlink가 발생 → 자식 프로세스가 shm_open 실패 → SHM 없이 실행 → 자율 종료 조건 미충족 → 프로세스가 영원히 생존하는 버그
- **단순성**: Supervisor가 SHM의 단일 소유자, 앱은 connect()로 읽기·쓰기만
