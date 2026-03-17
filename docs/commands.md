# CLI 명령어 레퍼런스

Platform 실행 후 stdin으로 입력합니다.

```
명령어: kill <id> | hkill <id> | list | quit
```

> `<id>` 는 manifest.json의 `process_id` 값입니다. (ex. `adas`, `navigation`)

---

## kill \<process_id\>

**graceful 종료** - 앱에게 SIGTERM을 보내고, 앱이 스스로 cleanup 후 종료하기를 기다립니다.

```
kill adas
```

| 단계 | 동작 |
|------|------|
| 1 | `state = Stopping` 으로 전환 (watchdog 재시작 차단) |
| 2 | supervisor가 해당 프로세스에 SIGTERM 전송 |
| 3 | 앱의 signal handler가 `g_running = 0` 세팅 |
| 4 | 앱이 루프를 빠져나와 cleanup 후 `exit(0)` |
| 5 | supervisor가 `waitpid()`로 종료 확인 후 registry에서 제거 |
| 6 | timeout(2초) 초과 시 자동으로 `hkill` 실행 |

**출력 예시:**
```
[Supervisor] SIGTERM → adas (pid=1234)
[ADAS] SIGTERM 수신 → 정상 종료
[Supervisor] 정상 종료: adas
```

> **주의:** kill 후에는 watchdog가 자동으로 재시작하지 않습니다 (state=Disabled).
> 재시작이 필요하면 Platform을 재실행하세요.

---

## hkill \<process_id\>

**강제 종료** - 앱에게 SIGKILL을 즉시 보냅니다. 앱의 cleanup 코드가 실행되지 않습니다.

```
hkill navigation
```

| 단계 | 동작 |
|------|------|
| 1 | `state = Disabled` 로 전환 |
| 2 | supervisor가 해당 프로세스에 SIGKILL 전송 |
| 3 | OS가 즉시 프로세스 강제 종료 (signal handler 없음) |
| 4 | supervisor가 `waitpid()`로 종료 확인 후 registry에서 제거 |

**출력 예시:**
```
[Supervisor] SIGKILL 완료: navigation
```

> **언제 쓰나:** 앱이 SIGTERM에 반응하지 않거나, `kill` 명령의 2초 timeout이 초과됐을 때.
> `kill`의 timeout 초과 시 내부적으로 자동 호출됩니다.

---

## list

**실행 목록 조회** - 현재 supervisor가 관리 중인 모든 프로세스와 피처 상태를 출력합니다.

```
list
```

**출력 예시:**
```
[Supervisor] 실행 목록:
  [Running]  adas       pid=1234  alive  retries=0/3
    features: collision_avoidance=ON blind_spot_detection=ON
  [Disabled] navigation pid=-1    dead   retries=3/3
    features: route_guidance=ON map_rendering=OFF
```

| 항목 | 설명 |
|------|------|
| `[Running]` | 정상 실행 중, watchdog 재시작 대상 |
| `[Stopping]` | gracefulKill 요청 후 종료 대기 중 |
| `[Disabled]` | kill 완료 또는 max_retries 초과, 재시작 안 함 |
| `pid` | 현재 프로세스 ID. `-1`이면 실행 중이 아님 |
| `alive` / `dead` | `kill(pid, 0)`으로 확인한 생존 여부 |
| `retries` | 현재 재시작 횟수 / 최대 허용 횟수 |
| `features` | 프로세스가 소유한 피처와 활성 여부 |

---

## quit

**플랫폼 종료** - 모든 자식 프로세스에 SIGTERM을 보내고 종료합니다.

```
quit
```

| 단계 | 동작 |
|------|------|
| 1 | CLI 루프 탈출 |
| 2 | `~ProcessSupervisor()` 호출 |
| 3 | watchdog 스레드 종료 (`running_ = false` → join) |
| 4 | registry의 모든 프로세스에 SIGTERM + waitpid() |
| 5 | `EXIT_SUCCESS` 반환 |

---

## [Phase 2] set \<process_id\> \<feature_id\> on|off

> SHM 연동 후 추가될 명령어. 프로세스를 재시작하지 않고 피처를 런타임에 ON/OFF.

```
set navigation map_rendering on
set navigation map_rendering off
```

| 단계 | 동작 |
|------|------|
| 1 | supervisor가 해당 피처의 SHM 슬롯에 `is_enabled` 값을 씀 |
| 2 | 앱의 루프가 다음 주기(~100ms)에 SHM을 읽어 피처 로직 시작/중단 |

> 프로세스 자체는 살아있고 피처 단위로 동작을 제어합니다.

---

## 에러 메시지 대응표

| 메시지 | 원인 | 대응 |
|--------|------|------|
| `이미 실행 중: <id>` | 동일 process_id가 이미 실행됨 | 무시 또는 kill 후 재시작 |
| `NotFound` | registry에 없는 process_id 입력 | `list`로 정확한 id 확인 |
| `NotRunning` | 이미 죽어있는 프로세스에 kill 시도 | 무시 (watchdog가 처리 중) |
| `fork 실패` | 시스템 리소스 부족 | 다른 프로세스 종료 후 재시도 |
| `[Watchdog] 최대 재시도 초과 → Disabled` | 앱이 max_retries 이상 반복 crash | 앱 로그 확인 후 수동 조치 |
