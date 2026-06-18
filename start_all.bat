@echo off
chcp 65001 >nul
echo ================================================
echo 古代霹雳车仿真系统 - 一键启动脚本
echo ================================================
echo.

cd /d "%~dp0"

echo [1/4] 检查 ClickHouse 连接...
echo 请确保 ClickHouse 已启动并在 127.0.0.1:8123 运行
echo 如未初始化数据库，请先执行: clickhouse-client --multiline < database\init_clickhouse.sql
echo.

echo [2/4] 启动后端服务...
start "霹雳车后端服务" cmd /k "cd /d %~dp0backend\build\Release && trebuchet_backend.exe --udp-port 9000 --http-port 8080"

timeout /t 3 /nobreak >nul

echo [3/4] 启动传感器模拟器...
start "UDP传感器模拟器" cmd /k "cd /d %~dp0simulator && python trebuchet_sensor_simulator.py --interval 10 --machines 3"

echo [4/4] 启动前端页面...
echo 正在打开浏览器...
start "" "%~dp0frontend\index.html"

echo.
echo ================================================
echo 系统已启动!
echo   后端API:    http://127.0.0.1:8080/api/health
echo   UDP端口:    9000 (传感器数据)
echo   前端页面:   frontend\index.html
echo ================================================
echo.
echo 提示: 关闭对应窗口即可停止各服务
pause
