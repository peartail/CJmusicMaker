# 반복 재생 UI 설계

기준 버전: v0.1026

## 용어

- **Loop(루프) / Cycle(사이클)**: 지정한 구간의 끝에 도달하면 시작으로 돌아가 반복 재생하는 기능이다.
- **Loop Range(루프 구간)**: 반복할 시작 시각과 끝 시각 사이의 범위다.
- **Ruler(룰러, 시간 자)**: 모든 트랙 위에서 시간 위치를 공통으로 보여 주는 가로 막대다.
- **Handle(핸들)**: 루프 구간의 시작점이나 끝점을 마우스로 잡아 움직이는 부분이다. 이 프로젝트에서 사용자가 말한 피벗 포인트에 해당한다.

## 상용 DAW 비교

| 제품 | 반복 켜기 | 범위 지정 방식 | 공통점 |
| --- | --- | --- | --- |
| Ableton Live | Control Bar의 Arrangement Loop | 상단 Loop Brace 양끝을 드래그하고 가운데를 끌어 이동 | 토글과 범위 편집을 분리 |
| Logic Pro | Control Bar의 Cycle 버튼 | Ruler의 Cycle Area 양끝 Locator를 드래그하고 가운데를 끌어 이동 | 전 트랙 공통 시간 구간 |
| Cubase | Transport의 Activate Cycle | Ruler의 Left/Right Locator와 그 사이 범위 | 좌우 경계와 범위 강조 |
| REAPER | Transport의 Repeat 버튼 | Timeline을 드래그해 Loop Points 생성 | 재생 토글과 시간 선택을 연결 |

공식 문서:

- [Ableton Live Arrangement Loop](https://www.ableton.com/en/manual/arrangement-view/#the-arrangement-loop)
- [Logic Pro Cycle Area](https://support.apple.com/guide/logicpro/playback-and-navigation-lgcp9ede81be/mac)
- [Cubase Left and Right Locators](https://www.steinberg.help/r/cubase-artist/15.0/en/cubase_nuendo/topics/playback/playback_left_and_right_locator_c.html)
- [REAPER User Guide](https://www.reaper.fm/userguide.php)

## MusicBitMaker의 결정

반복 구간은 트랙별 설정이 아니라 프로젝트 전체 Transport 설정이다. 같은 시간대의 신시사이저·킥·스네어·하이햇을 함께 반복해야 음악의 한 마디나 한 섹션을 정확히 들을 수 있기 때문이다.

- 상단 재생 툴바: `LOOP` / `LOOP ON` 토글
- 전체 트랙 위: `LOOP RANGE` 룰러
- 룰러 빈 곳 드래그: 새 범위 생성
- 왼쪽/오른쪽 경계 드래그: 시작/끝 조절
- 범위 가운데 드래그: 길이를 유지한 채 이동
- 트랙 전체: 선택 범위를 옅게 칠하고 경계선을 표시
- 상태 저장: Tracktion `TransportControl::setLoopRange`와 `looping`을 사용해 `.tracktionedit`에 유지

최소 루프 길이는 0.25초이며 현재 전체 타임라인인 0~16초 안으로 제한한다. 이번 단계는 반복 **재생**만 다룬다. 반복 녹음은 루프 경계를 넘는 MIDI 노트를 여러 번의 take로 분리하는 정책이 필요하므로 후속 단계로 둔다. 데이터 손상을 피하기 위해 현재는 녹음을 시작하면 루프 재생을 끈다.
