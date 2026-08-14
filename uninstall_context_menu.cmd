@echo off
chcp 65001 >nul
setlocal

for %%E in (.smi .srt .ass) do (
	reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\shell\subConverter" /f >nul 2>nul
	reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\shell\subConverter.ToSmi" /f >nul 2>nul
	reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\shell\subConverter.ToSrt" /f >nul 2>nul
	reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\shell\subConverter.ToAss" /f >nul 2>nul
	reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\shell\subConverter.Rename" /f >nul 2>nul
)

reg delete "HKCU\Software\Classes\SystemFileAssociations\.smi\shell\ConvertToSRT" /f >nul 2>nul

echo [OK] .smi/.srt/.ass 컨텍스트 메뉴 등록 해제 완료
exit /b 0
