@echo off
chcp 65001 >nul
setlocal

set ROOT=%~dp0
set EXE=%ROOT%build\subConverter.exe
set ICO=%ROOT%icon.ico

if not exist "%EXE%" (
  echo [ERROR] %EXE% 파일이 없습니다. 먼저 build.bat 을 실행하세요.
  exit /b 1
)

if not exist "%ICO%" (
  echo [ERROR] %ICO% 파일이 없습니다.
  exit /b 1
)

for %%E in (.smi .srt .ass) do (
  call :REGISTER_ONE "%%E"
)

echo [OK] .smi/.srt/.ass 컨텍스트 메뉴 등록 완료
exit /b 0

:REGISTER_ONE
set "EXT=%~1"
set "ROOT=HKCU\Software\Classes\SystemFileAssociations\%EXT%\shell"

reg delete "%ROOT%\subConverter" /f >nul 2>nul
reg delete "%ROOT%\subConverter.Rename" /f >nul 2>nul

reg add "%ROOT%\subConverter.ToSmi" /ve /t REG_SZ /d "subConverter - ToSmi" /f >nul
reg add "%ROOT%\subConverter.ToSmi" /v "Icon" /t REG_SZ /d "\"%ICO%\"" /f >nul
reg add "%ROOT%\subConverter.ToSmi" /v "MultiSelectModel" /t REG_SZ /d "Player" /f >nul
reg add "%ROOT%\subConverter.ToSmi\command" /ve /t REG_SZ /d "\"%EXE%\" /to:smi \"%%1\"" /f >nul

reg add "%ROOT%\subConverter.ToSrt" /ve /t REG_SZ /d "subConverter - ToSrt" /f >nul
reg add "%ROOT%\subConverter.ToSrt" /v "Icon" /t REG_SZ /d "\"%ICO%\"" /f >nul
reg add "%ROOT%\subConverter.ToSrt" /v "MultiSelectModel" /t REG_SZ /d "Player" /f >nul
reg add "%ROOT%\subConverter.ToSrt\command" /ve /t REG_SZ /d "\"%EXE%\" /to:srt \"%%1\"" /f >nul

reg add "%ROOT%\subConverter.ToAss" /ve /t REG_SZ /d "subConverter - ToAss" /f >nul
reg add "%ROOT%\subConverter.ToAss" /v "Icon" /t REG_SZ /d "\"%ICO%\"" /f >nul
reg add "%ROOT%\subConverter.ToAss" /v "MultiSelectModel" /t REG_SZ /d "Player" /f >nul
reg add "%ROOT%\subConverter.ToAss\command" /ve /t REG_SZ /d "\"%EXE%\" /to:ass \"%%1\"" /f >nul

exit /b 0
