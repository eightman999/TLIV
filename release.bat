@echo off
setlocal
rem Release build: clean build -> ISCC -> dist\ (TLIVSetup-<version>.exe, TLIV.exe, Readme.txt, LICENSE)
rem The version comes from src\version.h (through the exe).
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
cd /d "%~dp0"
if not exist "%ISCC%" goto noiscc
call "%~dp0build.bat" clean || goto fail
if exist dist rmdir /s /q dist
mkdir dist
"%ISCC%" /Qp installer\TLIV.iss || goto fail
copy /y build\TLIV.exe dist\TLIV.exe >nul || goto fail
copy /y installer\Readme.txt dist\Readme.txt >nul || goto fail
copy /y LICENSE dist\LICENSE >nul || goto fail
echo.
dir /b dist
exit /b 0
:noiscc
echo ISCC not found
exit /b 1
:fail
echo release failed
exit /b 1
