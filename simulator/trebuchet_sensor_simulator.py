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
DENSITY = 7850.0
FATIGUE_DUCTILITY_COEFF = 0.42
FATIGUE_DUCTILITY_EXP = -0.58
CYCLIC_STRENGTH_COEFF = 1300e6
CYCLIC_STRENGTH_EXP = -0.10
GRAVITY = 9.80665
AIR_DENSITY = 1.225
DRAG_COEFFICIENT_INCOMPRESSIBLE = 0.47
SPEED_OF_SOUND = 343.2
SUTHERLAND_T0 = 273.15
SUTHERLAND_MU0 = 1.716e-5
SUTHERLAND_S = 110.4
TEMPERATURE_K = 288.15

C1_KINEMATIC = 40.0e9
D1_KINEMATIC = 200.0
Q_SAT_SOFTENING = 120e6
B_SOFTENING = 30.0


def calculate_wahl_factor(spring_index):
    return (4 * spring_index - 1) / (4 * spring_index - 4) + 0.615 / spring_index


def calculate_viscosity(temperature_k):
    return SUTHERLAND_MU0 * ((temperature_k / SUTHERLAND_T0) ** 1.5) * \
           (SUTHERLAND_T0 + SUTHERLAND_S) / (temperature_k + SUTHERLAND_S)


def calculate_mach_number(velocity, temperature_k=TEMPERATURE_K):
    c = math.sqrt(1.4 * 287.05 * temperature_k)
    return velocity / c


def calculate_prandtl_glauert_correction(mach_number):
    if mach_number >= 1.0:
        return 1.0
    beta = math.sqrt(max(1e-9, 1.0 - mach_number * mach_number))
    return 1.0 / beta


def calculate_karman_tsien_correction(mach_number):
    if mach_number < 0.3:
        return 1.0
    beta = math.sqrt(max(1e-9, 1.0 - mach_number * mach_number))
    pg = 1.0 / beta
    return pg / (1.0 + mach_number * mach_number * (pg - 1.0) / (1.0 + beta))


def calculate_transonic_wave_drag(mach_number):
    if mach_number < 0.75 or mach_number > 1.3:
        return 0.0
    if mach_number <= 1.0:
        t = (mach_number - 0.75) / 0.25
        return 0.35 * t * t
    else:
        t = (1.3 - mach_number) / 0.3
        return 0.35 * t * t


def calculate_supersonic_newtonian_drag(mach_number):
    if mach_number < 1.2:
        return 0.0
    sin_sq = 0.36
    cd_pressure = 2.0 * sin_sq
    cd_friction = 0.1 / math.sqrt(max(1.0, mach_number))
    return cd_pressure + cd_friction


def calculate_compressible_drag_coefficient(mach_number, incompressible_cd, reynolds_number=1e6):
    if mach_number < 0.3:
        return incompressible_cd
    if mach_number < 0.8:
        kt = calculate_karman_tsien_correction(mach_number)
        return incompressible_cd * kt
    if mach_number <= 1.2:
        cd_sub = incompressible_cd * calculate_karman_tsien_correction(0.8)
        cd_wave = calculate_transonic_wave_drag(mach_number)
        return cd_sub + cd_wave
    return calculate_supersonic_newtonian_drag(mach_number) + 0.05 / math.sqrt(max(1.0, mach_number))


def calculate_coffin_manson_life(plastic_strain_amplitude):
    if plastic_strain_amplitude <= 0:
        return 1e12
    elastic_term = (CYCLIC_STRENGTH_COEFF / SHEAR_MODULUS) * \
                   ((2.0 * plastic_strain_amplitude) ** CYCLIC_STRENGTH_EXP)
    plastic_term = FATIGUE_DUCTILITY_COEFF * \
                   ((2.0 * plastic_strain_amplitude) ** FATIGUE_DUCTILITY_EXP)
    total = max(1e-12, abs(elastic_term + plastic_term))
    return 1.0 / total


class CyclicSofteningState:
    def __init__(self):
        self.cycle_count = 0
        self.accumulated_plastic_strain = 0.0
        self.degraded_shear_modulus = SHEAR_MODULUS
        self.degraded_yield_strength = YIELD_STRENGTH
        self.back_stress = 0.0
        self.kinematic_hardening = 0.0
        self.current_damage_parameter = 0.0

    def update(self, torsion_angle_rad, shear_stress_amplitude, wire_d, coil_D, active_coils):
        self.cycle_count += 1
        tau_eff = abs(shear_stress_amplitude - self.back_stress)
        tau_y = self.degraded_yield_strength
        plastic_inc = 0.0
        if tau_eff > tau_y and self.degraded_shear_modulus > 1e-6:
            plastic_inc = (tau_eff - tau_y) / self.degraded_shear_modulus
            direction = 1.0 if shear_stress_amplitude > self.back_stress else -1.0
            dx = C1_KINEMATIC * plastic_inc * direction - \
                 D1_KINEMATIC * self.back_stress * abs(plastic_inc)
            self.back_stress += dx
            dq = 0.3 * Q_SAT_SOFTENING * (1.0 - math.exp(-B_SOFTENING * abs(plastic_inc)))
            self.degraded_yield_strength = max(YIELD_STRENGTH * 0.5,
                                                self.degraded_yield_strength - dq)

        self.accumulated_plastic_strain += abs(plastic_inc)
        if self.accumulated_plastic_strain > 1e-9:
            ratio = self.degraded_yield_strength / YIELD_STRENGTH
            self.degraded_shear_modulus = SHEAR_MODULUS * max(
                0.55, math.exp(-0.15 * self.accumulated_plastic_strain / ratio)
            )
        if plastic_inc > 1e-12:
            nf = calculate_coffin_manson_life(plastic_inc)
            self.current_damage_parameter += 1.0 / max(1.0, nf)
        self.current_damage_parameter = min(1.5, self.current_damage_parameter)

    @property
    def fatigue_risk(self):
        return self.current_damage_parameter > 0.5


class SpringSimulator:
    def __init__(self, wire_diameter=0.02, coil_mean_diameter=0.15, active_coils=12):
        self.d = wire_diameter
        self.D = coil_mean_diameter
        self.Na = active_coils
        self.cyclic = CyclicSofteningState()

    def calculate_shear_stress(self, torsion_angle_rad):
        G = self.cyclic.degraded_shear_modulus
        k_spring = (G * (self.d ** 4)) / (32.0 * self.D * self.Na)
        torque = k_spring * torsion_angle_rad
        spring_index = self.D / self.d
        K = calculate_wahl_factor(spring_index)
        tau = (16.0 * K * torque) / (math.pi * (self.d ** 3))
        return tau, k_spring

    def calculate_stored_energy(self, torsion_angle_rad):
        tau, k_spring = self.calculate_shear_stress(torsion_angle_rad)
        self.cyclic.update(torsion_angle_rad, tau, self.d, self.D, self.Na)
        G = self.cyclic.degraded_shear_modulus
        k_spring = (G * (self.d ** 4)) / (32.0 * self.D * self.Na)
        return 0.5 * k_spring * torsion_angle_rad * torsion_angle_rad, k_spring, tau

    def calculate_release_velocity(self, torsion_angle_rad, projectile_mass, efficiency=0.85):
        energy, k_spring, tau = self.calculate_stored_energy(torsion_angle_rad)
        eff = efficiency * (0.7 + 0.3 * (self.cyclic.degraded_shear_modulus / SHEAR_MODULUS))
        usable = energy * eff
        return math.sqrt(2.0 * usable / projectile_mass), k_spring, tau


class ProjectileSimulator:
    def __init__(self, mass=10.0, cross_section_area=0.0314, diameter=0.2):
        self.mass = mass
        self.A = cross_section_area
        self.diameter = diameter
        self.Cd0 = DRAG_COEFFICIENT_INCOMPRESSIBLE

    def calculate_range(self, velocity, launch_angle_deg, air_factor=1.0):
        theta = math.radians(launch_angle_deg)
        v0x = velocity * math.cos(theta)
        v0y = velocity * math.sin(theta)
        dt = 0.002
        x, y = 0.0, 0.0
        vx, vy = v0x, v0y
        max_mach = calculate_mach_number(velocity)
        compressibility_sum = 0.0
        count = 0
        while y >= 0.0:
            v_mag = math.sqrt(vx * vx + vy * vy)
            if v_mag < 0.01:
                break
            ma = calculate_mach_number(v_mag)
            if ma > max_mach:
                max_mach = ma
            cd = calculate_compressible_drag_coefficient(ma, self.Cd0)
            compressibility_sum += cd / self.Cd0
            count += 1
            drag = 0.5 * AIR_DENSITY * cd * self.A * air_factor / self.mass
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
        compressibility_correction = compressibility_sum / max(1, count)
        return max(0.0, x), max_mach, compressibility_correction


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

    def generate_reading(self):
        noise = random.uniform(-0.02, 0.02) * self.base_torsion
        self.torsion_angle = self.base_torsion + noise

        if self.spring.cyclic.cycle_count > 0 and self.spring.cyclic.cycle_count % 50 == 0:
            self.torsion_angle *= random.uniform(1.08, 1.2)

        efficiency = 0.78 + random.uniform(-0.04, 0.04)
        stored_energy, k_spring, shear_stress = self.spring.calculate_stored_energy(
            self.torsion_angle
        )
        release_velocity, _, _ = self.spring.calculate_release_velocity(
            self.torsion_angle, self.projectile_mass, efficiency
        )
        launch_angle = 45.0 + random.uniform(-3, 3)
        actual_range, max_mach, comp_corr = self.projectile.calculate_range(
            release_velocity, launch_angle, random.uniform(0.92, 1.08)
        )
        predicted_range, _, _ = self.projectile.calculate_range(
            release_velocity, launch_angle, 1.0
        )

        modulus_reduction = self.spring.cyclic.degraded_shear_modulus / SHEAR_MODULUS
        efficiency *= max(0.65, modulus_reduction)

        return {
            "machine_id": self.machine_id,
            "torsion_angle": self.torsion_angle,
            "stored_energy": stored_energy,
            "release_velocity": release_velocity,
            "actual_range": actual_range,
            "predicted_range": predicted_range,
            "projectile_mass": self.projectile_mass,
            "launch_angle": launch_angle,
            "spring_status": "normal" if modulus_reduction > 0.85 else
                            ("degraded" if modulus_reduction > 0.7 else "critical"),
            "efficiency": efficiency,
            "cycle_count": self.spring.cyclic.cycle_count,
            "cyclic_damage_ratio": self.spring.cyclic.current_damage_parameter,
            "plastic_strain": self.spring.cyclic.accumulated_plastic_strain,
            "max_mach": max_mach,
            "compressibility_correction": comp_corr,
            "shear_stress": shear_stress,
            "yield_strength_ratio": shear_stress / self.spring.cyclic.degraded_yield_strength,
            "fatigue_risk": 1 if self.spring.cyclic.fatigue_risk else 0,
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
                data["spring_status"],
                f"{data['efficiency']:.6f}",
                f"{data['cycle_count']}",
                f"{data['cyclic_damage_ratio']:.8f}",
                f"{data['plastic_strain']:.10f}",
                f"{data['max_mach']:.4f}",
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
                    print(f"[{self.machine_id}] #{reading['cycle_count']} "
                          f"扭转角={math.degrees(reading['torsion_angle']):.1f}° "
                          f"储能={reading['stored_energy']:.1f}J "
                          f"初速={reading['release_velocity']:.2f}m/s "
                          f"Ma={reading['max_mach']:.2f} "
                          f"射程={reading['actual_range']:.1f}m "
                          f"损伤={(reading['cyclic_damage_ratio']*100):.2f}% "
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
