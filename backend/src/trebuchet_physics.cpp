#include "trebuchet_physics.h"
#include <algorithm>
#include <numeric>

namespace trebuchet {
namespace physics {

double convertDegToRad(double deg) {
    return deg * M_PI / 180.0;
}

double convertRadToDeg(double rad) {
    return rad * 180.0 / M_PI;
}

double calculateSpringConstant(const TorsionSpringConfig& config) {
    double G = config.material.shear_modulus;
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
    double tau_max = K * (16.0 * T) / (M_PI * std::pow(d, 3));
    return tau_max;
}

double calculateSpringEfficiency(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
) {
    double yield_ratio = calculateShearStress(config, torsion_angle_rad) 
                        / config.material.yield_strength;
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
    return std::clamp(efficiency, 0.0, 1.0);
}

SpringEnergyResult calculateSpringEnergy(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
) {
    SpringEnergyResult result;
    result.spring_constant = calculateSpringConstant(config);
    result.stored_energy = 0.5 * result.spring_constant 
                           * torsion_angle_rad * torsion_angle_rad;
    result.shear_stress = calculateShearStress(config, torsion_angle_rad);
    result.efficiency = calculateSpringEfficiency(config, torsion_angle_rad);
    result.yield_strength_ratio = result.shear_stress 
                                  / config.material.yield_strength;
    result.fracture_risk = result.yield_strength_ratio > 0.85;
    return result;
}

RangePredictionResult predictTrajectoryRange(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor
) {
    RangePredictionResult result;
    double theta = convertDegToRad(launch_angle_deg);
    double v0x = release_velocity * std::cos(theta);
    double v0y = release_velocity * std::sin(theta);
    double Cd = projectile.drag_coefficient * air_resistance_factor;
    double A = projectile.cross_section_area;
    double m = projectile.mass;
    double drag_coeff = 0.5 * AIR_DENSITY * Cd * A / m;

    double ideal_range = (release_velocity * release_velocity 
                         * std::sin(2.0 * theta)) / GRAVITY;

    double dt = 0.001;
    double x = 0.0, y = 0.0;
    double vx = v0x, vy = v0y;
    double max_h = 0.0;
    double t = 0.0;

    while (y >= 0.0 && t < 100.0) {
        double v_mag = std::sqrt(vx * vx + vy * vy);
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
    result.insufficient_range = result.predicted_range < (ideal_range * OPTIMAL_RANGE_PERCENT);

    return result;
}

TrajectoryResult calculateFullTrajectory(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor,
    double time_step
) {
    TrajectoryResult result;
    double theta = convertDegToRad(launch_angle_deg);
    double v0x = release_velocity * std::cos(theta);
    double v0y = release_velocity * std::sin(theta);
    double Cd = projectile.drag_coefficient * air_resistance_factor;
    double A = projectile.cross_section_area;
    double m = projectile.mass;
    double drag_coeff = 0.5 * AIR_DENSITY * Cd * A / m;

    double dt = time_step;
    double x = 0.0, y = 0.0;
    double vx = v0x, vy = v0y;
    double max_h = 0.0;
    double t = 0.0;

    result.trajectory_points.emplace_back(x, y);

    while (y >= 0.0 && t < 100.0) {
        double v_mag = std::sqrt(vx * vx + vy * vy);
        double ax = -drag_coeff * v_mag * vx;
        double ay = -GRAVITY - drag_coeff * v_mag * vy;
        vx += ax * dt;
        vy += ay * dt;
        x += vx * dt;
        y += vy * dt;
        max_h = std::max(max_h, y);
        t += dt;
        result.trajectory_points.emplace_back(x, std::max(0.0, y));
    }

    result.predicted_range = std::max(0.0, x);
    result.max_height = max_h;
    result.flight_time = t;
    result.impact_velocity = std::sqrt(vx * vx + vy * vy);
    result.launch_angle_optimal = findOptimalLaunchAngle(
        projectile, release_velocity, air_resistance_factor
    );

    return result;
}

double findOptimalLaunchAngle(
    const ProjectileConfig& projectile,
    double release_velocity,
    double air_resistance_factor
) {
    double best_angle = 45.0;
    double best_range = 0.0;
    for (double angle = 10.0; angle <= 80.0; angle += 1.0) {
        auto pred = predictTrajectoryRange(
            projectile, release_velocity, angle, air_resistance_factor
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
