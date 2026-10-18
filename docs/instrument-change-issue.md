# BUG-001: 실행 중 악기 변경이 실제 음색에 반영되지 않음

작성일: 2026-10-16 · 상태: 출력 라우팅 원인 수정, v0.1018 실제 연주 재검증 대기 · 기준 소스 버전: v0.1018

진단 추가: v0.1016에서는 실행 경로, 악기 변경 전후의 FourOsc 상태, 건반·드럼 MIDI 주입을 `View > Show Instrument Diagnostic Log`에서 열 수 있는 로컬 파일에 기록한다. 이 로그는 실제 오디오 출력을 측정하지 않는다.

진단 추가: v0.1017에서는 변경·건반 입력·재생 시작 뒤의 `post-synth`, `post-track`, `post-master` 실시간 오디오 구간에 대해 해시, RMS, 피크, 비영(非零) 샘플 수를 같은 로그에 기록한다. 물리적 오디오 장치 뒤의 소리는 측정하지 않는다.

v0.1017 실제 연주 로그(2026-10-17 00:12)에서 `post-synth`와 `post-track`은 154개 구간의 해시가 모두 일치하고, 119개 구간에 신호가 있었다. `post-master` 154개 구간은 모두 무음이었다. Tracktion의 `setOutputToDefaultDevice(bool isMidi)` 구현을 확인한 결과 앱의 세 호출이 `true`를 전달해 모든 트랙을 기본 MIDI 출력으로 라우팅하고 있었다. v0.1018에서 기본 오디오 출력(`false`)으로 수정하고 라우팅 회귀 검사를 추가했다. 새 빌드의 실제 연주로 `post-master`가 비영 신호를 받는지 재확인이 필요하다.

## 문제와 요청

MusicBitMaker 앱에서 트랙의 Instrument를 변경해도 실제 소리가 기대한 대로 바뀌지 않는다는 사용자 보고가 있다. 별도로 각 신시사이저 프리셋을 새 인스턴스에 적용해 같은 멜로디로 만든 WAV는 모두 다르게 들린다는 사용자 확인을 받았다.

별도 비교 WAV는 프리셋별 소리가 다른데 앱에서 악기 변경이 반영되지 않아 조사를 시작했다. v0.1017 실시간 로그와 Tracktion 소스에서 출력 라우팅 오류를 확인해 v0.1018에 수정했다. 실제 장치에서 새 빌드의 변경 전후 소리는 아직 확인해야 한다.

요청 범위: 증상을 재현하고 원인을 분리한 뒤, 실제 출력에 새 음색이 적용되도록 최소한의 수정과 회귀 검증을 진행한다. 음악 상태의 기준은 Tracktion Edit로 유지한다.

## 확인된 사실과 아직 모르는 점

| 구분 | 내용 |
| --- | --- |
| 사용자 보고 | 앱에서 실행 중 악기를 바꿔도 소리가 제대로 바뀌지 않음 |
| 사용자 청취 확인 | 별도 비교 WAV의 다섯 프리셋은 분명히 서로 다르게 들림 |
| 파일 검사 | 다섯 WAV 모두 12초, 무음 아님, 최종 파일 클리핑 없음. 10개 프리셋 조합 모두 PCM 파형이 동일하지 않음 |
| 비교 생성 조건 | 현재 앱의 프리셋 XML과 `configureSynth` 사용. 프리셋마다 새 FourOsc 생성 후 초기화하여 직접 오프라인 처리 |
| 비교 진단 한계 | 기존 인스턴스 재사용, 실행 중 파라미터 교체, 트랙 플러그인 체인, 실시간 오디오 그래프, Edit 렌더 경로를 검증하지 않음 |
| 실시간 로그 | v0.1017에서 프리셋 변경 후 FourOsc 상태가 바뀌었고, 신시사이저·트랙 끝에는 신호가 있었으나 마스터 오디오 경로는 무음 |
| 확인된 원인 | 세 곳의 `setOutputToDefaultDevice(true)`가 트랙을 기본 MIDI 출력으로 보냄. Tracktion API에서 `true`는 `isMidi` |
| 미확인 | 영향을 받는 악기 조합, 정지 상태에서도 발생하는지, 직접 건반 연주/기존 MIDI 재생의 차이, 저장·재실행 후 차이 |
| 관련성 미확인 | 전체 자체 테스트 접근 위반과 음색 변경 문제의 원인이 같은지 여부 |

파형이 다르다는 사실만으로 실시간 악기 교체가 정상이라고 결론 내리지 않는다. 합성기의 랜덤 위상·노이즈 등도 파형에 영향을 주므로, 다음 조사에서는 상태값과 실제 소리의 변화를 함께 확인한다.

## 재현 절차

1. 실행 파일 경로와 창 제목 버전을 기록한다. 기존 앱을 종료하고 조사할 빌드를 실행한다.
2. 새 프로젝트에 Generated Keys 트랙 하나를 추가한다. Volume/Pan은 고정하고 Mute/Solo 영향을 제거한다.
3. 건반 Octave 4에서 C4를 연주해 기준 소리를 확인한다.
4. 짧은 MIDI 패턴을 녹음한 뒤 정지한다.
5. 트랙 우클릭 → `Settings` → `Change Instrument...` → `8-Bit Lead`를 선택한다.
6. 같은 C4 직접 연주와 기존 MIDI 패턴 재생을 각각 확인한다. 악기 이름 표시와 실제 소리를 따로 기록한다.
7. `Cosmic Pad`로 바꿔 어택·릴리스 차이를 확인하고 Generated Keys로 되돌린다.
8. 재생 중 변경, 음을 누른 상태의 변경, 다른 트랙을 추가한 상태에서도 반복한다.
9. 변경 후 저장·종료·재실행·불러오기를 진행해 음색이 그때는 바뀌는지도 확인한다.

재현 결과에 기록할 항목: 빌드/실행 경로, 오디오 장치, 트랙, 이전·새 프리셋, 선택 다이얼로그 결과, 재생·녹음 상태, 입력 창 포커스, 직접 연주 결과, MIDI 재생 결과, 재실행 후 결과.

## 관련 코드

파일은 모두 저장소 루트 기준이며 줄 번호는 문서 작성 시점이다. 후속 수정 시 함수 이름으로 다시 찾는다.

| 위치 | 역할 / 확인할 내용 |
| --- | --- |
| `src/Main.cpp:139` `configureSynth` | XML을 `restorePluginStateFromValueTree`로 적용하고 프리셋 메타데이터 갱신 |
| `src/Main.cpp:153` `configureDrumSynth` | 드럼 설정 복원 및 종류 메타데이터 갱신 |
| `src/Main.cpp:914` `findInstrument` | 선택 트랙의 FourOsc 조회. 실제 출력에 쓰이는 대상과 동일한지 확인 |
| `src/Main.cpp:940` `ensureInstrument` | 기존 FourOsc가 있으면 그대로 반환. 없을 때만 새 플러그인을 만들어 트랙에 삽입 |
| `src/Main.cpp:1146` `configureTrackInstrument` | 선택한 종류에 따라 위 설정 함수를 호출. 기존 악기에도 재적용하는 경로가 존재 |
| `src/Main.cpp:1157` `changeSelectedTrackInstrument` | UI 선택, 열린 노트 정리, 악기 적용, 노트 처리, 컨텍스트·UI 갱신 |
| `src/Main.cpp:1643` `updateCombinedLiveNote` | 선택 트랙에 라이브 MIDI 주입 |
| `src/Main.cpp:2051` `renderSynthForTest` | 기존 전체 자체 테스트의 Edit 렌더 경로. 자체 테스트 충돌 위치는 미확인 |
| `src/Main.cpp:2270` `renderSynthDemos` | 성공한 비교 음원 생성 경로. 새 인스턴스 + 명시적 초기화 + 직접 버퍼 처리 |
| `tools/compare-synth-demos.py` | WAV 검사와 전체 순차 비교 WAV 생성 |

현재 앱의 변경 경로:

```text
changeSelectedTrackInstrument
  → finishOpenNotes / releaseAllLiveNotes
  → configureTrackInstrument
      → ensureInstrument (보통 기존 FourOsc 반환)
      → configureSynth 또는 configureDrumSynth
          → restorePluginStateFromValueTree
  → 종류 변경에 따른 MIDI 정리
  → getTransport().ensureContextAllocated(true)
  → syncTrackControls / 상태 메시지 / repaint
```

의존성 확인 지점:

- `external/tracktion_engine/modules/tracktion_engine/plugins/effects/tracktion_FourOscPlugin.cpp`: `restorePluginStateFromValueTree`, `initialise`, `applyToBuffer`, 파라미터 캐시 및 보이스 갱신.
- 같은 폴더의 `.h`: `OscParams::restorePluginStateFromValueTree` 등.
- Transport의 `ensureContextAllocated(true)` 구현과 플러그인 변경 시 지원되는 그래프 갱신 경로. 해당 호출이 실제로 무엇을 갱신하는지 소스를 보고 판단한다.
- 고정 의존성: Tracktion v3.2.0(`0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6`), JUCE `19edd538429c93d277bf95b55aaa7e3eb545f951`. 조사 편의를 위해 버전을 올리지 않는다.

## 초기 조사 가설 — v0.1018 이전 기록

1. UI가 가리키는 플러그인과 오디오 출력에 사용되는 플러그인이 일치하지 않는가? 선택 트랙 ID, 플러그인 ID·주소, 체인 내 FourOsc 수를 확인한다.
2. 프리셋 메타데이터만 바뀌고 파형·엔벌로프·이펙트의 런타임 값이 갱신되지 않는가? 변경 전후 ValueTree, CachedValue, AutomatableParameter를 비교한다.
3. 새 인스턴스의 초기화에서는 반영되지만 초기화된 인스턴스에 프리셋을 복원할 때는 일부 내부 상태가 갱신되지 않는가?
4. 라이브 MIDI와 MIDI 클립 재생이 다른 경로를 사용하거나, 기존 재생 그래프가 이전 상태를 유지하는가?
5. 오래 실행된 이전 빌드를 새 빌드로 착각한 것은 아닌가? 실행 경로와 버전을 먼저 기록한다.

권장 분리 실험: 동일 MIDI에 대해 `새 A`, `새 B`, `기존 A → B`, `기존 B → A`를 비교하고, 이어서 실제 트랙/오디오 그래프에서도 같은 검사를 한다. 신규 인스턴스 교체는 진단 실험으로 사용할 수 있지만, 그것이 필요하다고 확정하기 전에 엔진의 지원 API와 상태 갱신 방식을 확인한다.

진단 로그는 메시지 스레드나 비실시간 경로에서 수집한다. 오디오 콜백에 파일 I/O, 할당, 로그, 긴 잠금을 넣지 않는다.

## 기대 동작과 완료 기준

- Synth → Synth: 기존 MIDI 음높이·시작·길이를 유지하며, 이후 직접 연주와 패턴 재생 모두 새 음색을 출력한다.
- A → B → A: 이전 프리셋 파라미터가 섞이지 않고 원래 음색으로 복귀한다.
- 재생 중·음을 누른 상태에서도 전환 후 비정상 지속음이나 중복 발음이 없다.
- 저장 후 불러와도 선택한 악기 이름과 실제 음색이 일치한다.
- 대상 외 트랙의 악기·MIDI·믹서 설정은 유지한다.
- Drum → Drum은 패턴 타이밍을 유지하고 노트 번호와 실제 소리를 새 드럼에 맞춘다.
- Synth ↔ Drum은 기존 초기화 경고·취소 동작을 유지한다.
- 메뉴 표시나 XML 값만으로 통과 판정하지 않고 실제 출력으로 확인한다.
- 재현을 잡는 유용한 회귀 검사를 추가하고, 기존 테스트 실패가 남으면 결과와 충돌 지점을 따로 보고한다.

상세 인수 기준은 [기능 명세의 INST-AC01~10](functional-spec.md)을 참고한다.

## 비교 음원 및 실행 방법

생성 파일은 Git에서 제외되는 로컬 `build/synth-demos/`에 있다.

| 파일 | 내용 |
| --- | --- |
| `1-Generated-Keys.wav` | Generated Keys |
| `2-Retro-Pulse.wav` | Retro Pulse |
| `3-8-Bit-Lead.wav` | 8-Bit Lead |
| `4-Space-Beep.wav` | Space Beep |
| `5-Cosmic-Pad.wav` | Cosmic Pad |
| `all-synths-comparison.wav` | 위 순서로 연결, 파일 사이 1초 무음 |
| `comparison.json` | 파형 차이, PCM 해시, 피크·RMS·클리핑 검사 |
| `render-log.txt` | 비교 파일 생성 진행 로그 |

동일 멜로디, 120 BPM, 벨로시티 100, 8초 연주 + 4초 잔향, 44.1 kHz 스테레오, 개별 WAV 24-bit다. 모든 파일에 동일한 출력 배율 0.25를 적용하며 개별 음량 정규화는 하지 않았다. 프리셋 자체는 수정하지 않았다.

현재 로컬 진단 실행 파일:

```powershell
.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker-SynthDemos.exe --render-synth-demos
python tools/compare-synth-demos.py
```

표준 빌드에는 동일 진단 명령이 포함되어 있으므로 실행 파일명만 해당 빌드에 맞추면 된다. Python 비교 도구에는 NumPy가 필요하다.

## 환경·검증·작업 폴더 인계

- 작업 폴더: `C:\githubdescktops\musicMaker`, 원격: `https://github.com/peartail/CJmusicMaker`.
- 현재 PC: Visual Studio 2026 + MSVC, VS 내장 CMake 사용. 저장소의 표준 문서는 VS 2022를 기준으로 하므로 환경 차이를 구분한다.
- v0.1015 사용자 테스트 파일: `build/MusicBitMaker_artefacts/Debug/MusicBitMaker-v0.1015.exe`.
- 기본 `MusicBitMaker.exe`는 이전 빌드일 수 있다. 기존 앱 실행으로 링크 잠금이 발생해 별도 파일명을 사용했다.
- 옥타브 전용 테스트와 빈 프로젝트 저장 테스트는 통과했다.
- `--self-test`는 이전 빌드와 v0.1015 모두 `-1073741819`(`0xC0000005`)로 종료됐다. 충돌 위치는 아직 특정하지 않았다.
- v0.1018 빌드에서는 `--self-test`, `--self-test-empty-project`, `--self-test-instrument-log`, `--self-test-audio-tap`, `--self-test-keyboard-octave`가 각각 종료 코드 0이었다. 이전 실패가 재발하지 않는다는 뜻으로 해석하지는 않는다.
- 비교 진단 빌드와 `--render-synth-demos`는 종료 코드 0. 비교 스크립트의 다섯 파일 검사도 통과했다.
- 현재 작업 폴더에는 옥타브 기능, 기능 명세, 비교 진단 도구 등의 **미커밋 변경**이 있다. 새 클론이나 별도 작업 폴더에는 이 변경이 없을 수 있으므로 먼저 `git status`와 실제 파일을 확인한다. 기존 변경을 되돌리지 않는다.
- v0.1015 당시에는 악기 변경 코드를 수정하거나 원인을 확정하지 않았다. v0.1018에서 출력 라우팅을 수정했다.

다른 세션 시작용 요청:

> `docs/instrument-change-issue.md`와 `AGENTS.md`를 읽고 BUG-001을 조사해줘. 개별 신시사이저 비교 WAV는 다르게 들리지만 앱에서 실행 중 Instrument 변경은 제대로 반영되지 않는다. 기존 인스턴스 재사용과 실시간 출력 경로를 분리해서 재현하고, 실제 출력 검증을 포함해 수정해줘. 현재 미커밋 변경과 고정 의존성은 보존해줘.
