@echo off
rem Build and test on Windows (MSVC + CMake + Ninja bundled with Visual Studio).
rem Works from a plain cmd window: Visual Studio is located via vswhere.
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if not errorlevel 1 goto build
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
ctest --test-dir build --output-on-failure || exit /b 1
echo.
echo Done: build\server.exe build\client.exe build\netem_proxy.exe build\latency_report.exe build\tests\unit_tests.exe
