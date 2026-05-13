@echo off
echo ============================================
echo EmmyLua VSCode Extension - Full Build
echo ============================================
echo.
echo Dependencies (auto-downloaded by CMake):
echo   - nlohmann-json v3.11.3
echo   - spdlog v1.14.1
echo   - doctest v2.4.11
echo.
echo Note: First build requires internet connection.
echo.

echo [1/5] Preparing version...
node ./build/prepare-version.js

echo.
echo [2/5] Downloading debugger binaries...
node ./build/prepare.js

echo.
echo [3/5] Building C++ language server...
if not exist "emmylua-ls\build" mkdir emmylua-ls\build
cd emmylua-ls\build
cmake .. -G "Visual Studio 16 2019" -A x64 2>nul
if errorlevel 1 (
    cmake .. 2>nul
)
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: C++ build failed!
    cd ..\..
    exit /b 1
)
cd ..\..

echo Copying C++ binary...
copy /Y "emmylua-ls\build\Release\emmylua-ls.exe" "server\emmylua-ls.exe" >nul

echo.
echo [4/5] Compiling TypeScript...
call npm run compile

echo.
echo [5/5] Packaging VSIX...
call vsce package -o VSCode-EmmyLua.vsix

echo.
echo ============================================
echo Build complete: VSCode-EmmyLua.vsix
echo ============================================
