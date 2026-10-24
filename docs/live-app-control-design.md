# 실행 중 앱 제어 CLI 설계

## 1. 목표

실행 중인 MusicBitMaker의 실제 메모리 상태에 로컬 CLI로 접근한다. 저장된 `.tracktionedit` 파일만 별도로 읽는 `--headless`와 달리 다음 상태를 조회하거나 변경할 수 있어야 한다.

- 현재 열린 프로젝트와 저장되지 않은 변경 상태
- 재생 여부, 재생 헤드, 녹음 모드, 루프와 메트로놈
- 현재 선택 트랙, 타임라인 확대와 표시 구간
- 실행 중 앱의 트랙과 노트
- 재생·정지·이동 같은 Transport 제어
- 현재 JUCE 메인 UI의 PNG 캡처
- 이후 트랙·노트 편집과 프로젝트 저장

이 기능은 같은 Windows 사용자 세션에서만 쓰는 개발·에이전트 자동화 기능이다. 네트워크 원격 제어 기능은 아니다.

## 2. `--headless`와 `--live` 구분

| 모드 | 데이터 원본 | GUI 실행 필요 | 저장되지 않은 상태 | 주 용도 |
|---|---|---:|---:|---|
| `--headless` | 디스크의 `.tracktionedit` | 아니요 | 볼 수 없음 | 검사, 배치 편집, 렌더링 |
| `--live` | 실행 중 GUI의 Tracktion `Edit` | 예 | 볼 수 있음 | 실시간 조회, 조작, 화면 캡처 |

명령 형태:

```powershell
MusicBitMaker.exe --live <command> [options] --result-json <result.json>
```

`--live` 프로세스는 GUI를 새로 띄우지 않는다. 실행 중 GUI가 없으면 즉시 `app_not_running`으로 실패한다.

## 3. 통신 방식 결정

JUCE `InterprocessConnection`의 Named Pipe를 사용한다.

```text
Codex / PowerShell
        |
        | MusicBitMaker.exe --live ...
        v
Live CLI client process
        |
        | JUCE InterprocessConnection + Windows Named Pipe
        v
Running MusicBitMaker GUI
        |
        | JUCE message thread
        v
MainComponent + Tracktion Edit + Transport
```

선택 이유:

- JUCE에 이미 포함되어 있어 새 라이브러리가 필요 없다.
- 로컬 프로세스만 연결하며 TCP 포트나 Windows 방화벽 설정이 필요 없다.
- 요청과 응답을 같은 연결에서 주고받을 수 있다.
- 콜백을 JUCE 메시지 스레드에서 받도록 설정할 수 있다.

첫 버전은 한 번에 클라이언트 하나, 연결 하나당 요청 하나를 처리한다. 에이전트 자동화에는 충분하며 동시 다중 클라이언트 서버는 필요할 때 추가한다.

## 4. 앱 발견과 세션 파일

GUI 시작 시 사용자 로컬 애플리케이션 데이터 폴더에 세션 설명 파일을 쓴다.

```text
%LOCALAPPDATA%\MusicBitMaker\live-session.json
```

예시:

```json
{
  "schemaVersion": 1,
  "appVersion": "v0.1033",
  "pid": 12345,
  "pipeName": "MusicBitMaker.Live.12345.8f1c...",
  "token": "무작위 세션 토큰",
  "startedAt": "2026-10-24T12:34:56+09:00"
}
```

- 파이프 이름에 PID와 무작위 값을 넣어 이전 비정상 종료의 잔여 정보와 충돌하지 않게 한다.
- 토큰은 연결한 프로세스가 같은 세션 파일을 읽었는지 확인하는 최소 인증값이다.
- GUI 정상 종료 시 세션 파일을 삭제한다.
- 비정상 종료로 파일이 남아도 파이프 연결 실패를 `app_not_running`으로 처리한다.
- 세션 파일은 사용자의 기존 로컬 프로필 권한을 그대로 사용한다.

고정 TCP 포트, 외부 IP 바인딩, 방화벽 예외는 만들지 않는다.

## 5. 프로토콜

요청과 응답은 UTF-8 JSON을 JUCE 메시지 블록으로 한 번씩 전송한다. 프로토콜 스키마 버전은 앱 버전과 별도로 `1`부터 시작한다.

요청:

```json
{
  "schemaVersion": 1,
  "requestId": "0d6b...",
  "token": "세션 파일의 토큰",
  "command": "status",
  "arguments": {}
}
```

성공 응답:

```json
{
  "schemaVersion": 1,
  "requestId": "0d6b...",
  "ok": true,
  "command": "status",
  "appVersion": "v0.1033",
  "data": {}
}
```

실패 응답:

```json
{
  "schemaVersion": 1,
  "requestId": "0d6b...",
  "ok": false,
  "command": "seek",
  "appVersion": "v0.1033",
  "error": {
    "code": "invalid_position",
    "message": "seconds must be between 0 and 16."
  }
}
```

규칙:

- 알 수 없는 필드가 아니라 알 수 없는 명령과 옵션을 거부한다.
- 요청 크기는 64 KiB 이하로 제한한다.
- 응답의 `requestId`는 요청과 같아야 한다.
- 기본 연결 제한 시간은 2초, 명령 제한 시간은 5초다.
- CLI의 `--timeout-ms`는 250~30000 범위에서만 허용한다.
- 모든 CLI 결과는 기존 헤드리스 명령과 동일하게 `--result-json`에 기록한다.

## 6. 스레드 규칙

`InterprocessConnection` 서버 콜백은 JUCE 메시지 스레드에서 받는다. 따라서 다음 작업은 메시지 스레드에서 순서대로 처리한다.

- `MainComponent` 상태 읽기와 변경
- Tracktion `Edit`, `Transport`, `UndoManager` 접근
- 버튼·선택·타임라인 표시 상태 동기화
- JUCE 컴포넌트 스냅샷 생성

실시간 오디오 콜백에서는 IPC, JSON, 파일 I/O, UI 호출, 메모리 할당을 하지 않는다.

첫 PNG 캡처는 드물게 실행되는 개발 기능이므로 메시지 스레드에서 화면을 이미지로 복사하고 PNG를 쓴다. 실제 측정에서 UI 멈춤이 눈에 띌 때만 이미지 인코딩과 파일 쓰기를 작업 스레드로 옮긴다.

## 7. 라이브 명령

### 7.1 1단계: 연결과 읽기 전용 상태

| 명령 | 결과 |
|---|---|
| `ping` | 연결, 프로토콜 버전, 앱 버전 확인 |
| `status` | 프로젝트, 저장 상태, 재생 상태, 재생 헤드, BPM, 루프, 녹음 모드, 선택 트랙, 확대/표시 구간 |
| `list-tracks` | 현재 메모리의 트랙 ID, 순서, 이름, 악기, 음소거/솔로 |
| `list-notes` | 지정 트랙의 현재 메모리 노트 목록 |
| `capture-ui` | 현재 메인 컴포넌트를 PNG로 저장 |

예시:

```powershell
MusicBitMaker.exe --live status `
  --result-json "E:\Temp\music-status.json"

MusicBitMaker.exe --live list-notes `
  --track-id "123456" `
  --result-json "E:\Temp\live-notes.json"

MusicBitMaker.exe --live capture-ui `
  --output "E:\Temp\music-ui.png" `
  --result-json "E:\Temp\music-ui-result.json"
```

`capture-ui`는 메인 JUCE 콘텐츠를 현재 창 크기로 캡처한다. 기본 캡처에는 Windows 제목 표시줄, 다른 앱, 화면 배경, 분리된 Keyboard Pad 창은 포함하지 않는다.

### 7.2 2단계: Transport와 UI 제어

| 명령 | 동작 |
|---|---|
| `play` | 현재 재생 헤드부터 재생 |
| `pause` | 현재 위치를 유지하며 재생만 중지 |
| `stop` | 기존 STOP과 동일하게 중지 후 0초로 이동 |
| `seek --seconds` | 검증된 위치로 재생 헤드 이동 |
| `record-mode --enabled` | 녹음 대기 모드 변경 |
| `loop --enabled --start --end` | 루프 상태와 범위 변경 |
| `metronome --enabled` | 메트로놈 변경 |
| `tempo --bpm` | 40~240 BPM 변경 |
| `select-track --track-id` | 트랙 선택 및 UI 동기화 |
| `timeline-view --zoom --start` | 가로 확대와 표시 시작점 변경 |
| `save` | 현재 프로젝트를 기존 경로에 저장 |

재생·정지·녹음은 기존 `MainComponent` 메서드를 호출한다. Transport 상태를 별도로 복제하지 않는다.

### 7.3 3단계: 실행 중 프로젝트 편집

| 명령 | 동작 |
|---|---|
| `add-track` | 악기 종류와 이름으로 트랙 추가 |
| `rename-track` | 트랙 이름 변경 |
| `delete-track` | 트랙 삭제 |
| `add-note` | 시간, 길이, 피치, 벨로시티로 노트 추가 |
| `delete-note` | 조회된 노트 선택자를 이용해 삭제 |
| `move-note` | 시작·길이·피치 변경 |
| `change-instrument` | 기존 트랙 설정 규칙에 따라 악기 변경 |

- 모든 편집은 Tracktion `UndoManager`를 사용한다.
- 한 CLI 요청은 하나의 Undo 트랜잭션으로 처리한다.
- 편집 명령은 자동 저장하지 않는다. `save`를 별도로 호출한다.
- 드럼과 신시사이저 사이 변환 시 기존 GUI와 같은 데이터 초기화 확인 규칙을 적용한다.

## 8. 잘못된 프로젝트 조작 방지

조회 명령은 현재 열린 프로젝트를 그대로 반환한다. 변경 명령은 선택적으로가 아니라 반드시 다음 보호값을 받는다.

```text
--expect-project <absolute.tracktionedit>
```

실행 중 앱의 `currentProjectFile`과 정규화된 경로가 다르면 `project_mismatch`로 거부한다. 새로 생성되어 아직 경로가 없는 프로젝트는 먼저 GUI에서 저장해야 라이브 변경 CLI를 허용한다.

이 규칙은 에이전트가 사용자가 다른 프로젝트로 전환한 사실을 모르고 잘못된 음악을 변경하는 것을 막는다.

## 9. 종료 코드

기존 헤드리스 종료 코드에 다음 값을 추가한다.

| 코드 | 의미 |
|---:|---|
| 0 | 성공 |
| 2 | 잘못된 명령 또는 옵션 |
| 3 | 결과/스크린샷 파일 오류 |
| 5 | 트랙 또는 노트를 찾지 못함 |
| 7 | 실행 중 GUI 또는 세션 파이프를 찾지 못함 |
| 8 | 연결 또는 명령 시간 초과 |
| 9 | 프로토콜 버전, 토큰 또는 응답 ID 불일치 |
| 10 | 예기치 않은 내부 오류 |

## 10. 최소 코드 구조

```text
src/Main.cpp                 MainComponent의 실제 명령 처리와 UI 접근
src/AutomationCli.*         기존 디스크 기반 헤드리스 명령
src/LiveControl.*           Named Pipe 서버/클라이언트와 JSON 프로토콜
```

필요한 연결은 단순하게 유지한다.

- GUI는 `LiveControlServer`에 `MainComponent`의 명령 처리 콜백을 전달한다.
- CLI 클라이언트는 한 요청을 보내고 한 응답을 기다린 뒤 종료한다.
- 별도 Command Bus, 서비스 로케이터, 플러그인 인터페이스는 만들지 않는다.
- 트랙·노트 JSON 직렬화와 편집 helper는 헤드리스/라이브 양쪽에서 필요해질 때만 공통 함수로 이동한다.

## 11. 앱 생명주기

GUI 시작:

1. `Engine`과 `MainWindow` 생성.
2. `MainComponent` 준비 완료.
3. 무작위 파이프 이름과 토큰 생성.
4. Named Pipe 대기 시작.
5. 세션 파일 기록.

GUI 종료:

1. 새 요청 수신 중지.
2. Named Pipe 연결 종료.
3. 세션 파일이 현재 프로세스의 것인지 확인 후 삭제.
4. `MainWindow`와 `Engine` 종료.

CLI 시작:

1. 세션 파일 읽기와 스키마 검증.
2. Named Pipe 연결.
3. 요청 JSON 전송.
4. 제한 시간 동안 응답 대기.
5. 응답 ID, 스키마, 토큰 결과 검증.
6. `--result-json` 기록 후 종료 코드 반환.

## 12. 테스트 계획

자동 검사 `--self-test-live-control`:

- 요청 JSON 파싱과 64 KiB 제한.
- 잘못된 토큰·스키마·명령 거부.
- 고유 테스트 파이프의 연결, 요청, 응답 왕복.
- 제한 시간 만료 처리.
- 라이브 `status` 결과의 필수 필드.
- GUI/오디오 콜백이 아닌 메시지 스레드에서 handler가 실행되는지 확인.

Windows 통합 검사:

1. GUI 앱을 연다.
2. 프로젝트를 불러오고 트랙을 선택한다.
3. `--live status`가 저장되지 않은 현재 상태와 선택 트랙을 반환하는지 확인한다.
4. GUI에서 재생 헤드를 옮긴 뒤 다시 조회한다.
5. `play`, `pause`, `seek`, `stop`이 화면과 소리에 반영되는지 확인한다.
6. 확대와 선택 상태를 바꾼 뒤 `capture-ui` PNG가 현재 화면과 일치하는지 확인한다.
7. 다른 `--expect-project`를 준 변경 명령이 거부되는지 확인한다.

## 13. 구현 순서와 버전

### v0.1033: 라이브 연결과 조회

구현 완료:

- 세션 파일과 Named Pipe 요청/응답.
- `ping`, `status`, `list-tracks`, `list-notes`.
- `--self-test-live-control`.
- 앱 미실행, 제한 시간, 프로토콜 불일치 종료 코드 구분.

### v0.1034: 현재 UI 캡처

구현 완료:

- `capture-ui`와 PNG 결과.
- 이미지 크기/비어 있지 않음 자동 검사.
- 실제 Windows GUI에서 1000×720 현재 화면 캡처 확인.

### v0.1035: 타임라인 확대 경계 수정

- 확대·이동 시 시간 기반 그래픽을 트랙 콘텐츠 안으로 제한.
- 라벨 영역과 타임라인 히트테스트 분리.

### v0.1036: 재생 단축키와 툴바 정리

- PLAY/STOP 단일 토글 버튼과 Space 기본 단축키.
- 설정 가능한 Loop 단축키와 기존 단축키 설정 마이그레이션.
- 상단 개별 드럼 버튼 제거.

### v0.1037: Transport와 UI 제어

- `play`, `pause`, `stop`, `seek`, record mode, loop, metronome, tempo, 선택, timeline view, save.
- 프로젝트 경로 보호와 상태 회귀 검사.

### v0.1038: 라이브 트랙·노트 편집

- 트랙과 노트 편집.
- 한 요청당 하나의 Undo 트랜잭션.
- GUI와 CLI helper 통합.

각 단계가 실제 사용자 테스트 가능한 빌드가 될 때만 `VERSION`을 `0.0001` 증가시킨다. 설계 문서만 추가할 때는 버전을 올리지 않는다.

## 14. 이번 범위에서 제외

- 인터넷 또는 LAN 원격 접속
- HTTP/REST/WebSocket 서버
- 여러 앱 인스턴스 동시 선택
- 지속적인 상태 이벤트 스트리밍
- 오디오 콜백 단위 파형 스트리밍
- Windows 데스크톱 전체 캡처
- 음성이나 자연어를 앱 내부에서 직접 해석하는 기능

에이전트가 상태 변화를 계속 감시해야 하는 실제 요구가 생기기 전까지는 짧은 `status` 폴링으로 충분하다.
