@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem 轻映 QingYing — one-click CMake build
rem Usage:
rem   build.bat              Release (default)
rem   build.bat Debug
rem   build.bat Release clean
rem   build.bat clean        clean + Release

cd /d "%~dp0"

set "CONFIG=Release"
set "DO_CLEAN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="Release" set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="Debug" set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="clean" set "DO_CLEAN=1" & shift & goto parse_args
if /I "%~1"=="Clean" set "DO_CLEAN=1" & shift & goto parse_args
echo [ERROR] Unknown argument: %~1
echo Usage: build.bat [Release^|Debug] [clean]
exit /b 1
:args_done

where cmake >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cmake not found. Install CMake and add it to PATH.
  exit /b 1
)

if "%DO_CLEAN%"=="1" (
  echo [INFO] Cleaning build\
  if exist "build" rmdir /s /q "build"
)

set "GENERATOR="
set "ARCH=-A x64"

rem Prefer generators known to work on this machine (2019 first, then 2022)
cmake -G "Visual Studio 16 2019" -A x64 -S . -B build >nul 2>&1
if not errorlevel 1 (
  set "GENERATOR=Visual Studio 16 2019"
  goto configure
)

if exist "build\CMakeCache.txt" del /f /q "build\CMakeCache.txt" >nul 2>&1

cmake -G "Visual Studio 17 2022" -A x64 -S . -B build >nul 2>&1
if not errorlevel 1 (
  set "GENERATOR=Visual Studio 17 2022"
  goto configure
)

if exist "build\CMakeCache.txt" del /f /q "build\CMakeCache.txt" >nul 2>&1

rem Fallback: let CMake pick the default generator
set "GENERATOR="
set "ARCH="
echo [WARN] VS 2019/2022 generators not found, using CMake default generator.

:configure
echo [INFO] Configure  config=%CONFIG%
if defined GENERATOR (
  echo [INFO] Generator: %GENERATOR% ^(%ARCH:~3%^)
  cmake -S . -B build -G "%GENERATOR%" %ARCH%
) else (
  cmake -S . -B build
)
if errorlevel 1 (
  echo [ERROR] CMake configure failed.
  exit /b 1
)

echo [INFO] Build %CONFIG% ...
cmake --build build --config %CONFIG% --parallel
if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

set "EXE=build\bin\%CONFIG%\qingying.exe"
if not exist "%EXE%" (
  rem Some generators put output differently
  set "EXE=build\%CONFIG%\qingying.exe"
)

if exist "%EXE%" (
  echo.
  echo [OK] Build succeeded.
  echo      %CD%\%EXE%
) else (
  echo.
  echo [OK] Build finished, but exe path not at the expected location.
  echo      Please check build\ for qingying.exe
)

exit /b 0
