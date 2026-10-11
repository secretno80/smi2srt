# subConverter

Windows 탐색기 컨텍스트 메뉴에서 .smi/.srt/.ass 파일을 여러 개 한 번에 변환하는 도구입니다.

## 변환 라이브러리

자막 형식의 파싱·직렬화는 자체 구현하지 않고 검증된 라이브러리에 맡깁니다.

| 역할 | 라이브러리 |
| --- | --- |
| SRT/ASS 읽기·쓰기, SAMI(SMI) 읽기, 타임스탬프 처리 | [pysubs2](https://github.com/tkarabela/pysubs2) (MIT) |
| SAMI(SMI) 쓰기 | [pycaption](https://github.com/pbs/pycaption) (Apache-2.0) — pysubs2는 SAMI 쓰기 미지원 |
| SMI `<STYLE>` CSS 해석 | [tinycss2](https://github.com/Kozea/tinycss2) (BSD) |
| `<font color>` 색상명/HEX 해석 | [webcolors](https://github.com/ubernostrum/webcolors) (BSD) |

pysubs2의 SAMI 읽기는 한 SYNC 안의 `<P Class=KRCC>`/`<P Class=ENCC>`를 한 줄로 합치고
`<font color>`를 무시하며 `</BODY>`가 없으면 마지막 자막을 버리므로, `SAMIParser`를 상속해
이 세 가지만 보강합니다(`engine/subconverter/formats.py`의 `TrackedSamiParser`).
그 외 크레딧 제거, 언어 판별·파일명, 영상 매칭, 같은 형식 원본 보존은 subConverter 자체 규칙입니다.

> 이전 자체 변환기에는 시간 표기에서 1시간을 2,400,000ms(40분)로 계산하는 버그가 있어,
> 40분 이후 자막의 시점이 모두 틀어졌습니다(예: 45:00 → `01:05:00`). 라이브러리 전환으로 해소되었습니다.

## 구현된 기능

1. `.smi`/`.srt`/`.ass` 파일을 `ToSmi`/`ToSrt`/`ToAss`로 변환
2. 결과 파일은 원본과 같은 폴더에 생성
3. 인코딩 자동 감지(BOM UTF-8/UTF-16, UTF-8 추정, CP949/ACP fallback)
4. SMI `SYNC`를 기준으로 자막 구간 생성 — SAMI 규칙대로 각 자막은 다음 `SYNC` 시점까지 표시
5. 언어 감지 후 파일명에 언어 태그 반영 (`.ko.srt`, `.en.srt`, `.jp.srt`, 미확정은 `.srt`)
   - SMI의 `class`(예: KRCC/ENCC)는 어떤 캡션들이 같은 트랙인지 묶는 용도로만 쓰고,
     그 클래스 이름 자체(예: "ENCC"라서 영어)는 신뢰하지 않음 — 실제로 한글만 들어있는데
     class가 ENCC로만 잘못 표기된 SMI가 있어, 클래스명과 무관하게 항상 실제 텍스트로
     언어를 판별
   - 같은 트랙(SRT/ASS는 파일 전체가 한 트랙) 안에서 5번째 자막 즈음부터 여러 줄
     (모자라면 트랙 전체)을 태그 제거된 순수 텍스트로 샘플링해 언어별 글자 수 비율이
     가장 높은 언어 하나로 그 트랙 전체에 일괄 적용 (한 줄 한 줄 개별 판별하지 않음 →
     특정 대사 한두 줄이 외국어라는 이유로 별도 파일로 쪼개지는 것을 방지)
   - 원본 파일명(또는 매칭된 영상 파일명) 끝에 이미 `.ko`/`.en`/`.jp` 언어 코드가 붙어
     있으면 새로 붙일 언어 코드 이전에 먼저 제거 — `Show.ko.smi` → `Show.ko.srt`
     (기존에는 `Show.ko.ko.srt`처럼 중복 부착됨). 파일명의 기존 태그와 실제 감지된
     언어가 다르면(예: `Show.en.smi`인데 실제 내용은 한글) 옛 태그를 버리고 감지된
     언어로 교체됨
   - 같은 형식 변환(`ToSmi`로 `.smi`, `ToSrt`로 `.srt` 등)도 내용은 원본 그대로 두되(크레딧만 제거),
     실제 텍스트로 언어를 판별해 파일명에 언어 코드를 붙임 — `Movie.srt` → `Movie.ko.srt`.
     한 파일에 여러 언어 트랙이 있는 SMI는 파일을 나눌 수 없으므로 첫 번째 트랙(플레이어가 기본으로
     표시하는 트랙)의 언어를 사용
6. 컨텍스트 메뉴 1차 메뉴(`subConverter - ToSmi/ToSrt/ToAss`) 지원
8. 다중 선택 처리(`%1` + 프로세스 집계)로 여러 파일 일괄 변환
9. 변환 완료 후 성공/실패 개수 요약 알림(0개 항목은 미표시)
10. 자막 시작/끝 구간의 제작자 크레딧(`smi by`, `sub by` 등) 자동 제거
11. 여러 `.smi` 파일 변환 시 멀티스레드 병렬 처리로 고속 변환
12. UNC 네트워크 경로(`\\server\share\...`) 및 긴 경로 환경에서 변환 안정성 개선
13. 동일 폴더의 영상 파일명과 자막 파일명을 비교하여 연도·에피소드·토큰 유사도로 매칭,
    결과 `.srt` 파일명을 매칭된 영상 파일명 기준으로 저장
    (영상 파일이 없거나 매칭 신뢰도가 낮으면 원본 자막 파일명 유지)
14. 출력 파일명이 이미 존재하면 `파일명 (n).확장자` 형태로 자동 생성
15. 자막 파일이 아닌 입력은 자동 건너뜀
16. SMI→SRT/ASS 변환 시 종료 시각은 다음 SYNC 경계로 확정하여 비정상적으로 긴 구간 방지
17. 내용 없는 언어 트랙 필터링: 빈 자막 항목만 있는 언어는 파일 생성 안 함 (예: 모두 `&nbsp;`인 영문)
18. 크레딧(제작자 코멘트) 자동 제거를 모든 입력 포맷(smi/srt/ass)에 적용, 탐지 패턴 확장
    (이메일 주소, `Downloaded from` 류 문구, `자막:`/`번역:` 콜론 라벨, `SUB 변환 홍길동`처럼
    "by" 없이 제작 관련 단어 두 개가 겹치는 짧은 문장까지 인식)
19. `.ass` 변환 시 스크립트 해상도·폰트 크기는 FFmpeg이 SRT/SMI→ASS 변환 시 쓰는 표준 헤더
    (`PlayResX/PlayResY` 384x288, 폰트 크기 16, 외곽선 1)를 따름. 플레이어가 스크립트 해상도를 영상
    크기에 맞춰 확대하므로 영상 해상도와 관계없이 화면 대비 같은 비율로 표시됨(영상 해상도를 읽어 폰트
    크기를 계산하던 자체 기준은 제거)
20. 실행 파일과 설치 프로그램에 아이콘 적용(`icon.png` → `icon.ico`), Windows
    "프로그램 추가/제거" 목록에도 아이콘 표시
21. 다중 선택 시 프로세스 집계(수집) 대기를 고정 시간이 아닌 "큐 파일이 더 이상 커지지
    않을 때까지" 적응형으로 처리 — 탐색기가 파일이 많을 때(10개 이상) 프로세스를 한 번에
    띄우지 않고 여러 번에 나눠 띄우는 경우에도, 뒤늦게 들어오는 파일까지 같은 배치로
    묶어 하나의 결과 요약으로 처리 (고정 대기였을 때는 24개 선택 시 12개/9개/3개처럼
    여러 배치로 쪼개지는 문제가 있었음)
22. S01E05/EP05/E05 같은 표시가 전혀 없는 "맨 숫자" 파일명끼리도(예: `쇼.05.smi` ↔
    `RandomTitle_05_1080p.mkv`) 같은 폴더의 자막·영상을 한 번에 여러 개 선택해 변환하면
    n:n으로 에피소드 매칭 — 각 파일명의 숫자들을 위치별로 정렬해, 파일마다 값이 겹치는
    (해상도처럼 모든 파일에서 동일한) 열은 제외하고, 남은 후보 중 선택한 파일 개수만큼
    연속된 정수 수열을 이루는 열을 에피소드 번호로 확정한 뒤 영상 쪽도 동일한 방식으로
    추정해 같은 번호끼리 매칭. 수열이 반드시 1부터 시작할 필요는 없어서, 예를 들어
    자막은 05~10화만 있고 영상은 01~20화가 모두 있어도 자막 6개가 영상 쪽 05~10화와
    정확히 매칭됨 (동일 폴더의 자막 전체를 선택했을 때만 적용 — 일부만 선택하면
    "연속 수열" 전제가 깨지므로 기존 방식으로 동작)

## ASS 변환 형식

- 스크립트 정보·기본 스타일: FFmpeg 기본 ASS 헤더와 동일한 값(384x288, 16pt, 흰색, 외곽선 1, 하단 중앙,
  여백 10) — 폰트만 한글 글리프가 있는 `맑은 고딕`
- SMI 서식: `<b>`/`<i>`/`<u>`/`<s>`/`<br>`은 pysubs2, `<font color>`는 보강 파서가 ASS 태그로 변환.
  `<font size>`/`face`는 반영하지 않음(기본 스타일 크기 유지)
- SMI `<STYLE>`의 `font-weight`(`P` 또는 클래스 선택자)가 bold면 `Bold` 스타일로 출력
- 문자 인코딩: UTF-8(BOM), 스타일 `Encoding: 0`

## 동영상-자막 파일명 매칭 및 자동 이름 변경

변환 시(13번 항목) 같은 폴더 안의 영상 파일들과 자막 파일명을 비교해 가장 신뢰도 높은
영상을 찾고, 결과 파일명을 그 영상 파일명 기준으로 저장합니다. 매칭 점수는 다음을
조합해 계산합니다.

- 연도 일치 여부 (`19xx`/`20xx`)
- 에피소드 번호 일치 여부 — `S01E05`, `E05`, `EP05` 등 정규식으로 추출
  (자막과 영상 각각에서 에피소드 번호를 추출해 비교하므로, 한 폴더에 여러 회차의
  영상·자막이 섞여 있는 n:n 상황에서도 파일명 스타일이 서로 달라도 에피소드 번호가
  일치하는 쌍끼리 매칭됨 — 예: `Show.EP03.smi` ↔ `MyShow.EP03.1080p.mkv`)
- 정규화된 파일명 토큰(단어) 겹침 개수 (해상도/코덱/배포자 등 노이즈 토큰은 제외)

최고 점수 영상과 그 다음 점수 영상의 격차가 충분히 크지 않거나(즉 애매한 경우) 영상이
없으면, 원본 자막 파일명을 그대로 사용합니다.

## 프로젝트 구조

- `src/main.cpp`: 탐색기 진입점 — 다중 선택 수집 후 변환 엔진을 한 번 실행하고 결과 요약 표시
- `engine/subconverter/`: 변환 엔진(Python)
  - `formats.py`: 라이브러리 연동(pysubs2/pycaption/tinycss2/webcolors), SAMI 트랙 보강 파서, ASS 헤더
  - `convert.py`: 파일 단위 변환 흐름(파싱 → 크레딧 제거 → 언어별 분리 → 쓰기)
  - `credits.py` / `language.py` / `matching.py` / `rawedit.py`: 크레딧 판별, 언어 판별·파일명,
    영상 매칭, 같은 형식 원본 보존 편집
  - `cli.py`: `subConverterEngine --to srt --list 목록.txt --result 결과.txt [파일...]`
- `engine/tests/`: 엔진 단위 테스트(`unittest`)
- `src/resource.rc` / `src/resource.h`: 앱 아이콘 리소스
- `icon.png` / `icon.ico`: 앱 아이콘 원본 및 실행파일에 내장되는 다중 해상도 아이콘
- `build.bat`: MinGW-w64 기반 빌드 스크립트 (windres로 리소스 컴파일 후 g++로 링크)
- `install_context_menu.cmd`: 현재 사용자(HKCU) 컨텍스트 메뉴 등록
- `uninstall_context_menu.cmd`: 컨텍스트 메뉴 해제
- `setup.iss`: Inno Setup 설치 스크립트

## 빌드

1. MinGW-w64 `g++`/`windres`, Python 3.11 이상 설치 및 PATH 설정
2. 프로젝트 루트에서 `build.bat` 실행 — `.venv` 가상환경에 `engine/requirements.txt`와 PyInstaller를
   설치하고, 엔진 단위 테스트 통과 후 엔진을 빌드

빌드 산출물:

- `build/subConverter.exe` (아이콘 내장)
- `build/subConverterEngine/subConverterEngine.exe` (PyInstaller onedir, `subConverter.exe`와 같은 폴더에 배치)

엔진만 따로 실행/테스트:

```
cd engine
python -m subconverter --to srt 파일.smi
python -m unittest discover -s tests -t .
```

개발 중에는 환경 변수 `SUBCONVERTER_ENGINE`에 엔진 exe 경로를 지정하면 그 엔진을 사용합니다.

## 컨텍스트 메뉴 등록/해제

- 등록: `install_context_menu.cmd`
- 해제: `uninstall_context_menu.cmd`

테스트 자동화를 위해 `subConverter.exe /silent /to:srt <파일들...>` 옵션을 지원합니다.
(` /silent` 사용 시 결과 메시지박스 미표시)

대상 포맷 인자:

- `/to:smi`
- `/to:srt`
- `/to:ass`

멀티스레드 스레드 수는 기본적으로 CPU 논리 코어 수(입력 파일 수 상한)로 자동 설정됩니다.
필요 시 환경 변수 `SUBCONVERTER_THREADS`로 스레드 수를 지정할 수 있습니다.

등록 키:

- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToSmi`
- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToSrt`
- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToAss`

참고(Windows 11):

- 새 컨텍스트 메뉴 UI에 맞춰 1차 메뉴 `subConverter - ToSmi/ToSrt/ToAss`만 등록합니다.
- 상위 `subConverter`(무동작) 항목은 등록하지 않습니다.

명령 인자는 Explorer 호환성을 위해 `%1` 형식으로 등록하며, 다중 선택 시 프로세스 집계
로직으로 선택 파일들을 한 번에 처리합니다. 파일이 많을 때 탐색기가 프로세스를 여러
번에 나눠 띄우더라도(21번 항목), 큐 파일이 더 이상 커지지 않을 때까지 적응형으로
대기했다가 한 번에 드레인하므로 배치가 쪼개지지 않습니다.

## 설치 프로그램 생성(Inno Setup)

1. `build.bat` 실행으로 `build/subConverter.exe`, `build/subConverterEngine/` 생성
2. Inno Setup에서 `setup.iss` 컴파일
3. 설치 파일은 `Output/subConverter_setup.exe`로 생성

설치 시 컨텍스트 메뉴 등록, 제거 시 자동 해제됩니다.