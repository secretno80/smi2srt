# subConverter

Windows 탐색기 컨텍스트 메뉴에서 .smi/.srt/.ass 파일을 여러 개 한 번에 변환하는 도구입니다.

## 구현된 기능

1. `.smi`/`.srt`/`.ass` 파일을 `ToSmi`/`ToSrt`/`ToAss`로 변환
2. 결과 파일은 원본과 같은 폴더에 생성
3. 인코딩 자동 감지(BOM UTF-8/UTF-16, UTF-8 추정, CP949/ACP fallback)
4. SMI `SYNC`를 기준으로 SRT 구간 생성 및 기본 싱크 보정
5. 언어 감지 후 파일명에 언어 태그 반영 (`.ko.srt`, `.en.srt`, `.jp.srt`, 미확정은 `.srt`)
   - SMI의 `class`(예: KRCC/ENCC)는 어떤 캡션들이 같은 트랙인지 묶는 용도로만 쓰고,
     그 클래스 이름 자체(예: "ENCC"라서 영어)는 신뢰하지 않음 — 실제로 한글만 들어있는데
     class가 ENCC로만 잘못 표기된 SMI가 있어, 클래스명과 무관하게 항상 실제 텍스트로
     언어를 판별
   - 같은 트랙(SRT/ASS는 파일 전체가 한 트랙) 안에서 5번째 자막 즈음부터 여러 줄
     (모자라면 트랙 전체)을 태그 제거된 순수 텍스트로 샘플링해 언어별 글자 수 비율이
     가장 높은 언어 하나로 그 트랙 전체에 일괄 적용 (한 줄 한 줄 개별 판별하지 않음 →
     특정 대사 한두 줄이 외국어라는 이유로 별도 파일로 쪼개지는 것을 방지)
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
16. SMI→SRT/ASS 변환 시 자막 구간(duration) 최적화: 파싱 단계에서 종료 시각을 다음 SYNC 경계로 미리 확정하여 비정상적으로 긴 구간 방지
17. 내용 없는 언어 트랙 필터링: 빈 자막 항목만 있는 언어는 파일 생성 안 함 (예: 모두 `&nbsp;`인 영문)
18. 크레딧(제작자 코멘트) 자동 제거를 모든 입력 포맷(smi/srt/ass)에 적용, 탐지 패턴 확장
    (이메일 주소, `Downloaded from` 류 문구, `자막:`/`번역:` 콜론 라벨, `SUB 변환 홍길동`처럼
    "by" 없이 제작 관련 단어 두 개가 겹치는 짧은 문장까지 인식)
19. `.ass` 변환 시 동일 폴더에서 매칭된 동영상의 실제 해상도를 읽어 `PlayResX`/`PlayResY`를
    영상 해상도에 맞추고 기본 폰트 크기(24pt 기준, 720p)를 해상도 비율로 확대/축소
    (mp4/mov/m4v는 ISO-BMFF `tkhd`, mkv/webm은 EBML `PixelWidth`/`PixelHeight`를 직접 파싱,
    그 외 포맷이거나 매칭된 영상이 없으면 기존 1280x720/24pt 기본값 사용)
20. 컨텍스트 메뉴에서 "이름변경(정규식)" 실행 시, 선택한 파일과 같은 폴더의 파일 목록을 불러와
    정규식 패턴/치환 문자열로 파일명을 일괄 변경하는 다이얼로그 제공 (미리보기 후 적용)
21. 실행 파일과 설치 프로그램에 아이콘 적용(`icon.png` → `icon.ico`), Windows
    "프로그램 추가/제거" 목록에도 아이콘 표시

## ASS 변환 형식 개선사항

- 폰트 크기: 기본 24pt(720p 기준), 매칭된 동영상이 있으면 그 해상도 비율로 자동 확대/축소
  (예: 1080p 영상이면 36pt, 360p 영상이면 12pt)
- 스크립트 정보: `PlayResX`/`PlayResY`를 매칭된 동영상의 실제 해상도로 설정(없으면 `1280x720`
  기본값) → 플레이어의 일관된 스케일링과 적절한 폰트 크기 보장
- 문자 인코딩: UTF-8 표준화 (`Encoding: 0`) → 한글/다국어 호환성 증대

## 이름변경(정규식) 기능

컨텍스트 메뉴의 "subConverter - 이름변경(정규식)"를 실행하면, 선택한 파일과 같은 폴더의
전체 파일 목록을 불러오는 다이얼로그가 열립니다. 정규식 패턴과 치환 문자열(`$1`, `$2` 등
그룹 참조 가능)을 입력하면 실시간으로 변경 결과를 미리보기로 보여주고, "변경 적용"을 누르면
일괄 이름변경을 수행합니다. 대소문자 구분 여부를 선택할 수 있고, 이름이 중복되면
`파일명 (n).확장자` 형태로 자동 처리됩니다.

명령행에서는 `subConverter.exe /mode:rename <파일들...>` 형태로 호출되며, 탐색기에서
여러 파일을 동시에 선택해도 변환 기능과 동일한 다중 인스턴스 집계 로직으로 다이얼로그가
한 번만 뜹니다.

## 프로젝트 구조

- `src/main.cpp`: 변환 프로그램 본체
- `src/resource.rc` / `src/resource.h`: 앱 아이콘 및 이름변경 다이얼로그 리소스
- `icon.png` / `icon.ico`: 앱 아이콘 원본 및 실행파일에 내장되는 다중 해상도 아이콘
- `build.bat`: MinGW-w64 기반 빌드 스크립트 (windres로 리소스 컴파일 후 g++로 링크)
- `install_context_menu.cmd`: 현재 사용자(HKCU) 컨텍스트 메뉴 등록
- `uninstall_context_menu.cmd`: 컨텍스트 메뉴 해제
- `setup.iss`: Inno Setup 설치 스크립트

## 빌드

1. MinGW-w64 `g++`/`windres` 설치 및 PATH 설정
2. 프로젝트 루트에서 `build.bat` 실행

빌드 산출물:

- `build/subConverter.exe` (아이콘 내장)

## 컨텍스트 메뉴 등록/해제

- 등록: `install_context_menu.cmd`
- 해제: `uninstall_context_menu.cmd`

테스트 자동화를 위해 `subConverter.exe /silent /to:srt <파일들...>` 옵션을 지원합니다.
(` /silent` 사용 시 결과 메시지박스 미표시)

대상 포맷 인자:

- `/to:smi`
- `/to:srt`
- `/to:ass`
- `/mode:rename` (이름변경(정규식) 다이얼로그 실행)

멀티스레드 스레드 수는 기본적으로 CPU 논리 코어 수(입력 파일 수 상한)로 자동 설정됩니다.
필요 시 환경 변수 `SUBCONVERTER_THREADS`로 스레드 수를 지정할 수 있습니다.

등록 키:

- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToSmi`
- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToSrt`
- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.ToAss`
- `HKCU\Software\Classes\SystemFileAssociations\.<ext>\shell\subConverter.Rename`

참고(Windows 11):

- 새 컨텍스트 메뉴 UI에 맞춰 1차 메뉴 `subConverter - ToSmi/ToSrt/ToAss/이름변경(정규식)`만 등록합니다.
- 상위 `subConverter`(무동작) 항목은 등록하지 않습니다.

명령 인자는 Explorer 호환성을 위해 `%1` 형식으로 등록하며,
다중 선택 시 프로세스 집계 로직으로 선택 파일들을 한 번에 처리합니다.

## 설치 프로그램 생성(Inno Setup)

1. `build.bat` 실행으로 `build/subConverter.exe` 생성
2. Inno Setup에서 `setup.iss` 컴파일
3. 설치 파일은 `Output/subConverter_setup.exe`로 생성

설치 시 컨텍스트 메뉴 등록, 제거 시 자동 해제됩니다.