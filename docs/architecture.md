# Architecture

## 전체 컴포넌트 구조 (현재)

```
┌─────────────────────────────────────────────────────────────┐
│                      Platform (main)                        │
│                                                             │
│  manifest.json ──► ManifestLoader ──► ProcessProfile        │
│                                            │                │
│                                     ProcessSupervisor       │
│                                     ├── registry_           │
│                                     │   (ProcessState 기반) │
│                                     └── watchdog 스레드     │◄── 500ms 주기
└─────────────────────────────────────────────────────────────┘
         │ fork/execvp + argv(활성 피처 목록)
         ├──────────────────────┐
         ▼                      ▼
  [adas 프로세스]        [navigation 프로세스]
  argv: collision_avoidance   argv: route_guidance
        blind_spot_detection
  SIGTERM 핸들러              SIGTERM 핸들러
```

---

## 전체 컴포넌트 구조 (Phase 2 - SHM 연동 후)

```
┌─────────────────────────────────────────────────────────────┐
│                      Platform (main)                        │
│                                                             │
│  manifest.json ──► ManifestLoader ──► ProcessProfile        │
│                                            │                │
│                                     ProcessSupervisor       │
│                                     ├── registry_           │
│                                     ├── ShmManager          │◄── SHM 생성/관리
│                                     └── watchdog 스레드     │
└─────────────────────────────────────────────────────────────┘
         │ fork/execvp                  │ SHM write (setFeatureFlag)
         ├─────────────────┐            │
         ▼                 ▼            ▼
  [adas 프로세스]   [navigation 프로세스]
  SHM 읽기          SHM 읽기
  ├ collision_avoidance: is_enabled, is_killed
  └ blind_spot_detection: is_enabled, is_killed

  ※ 프로세스를 죽이지 않고 피처 단위 ON/OFF 가능
```

---

## 핵심 구조체

### ProcessProfile (FeatureProfile.h)
```
ProcessProfile
├── process_id       : 프로세스 식별자 (ex. "navigation")
├── binary_path      : 실행 바이너리 경로
├── restart_policy   : max_retries, retry_delay_ms, action_on_failure
└── features[]       : FeatureFlag 배열
        └── FeatureFlag
            ├── feature_id : 피처 식별자 (ex. "route_guidance")
            └── flag       : 활성 여부 (부팅 시 기준)
```

### ProcessState (ProcessSupervisor.h)
```
Running   → 정상 실행 중, watchdog 재시작 대상
Stopping  → gracefulKill 요청 후 SIGTERM 대기 중 (watchdog 재시작 안 함)
Disabled  → max_retries 초과 또는 kill 완료 (재시작 안 함)
```

---

## 1. 부트 시퀀스

```
main()
  │
  ├─ ManifestLoader::load("manifest.json")
  │     ├─ JSON 파싱
  │     ├─ 각 항목 → ProcessProfile 변환
  │     │     ├─ process_id, binary_path, restart_policy
  │     │     └─ features[] 파싱 (feature_id + flag)
  │     └─ optional<vector<ProcessProfile>> 반환
  │
  ├─ [로드 실패 시] → EXIT_FAILURE
  │
  ├─ ProcessSupervisor 생성
  │     └─ watchdog 스레드 시작 (백그라운드, 500ms 주기)
  │
  ├─ 각 ProcessProfile 순회
  │     ├─ hasAnyEnabledFeature() == true  → supervisor.launch(profile)
  │     │     └─ 활성 피처 ID를 argv로 전달: ./binary feature1 feature2 ...
  │     └─ hasAnyEnabledFeature() == false → skip 로그 출력
  │
  └─ CLI 루프 진입 (stdin 대기)
```

---

## 2. 시퀀스 다이어그램

### 2-1. 부트 / 프로세스 실행 (launch)

```mermaid
sequenceDiagram
    participant M  as main()
    participant ML as ManifestLoader
    participant PS as ProcessSupervisor
    participant OS as OS (fork/exec)
    participant AP as App (adas 등)

    M->>ML: load("manifest.json")
    ML-->>M: vector<ProcessProfile>

    M->>PS: new ProcessSupervisor()
    PS->>PS: watchdog 스레드 시작

    loop 각 ProcessProfile (hasAnyEnabledFeature=true)
        M->>PS: launch(profile)
        PS->>OS: fork()
        OS-->>PS: pid (부모)
        OS-->>AP: pid=0 (자식)
        AP->>OS: execvp(binary_path, [feature1, feature2, ...])
        Note over AP: 앱 프로세스 실행<br/>argv로 활성 피처 목록 수신
        PS->>PS: registry_[process_id] = {pid, state=Running}
        PS-->>M: SupervisorError::Ok
    end

    M->>M: CLI 루프 진입
```

---

### 2-2. gracefulKill (정상 종료)

> 핵심: SIGTERM 발송 후 state = **Stopping**으로 전환 → watchdog 재시작 차단.
> 앱이 SIGTERM 핸들러에서 cleanup 후 스스로 exit. timeout 초과 시 SIGKILL.

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant M  as main()
    participant PS as ProcessSupervisor
    participant AP as App (adas 등)

    U->>M: "kill adas" 입력
    M->>PS: gracefulKill("adas")

    PS->>PS: registry에서 pid 조회
    PS->>PS: state = Stopping (watchdog 재시작 방지)
    PS->>AP: SIGTERM 전송

    Note over AP: onSignal() 호출됨
    AP->>AP: g_running = 0
    AP->>AP: while 루프 탈출 → cleanup → exit(0)

    PS->>PS: waitpid(WNOHANG) 폴링 (50ms 간격)
    PS->>PS: registry_.erase("adas")
    PS-->>M: SupervisorError::Ok
    M-->>U: "[Supervisor] 정상 종료: adas"

    Note over PS: timeout(2000ms) 초과 시 → hardKill 호출
```

---

### 2-3. hardKill (강제 종료)

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant M  as main()
    participant PS as ProcessSupervisor
    participant AP as App

    U->>M: "hkill adas" 입력
    M->>PS: hardKill("adas")

    PS->>PS: state = Disabled
    PS->>AP: SIGKILL 전송
    Note over AP: 즉시 강제 종료 (cleanup 없음)

    PS->>PS: waitpid() 블로킹 대기
    PS->>PS: registry_.erase("adas")
    PS-->>M: SupervisorError::Ok
    M-->>U: "[Supervisor] SIGKILL 완료: adas"
```

---

### 2-4. Watchdog - 자동 재시작 (+ retry_count 초기화)

> **retry_count 초기화 규칙**: 프로세스가 30초 이상 안정적으로 실행된 뒤 crash하면
> "일시적 오류"로 보고 retry_count를 0으로 초기화. 영구 Disabled 방지.

```mermaid
sequenceDiagram
    participant WD as Watchdog 스레드
    participant PS as ProcessSupervisor
    participant AP as App (adas 등)
    participant OS as OS

    loop 500ms마다
        WD->>PS: registry_ 순회 (state == Running 만 대상)
        PS->>OS: waitpid(pid, WNOHANG)

        alt 앱이 살아있음
            OS-->>WD: 0 반환 → skip
        else 앱이 crash / 비정상 종료
            OS-->>WD: pid 반환
            WD->>WD: 가동 시간 계산<br/>(now - last_started_at)

            alt 가동 시간 >= 30초 (안정적으로 실행됐음)
                WD->>WD: retry_count = 0 (초기화)
            end

            WD->>WD: retry_count 확인

            alt retry_count < max_retries
                WD->>WD: retry_count++
                WD->>WD: retry_delay_ms 대기
                WD->>PS: launch(profile)
                PS->>OS: fork() + execvp()
                OS-->>AP: 앱 재시작
            else retry_count >= max_retries
                WD->>WD: state = Disabled
                WD->>WD: DISABLE_FLAG 로그 출력
            end
        end
    end
```

---

### 2-5. 종료 시 소멸자 처리

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant M  as main()
    participant PS as ProcessSupervisor
    participant WD as Watchdog 스레드
    participant AP as App들

    U->>M: "quit" 입력
    M->>M: CLI 루프 탈출
    M->>PS: ~ProcessSupervisor() 호출

    PS->>WD: running_ = false
    WD->>WD: 루프 탈출
    PS->>PS: watchdog_thread_.join()

    loop registry_의 모든 프로세스
        PS->>AP: SIGTERM 전송
        PS->>PS: waitpid() 대기
    end

    PS-->>M: 소멸 완료
    M->>M: EXIT_SUCCESS
```

---

### 2-6. [Phase 2] SHM 기반 피처 단위 런타임 제어

> 프로세스를 재시작하지 않고 개별 피처를 ON/OFF.
> Supervisor가 SHM에 쓰고, 앱이 자신의 루프에서 읽는다.

```mermaid
sequenceDiagram
    participant U   as User (CLI)
    participant M   as main()
    participant PS  as ProcessSupervisor
    participant SHM as Shared Memory
    participant AP  as App (navigation 등)

    Note over AP: 부팅 시 SHM 연결 후 루프 실행 중

    U->>M: "set navigation map_rendering on" 입력
    M->>PS: setFeatureFlag("navigation", "map_rendering", true)
    PS->>SHM: shm["map_rendering"].is_enabled = true

    Note over AP: 다음 루프 주기(100ms)에 SHM 체크
    AP->>SHM: is_enabled 읽기
    SHM-->>AP: true
    AP->>AP: map_rendering 로직 실행 시작

    U->>M: "set navigation map_rendering off" 입력
    M->>PS: setFeatureFlag("navigation", "map_rendering", false)
    PS->>SHM: shm["map_rendering"].is_enabled = false

    Note over AP: 다음 루프 주기에 SHM 체크
    AP->>SHM: is_enabled 읽기
    SHM-->>AP: false
    AP->>AP: map_rendering 로직 중단 (프로세스는 유지)
```

---

## 3. 프로세스 상태 전이

```
         launch()
  [없음] ─────────► [Running]
                        │
            gracefulKill()│           앱 crash 감지 (watchdog)
                          │           ┌─────────────────────────┐
                    ┌─────▼─────┐     │  가동 >= 30s → retry_count = 0
                    │ Stopping  │     │  retry_count < max_retries
                    │ (SIGTERM) │     │       → retry_count++
                    └─────┬─────┘     │       → retry_delay_ms 후 launch()
                          │           │       → [Running]
                   waitpid()          │
                   성공 or            │  retry_count >= max_retries
                   timeout→SIGKILL    │       → state = Disabled
                          │           └─────────────────────────┘
                          ▼
                     [Disabled]
                (재시작 없음, registry 유지)
```

---

## 4. SHM 레이아웃 (Phase 2)

```
/dev/shm/platform_<feature_id>  (POSIX SHM, 피처별 1개)

struct FeatureControlState {
    atomic<bool>     is_enabled;   // 피처 활성 여부 (Feature Flag)
    atomic<bool>     is_killed;    // 비상 정지 (Kill Switch)
    atomic<uint64_t> heartbeat;    // 앱이 주기적으로 증가시킴 (생존 확인)
};

우선순위: is_killed > is_enabled
is_killed == true  → 즉시 해당 피처 중단 (is_enabled 무시)
is_enabled == false → 해당 피처 로직 실행 안 함
```
