# 古代霹雳车（投石机）扭力弹簧储能仿真与射程预测系统

某军事史团队对三国时期霹雳车进行复原研究的全栈仿真系统。

## 系统架构

```
┌──────────────────┐     UDP      ┌──────────────────┐    HTTP     ┌──────────────────┐
│  传感器模拟器     │ ──────────► │  C++ 后端服务    │ ─────────► │  ClickHouse 数据库│
│  (Python)        │  端口 9000   │                  │   端口 8123 │                  │
└──────────────────┘              │  - UDP接收器     │             └──────────────────┘
                                  │  - 物理仿真计算   │
┌──────────────────┐   MQTT       │  - MQTT告警推送  │
│  告警订阅端       │ ◄─────────── │  - HTTP API      │
│  (任意MQTT客户端) │  端口 1883   └──────────────────┘
└──────────────────┘                       ▲
                                           │ HTTP API
                                           ▼
                                  ┌──────────────────┐
                                  │  前端可视化页面   │
                                  │  Three.js + Canvas│
                                  └──────────────────┘
```

## 目录结构

```
AI_solo_coder_task_A_149/
├── backend/                    # C++ 后端服务
│   ├── CMakeLists.txt         # 构建配置
│   ├── include/               # 头文件
│   │   ├── trebuchet_physics.h      # 物理仿真模型
│   │   ├── clickhouse_client.h      # ClickHouse客户端
│   │   ├── mqtt_alert_manager.h     # MQTT告警管理
│   │   ├── udp_sensor_receiver.h    # UDP数据接收
│   │   └── http_api_server.h        # HTTP API服务
│   └── src/                   # 实现文件
│       ├── trebuchet_physics.cpp
│       ├── clickhouse_client.cpp
│       ├── mqtt_alert_manager.cpp
│       ├── udp_sensor_receiver.cpp
│       ├── http_api_server.cpp
│       └── main.cpp
├── frontend/                   # 前端可视化
│   ├── index.html             # 主页面
│   ├── css/style.css          # 样式
│   └── js/
│       ├── physics.js         # 前端物理计算
│       ├── trebuchet-model.js # Three.js三维模型
│       ├── spring-animation.js# 扭力弹簧动画
│       ├── trajectory-view.js # 弹道轨迹可视化
│       ├── data-chart.js      # 数据图表
│       ├── api.js             # API客户端
│       └── app.js             # 主应用逻辑
├── database/
│   └── init_clickhouse.sql    # ClickHouse初始化脚本
├── simulator/
│   └── trebuchet_sensor_simulator.py  # UDP传感器模拟器
├── build_backend.bat / .sh    # 后端构建脚本
└── start_all.bat              # 一键启动脚本(Windows)
```

## 核心物理模型

### 1. 扭力弹簧储能计算

基于材料力学扭簧特性：

**弹簧刚度** (N·m/rad):
```
k = G · d⁴ / (32 · D · Na)
```
- G: 剪切模量 (65Mn钢: 79.3 GPa)
- d: 钢丝直径
- D: 弹簧中径
- Na: 有效圈数

**储能** (J):
```
E = ½ · k · θ²
```
- θ: 扭转角 (rad)

**最大剪应力** (Pa):
```
τ_max = K · (16T) / (πd³)
K = (4D-d)/(4(D-d)) + 0.615·d/D (Wahl修正系数)
```

### 2. 抛体弹道与射程预测

考虑空气阻力的抛体运动数值积分：

```
aₓ = -½ρ·Cd·A·|v|·vₓ / m
aᵧ = -g - ½ρ·Cd·A·|v|·vᵧ / m
```
- ρ: 空气密度 (1.225 kg/m³)
- Cd: 阻力系数 (球形: 0.47)
- A: 横截面积
- m: 弹丸质量

## 告警机制

| 告警类型 | 触发条件 | 级别 |
|---------|---------|------|
| 弹簧断裂风险 | 屈服强度比 > 85% | WARNING/CRITICAL |
| 射程不足 | 实际射程 < 预测射程 × 85% | WARNING |
| 效率偏低 | 能量转换效率 < 60% | INFO/WARNING |

告警通过 MQTT 推送至 `trebuchet/alerts/{machine_id}/{alert_type}` 主题。

## 快速开始

### 前置依赖

- **C++编译器**: Visual Studio 2022 / GCC 8+ / Clang
- **CMake**: 3.15+
- **ClickHouse**: 21.8+
- **Python**: 3.7+ (用于模拟器)
- **MQTT Broker**: Mosquitto / EMQX (可选，告警功能需要)
- **浏览器**: Chrome/Edge/Firefox 最新版

### 1. 初始化数据库

```bash
clickhouse-client --multiline < database/init_clickhouse.sql
```

### 2. 编译后端

**Windows:**
```cmd
build_backend.bat
```

**Linux/macOS:**
```bash
chmod +x build_backend.sh
./build_backend.sh
```

### 3. 启动后端

```bash
# Windows
backend\build\Release\trebuchet_backend.exe --udp-port 9000 --http-port 8080

# Linux/macOS
./backend/build/trebuchet_backend --udp-port 9000 --http-port 8080
```

### 4. 启动传感器模拟器

```bash
cd simulator
python trebuchet_sensor_simulator.py --interval 10 --machines 3
```

模拟器参数:
- `--host`: 后端地址 (默认 127.0.0.1)
- `--port`: 后端UDP端口 (默认 9000)
- `--interval`: 上报间隔秒数 (默认 60)
- `--machines`: 模拟霹雳车数量 (默认 3)
- `--mass`: 弹丸质量kg (默认 10)

### 5. 打开前端

直接用浏览器打开 `frontend/index.html`，或启动一个静态HTTP服务器：

```bash
cd frontend
python -m http.server 8000
# 然后访问 http://localhost:8000
```

## API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/health` | 健康检查 |
| GET | `/api/predict-range?velocity=&angle=&mass=&drag=` | 预测射程 |
| GET | `/api/spring-energy?angle=` | 计算弹簧储能 |
| GET | `/api/sensor-data?machine_id=&limit=` | 查询传感器数据 |
| GET | `/api/alerts?machine_id=&limit=` | 查询告警 |
| GET | `/api/machine-status` | 所有设备状态 |

## UDP 数据协议

后端监听UDP端口（默认9000），接收格式:

**管道分隔格式:**
```
machine_id|torsion_angle_rad|stored_energy|release_velocity|actual_range|projectile_mass|launch_angle_deg|spring_status
```

**键值对格式:**
```
machine_id=TREB-001,torsion_angle=2.094,stored_energy=15000,release_velocity=35.5,actual_range=120.5,projectile_mass=10,launch_angle=45,spring_status=normal
```

## MQTT 告警格式

Topic: `trebuchet/alerts/{machine_id}/{alert_type}`

Payload (JSON):
```json
{
  "machine_id": "TREB-001",
  "timestamp": "2024-01-01T12:00:00",
  "alert_type": "spring_fracture_risk",
  "alert_level": "critical",
  "message": "弹簧断裂风险: 扭转角=3.05rad, 屈服强度比=92%",
  "torsion_angle": 3.05,
  "threshold_value": 0.85
}
```

## 前端功能

1. **三维模型视图**: Three.js渲染的霹雳车模型，支持鼠标拖拽旋转、滚轮缩放、模拟发射动画
2. **弹簧储能仿真**: Canvas绘制扭转弹簧螺旋动画，实时显示扭转角、储能、应力、效率、风险等级
3. **弹道轨迹可视化**: 可配置参数（质量/初速/仰角/阻力），绘制抛物线轨迹，支持多参数对比曲线
4. **设备状态监控**: 实时显示各台霹雳车的运行状态、关键参数、风险等级
5. **实时数据图表**: 射程/储能/扭转角/效率的趋势图
6. **告警中心**: 实时展示弹簧断裂、射程不足等告警信息
7. **历史数据查询**: 按设备、数量查询历史传感器数据
