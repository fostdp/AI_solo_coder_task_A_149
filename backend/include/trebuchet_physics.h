#ifndef TREBUCHET_PHYSICS_H
#define TREBUCHET_PHYSICS_H

#include <cmath>
#include <string>
#include <vector>

namespace trebuchet {
namespace physics {

struct SpringMaterial {
    double shear_modulus;
    double yield_strength;
    double density;
    std::string name;
};

struct TorsionSpringConfig {
    double wire_diameter;
    double coil_mean_diameter;
    int active_coils;
    SpringMaterial material;
};

struct SpringEnergyResult {
    double stored_energy;
    double spring_constant;
    double shear_stress;
    double efficiency;
    double yield_strength_ratio;
    bool fracture_risk;
};

struct ProjectileConfig {
    double mass;
    double drag_coefficient;
    double cross_section_area;
};

struct TrajectoryResult {
    double predicted_range;
    double max_height;
    double flight_time;
    double impact_velocity;
    double launch_angle_optimal;
    std::vector<std::pair<double, double>> trajectory_points;
};

struct RangePredictionResult {
    double predicted_range;
    double max_height;
    double flight_time;
    double air_resistance_factor;
    bool insufficient_range;
};

const SpringMaterial STEEL_65MN = {
    79.3e9,
    785e6,
    7850.0,
    "65Mn弹簧钢"
};

const SpringMaterial STEEL_50CRVA = {
    78.5e9,
    1080e6,
    7800.0,
    "50CrVA弹簧钢"
};

constexpr double GRAVITY = 9.80665;
constexpr double AIR_DENSITY = 1.225;
constexpr double OPTIMAL_RANGE_PERCENT = 0.85;

SpringEnergyResult calculateSpringEnergy(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
);

double calculateSpringConstant(const TorsionSpringConfig& config);

double calculateShearStress(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
);

double calculateSpringEfficiency(
    const TorsionSpringConfig& config,
    double torsion_angle_rad
);

RangePredictionResult predictTrajectoryRange(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor = 1.0
);

TrajectoryResult calculateFullTrajectory(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor = 1.0,
    double time_step = 0.01
);

double findOptimalLaunchAngle(
    const ProjectileConfig& projectile,
    double release_velocity,
    double air_resistance_factor = 1.0
);

double convertDegToRad(double deg);
double convertRadToDeg(double rad);

}
}

#endif
