@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem 轻映 QingYing — one-click CMake build (+ optional GoogleTest)
rem Usage:
rem   build.bat                 Release (default), with tests
rem   build.bat Debug
rem   build.bat Release clean
rem   build.bat test            Build Release then run ctest
rem   build.bat Debug test
rem   build.bat notest          Build without tests
rem   build.bat clean           clean + Release

cd /d "%~dp0"

set "CONFIG=Release"
set "DO_CLEAN=0"
set "DO_TEST=0"
set "BUILD_TESTS=ON"

rem Relative to this repo's parent: ../thirdparty_install/vcpkg (仓库同级目录，不入库)
set "VCPKG_ROOT=%~dp0..\thirdparty_install\vcpkg"
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="Release" set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="Debug" set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="clean" set "DO_CLEAN=1" & shift & goto parse_args
if /I "%~1"=="Clean" set "DO_CLEAN=1" & shift & goto parse_args
if /I "%~1"=="test" set "DO_TEST=1" & shift & goto parse_args
if /I "%~1"=="Test" set "DO_TEST=1" & shift & goto parse_args
if /I "%~1"=="notest" set "BUILD_TESTS=OFF" & shift & goto parse_args
if /I "%~1"=="NoTest" set "BUILD_TESTS=OFF" & shift & goto parse_args
echo [ERROR] Unknown argument: %~1
echo Usage: build.bat [Release^|Debug] [clean] [test^|notest]
exit /b 1
:args_done

where cmake >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cmake not found. Install CMake and add it to PATH.
  exit /b 1
)

if "%BUILD_TESTS%"=="ON" (
  if not exist "%VCPKG_ROOT%\installed\x64-windows\share\gtest\GTestConfig.cmake" (
    echo [ERROR] GTest not found under:
    echo         %VCPKG_ROOT%\installed\x64-windows
    echo         Install with: vcpkg install gtest:x64-windows
    echo         Or build without tests: build.bat notest
    exit /b 1
  )
)

if "%DO_CLEAN%"=="1" (
  echo [INFO] Cleaning build\
  if exist "build" rmdir /s /q "build"
)

rem Team baseline: require installed Visual Studio 2019 C++ build tools.
rem CMake listing a generator does not prove that the corresponding VS is installed.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer was not found.
  echo         Install Visual Studio 2019 with the Desktop development with C++ workload.
  exit /b 1
)
"%VSWHERE%" -latest -products * -version "[16.0,17.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | findstr /r "." >nul
if errorlevel 1 (
  echo [ERROR] Visual Studio 2019 C++ build tools are required by this project.
  echo         Install the Desktop development with C++ workload, then run this script again.
  exit /b 1
)
set "GENERATOR=Visual Studio 16 2019"
set "ARCH=-A x64"

:configure
echo [INFO] Configure  config=%CONFIG%  tests=%BUILD_TESTS%

set "CMAKE_ARGS=-DQINGYING_BUILD_TESTS=%BUILD_TESTS%"
if "%BUILD_TESTS%"=="ON" (
  rem Toolchain is optional; CMakeLists also appends vcpkg installed prefix.
  set "CMAKE_ARGS=!CMAKE_ARGS! -DQINGYING_VCPKG_ROOT=%VCPKG_ROOT% -DCMAKE_TOOLCHAIN_FILE=%VCPKG_TOOLCHAIN%"
)

if exist "build\CMakeCache.txt" (
  findstr /C:"CMAKE_GENERATOR:INTERNAL=Visual Studio 16 2019" "build\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo [ERROR] build\ was configured with a generator other than Visual Studio 2019.
    echo         Run: build.bat clean
    exit /b 1
  )
)
echo [INFO] Toolchain: Visual Studio 2019 C++ build tools
echo [INFO] Generator: %GENERATOR% ^(x64^)
cmake -S . -B build -G "%GENERATOR%" %ARCH% %CMAKE_ARGS%
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
if not exist "%EXE%" set "EXE=build\%CONFIG%\qingying.exe"

if exist "%EXE%" (
  echo.
  echo [OK] Build succeeded.
  echo      %CD%\%EXE%
) else (
  echo.
  echo [OK] Build finished, but exe path not at the expected location.
  echo      Please check build\ for qingying.exe
)

if "%BUILD_TESTS%"=="ON" (
  set "TEST_EXE=build\bin\%CONFIG%\qingying_tests.exe"
  if exist "!TEST_EXE!" (
    echo      !CD!\!TEST_EXE!
  )
)

if "%DO_TEST%"=="1" (
  if "%BUILD_TESTS%"=="OFF" (
    echo [ERROR] Cannot run tests with notest.
    exit /b 1
  )
  echo.
  echo [INFO] Running ctest ^(%CONFIG%^) ...
  ctest --test-dir build -C %CONFIG% --output-on-failure
  if errorlevel 1 (
    echo [ERROR] Tests failed.
    exit /b 1
  )
  echo [OK] All tests passed.
)

exit /b 0
