const TrebuchetPhysics = (function () {
    const GRAVITY = 9.80665;
    const AIR_DENSITY = 1.225;
    const SPEED_OF_SOUND = 343.2;
    const SUTHERLAND_T0 = 273.15;
    const SUTHERLAND_MU0 = 1.716e-5;
    const SUTHERLAND_S = 110.4;
    const GAMMA = 1.4;

    const MATERIALS = {
        steel65mn: {
            shearModulus: 79.3e9,
            yieldStrength: 785e6,
            density: 7850,
            fatigueDuctilityCoeff: 0.42,
            fatigueDuctilityExp: -0.58,
            cyclicStrengthCoeff: 1300e6,
            cyclicStrengthExp: -0.10,
            name: "65Mn弹簧钢"
        },
        steel50crva: {
            shearModulus: 78.5e9,
            yieldStrength: 1080e6,
            density: 7800,
            fatigueDuctilityCoeff: 0.38,
            fatigueDuctilityExp: -0.55,
            cyclicStrengthCoeff: 1650e6,
            cyclicStrengthExp: -0.08,
            name: "50CrVA弹簧钢"
        }
    };

    function createCyclicState(material) {
        return {
            cycleCount: 0,
            accumulatedPlasticStrain: 0,
            degradedShearModulus: material.shearModulus,
            degradedYieldStrength: material.yieldStrength,
            backStress: 0,
            kinematicHardening: material.yieldStrength * 0.05,
            currentDamageParameter: 0
        };
    }

    function degToRad(deg) { return deg * Math.PI / 180.0; }
    function radToDeg(rad) { return rad * 180.0 / Math.PI; }

    function calculateViscosity(temperatureK) {
        return SUTHERLAND_MU0 * Math.pow(temperatureK / SUTHERLAND_T0, 1.5)
            * (SUTHERLAND_T0 + SUTHERLAND_S) / (temperatureK + SUTHERLAND_S);
    }

    function calculateMachNumber(velocity, temperatureK = 288.15) {
        const R = 287.058;
        const c = Math.sqrt(GAMMA * R * temperatureK);
        return velocity / c;
    }

    function calculatePrandtlGlauertCorrection(mach) {
        if (mach >= 1.0) mach = 0.999;
        const beta = Math.sqrt(1.0 - mach * mach);
        const karmanTsien = 1.0 / beta
            + (mach * mach) / (2.0 * beta * beta * beta)
            * (1.0 + (GAMMA - 1.0) / 2.0 * beta * beta);
        return Math.min(karmanTsien, 1.0 / beta * 1.5);
    }

    function smoothstep(t) { return t * t * (3 - 2 * t); }

    function calculateTransonicWaveDrag(mach) {
        const mcrit = 0.75;
        if (mach <= mcrit) return 0.0;
        if (mach > 1.2) {
            const t = Math.min(1, (mach - 1.2) / 0.3);
            return 0.35 * (1.0 - smoothstep(t));
        }
        const t = Math.min(1, (mach - mcrit) / (1.05 - mcrit));
        return 0.35 * smoothstep(t);
    }

    function calculateSupersonicNewtonianDrag(mach) {
        if (mach <= 1.0) return 0.0;
        const beta = Math.sqrt(mach * mach - 1.0);
        const cdNewtonian = 2.0 / (GAMMA * mach * mach)
            * (1.0 + 0.5 * GAMMA * beta * beta);
        const cdCone = 0.7;
        const blend = Math.min(1, (mach - 1.2) / 1.5);
        return cdCone * (1.0 - blend) + cdNewtonian * blend;
    }

    function calculateCompressibleDragCoefficient(mach, incompressibleCd, reynoldsNumber = 1e6) {
        if (mach < 0) mach = 0;
        let cdViscous = incompressibleCd;
        if (reynoldsNumber > 0 && reynoldsNumber < 5e5) {
            cdViscous *= 1.0 + 0.3 * Math.exp(-reynoldsNumber / 1e5);
        }
        if (mach < 0.3) return cdViscous;
        if (mach < 0.8) {
            const pg = calculatePrandtlGlauertCorrection(mach);
            return cdViscous * Math.min(pg, 2.5);
        }
        if (mach <= 1.2) {
            const pg = calculatePrandtlGlauertCorrection(Math.min(mach, 0.79));
            const cdSub = cdViscous * Math.min(pg, 2.5);
            const cdWave = calculateTransonicWaveDrag(mach);
            const reFactor = mach > 0.95 ? 1.0 + 2.0 * (mach - 0.95) : 1.0;
            return (cdSub + cdWave) * reFactor;
        }
        const cdWaveSuper = calculateSupersonicNewtonianDrag(mach);
        const cdFriction = cdViscous * 1.15 / Math.sqrt(Math.max(1, mach));
        return cdFriction + cdWaveSuper;
    }

    function calculateCoffinMansonLife(material, plasticStrainAmp) {
        if (plasticStrainAmp <= 0) return 1e12;
        const NfElastic = Math.pow(
            material.cyclicStrengthCoeff
            / (material.shearModulus * plasticStrainAmp * Math.sqrt(3)),
            1.0 / material.cyclicStrengthExp
        );
        const NfPlastic = Math.pow(
            plasticStrainAmp / material.fatigueDuctilityCoeff,
            1.0 / material.fatigueDuctilityExp
        );
        return 1.0 / (1.0 / Math.max(NfElastic, 1) + 1.0 / Math.max(NfPlastic, 1));
    }

    function updateCyclicSoftening(config, torsionAngleRad, shearStressAmp) {
        const mat = config.material;
        const state = config.cyclicState;
        state.cycleCount++;

        const tauY = state.degradedYieldStrength;
        const tauEff = Math.abs(shearStressAmp - state.backStress);
        let plasticInc = 0;

        if (tauEff > tauY) {
            const excess = tauEff - tauY;
            const G = state.degradedShearModulus;
            plasticInc = excess / G;
            state.accumulatedPlasticStrain += plasticInc;

            const C1 = 3500, D1 = 120;
            state.backStress += C1 * plasticInc - D1 * state.backStress * Math.abs(plasticInc);

            const Qsat = mat.yieldStrength * 0.15;
            const b = 18;
            const isoInc = Qsat * (1 - Math.exp(-b * plasticInc));
            state.degradedYieldStrength -= isoInc * 0.3;

            const softeningFactor = Math.exp(
                -0.15 * state.accumulatedPlasticStrain / mat.yieldStrength * mat.shearModulus
            );
            state.degradedShearModulus = mat.shearModulus * Math.max(0.55, softeningFactor);
            state.degradedYieldStrength = Math.max(
                mat.yieldStrength * 0.5,
                state.degradedYieldStrength
            );

            if (plasticInc > 1e-10) {
                const Nf = calculateCoffinMansonLife(mat, plasticInc);
                state.currentDamageParameter = Math.min(1, state.currentDamageParameter + 1 / Math.max(Nf, 1));
            }
        }

        if (state.cycleCount % 10 === 0 && state.accumulatedPlasticStrain > 0) {
            const fatigueLife = calculateCoffinMansonLife(
                mat, state.accumulatedPlasticStrain / state.cycleCount
            );
            state.currentDamageParameter = Math.min(
                1, state.cycleCount / Math.max(fatigueLife, 1)
            );
        }
    }

    function calculateSpringConstant(config) {
        const { wireDiameter, coilMeanDiameter, activeCoils } = config;
        const G = config.cyclicState.degradedShearModulus;
        return (G * Math.pow(wireDiameter, 4)) / (32.0 * coilMeanDiameter * activeCoils);
    }

    function calculateShearStress(config, torsionAngleRad) {
        const { wireDiameter: d, coilMeanDiameter: D } = config;
        const K = (4.0 * D - d) / (4.0 * (D - d)) + 0.615 * d / D;
        const k = calculateSpringConstant(config);
        const T = k * torsionAngleRad;
        return K * (16.0 * T) / (Math.PI * Math.pow(d, 3));
    }

    function calculateSpringEfficiency(config, torsionAngleRad) {
        const stress = calculateShearStress(config, torsionAngleRad);
        const yieldRatio = stress / config.cyclicState.degradedYieldStrength;
        const damage = config.cyclicState.currentDamageParameter;
        let efficiency;
        if (yieldRatio < 0.3) efficiency = 0.75 + 0.1 * yieldRatio / 0.3;
        else if (yieldRatio < 0.6) efficiency = 0.85 + 0.08 * (yieldRatio - 0.3) / 0.3;
        else if (yieldRatio < 0.85) efficiency = 0.93 - 0.13 * (yieldRatio - 0.6) / 0.25;
        else efficiency = 0.80 - 0.5 * (yieldRatio - 0.85);
        efficiency *= Math.max(0.5, 1.0 - damage * 0.6);
        return Math.max(0, Math.min(1, efficiency));
    }

    function calculateSpringEnergy(config, torsionAngleRad) {
        if (!config.cyclicState || config.cyclicState.cycleCount === 0) {
            config.cyclicState = createCyclicState(config.material);
        }

        const springConstant = calculateSpringConstant(config);
        const Gcurr = config.cyclicState.degradedShearModulus;
        const Gorg = config.material.shearModulus;
        const modulusReduction = Gcurr / Gorg;

        const stressAmplitude = calculateShearStress(config, torsionAngleRad);
        updateCyclicSoftening(config, torsionAngleRad, stressAmplitude);

        const storedEnergy = 0.5 * springConstant * torsionAngleRad * torsionAngleRad * modulusReduction;
        const efficiency = calculateSpringEfficiency(config, torsionAngleRad);
        const yieldRatio = stressAmplitude / config.material.yieldStrength;
        const damage = config.cyclicState.currentDamageParameter;

        const elasticStress = Math.min(stressAmplitude, config.cyclicState.degradedYieldStrength);
        const tauDiff = Math.max(0, stressAmplitude - config.cyclicState.degradedYieldStrength);
        const plasticStrain = tauDiff / Gcurr;

        let riskLevel = "normal";
        if (yieldRatio > 0.85 || damage > 0.7) riskLevel = "critical";
        else if (yieldRatio > 0.70 || damage > 0.4) riskLevel = "warning";

        return {
            springConstant,
            storedEnergy,
            shearStress: stressAmplitude,
            elasticStress,
            plasticStrain,
            efficiency,
            yieldStrengthRatio: yieldRatio,
            cyclicDamageRatio: damage,
            cycleCount: config.cyclicState.cycleCount,
            fractureRisk: yieldRatio > 0.85,
            fatigueRisk: damage > 0.5,
            riskLevel,
            modulusReduction
        };
    }

    function predictTrajectoryRange(projectile, releaseVelocity, launchAngleDeg,
                                     airFactor = 1.0, temperatureK = 288.15) {
        const theta = degToRad(launchAngleDeg);
        const v0x = releaseVelocity * Math.cos(theta);
        const v0y = releaseVelocity * Math.sin(theta);
        const Cd0 = projectile.dragCoefficientIncompressible || projectile.drag_coefficient || 0.47;
        const A = projectile.crossSectionArea || projectile.cross_section_area || 0.0314;
        const m = projectile.mass;
        const diameter = projectile.diameter || 0.2;
        const mu = calculateViscosity(temperatureK);

        const idealRange = (releaseVelocity * releaseVelocity * Math.sin(2 * theta)) / GRAVITY;
        const dt = 0.001;
        let x = 0, y = 0, vx = v0x, vy = v0y, maxH = 0, t = 0;
        let maxMach = 0, avgCorr = 0, steps = 0;

        while (y >= 0 && t < 100) {
            const vMag = Math.sqrt(vx * vx + vy * vy);
            const localTemp = temperatureK - y * 0.0065;
            const mach = calculateMachNumber(vMag, localTemp);
            maxMach = Math.max(maxMach, mach);
            const Re = vMag > 0 ? AIR_DENSITY * vMag * diameter / mu : 0;
            const Cd = calculateCompressibleDragCoefficient(mach, Cd0, Re) * airFactor;
            const CdIncomp = Cd0 * airFactor;
            avgCorr += Cd / Math.max(CdIncomp, 0.01);
            steps++;
            const drag = 0.5 * AIR_DENSITY * Cd * A / m;
            vx += -drag * vMag * vx * dt;
            vy += (-GRAVITY - drag * vMag * vy) * dt;
            x += vx * dt;
            y += vy * dt;
            maxH = Math.max(maxH, y);
            t += dt;
            if (y < 0) {
                const xPrev = x - vx * dt, yPrev = y - vy * dt;
                if (Math.abs(vy) > 1e-9) x = xPrev + (-yPrev) * vx / vy;
                else x = xPrev;
                break;
            }
        }

        return {
            predictedRange: Math.max(0, x),
            maxHeight: maxH,
            flightTime: t,
            airResistanceFactor: airFactor,
            maxMach,
            compressibilityCorrection: steps > 0 ? avgCorr / steps : 1.0,
            insufficientRange: Math.max(0, x) < idealRange * 0.85
        };
    }

    function calculateFullTrajectory(projectile, releaseVelocity, launchAngleDeg,
                                      airFactor = 1.0, timeStep = 0.01, temperatureK = 288.15) {
        const theta = degToRad(launchAngleDeg);
        const v0x = releaseVelocity * Math.cos(theta);
        const v0y = releaseVelocity * Math.sin(theta);
        const Cd0 = projectile.dragCoefficientIncompressible || projectile.drag_coefficient || 0.47;
        const A = projectile.crossSectionArea || projectile.cross_section_area || 0.0314;
        const m = projectile.mass;
        const diameter = projectile.diameter || 0.2;
        const mu = calculateViscosity(temperatureK);

        const dt = timeStep;
        let x = 0, y = 0, vx = v0x, vy = v0y, maxH = 0, t = 0, maxMach = 0;
        const points = [{ x: 0, y: 0 }];
        const machProfile = [{ x: 0, mach: calculateMachNumber(releaseVelocity, temperatureK) }];

        while (y >= 0 && t < 100) {
            const vMag = Math.sqrt(vx * vx + vy * vy);
            const localTemp = temperatureK - y * 0.0065;
            const mach = calculateMachNumber(vMag, localTemp);
            maxMach = Math.max(maxMach, mach);
            const Re = vMag > 0 ? AIR_DENSITY * vMag * diameter / mu : 0;
            const Cd = calculateCompressibleDragCoefficient(mach, Cd0, Re) * airFactor;
            const drag = 0.5 * AIR_DENSITY * Cd * A / m;
            vx += -drag * vMag * vx * dt;
            vy += (-GRAVITY - drag * vMag * vy) * dt;
            x += vx * dt;
            y += vy * dt;
            maxH = Math.max(maxH, y);
            t += dt;
            points.push({ x: Math.max(0, x), y: Math.max(0, y) });
            machProfile.push({ x: Math.max(0, x), mach });
            if (y < 0) break;
        }

        const impactVel = Math.sqrt(vx * vx + vy * vy);
        return {
            predictedRange: Math.max(0, x),
            maxHeight: maxH,
            flightTime: t,
            impactVelocity: impactVel,
            impactMach: calculateMachNumber(impactVel, temperatureK),
            maxMach,
            launchAngleOptimal: findOptimalLaunchAngle(projectile, releaseVelocity, airFactor, temperatureK),
            trajectoryPoints: points,
            machProfile
        };
    }

    function findOptimalLaunchAngle(projectile, releaseVelocity, airFactor = 1.0, temperatureK = 288.15) {
        let bestAngle = 45, bestRange = 0;
        for (let angle = 10; angle <= 80; angle += 1) {
            const r = predictTrajectoryRange(projectile, releaseVelocity, angle, airFactor, temperatureK);
            if (r.predictedRange > bestRange) { bestRange = r.predictedRange; bestAngle = angle; }
        }
        return bestAngle;
    }

    function calculateReleaseVelocity(springEnergy, projectileMass, efficiency) {
        return Math.sqrt(2.0 * springEnergy * efficiency / projectileMass);
    }

    return {
        GRAVITY,
        AIR_DENSITY,
        SPEED_OF_SOUND,
        GAMMA,
        MATERIALS,
        degToRad,
        radToDeg,
        createCyclicState,
        calculateViscosity,
        calculateMachNumber,
        calculateCompressibleDragCoefficient,
        calculateCoffinMansonLife,
        updateCyclicSoftening,
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
