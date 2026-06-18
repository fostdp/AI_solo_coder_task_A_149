#include "trebuchet_physics.h"
#include <algorithm>
#include <numeric>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double GAMMA = 1.4;
static double gamma_ratio() { return GAMMA; }

namespace trebuchet {
namespace physics {

double convertDegToRad(double deg) {
    return deg * M_PI / 180.0;
}

double convertRadToDeg(double rad) {
    return rad * 180.0 / M_PI;
}

double calculateViscosity(double temperature_k) {
    return SUTHERLAND_MU0 * std::pow(temperature_k / SUTHERLAND_T0, 1.5)
           * (SUTHERLAND_T0 + SUTHERLAND_S) / (temperature_k + SUTHERLAND_S);
}

double calculateMachNumber(double velocity, double temperature_k) {
    double gamma = 1.4;
    double R = 287.058;
    double c = std::sqrt(gamma * R * temperature_k);
    return velocity / c;
}

double calculatePrandtlGlauertCorrection(double mach_number) {
    if (mach_number >= 1.0) mach_number = 0.999;
    double beta = std::sqrt(1.0 - mach_number * mach_number);
    double karman_tsien = 1.0 / beta
        + (mach_number * mach_number) / (2.0 * beta * beta * beta)
        * (1.0 + (GAMMA - 1.0) / 2.0 * beta * beta);
    return std::min(karman_tsien, 1.0 / beta * 1.5);
}

double calculateTransonicWaveDrag(double mach_number) {
    double mcrit = 0.75;
    if (mach_number <= mcrit) return 0.0;
    if (mach_number > 1.2) {
        double t = (mach_number - 1.2) / 0.3;
        t = std::clamp(t, 0.0, 1.0);
        double peak_drag = 0.35;
        return peak_drag * (1.0 - t * t * (3.0 - 2.0 * t));
    }
    double t = (mach_number - mcrit) / (1.05 - mcrit);
    t = std::clamp(t, 0.0, 1.0);
    double cd_wave = 0.35 * t * t * (3.0 - 2.0 * t);
    return cd_wave;
}

double calculateSupersonicNewtonianDrag(double mach_number) {
    if (mach_number <= 1.0) return 0.0;
    double beta = std::sqrt(mach_number * mach_number - 1.0);
    double cd_newtonian = 2.0 / (gamma_ratio() * mach_number * mach_number)
        * (1.0 + 0.5 * gamma_ratio() * beta * beta);
    double cd_cone = 0.7;
    double blend = std::clamp((mach_number - 1.2) / 1.5, 0.0, 1.0);
    return cd_cone * (1.0 - blend) + cd_newtonian * blend;
}

double gamma_ratio() { return 1.4; }

double calculateCompressibleDragCoefficient(
    double mach_number,
    double incompressible_cd,
    double reynolds_number
) {
    if (mach_number < 0.0) mach_number = 0.0;

    double cd_viscous = incompressible_cd;
    if (reynolds_number > 0 && reynolds_number < 5e5) {
        cd_viscous *= 1.0 + 0.3 * std::exp(-reynolds_number / 1e5);
    }

    if (mach_number < 0.3) {
        return cd_viscous;
    }

    if (mach_number < 0.8) {
        double pg = calculatePrandtlGlauertCorrection(mach_number);
        return cd_viscous * std::min(pg, 2.5);
    }

    if (mach_number <= 1.2) {
        double pg = calculatePrandtlGlauertCorrection(std::min(mach_number, 0.79));
        double cd_sub = cd_viscous * std::min(pg, 2.5);
        double cd_wave = calculateTransonicWaveDrag(mach_number);
        double re_factor = 1.0;
        if (mach_number > 0.95) re_factor = 1.0 + 2.0 * (mach_number - 0.95);
        return (cd_sub + cd_wave) * re_factor;
    }

    double cd_wave_super = calculateSupersonicNewtonianDrag(mach_number);
    double cd_friction = cd_viscous * 1.15 / std::sqrt(std::max(1.0, mach_number));
    return cd_friction + cd_wave_super;
}

CyclicSofteningState initializeCyclicState(const SpringMaterial& material) {
    CyclicSofteningState state;
    state.cycle_count = 0;
    state.accumulated_plastic_strain = 0.0;
    state.degraded_shear_modulus = material.shear_modulus;
    state.degraded_yield_strength = material.yield_strength;
    state.back_stress = 0.0;
    state.kinematic_hardening = material.yield_strength * 0.05;
    state.current_damage_parameter = 0.0;
    return state;
}

double calculateCoffinMansonLife(
    const SpringMaterial& material,
    double plastic_strain_amplitude
) {
    if (plastic_strain_amplitude <= 0.0) return 1e12;
    double N_f_elastic = std::pow(
        material.cyclic_strength_coefficient
        / (material.shear_modulus * plastic_strain_amplitude * std::sqrt(3.0)),
        1.0 / material.cyclic_strength_exponent
    );
    double N_f_plastic = std::pow(
        plastic_strain_amplitude / material.fatigue_ductility_coefficient,
        1.0 / material.fatigue_ductility_exponent
    );
    return 1.0 / (1.0 / std::max(N_f_elastic, 1.0) + 1.0 / std::max(N_f_plastic, 1.0));
}

double calculateMinerDamage(
    CyclicSofteningState& state,
    double plastic_strain_amplitude,
    const SpringMaterial& material
) {
    if (plastic_strain_amplitude <= 1e-10) return state.current_damage_parameter;
    double N_f = calculateCoffinMansonLife(material, plastic_strain_amplitude);
    double damage_inc = 1.0 / std::max(N_f, 1.0);
    state.current_damage_parameter = std::min(1.0, state.current_damage_parameter + damage_inc);
    return state.current_damage_parameter;
}

void updateCyclicSoftening(
    TorsionSpringConfig& config,
    double torsion_angle_rad,
    double shear_stress_amplitude
) {
    const SpringMaterial& mat = config.material;
    CyclicSofteningState& state = config.cyclic_state;

    state.cycle_count++;

    double tau_y_current = state.degraded_yield_strength;
    double tau_effective = std::abs(shear_stress_amplitude - state.back_stress);
    double plastic_strain_inc = 0.0;

    if (tau_effective > tau_y_current) {
        double excess_stress = tau_effective - tau_y_current;
        double G = state.degraded_shear_modulus;
        plastic_strain_inc = excess_stress / G;
        state.accumulated_plastic_strain += plastic_strain_inc;

        double C1 = 3500.0, D1 = 120.0;
        state.back_stress += C1 * plastic_strain_inc
            - D1 * state.back_stress * std::abs(plastic_strain_inc);

        double Q_sat = mat.yield_strength * 0.15;
        double b = 18.0;
        double isotropic_inc = Q_sat * (1.0 - std::exp(-b * plastic_strain_inc));
        state.degraded_yield_strength -= isotropic_inc * 0.3;

        double softening_factor = std::exp(
            -0.15 * state.accumulated_plastic_strain / mat.yield_strength * mat.shear_modulus
        );
        state.degraded_shear_modulus = mat.shear_modulus * std::max(0.55, softening_factor);
        state.degraded_yield_strength = std::max(
            mat.yield_strength * 0.5,
            state.degraded_yield_strength
        );

        calculateMinerDamage(state, plastic_strain_inc, mat);
    }

    if (state.cycle_count % 10 == 0 && state.accumulated_plastic_strain > 0) {
        double fatigue_life = calculateCoffinMansonLife(
            mat, state.accumulated_plastic_strain / state.cycle_count
        );
        state.current_damage_parameter = std::min(
            1.0, (double)state.cycle_count / std::max(fatigue_life, 1.0)
        );
    }
}

double calculateSpringConstant(const TorsionSpringConfig& config) {
    double G = config.cyclic_state.degraded_shear_modulus;
    double d = config.wire_diameter;
    double D = config.coil_mean_diameter;
    int Na = config.active_coils;
    return (G * std::pow(d, 4)) / (32.0 * D * Na);
}

double calculateShearStress(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
) {
    double D = config.coil_mean_diameter;
    double d = config.wire_diameter;
    double K = (4.0 * D - d) / (4.0 * (D - d)) + 0.615 * d / D;
    double k = calculateSpringConstant(config);
    double T = k * torsion_angle_rad;
    return K * (16.0 * T) / (M_PI * std::pow(d, 3));
}

double calculateSpringEfficiency(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
) {
    double yield_ratio = calculateShearStress(config, torsion_angle_rad)
                        / config.cyclic_state.degraded_yield_strength;
    double damage = config.cyclic_state.current_damage_parameter;
    double efficiency;
    if (yield_ratio < 0.3) {
        efficiency = 0.75 + 0.1 * yield_ratio / 0.3;
    } else if (yield_ratio < 0.6) {
        efficiency = 0.85 + 0.08 * (yield_ratio - 0.3) / 0.3;
    } else if (yield_ratio < 0.85) {
        efficiency = 0.93 - 0.13 * (yield_ratio - 0.6) / 0.25;
    } else {
        efficiency = 0.80 - 0.5 * (yield_ratio - 0.85);
    }
    efficiency *= std::max(0.5, 1.0 - damage * 0.6);
    return std::clamp(efficiency, 0.0, 1.0);
}

SpringEnergyResult calculateSpringEnergy(
    TorsionSpringConfig& config,
    double torsion_angle_rad
) {
    if (config.cyclic_state.cycle_count == 0) {
        config.cyclic_state = initializeCyclicState(config.material);
    }

    SpringEnergyResult result;
    result.spring_constant = calculateSpringConstant(config);
    double G_current = config.cyclic_state.degraded_shear_modulus;
    double G_original = config.material.shear_modulus;
    double modulus_reduction = G_current / G_original;

    double stress_amplitude = calculateShearStress(config, torsion_angle_rad);
    updateCyclicSoftening(config, torsion_angle_rad, stress_amplitude);

    result.shear_stress = stress_amplitude;
    result.elastic_stress = std::min(stress_amplitude, config.cyclic_state.degraded_yield_strength);
    double tau_diff = std::max(0.0, stress_amplitude - config.cyclic_state.degraded_yield_strength);
    result.plastic_strain = tau_diff / G_current;
    result.stored_energy = 0.5 * result.spring_constant
                           * torsion_angle_rad * torsion_angle_rad
                           * modulus_reduction;
    result.efficiency = calculateSpringEfficiency(config, torsion_angle_rad);
    result.yield_strength_ratio = result.shear_stress
                                  / config.material.yield_strength;
    result.cycle_count = config.cyclic_state.cycle_count;
    result.cyclic_damage_ratio = config.cyclic_state.current_damage_parameter;
    result.fracture_risk = result.yield_strength_ratio > 0.85;
    result.fatigue_risk = result.cyclic_damage_ratio > 0.5;
    return result;
}

RangePredictionResult predictTrajectoryRange(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor,
    double temperature_k
) {
    RangePredictionResult result;
    double theta = convertDegToRad(launch_angle_deg);
    double v0x = release_velocity * std::cos(theta);
    double v0y = release_velocity * std::sin(theta);
    double Cd0 = projectile.drag_coefficient_incompressible;
    double A = projectile.cross_section_area;
    double m = projectile.mass;
    double mu = calculateViscosity(temperature_k);

    double ideal_range = (release_velocity * release_velocity
                         * std::sin(2.0 * theta)) / GRAVITY;

    double dt = 0.001;
    double x = 0.0, y = 0.0;
    double vx = v0x, vy = v0y;
    double max_h = 0.0;
    double t = 0.0;
    double max_mach = 0.0;
    double avg_correction = 0.0;
    int n_steps = 0;

    while (y >= 0.0 && t < 100.0) {
        double v_mag = std::sqrt(vx * vx + vy * vy);
        double mach = calculateMachNumber(v_mag, temperature_k - y * 0.0065);
        max_mach = std::max(max_mach, mach);
        double Re = v_mag > 0 ? AIR_DENSITY * v_mag * projectile.diameter / mu : 0;
        double Cd = calculateCompressibleDragCoefficient(mach, Cd0, Re) * air_resistance_factor;
        double Cd_incomp = Cd0 * air_resistance_factor;
        avg_correction += Cd / std::max(Cd_incomp, 0.01);
        n_steps++;
        double drag_coeff = 0.5 * AIR_DENSITY * Cd * A / m;
        double ax = -drag_coeff * v_mag * vx;
        double ay = -GRAVITY - drag_coeff * v_mag * vy;
        vx += ax * dt;
        vy += ay * dt;
        x += vx * dt;
        y += vy * dt;
        max_h = std::max(max_h, y);
        t += dt;
    }

    double actual_x = x - vx * dt;
    double actual_y = y - vy * dt;
    if (std::abs(y - (y - vy * dt)) > 1e-9) {
        actual_x = (x - vx * dt) + (-actual_y) * vx / vy;
    }

    result.predicted_range = std::max(0.0, actual_x);
    result.max_height = max_h;
    result.flight_time = t;
    result.air_resistance_factor = air_resistance_factor;
    result.max_mach = max_mach;
    result.compressibility_correction = n_steps > 0 ? avg_correction / n_steps : 1.0;
    result.insufficient_range = result.predicted_range < (ideal_range * OPTIMAL_RANGE_PERCENT);

    return result;
}

TrajectoryResult calculateFullTrajectory(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor,
    double time_step,
    double temperature_k
) {
    TrajectoryResult result;
    double theta = convertDegToRad(launch_angle_deg);
    double v0x = release_velocity * std::cos(theta);
    double v0y = release_velocity * std::sin(theta);
    double Cd0 = projectile.drag_coefficient_incompressible;
    double A = projectile.cross_section_area;
    double m = projectile.mass;
    double mu = calculateViscosity(temperature_k);

    double dt = time_step;
    double x = 0.0, y = 0.0;
    double vx = v0x, vy = v0y;
    double max_h = 0.0;
    double t = 0.0;
    double max_mach = 0.0;

    result.trajectory_points.emplace_back(x, y);
    result.mach_profile.emplace_back(0.0, calculateMachNumber(release_velocity, temperature_k));

    while (y >= 0.0 && t < 100.0) {
        double v_mag = std::sqrt(vx * vx + vy * vy);
        double local_temp = temperature_k - y * 0.0065;
        double mach = calculateMachNumber(v_mag, local_temp);
        max_mach = std::max(max_mach, mach);
        double Re = v_mag > 0 ? AIR_DENSITY * v_mag * projectile.diameter / mu : 0;
        double Cd = calculateCompressibleDragCoefficient(mach, Cd0, Re) * air_resistance_factor;
        double drag_coeff = 0.5 * AIR_DENSITY * Cd * A / m;
        double ax = -drag_coeff * v_mag * vx;
        double ay = -GRAVITY - drag_coeff * v_mag * vy;
        vx += ax * dt;
        vy += ay * dt;
        x += vx * dt;
        y += vy * dt;
        max_h = std::max(max_h, y);
        t += dt;
        result.trajectory_points.emplace_back(x, std::max(0.0, y));
        result.mach_profile.emplace_back(x, mach);
        if (y < 0) break;
    }

    result.predicted_range = std::max(0.0, x);
    result.max_height = max_h;
    result.flight_time = t;
    result.impact_velocity = std::sqrt(vx * vx + vy * vy);
    result.impact_mach = calculateMachNumber(result.impact_velocity, temperature_k);
    result.max_mach = max_mach;
    result.launch_angle_optimal = findOptimalLaunchAngle(
        projectile, release_velocity, air_resistance_factor, temperature_k
    );

    return result;
}

double findOptimalLaunchAngle(
    const ProjectileConfig& projectile,
    double release_velocity,
    double air_resistance_factor,
    double temperature_k
) {
    double best_angle = 45.0;
    double best_range = 0.0;
    for (double angle = 10.0; angle <= 80.0; angle += 1.0) {
        auto pred = predictTrajectoryRange(
            projectile, release_velocity, angle, air_resistance_factor, temperature_k
        );
        if (pred.predicted_range > best_range) {
            best_range = pred.predicted_range;
            best_angle = angle;
        }
    }
    return best_angle;
}

}
}
