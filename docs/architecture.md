# Architecture - 부트 순서 및 시퀀스 다이어그램

## 전체 컴포넌트 구조

```
┌─────────────────────────────────────────────────────┐
│                    Platform (main)                  │
│                                                     │
│  manifest.json ──► ManifestLoader ──► FeatureProfile│
│                                           │         │
│                                    ProcessSupervisor│
│                                    ├── registry_    │
│                                    └── watchdog     │◄─── 백그라운드 스레드
└─────────────────────────────────────────────────────┘
         │ fork/execvp                │ fork/execvp
         ▼                           ▼
    [adas 프로세스]           [navigation 프로세스]
    SIGTERM 핸들러             SIGTERM 핸들러
```

---

## 1. 부트 시퀀스

```
main()
  │
  ├─ ManifestLoader::load("manifest.json")
  │     ├─ JSON 파일 열기 / 파싱
  │     ├─ 각 항목 → FeatureProfile 변환
  │     │     └─ 파싱 실패 항목은 skip (fail-safe)
  │     └─ optional<vector<FeatureProfile>> 반환
  │
  ├─ [로드 실패 시] → EXIT_FAILURE
  │
  ├─ ProcessSupervisor 생성
  │     └─ watchdog 스레드 시작 (백그라운드, 500ms 주기)
  │
  ├─ 각 FeatureProfile 순회
  │     ├─ flag == true  → supervisor.launch(profile)
  │     └─ flag == false → skip 로그 출력
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
    ML-->>M: vector<FeatureProfile>

    M->>PS: new ProcessSupervisor()
    PS->>PS: watchdog 스레드 시작

    loop 각 FeatureProfile (flag=true)
        M->>PS: launch(profile)
        PS->>OS: fork()
        OS-->>PS: pid (부모)
        OS-->>AP: pid=0 (자식)
        AP->>OS: execvp(binary_path)
        Note over AP: 앱 프로세스 실행 시작
        PS->>PS: registry_[feature_id] = pid
        PS-->>M: SupervisorError::Ok
    end

    M->>M: CLI 루프 진입
```

---

### 2-2. gracefulKill (정상 종료)

> 핵심: supervisor가 SIGTERM을 보내고, **앱이 스스로 핸들러에서 정리 후 exit**.
> supervisor는 기다리고, timeout 초과 시에만 SIGKILL.

```mermaid
sequenceDiagram
    participant U  as User (CLI)
    participant M  as main()
    participant PS as ProcessSupervisor
    participant AP as App (adas 등)

    U->>M: "kill adas" 입력
    M->>PS: gracefulKill("adas")

    PS->>PS: registry에서 pid 조회
    PS->>PS: disabled = true (watchdog 재시작 방지)
    PS->>AP: SIGTERM 전송

    Note over AP: onSignal() 호출됨
    AP->>AP: g_running = 0
    AP->>AP: while 루프 탈출
    AP->>AP: "정상 종료" 로그 출력
    AP->>AP: exit(0)  ← 앱이 스스로 종료

    PS->>PS: waitpid(WNOHANG) 폴링
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

    PS->>PS: disabled = true
    PS->>AP: SIGKILL 전송
    Note over AP: 즉시 강제 종료 (핸들러 없음)

    PS->>PS: waitpid() 블로킹 대기
    PS->>PS: registry_.erase("adas")
    PS-->>M: SupervisorError::Ok
    M-->>U: "[Supervisor] SIGKILL 완료: adas"
```

---

### 2-4. Watchdog - 자동 재시작

```mermaid
sequenceDiagram
    participant WD as Watchdog 스레드
    participant PS as ProcessSupervisor
    participant AP as App (adas 등)
    participant OS as OS

    loop 500ms마다
        WD->>PS: 내부 registry_ 순회
        PS->>OS: waitpid(pid, WNOHANG)

        alt 앱이 살아있음
            OS-->>WD: 0 반환 → skip
        else 앱이 죽어있음
            OS-->>WD: pid 반환
            WD->>WD: retry_count 확인

            alt retry_count < max_retries
                WD->>WD: retry_count++
                WD->>WD: retry_delay_ms 대기
                WD->>PS: launch(profile)
                PS->>OS: fork() + execvp()
                OS-->>AP: 앱 재시작
            else retry_count >= max_retries
                WD->>WD: disabled = true
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

## 3. 상태 전이 (프로세스 단위)

```
         launch()
  [없음] ─────────► [실행 중 (alive)]
                         │
              SIGTERM     │     앱 자체 crash
          gracefulKill()  │  (watchdog 감지)
                    ┌─────┴─────┐
                    ▼           ▼
              [종료 대기]    [재시작 대기]
                    │     retry < max  │
              waitpid()               │ retry_delay_ms 후
                    │                 ▼
              [registry      [실행 중 (alive)]  ← retry_count++
               에서 제거]
                               retry >= max
                                    │
                                    ▼
                             [disabled (DISABLE_FLAG)]
```
