CREATE DATABASE IF NOT EXISTS trebuchet_sim;

USE trebuchet_sim;

CREATE TABLE IF NOT EXISTS trebuchet_sim.sensor_data (
    id UUID DEFAULT generateUUIDv4(),
    machine_id String,
    timestamp DateTime64(3, 'Asia/Shanghai') DEFAULT now64(3, 'Asia/Shanghai'),
    torsion_angle Float64,
    stored_energy Float64,
    release_velocity Float64,
    actual_range Float64,
    predicted_range Float64,
    efficiency Float64,
    projectile_mass Float64,
    launch_angle Float64,
    spring_status String,
    risk_level String DEFAULT 'normal',
    created_at DateTime DEFAULT now()
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 30 DAY;

CREATE TABLE IF NOT EXISTS trebuchet_sim.alerts (
    id UUID DEFAULT generateUUIDv4(),
    machine_id String,
    timestamp DateTime64(3, 'Asia/Shanghai') DEFAULT now64(3, 'Asia/Shanghai'),
    alert_type String,
    alert_level String,
    message String,
    torsion_angle Nullable(Float64),
    stored_energy Nullable(Float64),
    actual_range Nullable(Float64),
    predicted_range Nullable(Float64),
    threshold_value Nullable(Float64),
    acknowledged UInt8 DEFAULT 0,
    created_at DateTime DEFAULT now()
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp, alert_type)
TTL timestamp + INTERVAL 90 DAY;

CREATE TABLE IF NOT EXISTS trebuchet_sim.range_predictions (
    id UUID DEFAULT generateUUIDv4(),
    machine_id String,
    timestamp DateTime64(3, 'Asia/Shanghai') DEFAULT now64(3, 'Asia/Shanghai'),
    projectile_mass Float64,
    launch_angle Float64,
    release_velocity Float64,
    predicted_range Float64,
    max_height Float64,
    flight_time Float64,
    air_resistance_factor Float64,
    created_at DateTime DEFAULT now()
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 90 DAY;

CREATE TABLE IF NOT EXISTS trebuchet_sim.spring_energy_data (
    id UUID DEFAULT generateUUIDv4(),
    machine_id String,
    timestamp DateTime64(3, 'Asia/Shanghai') DEFAULT now64(3, 'Asia/Shanghai'),
    torsion_angle Float64,
    stored_energy Float64,
    shear_stress Float64,
    spring_constant Float64,
    efficiency Float64,
    yield_strength_ratio Float64,
    created_at DateTime DEFAULT now()
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (machine_id, timestamp)
TTL timestamp + INTERVAL 90 DAY;

CREATE VIEW IF NOT EXISTS trebuchet_sim.latest_machine_status AS
SELECT
    machine_id,
    argMax(timestamp, timestamp) AS last_report_time,
    argMax(torsion_angle, timestamp) AS last_torsion_angle,
    argMax(stored_energy, timestamp) AS last_stored_energy,
    argMax(release_velocity, timestamp) AS last_release_velocity,
    argMax(actual_range, timestamp) AS last_actual_range,
    argMax(predicted_range, timestamp) AS last_predicted_range,
    argMax(risk_level, timestamp) AS current_risk_level,
    countIf(alert_type = 'spring_fracture_risk' AND acknowledged = 0) AS unacknowledged_alerts
FROM trebuchet_sim.sensor_data
LEFT ANY JOIN trebuchet_sim.alerts USING (machine_id)
GROUP BY machine_id;
