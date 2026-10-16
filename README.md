# MusicBitMaker

Windows에서 건반으로 연주하고, 연주 내용을 타임라인에 녹음·편집한 뒤 여러 악기와 오디오를 레이어링하여 한 곡으로 믹싱하고 출력하는 개인용 음악 제작 앱이다.

현재 저장소에는 C++20/JUCE/Tracktion 네이티브 앱과 초기 아이디어를 검증했던 C# WinForms 건반 프로토타입이 함께 있다.

현재 앱 버전은 루트의 `VERSION` 파일에서 관리한다. 초기 버전은 `v0.1001`이며, 사용자에게 전달하는 테스트 빌드마다 `v0.1002`, `v0.1003`처럼 `0.0001`씩 증가시킨다. 실행 중인 빌드는 앱 상단과 Windows 창 제목에서 확인할 수 있다.

## 확정한 기술 방향

| 항목 | 선택 | 이유 |
| --- | --- | --- |
| 대상 플랫폼 | Windows 10/11 x64 | 우선 지원 범위를 좁혀 오디오 장치와 패키징을 안정화한다. |
| 언어 | C++20 | Tracktion Engine의 요구사항이며 실시간 오디오 생태계가 성숙했다. |
| UI와 장치 계층 | JUCE 네이티브 UI | 건반, 타임라인, 믹서 같은 사용자 UI와 오디오/MIDI 장치를 한 프레임워크에서 다룬다. |
| DAW 엔진 | Tracktion Engine | 트랙, 클립, MIDI, 재생 위치, 플러그인, 자동화, 렌더링의 상위 데이터 모델을 사용한다. |
| 빌드 | CMake + Visual Studio 2022/MSVC | 현재 PC에서 바로 사용할 수 있고 별도 Ninja 설치가 필요 없다. |
| 오디오 백엔드 | WASAPI 우선, ASIO 후속 | 첫 실행의 장치 호환성을 먼저 확보하고 저지연 ASIO는 별도 단계에서 검증한다. |

JUCE는 화면과 운영체제에 가까운 계층이고, Tracktion Engine은 음악 프로젝트의 상태와 재생을 담당한다. MusicBitMaker 코드는 두 계층을 연결하면서 제품의 편집 흐름과 UI를 구현한다.

```text
MusicBitMaker UI와 작업 흐름
        │
        ├── JUCE: 창, 컴포넌트, 키보드/마우스, 오디오·MIDI 장치
        │
        └── Tracktion Engine: Edit, 트랙, 클립, 노트, Transport, 플러그인, 렌더링
                │
                └── Windows 오디오: WASAPI → 필요 시 ASIO
```

웹 UI는 빠른 시안에는 유리하지만, 이 프로젝트의 최종 목표인 저지연 재생, 플러그인 호스팅, 오디오 장치 제어에는 네이티브 구성이 더 직접적이다. 따라서 실제 앱은 JUCE 네이티브 UI를 사용한다.

## 의존성 고정 정책

초기 기준 버전은 다음과 같다.

- Tracktion Engine: `v3.2.0` 태그 (`0a5f4e6`)
- JUCE: 위 Tracktion 태그의 내장 서브모듈 커밋 `19edd538429c93d277bf95b55aaa7e3eb545f951`

Tracktion 저장소는 C++20을 요구하고 JUCE 모듈 형태로 제공되며, 예제 빌드용 JUCE를 Git 서브모듈로 포함한다. 따라서 JUCE를 독립적으로 최신 버전으로 올리지 않고 Tracktion이 고정한 커밋을 함께 사용한다.

의존성 갱신은 자동으로 하지 않는다. 별도 브랜치에서 새 Tracktion 태그와 그 태그의 JUCE 커밋으로 함께 올리고, 프로젝트 열기·재생·녹음·저장·오디오 출력 회귀 테스트를 통과한 뒤 반영한다.

공식 자료:

- [Tracktion Engine 저장소와 요구사항](https://github.com/Tracktion/tracktion_engine)
- [Tracktion Engine 릴리스](https://github.com/Tracktion/tracktion_engine/releases)
- [Tracktion v3.2.0이 고정한 JUCE 커밋](https://github.com/Tracktion/tracktion_engine/tree/v3.2.0/modules)
- [JUCE CMake 안내](https://github.com/juce-framework/JUCE#cmake)

## 라이선스 운영 기준

현재 용도는 혼자 사용하는 비공개·비상용 로컬 앱이며 외부에 실행 파일이나 서비스를 제공하지 않는다. 이 범위에서는 다음 오픈소스 라이선스 경로로 개발한다.

- JUCE: AGPLv3 경로
- Tracktion Engine: GPLv3 경로
- 현재 예상 비용: 0원

비공개로 직접 실행하는 것과 다른 사람에게 배포하는 것은 라이선스상 다른 상황이다. 아래 중 하나라도 시작하기 전에는 라이선스를 다시 검토한다.

- 실행 파일이나 소스 코드를 타인에게 전달
- 외부 테스터에게 설치본 제공
- 앱 기능을 네트워크 서비스로 제공
- 판매, 후원, 투자, 법인 또는 공동 개발로 전환
- 비공개 소스 상태로 제품을 공개

그때는 GPL/AGPL 조건에 맞춰 해당 소스를 제공하는 방법과 JUCE·Tracktion의 상용 라이선스를 구입하는 방법 중 하나를 선택한다. JUCE와 Tracktion은 별도 제품이므로 각각의 조건을 모두 충족해야 한다. 이 문단은 프로젝트 운영 방침이며 법률 자문은 아니다.

공식 자료:

- [JUCE 라이선스 선택과 가격](https://juce.com/get-juce/)
- [JUCE 9 라이선스 약관](https://juce.com/legal/juce-9-licence/)
- [Tracktion Engine 라이선스와 가격](https://engine.tracktion.com/)
- [Tracktion Engine 이용 약관](https://engine.tracktion.com/agreement)
- [GNU GPL FAQ: 사적 사용과 배포](https://www.gnu.org/licenses/gpl-faq.en.html#GPLRequireSourcePostedPublic)

## 현재 개발 환경

2026-10-11에 이 PC에서 확인한 상태다.

| 도구 | 확인된 상태 | 사용 방침 |
| --- | --- | --- |
| 운영체제 | Windows x64, 빌드 `10.0.26200.0` | Windows x64를 기준으로 테스트한다. |
| PowerShell | 7.6.5 | 저장소 관리와 빌드 명령에 사용한다. |
| Git | 2.47.0.windows.2 | 사용 가능 |
| CMake | 3.31.3 | 사용 가능 |
| Visual Studio | Community 2022 17.11.4, Community 2026 18.7.2 | CMake 호환성이 확인된 VS 2022 생성기를 기준으로 한다. |
| MSVC C++ 도구 | 설치됨, 최신 확인 버전 14.41.34120 | 일반 PowerShell의 `PATH`에 `cl.exe`가 없어도 CMake가 VS 설치를 선택하므로 정상이다. |
| Windows SDK | 10.0.22621.0, 10.0.26100.0 | 최신 설치본을 사용한다. |
| Ninja | 미설치 | VS 생성기를 쓰므로 현재는 설치하지 않는다. |
| .NET | SDK 8.0.400 포함 | 기존 WinForms 프로토타입에만 사용한다. |

Visual Studio 2026도 설치되어 있지만 현재 CMake 3.31.3이 제공하는 확정 생성기는 `Visual Studio 17 2022`다. 초기 빌드는 VS 2022로 통일한다.
두 Visual Studio 설치의 PDB 도구 충돌을 피하기 위해 Debug 빌드는 CMake에서 MSVC 내장 디버그 정보(`/Z7`)를 사용한다.

네이티브 프로젝트가 추가되면 사용할 표준 명령은 다음과 같다.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target MusicBitMaker
cmake --build build --config Release --target MusicBitMaker
```

실행 파일과 자체 테스트 경로는 다음과 같다.

```powershell
.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe
.\build\MusicBitMaker_artefacts\Debug\MusicBitMaker.exe --self-test
```

현재 C# 프로토타입은 다음 명령으로 확인할 수 있다.

```powershell
dotnet build
dotnet run
dotnet run -- --self-test
```

## 제품 데이터 구조

Tracktion Engine의 `Edit`를 프로젝트 상태의 기준으로 삼는다. UI 위젯에 음악 데이터를 따로 복제하지 않는다.

- 프로젝트: 템포, 박자표, 트랙과 마스터 버스
- 트랙: 악기 또는 오디오 레이어, 음량, 팬, 뮤트, 솔로
- MIDI 클립: 시작 시간, 길이, 노트, 벨로시티
- 오디오 클립: 파일 참조, 시작 지점, 길이, 게인
- Transport: 재생, 정지, 녹음, 재생 헤드 위치
- 플러그인 체인: 악기와 이펙트
- 자동화: 시간에 따른 파라미터 변화

건반 입력은 `A S D F G H J K`를 기본 C 장음계로 매핑한다. 이후 옥타브 이동, 키 재지정, MIDI 장치 입력과 벨로시티를 추가한다.

## 생성 악기와 음원 정책

초기 악기는 외부 녹음 파일을 사용하지 않고 Tracktion Engine의 `FourOsc` 소프트웨어 신시사이저로 직접 합성한다.

- `Generated Keys`: 부드러운 기본 건반
- `Retro Pulse`: 짧고 또렷한 펄스파 계열 음색
- `8-Bit Lead`: 사각파를 겹친 레트로 멜로디 음색
- `Space Beep`: 딜레이와 리버브가 적용된 독자적인 SF 전자음
- `Cosmic Pad`: 느린 어택과 긴 릴리스의 넓은 배경 음색
- 원본 드럼머신 녹음, 상용 샘플 팩, 추출 음원은 사용하지 않음
- Roland 명칭, 로고, 제품 외형을 앱의 브랜드나 UI로 사용하지 않음

오실레이터, 파형, 엔벌로프, 필터, LFO, 이펙트의 뜻과 현재 프리셋을 분석한 입문 자료는 [신시사이저 기초 문서](docs/synthesis-basics.md)에 정리했다.

`808-style`은 소리의 성격을 설명하는 표현으로만 사용한다. 기존 808 샘플 파일은 짧더라도 특정 녹음물이므로 출처와 라이선스를 모르면 가져오지 않는다. 미국 저작권청도 음원 저작권이 고정된 특정 소리의 배열과 그 연주·제작을 보호한다고 설명한다. 이 방침은 제3자 녹음물 사용 위험을 줄이기 위한 프로젝트 기준이며 법률 자문은 아니다.

- [미국 저작권청: Sound Recordings](https://www.copyright.gov/register/sr-definition.html)
- [미국 저작권청: Sampling, Interpolations, Beats Stores and More](https://www.copyright.gov/music-modernization/educational-materials/)
- [Roland의 공식 TR-808 소프트웨어 안내](https://www.roland.com/RolandComSite/media/global/release/pdf/2018/20180305_02_TR808_software.pdf)

## 구현 단계

### 1. 네이티브 기반과 건반

- [x] JUCE 창과 오디오 장치 초기화
- [x] 화면 건반과 `A S D F G H J K` 입력
- [x] `W E T Y U` 검은 건반과 장조·자연단조 음계 강조
- [x] 지연 없이 음을 내고 키를 놓으면 Note Off 처리
- [x] Tracktion `Edit` 생성, 재생·정지 구현

### 2. MIDI 타임라인

- [x] 16초 타임라인에 건반과 킥을 MIDI 클립으로 녹음
- [x] Tracktion Transport 기반 재생 헤드와 클릭 위치 이동
- [x] 노트 선택, 시간·음높이 이동, 길이 변경, 삭제
- [x] Tracktion `.tracktionedit` 프로젝트 저장과 불러오기
- [x] File 메뉴에서 새 프로젝트, 저장, 다른 이름으로 저장, 불러오기
- [ ] 확대·축소, 스크롤
- 템포와 그리드 스냅

### 3. 다중 트랙과 레이어 녹음

- [x] 악기 트랙 추가·삭제·선택·이름 변경
- [x] 재생 중 선택한 다른 트랙에 레이어 녹음
- [x] 트랙별 생성 악기 프리셋 선택
- [x] 트랙별 음량, 팬, 뮤트, 솔로
- [x] 트랙·악기·믹서 상태를 프로젝트에 저장하고 다시 열기
- [x] 건반을 별도 도구 창으로 분리하고 선택 트랙에 따라 활성화

### 4. 믹싱과 출력

- 트랙별 이펙트 체인과 마스터 버스
- 기본 EQ, 컴프레서, 리미터, 레벨 미터
- WAV 렌더링
- 오디오 파일 가져오기와 라이선스 메타데이터 기록

VST3 호스팅, ASIO, 복잡한 자동화, 고급 마스터링은 위 기본 흐름이 안정된 뒤 추가한다.

## 실시간 오디오 규칙

- 오디오 콜백에서 파일 입출력, 동적 메모리 할당, UI 호출, 긴 잠금, 로그 출력을 하지 않는다.
- UI와 오디오 상태는 안전한 메시지 또는 엔진이 제공하는 상태 모델을 통해 교환한다.
- 재생 시간의 기준은 UI 타이머가 아니라 Tracktion Transport다.
- 오디오 파일과 플러그인 검색은 백그라운드에서 수행한다.
- 무료 음원은 출처, 저작자, 라이선스, 표시 의무를 프로젝트와 함께 기록한다.

## 저장소 상태와 다음 작업

`CMakeLists.txt`와 `src/Main.cpp`가 네이티브 앱의 시작점이다. `Program.cs`와 `MusicBitMaker.csproj`는 이전 WinForms 검증용 프로토타입이며 네이티브 건반 동작을 확인할 때까지 참고용으로 보존한다.

Tracktion Engine은 `external/tracktion_engine` Git 서브모듈에 `v3.2.0`으로 고정했다. 현재 네이티브 앱은 JUCE 창, Tracktion 생성 악기, 다중 트랙, 레이어 녹음, 트랙별 믹서, MIDI 클립 녹음, 재생·정지, 재생 헤드, 노트 편집·삭제, 프로젝트 저장·불러오기를 제공한다. 모든 트랙은 왼쪽 트랙명과 오른쪽 타임라인으로 한 화면에 쌓이며, 신시사이저는 음계 레인으로 크게, 드럼은 단일 레인으로 작게 표시된다. 트랙 사이 경계선을 위아래로 드래그하면 높이가 조절되고 프로젝트에 저장된다.

`+ ADD TRACK`은 신시사이저와 드럼 악기를 선택한 뒤 트랙을 추가한다. 트랙을 마우스 오른쪽 버튼으로 누르면 이름 변경, 악기 변경, 삭제 메뉴가 열린다. 신시사이저와 드럼 사이에서 종류를 바꾸면 확인 후 해당 트랙의 MIDI 데이터가 초기화되며, 신시사이저 프리셋 변경과 드럼 종류 변경은 기존 패턴을 유지한다.

왼쪽의 `+ ADD TRACK`과 트랙 우클릭 메뉴로 트랙을 관리하고 트랙 행을 클릭해 녹음·편집 대상을 선택한다. 선택한 트랙은 Volume, Pan, Mute, Solo로 믹싱한다. 재생 중 다른 트랙을 선택해 건반을 녹음하면 기존 음악 위에 새 레이어가 추가된다.

타임라인에서 노트 본체를 드래그하면 시간과 음높이를 바꾸고, 선택된 노트 오른쪽의 흰색 손잡이를 드래그하면 길이를 바꾼다. `Delete` 또는 `DELETE NOTE`로 삭제한다. File 메뉴와 `Ctrl+N`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+O`는 Tracktion 원본 `.tracktionedit` 파일을 사용하며 새 프로젝트는 트랙이 없는 상태로 시작한다. 새 프로젝트 저장 창은 마지막으로 저장한 폴더를 기억하며, 해당 폴더가 없으면 Windows 문서 폴더에서 열린다. 건반 창에서 `A S D F G H J K`는 흰 건반, `W E T Y U`는 검은 건반을 연주한다. Root와 Scale에서 장조·자연단조를 선택하면 해당 음이 강조되며 설정은 프로젝트에 저장된다. 건반은 건반 트랙에서 활성화되고 드럼 트랙에서는 비활성화된다. X 버튼으로 닫고 `View > Keyboard Pad`에서 같은 창을 다시 열 수 있다. 앱은 한 번만 실행되므로 건반 창도 최대 하나만 존재한다. 다음 작업은 드럼 패드를 별도 입력 창으로 분리하는 것이다.
