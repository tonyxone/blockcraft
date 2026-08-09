@echo off
setlocal
rem Build BlockCraft (C++). Requires MSVC Build Tools (2017 or newer).

set VCVARS=
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat" (
  set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VCVARS%"=="" (
  echo Could not find vcvars64.bat - install Visual Studio Build Tools with the C++ workload.
  exit /b 1
)
call "%VCVARS%" >nul

if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0"

set RES=
where rc >nul 2>nul
if not errorlevel 1 (
  rc /nologo /fo build\blockcraft.res icon\blockcraft.rc
  if exist build\blockcraft.res set RES=build\blockcraft.res
)

cl /nologo /std:c++17 /O2 /EHsc /W3 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
  src\*.cpp %RES% /Fo:build\ /Fe:blockcraft.exe ^
  /link opengl32.lib gdi32.lib user32.lib winmm.lib /SUBSYSTEM:WINDOWS
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
