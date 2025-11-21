@echo off
REM Build and run the TextLayout Windows test using MSVC

echo Building TextLayout test with MSVC...

REM Create build directory if it doesn't exist
if not exist build_msvc mkdir build_msvc

REM Navigate to build directory
cd build_msvc

REM Run CMake with Visual Studio generator
cmake .. -G "Visual Studio 17 2022"
if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    echo You may need to adjust the Visual Studio version in this script.
    cd ..
    pause
    exit /b 1
)

REM Build the project
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo Build successful! Running test...
echo ================================
echo.

REM Run the test
Release\textlayout_test.exe

cd ..
