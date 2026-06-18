import socket
import time
import random
import math
import json
import argparse
import threading
import sys
from datetime import datetime


SHEAR_MODULUS = 79.3e9
YIELD_STRENGTH = 785e6
GRAVITY = 9.80665
AIR_DENSITY = 1.225
DRAG_COEFFICIENT = 0.47


class SpringSimulator:
    def __init__(self, wire_diameter=0.02, coil_mean_diameter=0.15, active_coils=12):
        self.d = wire_diameter
        self.D = coil_mean_diameter
        self.Na = active_coils
        self.G = SHEAR_MODULUS
        self.k = (self.G * (self.d ** 4)) / (32.0 * self.D * self.Na)

    def calculate_stored_energy(self, torsion_angle_rad):
        return 0.5 * self.k * torsion_angle_rad * torsion_angle_rad

    def calculate_release_velocity(self, torsion_angle_rad, projectile_mass, efficiency=0.85):
        energy = self.calculate_stored_energy(torsion_angle_rad) * efficiency
        return math.sqrt(2.0 * energy / projectile_mass)


class ProjectileSimulator:
    def __init__(self, mass=10.0, cross_section_area=0.0314):
        self.mass = mass
        self.A = cross_section_area
        self.Cd = DRAG_COEFFICIENT

    def calculate_range(self, velocity, launch_angle_deg, air_factor=1.0):
        theta = math.radians(launch_angle_deg)
        v0x = velocity * math.cos(theta)
        v0y = velocity * math.sin(theta)
        drag = 0.5 * AIR_DENSITY * self.Cd * self.A * air_factor / self.mass
        dt = 0.001
        x, y = 0.0, 0.0
        vx, vy = v0x, v0y
        while y >= 0.0:
            v_mag = math.sqrt(vx * vx + vy * vy)
            ax = -drag * v_mag * vx
            ay = -GRAVITY - drag * v_mag * vy
            vx += ax * dt
            vy += ay * dt
            x += vx * dt
            y += vy * dt
            if y < 0:
                x -= vx * dt
                y_prev = y - vy * dt
                if abs(vy) > 1e-9:
                    x += (-y_prev) * vx / vy
                break
        return max(0.0, x)


class TrebuchetSensorSimulator:
    def __init__(self, machine_id, server_host="127.0.0.1", server_port=9000,
                 interval_sec=60, projectile_mass=10.0):
        self.machine_id = machine_id
        self.server_host = server_host
        self.server_port = server_port
        self.interval_sec = interval_sec
        self.projectile_mass = projectile_mass
        self.spring = SpringSimulator()
        self.projectile = ProjectileSimulator(mass=projectile_mass)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.running = False
        self.torsion_angle = 0.0
        self.max_torsion = math.radians(175)
        self.base_torsion = math.radians(120)
        self.spring_degradation = 1.0
        self.firing_count = 0

    def generate_reading(self):
        self.firing_count += 1
        noise = random.uniform(-0.02, 0.02) * self.base_torsion
        self.torsion_angle = self.base_torsion + noise

        if self.firing_count > 100:
            self.spring_degradation = max(0.7, 1.0 - (self.firing_count - 100) * 0.001)

        if self.firing_count % 50 == 0:
            self.torsion_angle *= random.uniform(1.1, 1.3)

        efficiency = 0.75 + random.uniform(-0.05, 0.05)
        efficiency *= self.spring_degradation

        stored_energy = self.spring.calculate_stored_energy(self.torsion_angle)
        release_velocity = self.spring.calculate_release_velocity(
            self.torsion_angle, self.projectile_mass, efficiency
        )
        launch_angle = 45.0 + random.uniform(-3, 3)
        actual_range = self.projectile.calculate_range(
            release_velocity, launch_angle, random.uniform(0.9, 1.1)
        )
        predicted_range = self.projectile.calculate_range(
            release_velocity, launch_angle, 1.0
        )

        return {
            "machine_id": self.machine_id,
            "torsion_angle": self.torsion_angle,
            "stored_energy": stored_energy,
            "release_velocity": release_velocity,
            "actual_range": actual_range,
            "predicted_range": predicted_range,
            "projectile_mass": self.projectile_mass,
            "launch_angle": launch_angle,
            "spring_status": "normal" if self.spring_degradation > 0.85 else "degraded",
            "efficiency": efficiency,
            "firing_count": self.firing_count,
            "timestamp": datetime.now().isoformat()
        }

    def send_packet(self, data):
        try:
            payload_pipe = "|".join([
                data["machine_id"],
                f"{data['torsion_angle']:.6f}",
                f"{data['stored_energy']:.4f}",
                f"{data['release_velocity']:.4f}",
                f"{data['actual_range']:.4f}",
                f"{data['projectile_mass']:.4f}",
                f"{data['launch_angle']:.4f}",
                data["spring_status"]
            ])
            self.sock.sendto(payload_pipe.encode("utf-8"),
                             (self.server_host, self.server_port))
            return True
        except Exception as e:
            print(f"[{self.machine_id}] 发送失败: {e}", file=sys.stderr)
            return False

    def run(self):
        self.running = True
        print(f"[{self.machine_id}] 传感器模拟器启动 (目标: {self.server_host}:{self.server_port})")
        print(f"[{self.machine_id}] 上报间隔: {self.interval_sec}秒")
        try:
            while self.running:
                reading = self.generate_reading()
                if self.send_packet(reading):
                    print(f"[{self.machine_id}] #{reading['firing_count']} "
                          f"扭转角={math.degrees(reading['torsion_angle']):.1f}° "
                          f"储能={reading['stored_energy']:.1f}J "
                          f"初速={reading['release_velocity']:.2f}m/s "
                          f"射程={reading['actual_range']:.1f}m "
                          f"效率={reading['efficiency']*100:.1f}%")
                time.sleep(self.interval_sec)
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False
            self.sock.close()
            print(f"[{self.machine_id}] 模拟器已停止")


def main():
    parser = argparse.ArgumentParser(description="霹雳车UDP传感器模拟器")
    parser.add_argument("--host", default="127.0.0.1", help="后端服务器地址")
    parser.add_argument("--port", type=int, default=9000, help="后端UDP端口")
    parser.add_argument("--interval", type=int, default=60, help="上报间隔(秒)")
    parser.add_argument("--machines", type=int, default=3, help="模拟霹雳车数量")
    parser.add_argument("--mass", type=float, default=10.0, help="弹丸质量(kg)")
    args = parser.parse_args()

    threads = []
    for i in range(args.machines):
        machine_id = f"TREB-{i+1:03d}"
        mass = args.mass + random.uniform(-2, 2)
        interval = args.interval + random.randint(-5, 5)
        sim = TrebuchetSensorSimulator(
            machine_id=machine_id,
            server_host=args.host,
            server_port=args.port,
            interval_sec=max(5, interval),
            projectile_mass=mass
        )
        t = threading.Thread(target=sim.run, daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.5)

    print(f"\n已启动 {len(threads)} 台模拟器线程。按 Ctrl+C 退出。\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n正在停止所有模拟器...")
        time.sleep(1)


if __name__ == "__main__":
    main()
