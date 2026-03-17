# CLI 명령어 레퍼런스

Platform 실행 후 stdin으로 입력합니다.

```
명령어: list | quit
        kill <id> | hkill <id>
        set <process_id> <feature_id> on|off
```

> `<id>` / `<process_id>` 는 manifest.json의 `process_id` 값입니다. (ex. `adas`, `navigation`)

---

## list

실행 목록 조회. Supervisor가 관리하는 모든 프로세스와 피처 상태를 출력합니다.

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
| `[Running]` | 정상 실행 중, Watchdog 재시작 대상 |
| `[Stopping]` | gracefulKill 요청 후 종료 대기 중 |
| `[Disabled]` | kill 완료 또는 max_retries 초과, 재시작 안 함 |
| `alive/dead` | `kill(pid, 0)` 으로 확인한 생존 여부 |
| `retries` | 현재 재시작 횟수 / 최대 허용 횟수 |

---

## kill \<process_id\>

**graceful 종료** — SHM Kill Switch + SIGTERM 동시 전송. 앱이 onStop() 후 자발적으로 종료하기를 기다립니다.

```
kill adas
```

| 단계 | 동작 |
|------|------|
| 1 | `state = Stopping` (Watchdog 재시작 차단) |
| 2 | SHM `is_killed = true` (모든 피처) |
| 3 | `SIGTERM` 전송 |
| 4 | 앱이 `is_killed` 또는 signal 감지 → `onStop()` → exit |
| 5 | Supervisor `waitpid()` 폴링 확인 후 registry에서 제거 |
| 6 | 2000ms timeout 초과 시 자동으로 `hkill` 실행 |

> kill 이후 Watchdog은 재시작하지 않습니다 (`state = Disabled`).

---

## hkill \<process_id\>

**강제 종료** — SHM Kill Switch 먼저, 200ms 내 자발적 종료 안 되면 SIGKILL.

```
hkill navigation
```

| 단계 | 동작 |
|------|------|
| 1 | `state = Disabled` |
| 2 | SHM `is_killed = true` (모든 피처) |
| 3 | 200ms 폴링: 앱이 자발적으로 종료하면 종료 |
| 4 | 200ms 초과 시 `SIGKILL` (즉시 강제 종료) |
| 5 | `waitpid()` 확인 후 registry에서 제거 |

> gracefulKill의 2000ms timeout 초과 시 내부적으로 자동 호출됩니다.

---

## set \<process_id\> \<feature_id\> on|off

**런타임 피처 제어** — 프로세스를 재시작하지 않고 피처를 ON/OFF합니다.

```
set navigation map_rendering on
set navigation map_rendering off
```

| 단계 | 동작 |
|------|------|
| 1 | `all_profiles_` 내부 플래그 업데이트 |
| 2 | SHM `is_enabled = true/false` 기록 |
| 3 | ON 전환 + 프로세스가 죽어있으면 자동 기동 (`launchLocked`) |
| 4 | 앱이 다음 루프 주기에 SHM 읽어 피처 로직 시작/중단 |

**모든 피처 OFF 시 자동 흐름:**
```
set <proc> <feat> off  (마지막 활성 피처)
  → SHM is_enabled = false
  → 앱 루프: any_enabled = false → 자율 종료
  → Watchdog: hasAnyEnabledFeature() = false → 재시작 안 함
```

---

## quit

플랫폼 종료. 모든 자식 프로세스에 SIGTERM을 보내고 종료합니다.

```
quit
```

| 단계 | 동작 |
|------|------|
| 1 | CLI 루프 탈출 |
| 2 | `~ProcessSupervisor()` 호출 |
| 3 | Watchdog 스레드 종료 (`running_ = false` → join) |
| 4 | registry의 모든 프로세스에 SIGTERM + waitpid() |
| 5 | ShmManager 소멸 → shm_unlink (SHM 정리) |

---

## 에러 메시지 대응

| 메시지 | 원인 | 대응 |
|--------|------|------|
| `이미 실행 중: <id>` | 동일 process_id가 이미 실행됨 | 무시 또는 kill 후 재시작 |
| `NotFound` | registry에 없는 process_id | `list`로 정확한 id 확인 |
| `NotRunning` | 이미 죽어있는 프로세스에 kill 시도 | 무시 (Watchdog이 처리 중) |
| `fork 실패` | 시스템 리소스 부족 | 다른 프로세스 종료 후 재시도 |
| `[Watchdog] 최대 재시도 초과 → DISABLE_FLAG` | 앱이 max_retries 이상 반복 crash | 앱 로그 확인 후 수동 조치 |
| `[SDK] SHM 연결 실패: <feature>` | registerProfile 전에 launch됐거나 SHM 손상 | Platform 재시작 |
