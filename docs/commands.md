# CLI 명령어 레퍼런스

Platform 실행 후 stdin으로 입력합니다.

```
명령어: kill <id> | hkill <id> | list | quit
```

---

## kill \<feature_id\>

**graceful 종료** - 앱에게 SIGTERM을 보내고, 앱이 스스로 정리 후 종료하기를 기다립니다.

```
kill adas
```

| 단계 | 동작 |
|------|------|
| 1 | supervisor가 해당 프로세스에 SIGTERM 전송 |
| 2 | 앱의 signal handler가 `g_running = 0` 세팅 |
| 3 | 앱이 루프를 빠져나와 cleanup 후 `exit(0)` |
| 4 | supervisor가 `waitpid()`로 종료 확인 후 registry에서 제거 |
| 5 | timeout(2초) 초과 시 자동으로 `hkill` 실행 |

**출력 예시:**
```
[Supervisor] SIGTERM → adas (pid=1234)
[ADAS] SIGTERM 수신 → 정상 종료
[Supervisor] 정상 종료: adas
```

> **주의:** kill 후에는 watchdog가 자동으로 재시작하지 않습니다.
> 재시작이 필요하면 manifest.json에서 flag를 조정하고 재실행하세요.

---

## hkill \<feature_id\>

**강제 종료** - 앱에게 SIGKILL을 즉시 보냅니다. 앱의 cleanup 코드가 실행되지 않습니다.

```
hkill navigation
```

| 단계 | 동작 |
|------|------|
| 1 | supervisor가 해당 프로세스에 SIGKILL 전송 |
| 2 | OS가 즉시 프로세스 강제 종료 (signal handler 없음) |
| 3 | supervisor가 `waitpid()`로 종료 확인 후 registry에서 제거 |

**출력 예시:**
```
[Supervisor] SIGKILL 완료: navigation
```

> **언제 쓰나:** 앱이 SIGTERM에 반응하지 않거나, `kill` 명령의 2초 timeout이 초과됐을 때.
> `kill`의 timeout 초과 시 내부적으로 자동 호출됩니다.

---

## list

**실행 목록 조회** - 현재 supervisor가 관리 중인 모든 프로세스의 상태를 출력합니다.

```
list
```

**출력 예시:**
```
[Supervisor] 실행 목록:
  adas        pid=1234  alive   retries=0/3
  navigation  pid=-1    dead    [disabled]  retries=3/3
```

| 항목 | 설명 |
|------|------|
| `pid` | 현재 프로세스 ID. `-1`이면 실행 중이지 않음 |
| `alive` / `dead` | `kill(pid, 0)`으로 확인한 현재 생존 여부 |
| `[disabled]` | watchdog 재시작 비활성화 상태 (kill 명령 또는 max_retries 초과) |
| `retries` | 현재 재시작 횟수 / 최대 허용 횟수 |

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

**출력 예시:**
```
(quit 입력 후 터미널 종료)
```

---

## 에러 메시지 대응표

| 메시지 | 원인 | 대응 |
|--------|------|------|
| `이미 실행 중: <id>` | 동일 feature_id가 이미 실행됨 | 무시 또는 kill 후 재시작 |
| `NotFound` | registry에 없는 feature_id 입력 | `list`로 정확한 id 확인 |
| `NotRunning` | 이미 죽어있는 프로세스에 kill 시도 | 무시 (watchdog가 처리 중) |
| `fork 실패` | 시스템 리소스 부족 | 다른 프로세스 종료 후 재시도 |
| `execvp 실패` | binary_path 경로가 잘못됨 | manifest.json의 binary_path 확인 |
| `[Watchdog] 최대 재시도 초과 → DISABLE_FLAG` | 앱이 max_retries 이상 반복 crash | 앱 로그 확인 후 수동 조치 |
