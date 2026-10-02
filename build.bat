@echo off
setlocal
rem VS 2022 Build Tools (vcvars64 -> rc -> cl). Output: build\TLIV.exe
rem Usage: build.bat [clean] [icons]
rem   clean = delete build\ first
rem   icons = regenerate res\icon_*.png and res\app.ico from images\ (needs Python 3). Normally res\ is used as is.
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set "CLEAN=" & set "ICONS="
for %%A in (%*) do (
  if /i "%%A"=="clean" set "CLEAN=1"
  if /i "%%A"=="icons" set "ICONS=1"
)
if defined CLEAN if exist build rmdir /s /q build
if not exist build mkdir build
if defined ICONS (
  python tools\make_icons.py 2>nul || py tools\make_icons.py || (echo make_icons failed & exit /b 1)
)
rc /nologo /fo build\TLIV.res res\TLIV.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O1 /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:build\ /Fe:build\TLIV.exe ^
  src\main.cpp src\viewer.cpp src\decode.cpp src\filelist.cpp src\ui.cpp src\dialogs.cpp src\inflate.cpp src\meta.cpp src\exiftags.cpp src\infopanel.cpp ^
  src\lang.cpp src\util.cpp src\settings.cpp src\clipboard.cpp src\delete.cpp build\TLIV.res ^
  /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF || exit /b 1
echo.
for %%F in (build\TLIV.exe) do echo TLIV.exe %%~zF bytes
endlocal
