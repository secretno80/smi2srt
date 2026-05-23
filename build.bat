@echo off
setlocal

set ROOT=%~dp0
set SRC=%ROOT%src\main.cpp
set OUTDIR=%ROOT%build
set EXE=%OUTDIR%\subConverter.exe

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

where g++ >nul 2>nul
if errorlevel 1 (
  echo [ERROR] g++ 를 찾을 수 없습니다. MinGW-w64를 설치하고 PATH를 설정하세요.
  exit /b 1
)

echo [BUILD] %EXE%
g++ -std=c++17 -O2 -Wall -Wextra -o "%EXE%" "%SRC%" -static -mwindows -municode
if errorlevel 1 (
  echo [ERROR] 빌드에 실패했습니다.
  exit /b 1
)

echo [OK] 빌드 완료: %EXE%
exit /b 0
