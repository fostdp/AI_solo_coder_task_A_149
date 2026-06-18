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
    double fatigue_ductility_coefficient;
    double fatigue_ductility_exponent;
    double cyclic_strength_coefficient;
    double cyclic_strength_exponent;
    std::string name;
};

struct CyclicSofteningState {
    int cycle_count;
    double accumulated_plastic_strain;
    double degraded_shear_modulus;
    double degraded_yield_strength;
    double back_stress;
    double kinematic_hardening;
    double current_damage_parameter;
};

struct TorsionSpringConfig {
    double wire_diameter;
    double coil_mean_diameter;
    int active_coils;
    SpringMaterial material;
    CyclicSofteningState cyclic_state;
};

struct SpringEnergyResult {
    double stored_energy;
    double spring_constant;
    double shear_stress;
    double elastic_stress;
    double plastic_strain;
    double efficiency;
    double yield_strength_ratio;
    double cyclic_damage_ratio;
    int cycle_count;
    bool fracture_risk;
    bool fatigue_risk;
};

struct ProjectileConfig {
    double mass;
    double drag_coefficient_incompressible;
    double cross_section_area;
    double diameter;
};

struct TrajectoryResult {
    double predicted_range;
    double max_height;
    double flight_time;
    double impact_velocity;
    double impact_mach;
    double launch_angle_optimal;
    double max_mach;
    std::vector<std::pair<double, double>> trajectory_points;
    std::vector<std::pair<double, double>> mach_profile;
};

struct RangePredictionResult {
    double predicted_range;
    double max_height;
    double flight_time;
    double air_resistance_factor;
    double max_mach;
    double compressibility_correction;
    bool insufficient_range;
};

const SpringMaterial STEEL_65MN = {
    79.3e9,
    785e6,
    7850.0,
    0.42,
    -0.58,
    1300e6,
    -0.10,
    "65Mn弹簧钢"
};

const SpringMaterial STEEL_50CRVA = {
    78.5e9,
    1080e6,
    7800.0,
    0.38,
    -0.55,
    1650e6,
    -0.08,
    "50CrVA弹簧钢"
};

constexpr double GRAVITY = 9.80665;
constexpr double AIR_DENSITY = 1.225;
constexpr double OPTIMAL_RANGE_PERCENT = 0.85;
constexpr double SPEED_OF_SOUND = 343.2;
constexpr double SUTHERLAND_T0 = 273.15;
constexpr double SUTHERLAND_MU0 = 1.716e-5;
constexpr double SUTHERLAND_S = 110.4;

double calculateViscosity(double temperature_k);

double calculateMachNumber(double velocity, double temperature_k = 288.15);

double calculateCompressibleDragCoefficient(
    double mach_number,
    double incompressible_cd,
    double reynolds_number = 1e6
);

double calculatePrandtlGlauertCorrection(double mach_number);

double calculateTransonicWaveDrag(double mach_number);

double calculateSupersonicNewtonianDrag(double mach_number);

CyclicSofteningState initializeCyclicState(const SpringMaterial& material);

void updateCyclicSoftening(
    TorsionSpringConfig& config,
    double torsion_angle_rad,
    double shear_stress_amplitude
);

double calculateCoffinMansonLife(
    const SpringMaterial& material,
    double plastic_strain_amplitude
);

double calculateMinerDamage(
    CyclicSofteningState& state,
    double plastic_strain_amplitude,
    const SpringMaterial& material
);

SpringEnergyResult calculateSpringEnergy(
    TorsionSpringConfig& config,
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
    double air_resistance_factor = 1.0,
    double temperature_k = 288.15
);

TrajectoryResult calculateFullTrajectory(
    const ProjectileConfig& projectile,
    double release_velocity,
    double launch_angle_deg,
    double air_resistance_factor = 1.0,
    double time_step = 0.01,
    double temperature_k = 288.15
);

double findOptimalLaunchAngle(
    const ProjectileConfig& projectile,
    double release_velocity,
    double air_resistance_factor = 1.0,
    double temperature_k = 288.15
);

double convertDegToRad(double deg);
double convertRadToDeg(double rad);

}
}

#endif
