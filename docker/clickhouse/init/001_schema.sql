CREATE DATABASE IF NOT EXISTS trebuchet_sim;

USE trebuchet_sim;

CREATE TABLE IF NOT EXISTS sensor_data (
    machine_id String,
    timestamp DateTime64(9) CODEC(Delta(8), ZSTD(1)),
    torsion_angle_rad Float64,
    stored_energy_joules Float64,
    release_velocity Float64,
    actual_range_meters Float64,
    predicted_range_meters Float64,
    efficiency Float64,
    projectile_mass_kg Float64,
    launch_angle_deg Float64,
    spring_status String,
    risk_level UInt8,
    shear_stress_pa Float64,
    elastic_stress_pa Float64,
    plastic_strain Float64,
    cycle_count UInt64,
    cyclic_damage_ratio Float64,
    modulus_reduction Float64,
    max_mach Float64,
    compressibility_correction Float64,
    fatigue_risk Float64,
    checksum UInt32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 7 DAY
    TO VOLUME 'hot',
    timestamp + INTERVAL 30 DAY
    TO VOLUME 'cold',
    timestamp + INTERVAL 365 DAY
    DELETE
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS spring_energy_data (
    machine_id String,
    timestamp DateTime64(9) CODEC(Delta(8), ZSTD(1)),
    torsion_angle_rad Float64,
    stored_energy_joules Float64,
    max_stored_energy_joules Float64,
    release_efficiency Float64,
    theoretical_efficiency Float64,
    shear_stress_pa Float64,
    yield_strength_ratio Float64,
    modulus_reduction Float64,
    back_stress_pa Float64,
    degraded_yield_strength_pa Float64,
    cycle_count UInt64,
    cyclic_damage_ratio Float64,
    plastic_strain Float64,
    remaining_life_cycles Int64,
    fracture_risk_flag UInt8,
    fatigue_risk_flag UInt8,
    risk_level UInt8,
    checksum UInt32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 90 DAY
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS range_predictions (
    machine_id String,
    timestamp DateTime64(9) CODEC(Delta(8), ZSTD(1)),
    projectile_mass_kg Float64,
    launch_angle_deg Float64,
    release_velocity Float64,
    predicted_range_meters Float64,
    actual_range_meters Float64,
    range_accuracy_ratio Float64,
    optimal_launch_angle_deg Float64,
    optimal_range_meters Float64,
    max_mach Float64,
    avg_compressibility_correction Float64,
    impact_velocity Float64,
    impact_mach Float64,
    temperature_k Float64,
    air_density_kgm3 Float64,
    trajectory_points Nested(
        t Float64,
        x Float64,
        y Float64,
        vx Float64,
        vy Float64,
        mach Float64,
        cd Float64
    ),
    checksum UInt32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 90 DAY
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS alerts (
    machine_id String,
    timestamp DateTime64(9) CODEC(Delta(8), ZSTD(1)),
    alert_type String,
    alert_level String,
    message String,
    torsion_angle_rad Float64,
    stored_energy_joules Float64,
    actual_range_meters Float64,
    predicted_range_meters Float64,
    threshold_value Float64,
    cycle_count UInt64,
    cyclic_damage_ratio Float64,
    remaining_life_cycles Int64,
    risk_level UInt8,
    mqtt_published UInt8 DEFAULT 0,
    checksum UInt32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 365 DAY
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS cyclic_fatigue_log (
    machine_id String,
    timestamp DateTime64(9) CODEC(Delta(8), ZSTD(1)),
    cycle_count UInt64,
    accumulated_plastic_strain Float64,
    degraded_shear_modulus Float64,
    degraded_yield_strength Float64,
    back_stress Float64,
    kinematic_hardening Float64,
    current_damage_parameter Float64,
    strain_range Float64,
    fatigue_damage_per_cycle Float64,
    estimated_remaining_cycles Int64,
    checksum UInt32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 730 DAY
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS sensor_data_1m (
    machine_id String,
    timestamp DateTime CODEC(Delta(4), ZSTD(1)),
    avg_torsion_angle Float64,
    max_torsion_angle Float64,
    min_torsion_angle Float64,
    avg_stored_energy Float64,
    max_stored_energy Float64,
    avg_release_velocity Float64,
    avg_actual_range Float64,
    avg_efficiency Float64,
    sum_cycle_count UInt64,
    avg_cyclic_damage Float64,
    max_risk_level UInt8,
    sample_count UInt32
) ENGINE = AggregatingMergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 365 DAY
SETTINGS index_granularity = 8192;

CREATE MATERIALIZED VIEW IF NOT EXISTS sensor_data_1m_mv
TO sensor_data_1m
AS SELECT
    machine_id,
    toStartOfMinute(timestamp) AS timestamp,
    avg(torsion_angle_rad) AS avg_torsion_angle,
    max(torsion_angle_rad) AS max_torsion_angle,
    min(torsion_angle_rad) AS min_torsion_angle,
    avg(stored_energy_joules) AS avg_stored_energy,
    max(stored_energy_joules) AS max_stored_energy,
    avg(release_velocity) AS avg_release_velocity,
    avg(actual_range_meters) AS avg_actual_range,
    avg(efficiency) AS avg_efficiency,
    sum(cycle_count) AS sum_cycle_count,
    avg(cyclic_damage_ratio) AS avg_cyclic_damage,
    max(risk_level) AS max_risk_level,
    count() AS sample_count
FROM sensor_data
GROUP BY machine_id, timestamp;

CREATE TABLE IF NOT EXISTS sensor_data_1h (
    machine_id String,
    timestamp DateTime CODEC(Delta(4), ZSTD(1)),
    avg_torsion_angle Float64,
    max_torsion_angle Float64,
    min_torsion_angle Float64,
    avg_stored_energy Float64,
    max_stored_energy Float64,
    avg_release_velocity Float64,
    avg_actual_range Float64,
    avg_efficiency Float64,
    sum_cycle_count UInt64,
    avg_cyclic_damage Float64,
    max_risk_level UInt8,
    sample_count UInt32
) ENGINE = AggregatingMergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 730 DAY
SETTINGS index_granularity = 8192;

CREATE MATERIALIZED VIEW IF NOT EXISTS sensor_data_1h_mv
TO sensor_data_1h
AS SELECT
    machine_id,
    toStartOfHour(timestamp) AS timestamp,
    avg(avg_torsion_angle) AS avg_torsion_angle,
    max(max_torsion_angle) AS max_torsion_angle,
    min(min_torsion_angle) AS min_torsion_angle,
    avg(avg_stored_energy) AS avg_stored_energy,
    max(max_stored_energy) AS max_stored_energy,
    avg(avg_release_velocity) AS avg_release_velocity,
    avg(avg_actual_range) AS avg_actual_range,
    avg(avg_efficiency) AS avg_efficiency,
    sum(sum_cycle_count) AS sum_cycle_count,
    avg(avg_cyclic_damage) AS avg_cyclic_damage,
    max(max_risk_level) AS max_risk_level,
    sum(sample_count) AS sample_count
FROM sensor_data_1m
GROUP BY machine_id, timestamp;

CREATE TABLE IF NOT EXISTS latest_machine_status (
    machine_id String PRIMARY KEY,
    last_seen DateTime64(9),
    last_torsion_angle Float64,
    last_stored_energy Float64,
    last_release_velocity Float64,
    last_actual_range Float64,
    last_risk_level UInt8,
    cycle_count UInt64,
    cyclic_damage_ratio Float64,
    remaining_life_cycles Int64,
    spring_status String
) ENGINE = ReplacingMergeTree(last_seen)
ORDER BY machine_id
TTL last_seen + INTERVAL 30 DAY DELETE
SETTINGS index_granularity = 8192;

CREATE MATERIALIZED VIEW IF NOT EXISTS latest_machine_status_mv
TO latest_machine_status
AS SELECT
    machine_id,
    timestamp AS last_seen,
    torsion_angle_rad AS last_torsion_angle,
    stored_energy_joules AS last_stored_energy,
    release_velocity AS last_release_velocity,
    actual_range_meters AS last_actual_range,
    risk_level AS last_risk_level,
    cycle_count,
    cyclic_damage_ratio,
    toInt64(1000000) AS remaining_life_cycles,
    spring_status
FROM sensor_data
ORDER BY machine_id;
