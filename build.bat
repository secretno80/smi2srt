@echo off
chcp 65001 >nul
setlocal

set ROOT=%~dp0
set ROOTNOBS=%~dp0
set ROOTNOBS=%ROOTNOBS:~0,-1%
set SRC=%ROOT%src\main.cpp
set RC=%ROOT%src\resource.rc
set OUTDIR=%ROOT%build
set RES=%OUTDIR%\resource.o
set EXE=%OUTDIR%\subConverter.exe

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

where g++ >nul 2>nul
if errorlevel 1 (
  echo [ERROR] g++ 를 찾을 수 없습니다. MinGW-w64를 설치하고 PATH를 설정하세요.
  exit /b 1
)

where windres >nul 2>nul
if errorlevel 1 (
  echo [ERROR] windres 를 찾을 수 없습니다. MinGW-w64를 설치하고 PATH를 설정하세요.
  exit /b 1
)

echo [RC] %RES%
windres -I "%ROOTNOBS%" -o "%RES%" "%RC%"
if errorlevel 1 (
  echo [ERROR] 리소스 컴파일에 실패했습니다.
  exit /b 1
)

echo [BUILD] %EXE%
g++ -std=c++17 -O2 -Wall -Wextra -o "%EXE%" "%SRC%" "%RES%" -static -mwindows -municode
if errorlevel 1 (
  echo [ERROR] 빌드에 실패했습니다.
  exit /b 1
)

echo [OK] 빌드 완료: %EXE%

set "ISCC="
for /f "delims=" %%I in ('where ISCC 2^>nul') do (
    if not defined ISCC set "ISCC=%%I"
)
if not defined ISCC if exist "C:\Progra~2\Inno Setup 6\ISCC.exe" set "ISCC=C:\Progra~2\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "C:\Progra~1\Inno Setup 6\ISCC.exe" set "ISCC=C:\Progra~1\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "C:\Progra~2\Inno Setup 5\ISCC.exe" set "ISCC=C:\Progra~2\Inno Setup 5\ISCC.exe"
if not defined ISCC if exist "C:\Progra~1\Inno Setup 5\ISCC.exe" set "ISCC=C:\Progra~1\Inno Setup 5\ISCC.exe"

if not defined ISCC (
  echo [WARN] Inno Setup^(ISCC.exe^)을 찾을 수 없어 설치 파일은 만들지 않았습니다.
  exit /b 0
)

echo [INSTALLER] %ROOT%setup.iss
"%ISCC%" "%ROOT%setup.iss"
if errorlevel 1 (
  echo [ERROR] 설치 파일 생성에 실패했습니다.
  exit /b 1
)

echo [OK] 설치 파일 생성 완료
exit /b 0
