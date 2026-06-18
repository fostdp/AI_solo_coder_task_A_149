@echo off
chcp 65001 >nul
echo ================================================
echo 古代霹雳车仿真系统 - 后端构建脚本 (Windows)
echo ================================================
echo.

cd /d "%~dp0backend"

if not exist build (
    echo [1/4] 创建构建目录...
    mkdir build
)

cd build

echo [2/4] 运行 CMake 配置...
cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake 配置失败! 尝试使用 MinGW Makefiles...
    cd ..
    if exist build rmdir /s /q build
    mkdir build
    cd build
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo CMake 配置仍然失败，请检查编译器是否安装。
        echo 需要: Visual Studio 2022 或 MinGW-w64 + CMake
        pause
        exit /b 1
    )
)

echo.
echo [3/4] 编译项目...
cmake --build . --config Release
if errorlevel 1 (
    echo 编译失败!
    pause
    exit /b 1
)

echo.
echo [4/4] 构建完成!
echo.
echo 可执行文件位置: backend\build\Release\trebuchet_backend.exe
echo.
echo 使用方法:
echo   .\trebuchet_backend.exe --udp-port 9000 --http-port 8080
echo.
pause
