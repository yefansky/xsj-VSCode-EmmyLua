@echo off
echo ============================================
echo EmmyLua Language Server - Build Script
echo ============================================
echo.
echo This script will:
echo 1. Download third-party dependencies (nlohmann-json, spdlog, doctest)
echo 2. Build the C++ language server
echo 3. Copy the binary to server/
echo.
echo Note: First build requires internet connection to download dependencies.
echo.

cd /d "%~dp0"

echo [1/3] Configuring CMake (download dependencies)...
if not exist build mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64
if errorlevel 1 (
    echo Trying default generator...
    cmake ..
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo [2/3] Building Release...
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: Build failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo [3/3] Copying binary to server/...
cd ..
copy /Y "build\Release\emmylua-ls.exe" "..\server\emmylua-ls.exe"
if errorlevel 1 (
    copy /Y "build\emmylua-ls" "..\server\emmylua-ls"
)

echo.
echo ============================================
echo Build complete!
echo Binary: server/emmylua-ls.exe
echo.
echo Dependencies (auto-downloaded by CMake):
echo   - nlohmann-json v3.11.3
echo   - spdlog v1.14.1
echo   - doctest v2.4.11
echo ============================================
pause
