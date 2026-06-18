const TrebuchetPhysics = (function () {
    const GRAVITY = 9.80665;
    const AIR_DENSITY = 1.225;

    const MATERIALS = {
        steel65mn: { shearModulus: 79.3e9, yieldStrength: 785e6, name: "65Mn弹簧钢" },
        steel50crva: { shearModulus: 78.5e9, yieldStrength: 1080e6, name: "50CrVA弹簧钢" }
    };

    function degToRad(deg) { return deg * Math.PI / 180.0; }
    function radToDeg(rad) { return rad * 180.0 / Math.PI; }

    function calculateSpringConstant(config) {
        const { wireDiameter, coilMeanDiameter, activeCoils, material } = config;
        const G = material.shearModulus;
        const d = wireDiameter;
        const D = coilMeanDiameter;
        const Na = activeCoils;
        return (G * Math.pow(d, 4)) / (32.0 * D * Na);
    }

    function calculateShearStress(config, torsionAngleRad) {
        const { wireDiameter, coilMeanDiameter } = config;
        const D = coilMeanDiameter;
        const d = wireDiameter;
        const K = (4.0 * D - d) / (4.0 * (D - d)) + 0.615 * d / D;
        const k = calculateSpringConstant(config);
        const T = k * torsionAngleRad;
        return K * (16.0 * T) / (Math.PI * Math.pow(d, 3));
    }

    function calculateSpringEfficiency(config, torsionAngleRad) {
        const stress = calculateShearStress(config, torsionAngleRad);
        const yieldRatio = stress / config.material.yieldStrength;
        let efficiency;
        if (yieldRatio < 0.3) {
            efficiency = 0.75 + 0.1 * yieldRatio / 0.3;
        } else if (yieldRatio < 0.6) {
            efficiency = 0.85 + 0.08 * (yieldRatio - 0.3) / 0.3;
        } else if (yieldRatio < 0.85) {
            efficiency = 0.93 - 0.13 * (yieldRatio - 0.6) / 0.25;
        } else {
            efficiency = 0.80 - 0.5 * (yieldRatio - 0.85);
        }
        return Math.max(0, Math.min(1, efficiency));
    }

    function calculateSpringEnergy(config, torsionAngleRad) {
        const springConstant = calculateSpringConstant(config);
        const storedEnergy = 0.5 * springConstant * torsionAngleRad * torsionAngleRad;
        const shearStress = calculateShearStress(config, torsionAngleRad);
        const efficiency = calculateSpringEfficiency(config, torsionAngleRad);
        const yieldRatio = shearStress / config.material.yieldStrength;
        const fractureRisk = yieldRatio > 0.85;
        let riskLevel = "normal";
        if (yieldRatio > 0.85) riskLevel = "critical";
        else if (yieldRatio > 0.70) riskLevel = "warning";

        return {
            springConstant,
            storedEnergy,
            shearStress,
            efficiency,
            yieldStrengthRatio: yieldRatio,
            fractureRisk,
            riskLevel
        };
    }

    function predictTrajectoryRange(projectile, releaseVelocity, launchAngleDeg, airFactor = 1.0) {
        const theta = degToRad(launchAngleDeg);
        const v0x = releaseVelocity * Math.cos(theta);
        const v0y = releaseVelocity * Math.sin(theta);
        const Cd = projectile.dragCoefficient * airFactor;
        const A = projectile.crossSectionArea;
        const m = projectile.mass;
        const drag = 0.5 * AIR_DENSITY * Cd * A / m;
        const dt = 0.001;
        let x = 0, y = 0, vx = v0x, vy = v0y;
        let maxH = 0, t = 0;

        while (y >= 0 && t < 100) {
            const vMag = Math.sqrt(vx * vx + vy * vy);
            const ax = -drag * vMag * vx;
            const ay = -GRAVITY - drag * vMag * vy;
            vx += ax * dt;
            vy += ay * dt;
            x += vx * dt;
            y += vy * dt;
            maxH = Math.max(maxH, y);
            t += dt;
            if (y < 0) {
                const xPrev = x - vx * dt;
                const yPrev = y - vy * dt;
                if (Math.abs(vy) > 1e-9) {
                    x = xPrev + (-yPrev) * vx / vy;
                } else {
                    x = xPrev;
                }
                break;
            }
        }

        return {
            predictedRange: Math.max(0, x),
            maxHeight: maxH,
            flightTime: t,
            airResistanceFactor: airFactor,
            insufficientRange: false
        };
    }

    function calculateFullTrajectory(projectile, releaseVelocity, launchAngleDeg, airFactor = 1.0, timeStep = 0.01) {
        const theta = degToRad(launchAngleDeg);
        const v0x = releaseVelocity * Math.cos(theta);
        const v0y = releaseVelocity * Math.sin(theta);
        const Cd = projectile.dragCoefficient * airFactor;
        const A = projectile.crossSectionArea;
        const m = projectile.mass;
        const drag = 0.5 * AIR_DENSITY * Cd * A / m;
        const dt = timeStep;
        let x = 0, y = 0, vx = v0x, vy = v0y;
        let maxH = 0, t = 0;
        const points = [{ x: 0, y: 0 }];

        while (y >= 0 && t < 100) {
            const vMag = Math.sqrt(vx * vx + vy * vy);
            const ax = -drag * vMag * vx;
            const ay = -GRAVITY - drag * vMag * vy;
            vx += ax * dt;
            vy += ay * dt;
            x += vx * dt;
            y += vy * dt;
            maxH = Math.max(maxH, y);
            t += dt;
            points.push({ x: Math.max(0, x), y: Math.max(0, y) });
            if (y < 0) break;
        }

        return {
            predictedRange: Math.max(0, x),
            maxHeight: maxH,
            flightTime: t,
            impactVelocity: Math.sqrt(vx * vx + vy * vy),
            launchAngleOptimal: findOptimalLaunchAngle(projectile, releaseVelocity, airFactor),
            trajectoryPoints: points
        };
    }

    function findOptimalLaunchAngle(projectile, releaseVelocity, airFactor = 1.0) {
        let bestAngle = 45;
        let bestRange = 0;
        for (let angle = 10; angle <= 80; angle += 1) {
            const result = predictTrajectoryRange(projectile, releaseVelocity, angle, airFactor);
            if (result.predictedRange > bestRange) {
                bestRange = result.predictedRange;
                bestAngle = angle;
            }
        }
        return bestAngle;
    }

    function calculateReleaseVelocity(springEnergy, projectileMass, efficiency) {
        return Math.sqrt(2.0 * springEnergy * efficiency / projectileMass);
    }

    return {
        GRAVITY,
        AIR_DENSITY,
        MATERIALS,
        degToRad,
        radToDeg,
        calculateSpringConstant,
        calculateShearStress,
        calculateSpringEfficiency,
        calculateSpringEnergy,
        predictTrajectoryRange,
        calculateFullTrajectory,
        findOptimalLaunchAngle,
        calculateReleaseVelocity
    };
})();
