#!/bin/bash
# 古代霹雳车仿真系统 - 后端构建脚本 (Linux/macOS)

set -e

cd "$(dirname "$0")/backend"

if [ ! -d build ]; then
    echo "[1/4] 创建构建目录..."
    mkdir -p build
fi

cd build

echo "[2/4] 运行 CMake 配置..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo
echo "[3/4] 编译项目..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo
echo "[4/4] 构建完成!"
echo
echo "可执行文件位置: backend/build/trebuchet_backend"
echo
echo "使用方法:"
echo "  ./trebuchet_backend --udp-port 9000 --http-port 8080"
