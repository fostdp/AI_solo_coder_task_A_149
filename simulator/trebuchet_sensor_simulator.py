import socket
import time
import random
import math
import json
import argparse
import threading
import sys
from datetime import datetime


MATERIAL_LIBRARY = {
    "65mn": {
        "shear_modulus": 79.3e9,
        "yield_strength": 785e6,
        "density": 7850.0,
        "fatigue_ductility_coeff": 0.42,
        "fatigue_ductility_exp": -0.58,
        "cyclic_strength_coeff": 1300e6,
        "cyclic_strength_exp": -0.10,
        "name": "65Mn Spring Steel"
    },
    "50crva": {
        "shear_modulus": 79.0e9,
        "yield_strength": 1177e6,
        "density": 7850.0,
        "fatigue_ductility_coeff": 0.38,
        "fatigue_ductility_exp": -0.62,
        "cyclic_strength_coeff": 1500e6,
        "cyclic_strength_exp": -0.12,
        "name": "50CrVA Spring Steel"
    }
}

SHEAR_MODULUS = MATERIAL_LIBRARY["65mn"]["shear_modulus"]
YIELD_STRENGTH = MATERIAL_LIBRARY["65mn"]["yield_strength"]
DENSITY = MATERIAL_LIBRARY["65mn"]["density"]
FATIGUE_DUCTILITY_COEFF = MATERIAL_LIBRARY["65mn"]["fatigue_ductility_coeff"]
FATIGUE_DUCTILITY_EXP = MATERIAL_LIBRARY["65mn"]["fatigue_ductility_exp"]
CYCLIC_STRENGTH_COEFF = MATERIAL_LIBRARY["65mn"]["cyclic_strength_coeff"]
CYCLIC_STRENGTH_EXP = MATERIAL_LIBRARY["65mn"]["cyclic_strength_exp"]
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
    def __init__(self, material_key="65mn"):
        self.material_key = material_key
        mat = MATERIAL_LIBRARY[material_key]
        self.cycle_count = 0
        self.accumulated_plastic_strain = 0.0
        self.degraded_shear_modulus = mat["shear_modulus"]
        self.degraded_yield_strength = mat["yield_strength"]
        self.back_stress = 0.0
        self.kinematic_hardening = 0.0
        self.current_damage_parameter = 0.0
        self.fatigue_ductility_coeff = mat["fatigue_ductility_coeff"]
        self.fatigue_ductility_exp = mat["fatigue_ductility_exp"]
        self.cyclic_strength_coeff = mat["cyclic_strength_coeff"]
        self.cyclic_strength_exp = mat["cyclic_strength_exp"]

    def update(self, torsion_angle_rad, shear_stress_amplitude, wire_d, coil_D, active_coils):
        mat = MATERIAL_LIBRARY[self.material_key]
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
            self.degraded_yield_strength = max(mat["yield_strength"] * 0.5,
                                                self.degraded_yield_strength - dq)

        self.accumulated_plastic_strain += abs(plastic_inc)
        if self.accumulated_plastic_strain > 1e-9:
            ratio = self.degraded_yield_strength / mat["yield_strength"]
            self.degraded_shear_modulus = mat["shear_modulus"] * max(
                0.55, math.exp(-0.15 * self.accumulated_plastic_strain / ratio)
            )
        if plastic_inc > 1e-12:
            nf = self.calculate_coffin_manson_life(plastic_inc)
            self.current_damage_parameter += 1.0 / max(1.0, nf)
        self.current_damage_parameter = min(1.5, self.current_damage_parameter)

    def calculate_coffin_manson_life(self, plastic_strain_amplitude):
        if plastic_strain_amplitude <= 0:
            return 1e12
        mat = MATERIAL_LIBRARY[self.material_key]
        elastic_term = (self.cyclic_strength_coeff / self.degraded_shear_modulus) * \
                       ((2.0 * plastic_strain_amplitude) ** self.cyclic_strength_exp)
        plastic_term = self.fatigue_ductility_coeff * \
                       ((2.0 * plastic_strain_amplitude) ** self.fatigue_ductility_exp)
        total = max(1e-12, abs(elastic_term + plastic_term))
        return 1.0 / total

    @property
    def fatigue_risk(self):
        return self.current_damage_parameter > 0.5


class SpringSimulator:
    def __init__(self, wire_diameter=0.02, coil_mean_diameter=0.15, active_coils=12,
                 material_key="65mn"):
        self.d = wire_diameter
        self.D = coil_mean_diameter
        self.Na = active_coils
        self.material_key = material_key
        self.cyclic = CyclicSofteningState(material_key)

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
        mat = MATERIAL_LIBRARY[self.material_key]
        eff = efficiency * (0.7 + 0.3 * (self.cyclic.degraded_shear_modulus / mat["shear_modulus"]))
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
                 interval_ms=60000, projectile_mass=10.0, launch_angle_deg=None,
                 wire_diameter_mm=20.0, coil_mean_diameter_mm=150.0,
                 active_coils=12, material_key="65mn",
                 fixed_parameters=False):
        self.machine_id = machine_id
        self.server_host = server_host
        self.server_port = server_port
        self.interval_sec = interval_ms / 1000.0
        self.interval_ms = interval_ms
        self.projectile_mass = projectile_mass
        self.launch_angle_deg = launch_angle_deg
        self.wire_d = wire_diameter_mm / 1000.0
        self.coil_mean_diameter_m = coil_mean_diameter_mm / 1000.0
        self.active_coils = active_coils
        self.material_key = material_key
        self.fixed_parameters = fixed_parameters
        self.spring = SpringSimulator(
            wire_diameter=self.wire_d,
            coil_mean_diameter=self.coil_mean_diameter_m,
            active_coils=active_coils,
            material_key=material_key
        )
        cross_section_area = math.pi * (self.coil_mean_diameter_m / 2.0) ** 2
        self.projectile = ProjectileSimulator(
            mass=projectile_mass,
            cross_section_area=cross_section_area,
            diameter=self.coil_mean_diameter_m
        )
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.running = False
        self.torsion_angle = 0.0
        self.max_torsion = math.radians(175)
        self.base_torsion = math.radians(120)
        self._status_counter = 0

    def generate_reading(self):
        noise = random.uniform(-0.02, 0.02) * self.base_torsion
        self.torsion_angle = self.base_torsion + noise

        self._status_counter += 1
        if self.spring.cyclic.cycle_count > 0 and self._status_counter % 50 == 0:
            self.torsion_angle *= random.uniform(1.08, 1.2)

        efficiency = 0.78 + random.uniform(-0.04, 0.04)
        stored_energy, k_spring, shear_stress = self.spring.calculate_stored_energy(
            self.torsion_angle
        )
        release_velocity, _, _ = self.spring.calculate_release_velocity(
            self.torsion_angle, self.projectile_mass, efficiency
        )

        if self.launch_angle_deg is not None and self.fixed_parameters:
            launch_angle = float(self.launch_angle_deg)
        else:
            launch_angle = 45.0 + random.uniform(-3, 3)

        actual_range, max_mach, comp_corr = self.projectile.calculate_range(
            release_velocity, launch_angle, random.uniform(0.92, 1.08)
        )
        predicted_range, _, _ = self.projectile.calculate_range(
            release_velocity, launch_angle, 1.0
        )

        mat = MATERIAL_LIBRARY[self.material_key]
        modulus_reduction = self.spring.cyclic.degraded_shear_modulus / mat["shear_modulus"]
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
        mat_info = MATERIAL_LIBRARY[self.material_key]["name"]
        print(f"[{self.machine_id}] 传感器模拟器启动 (目标: {self.server_host}:{self.server_port})")
        print(f"[{self.machine_id}] 上报间隔: {self.interval_ms}ms ({self.interval_sec:.3f}s)")
        print(f"[{self.machine_id}] 弹丸重量: {self.projectile_mass:.2f} kg")
        if self.launch_angle_deg is not None and self.fixed_parameters:
            print(f"[{self.machine_id}] 发射仰角: {self.launch_angle_deg:.1f}° (固定)")
        else:
            print(f"[{self.machine_id}] 发射仰角: 45° ± 3° (随机)")
        print(f"[{self.machine_id}] 弹簧参数: 线径={self.wire_d*1000:.1f}mm, "
              f"中径={self.coil_mean_diameter_m*1000:.1f}mm, "
              f"有效圈数={self.active_coils}, 材料={mat_info}")
        try:
            while self.running:
                reading = self.generate_reading()
                if self.send_packet(reading):
                    print(f"[{self.machine_id}] #{reading['cycle_count']} "
                          f"扭转角={math.degrees(reading['torsion_angle']):.1f}° "
                          f"储能={reading['stored_energy']:.1f}J "
                          f"初速={reading['release_velocity']:.2f}m/s "
                          f"Ma={reading['max_mach']:.2f} "
                          f"仰角={reading['launch_angle']:.1f}° "
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
    parser = argparse.ArgumentParser(
        description="霹雳车UDP传感器模拟器 - 扭力弹簧储能仿真",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python trebuchet_sensor_simulator.py --mass-kg 15.0 --launch-angle-deg 45 --interval-ms 1000
  python trebuchet_sensor_simulator.py --machines 5 --wire-diameter-mm 22 --material 50crva
  python trebuchet_sensor_simulator.py --host 192.168.1.100 --port 9000 --mass-kg 8.5
        """
    )
    parser.add_argument("--host", default="127.0.0.1", help="后端服务器地址 (默认: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9000, help="后端UDP端口 (默认: 9000)")
    parser.add_argument("--machines", type=int, default=3, help="模拟霹雳车数量 (默认: 3)")

    parser.add_argument("--mass-kg", type=float, default=10.0,
                        help="弹丸重量(kg) (默认: 10.0)")
    parser.add_argument("--launch-angle-deg", type=float, default=None,
                        help="固定发射仰角(°)，不设则45°±3°随机波动")
    parser.add_argument("--interval-ms", type=int, default=60000,
                        help="上报间隔毫秒(ms) (默认: 60000 = 60秒)")

    parser.add_argument("--wire-diameter-mm", type=float, default=20.0,
                        help="弹簧线径(mm) (默认: 20.0)")
    parser.add_argument("--coil-mean-diameter-mm", type=float, default=150.0,
                        help="弹簧中径(mm) (默认: 150.0)")
    parser.add_argument("--active-coils", type=int, default=12,
                        help="弹簧有效圈数 (默认: 12)")
    parser.add_argument("--material", choices=["65mn", "50crva"], default="65mn",
                        help="弹簧材料 (默认: 65mn)")

    parser.add_argument("--randomize-mass", action="store_true",
                        help="弹丸重量每台随机±2kg波动")
    parser.add_argument("--randomize-angle", action="store_true",
                        help="忽略--launch-angle-deg，每台独立随机")
    parser.add_argument("--randomize-interval", action="store_true",
                        help="上报间隔每台随机±5秒波动")

    args = parser.parse_args()

    fixed_parameters = args.launch_angle_deg is not None and not args.randomize_angle

    print("=" * 70)
    print("霹雳车扭力弹簧储能仿真系统 - 传感器模拟器 v2.0")
    print("=" * 70)
    print(f"目标服务器: {args.host}:{args.port}")
    print(f"模拟设备数: {args.machines}")
    print(f"弹丸重量:   {args.mass_kg:.2f} kg{' (随机波动)' if args.randomize_mass else ''}")
    print(f"发射仰角:   {args.launch_angle_deg if args.launch_angle_deg else '45°±3°'}"
          f"{' (固定模式)' if fixed_parameters else ' (随机模式)'}")
    print(f"上报间隔:   {args.interval_ms} ms{' (随机波动)' if args.randomize_interval else ''}")
    print(f"弹簧线径:   {args.wire_diameter_mm:.1f} mm")
    print(f"弹簧中径:   {args.coil_mean_diameter_mm:.1f} mm")
    print(f"有效圈数:   {args.active_coils}")
    print(f"材料:       {MATERIAL_LIBRARY[args.material]['name']}")
    print("=" * 70)

    threads = []
    for i in range(args.machines):
        machine_id = f"TREB-{i+1:03d}"

        mass = args.mass_kg
        if args.randomize_mass:
            mass += random.uniform(-2.0, 2.0)

        launch_angle = None if args.randomize_angle else args.launch_angle_deg

        interval_ms = args.interval_ms
        if args.randomize_interval:
            interval_ms += random.randint(-5000, 5000)
            interval_ms = max(1000, interval_ms)

        sim = TrebuchetSensorSimulator(
            machine_id=machine_id,
            server_host=args.host,
            server_port=args.port,
            interval_ms=interval_ms,
            projectile_mass=mass,
            launch_angle_deg=launch_angle,
            wire_diameter_mm=args.wire_diameter_mm,
            coil_mean_diameter_mm=args.coil_mean_diameter_mm,
            active_coils=args.active_coils,
            material_key=args.material,
            fixed_parameters=fixed_parameters
        )
        t = threading.Thread(target=sim.run, daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.3)

    print(f"\n已启动 {len(threads)} 台模拟器线程。按 Ctrl+C 退出。\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n正在停止所有模拟器...")
        time.sleep(1)
        print("所有模拟器已停止。")


if __name__ == "__main__":
    main()
