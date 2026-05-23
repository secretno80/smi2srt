# subConverter

Windows 탐색기 컨텍스트 메뉴에서 .smi/.srt/.ass 파일을 여러 개 한 번에 변환하는 도구입니다.

## 구현된 기능

1. `.smi`/`.srt`/`.ass` 파일을 `ToSmi`/`ToSrt`/`ToAss`로 변환
2. 결과 파일은 원본과 같은 폴더에 생성
3. 인코딩 자동 감지(BOM UTF-8/UTF-16, UTF-8 추정, CP949/ACP fallback)
4. SMI `SYNC`를 기준으로 SRT 구간 생성 및 기본 싱크 보정
5. 언어 감지 후 파일명에 언어 태그 반영 (`.ko.srt`, `.en.srt`, `.jp.srt`, 미확정은 `.srt`)
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

## ASS 변환 형식 개선사항

- 폰트 크기: 28pt (표준 크기, 이전 36pt에서 개선)
- 스크립트 정보: 해상도 메타데이터 추가 (`PlayResX: 1280`, `PlayResY: 720`) → 플레이어의 일관된 스케일링 보장
- 문자 인코딩: UTF-8 표준화 (`Encoding: 0`) → 한글/다국어 호환성 증대

## 프로젝트 구조

- `src/main.cpp`: 변환 프로그램 본체
- `build.bat`: MinGW-w64 기반 빌드 스크립트
- `install_context_menu.cmd`: 현재 사용자(HKCU) 컨텍스트 메뉴 등록
- `uninstall_context_menu.cmd`: 컨텍스트 메뉴 해제
- `setup.iss`: Inno Setup 설치 스크립트

## 빌드

1. MinGW-w64 `g++` 설치 및 PATH 설정
2. 프로젝트 루트에서 `build.bat` 실행

빌드 산출물:

- `build/subConverter.exe`

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

명령 인자는 Explorer 호환성을 위해 `%1` 형식으로 등록하며,
다중 선택 시 프로세스 집계 로직으로 선택 파일들을 한 번에 처리합니다.

## 설치 프로그램 생성(Inno Setup)

1. `build.bat` 실행으로 `build/subConverter.exe` 생성
2. Inno Setup에서 `setup.iss` 컴파일
3. 설치 파일은 `Output/subConverter_setup.exe`로 생성

설치 시 컨텍스트 메뉴 등록, 제거 시 자동 해제됩니다.