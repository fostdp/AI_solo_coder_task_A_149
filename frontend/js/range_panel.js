const RangePanel = (function () {
    "use strict";

    let UI = {
        root: null,
        angleSlider: null,
        angleValue: null,
        velocitySlider: null,
        velocityValue: null,
        massSlider: null,
        massValue: null,
        diameterSlider: null,
        diameterValue: null,
        predictBtn: null,
        optimalBtn: null,
        resultGrid: null,
        comparisonBtn: null,
        canvasId: 'trajectory-canvas'
    };

    let state = {
        config: {
            angle: 45,
            velocity: 80,
            mass: 10,
            diameter: 0.2,
            Cd0: 0.47,
            temperatureK: 288.15,
            dragFactor: 1.0
        },
        currentResult: null,
        comparisonResults: [],
        comparisonLabels: []
    };

    function init(rootId, options = {}) {
        UI.root = document.getElementById(rootId);
        if (!UI.root) {
            console.error('[RangePanel] root not found:', rootId);
            return false;
        }
        Object.assign(UI, options.uiOverrides || {});
        Object.assign(state.config, options.defaults || {});

        ensureDomStructure();
        bindEvents();
        if (window.TrajectoryView && UI.canvasId) {
            try { TrajectoryView.init(UI.canvasId); } catch (e) {
                console.warn('[RangePanel] TrajectoryView init:', e);
            }
        }
        syncControlsFromState();
        if (options.autoPredict) predict();
        return true;
    }

    function ensureDomStructure() {
        if (!UI.root.querySelector('.range-controls')) {
            const wrap = document.createElement('div');
            wrap.className = 'range-panel';
            wrap.innerHTML = `
<div class="panel">
  <h2>弹道预测面板</h2>
  <div class="range-controls">
    <div class="control-row">
      <label>发射角 (°): <span class="val" data-bind="angle">45</span></label>
      <input type="range" data-field="angle" min="0" max="89" step="1" value="45"/>
    </div>
    <div class="control-row">
      <label>释放速度 (m/s): <span class="val" data-bind="velocity">80</span></label>
      <input type="range" data-field="velocity" min="10" max="200" step="1" value="80"/>
    </div>
    <div class="control-row">
      <label>弹丸质量 (kg): <span class="val" data-bind="mass">10</span></label>
      <input type="range" data-field="mass" min="1" max="100" step="1" value="10"/>
    </div>
    <div class="control-row">
      <label>弹丸直径 (m): <span class="val" data-bind="diameter">0.20</span></label>
      <input type="range" data-field="diameter" min="0.05" max="1.0" step="0.01" value="0.2"/>
    </div>
    <div class="button-row">
      <button class="btn btn-primary" data-action="predict">预测射程</button>
      <button class="btn" data-action="optimal">最优角搜索</button>
      <button class="btn" data-action="compare">叠加对比</button>
      <button class="btn btn-ghost" data-action="clear">清空对比</button>
    </div>
  </div>
  <div class="result-grid" data-bind="resultGrid">
    <div class="result-item"><span class="label">预测射程 (m)</span><span class="value predictedRange">-</span></div>
    <div class="result-item"><span class="label">最优发射角 (°)</span><span class="value optimalAngle">-</span></div>
    <div class="result-item"><span class="label">最大高度 (m)</span><span class="value maxHeight">-</span></div>
    <div class="result-item"><span class="label">飞行时间 (s)</span><span class="value flightTime">-</span></div>
    <div class="result-item"><span class="label">最大马赫数 (Ma)</span><span class="value maxMach">-</span></div>
    <div class="result-item"><span class="label">落地马赫数 (Ma)</span><span class="value impactMach">-</span></div>
    <div class="result-item"><span class="label">落地速度 (m/s)</span><span class="value impactVelocity">-</span></div>
    <div class="result-item"><span class="label">可压缩修正系数</span><span class="value compressibilityCorrection">-</span></div>
    <div class="result-item"><span class="label">空气阻力衰减</span><span class="value airResistanceFactor">-</span></div>
    <div class="result-item"><span class="label">无阻力真空射程 (m)</span><span class="value vacuumRange">-</span></div>
  </div>
  <canvas id="trajectory-canvas" class="trajectory-canvas" style="width:100%;height:340px;"></canvas>
</div>`;
            UI.root.innerHTML = '';
            UI.root.appendChild(wrap);
        }
        UI.angleSlider = UI.root.querySelector('input[data-field="angle"]');
        UI.angleValue = UI.root.querySelector('span[data-bind="angle"]');
        UI.velocitySlider = UI.root.querySelector('input[data-field="velocity"]');
        UI.velocityValue = UI.root.querySelector('span[data-bind="velocity"]');
        UI.massSlider = UI.root.querySelector('input[data-field="mass"]');
        UI.massValue = UI.root.querySelector('span[data-bind="mass"]');
        UI.diameterSlider = UI.root.querySelector('input[data-field="diameter"]');
        UI.diameterValue = UI.root.querySelector('span[data-bind="diameter"]');
        UI.resultGrid = UI.root.querySelector('[data-bind="resultGrid"]');
        UI.canvasId = 'trajectory-canvas';
    }

    function bindEvents() {
        [['angle', 'angleSlider', 'angleValue', 'deg'],
         ['velocity', 'velocitySlider', 'velocityValue', 'mps'],
         ['mass', 'massSlider', 'massValue', 'kg'],
         ['diameter', 'diameterSlider', 'diameterValue', 'm']].forEach(([f, s, v, u]) => {
            const slider = UI[s];
            const disp = UI[v];
            if (!slider) return;
            slider.addEventListener('input', () => {
                state.config[f] = parseFloat(slider.value);
                if (f === 'diameter') {
                    disp.textContent = state.config[f].toFixed(2);
                } else if (f === 'angle') {
                    disp.textContent = Math.round(state.config[f]);
                } else if (f === 'velocity') {
                    disp.textContent = Math.round(state.config[f]);
                } else {
                    disp.textContent = state.config[f].toFixed(f === 'diameter' ? 2 : 1);
                }
            });
        });
        UI.root.querySelectorAll('button[data-action]').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const action = e.currentTarget.dataset.action;
                if (action === 'predict') predict();
                else if (action === 'optimal') findOptimal();
                else if (action === 'compare') compare();
                else if (action === 'clear') clearComparisons();
            });
        });
    }

    function syncControlsFromState() {
        UI.angleSlider.value = state.config.angle;
        UI.angleValue.textContent = Math.round(state.config.angle);
        UI.velocitySlider.value = state.config.velocity;
        UI.velocityValue.textContent = Math.round(state.config.velocity);
        UI.massSlider.value = state.config.mass;
        UI.massValue.textContent = state.config.mass.toFixed(1);
        UI.diameterSlider.value = state.config.diameter;
        UI.diameterValue.textContent = state.config.diameter.toFixed(2);
    }

    function buildProjectile() {
        const cfg = state.config;
        return {
            mass: cfg.mass,
            drag_coefficient_incompressible: cfg.Cd0,
            cross_section_area: Math.PI * Math.pow(cfg.diameter / 2, 2),
            diameter: cfg.diameter
        };
    }

    function predict() {
        if (!window.TrebuchetPhysics) {
            console.error('[RangePanel] TrebuchetPhysics not loaded');
            return null;
        }
        const projectile = buildProjectile();
        const result = TrebuchetPhysics.predictTrajectoryRange(
            projectile, state.config.velocity, state.config.angle,
            state.config.dragFactor, state.config.temperatureK
        );
        const vacuum = Math.pow(state.config.velocity, 2) *
            Math.sin(state.config.angle * Math.PI / 180 * 2) / 9.80665;
        state.currentResult = { ...result, vacuumRange: vacuum };
        renderResult(state.currentResult);

        const full = TrebuchetPhysics.calculateFullTrajectory(
            projectile, state.config.velocity, state.config.angle,
            state.config.dragFactor, 0.01, state.config.temperatureK
        );
        if (window.TrajectoryView) {
            try {
                TrajectoryView.setTrajectory(full, state.config);
                if (state.comparisonResults.length > 0) {
                    const labels = state.comparisonLabels;
                    const results = state.comparisonResults.map(r => r.full);
                    TrajectoryView.addComparisonTrajectories(results, labels);
                }
            } catch (e) { console.warn(e); }
        }
        if (typeof UI.onPredict === 'function') UI.onPredict(state.currentResult, full);
        return state.currentResult;
    }

    function findOptimal() {
        if (!window.TrebuchetPhysics) return;
        const projectile = buildProjectile();
        const optimal = TrebuchetPhysics.findOptimalLaunchAngle(
            state.config.velocity, projectile,
            window.TrebuchetPhysics && TrebuchetPhysics.OPTIMAL_RANGE_MIN || 10,
            window.TrebuchetPhysics && TrebuchetPhysics.OPTIMAL_RANGE_MAX || 80,
            1
        );
        state.config.angle = optimal;
        syncControlsFromState();
        predict();
    }

    function compare() {
        if (!state.currentResult) predict();
        if (!state.currentResult) return;
        const full = TrebuchetPhysics.calculateFullTrajectory(
            buildProjectile(), state.config.velocity, state.config.angle,
            state.config.dragFactor, 0.01, state.config.temperatureK
        );
        state.comparisonResults.push({ cfg: { ...state.config }, result: { ...state.currentResult }, full });
        state.comparisonLabels.push(`${state.config.angle.toFixed(0)}°/${state.config.velocity.toFixed(0)}m/s`);
        if (window.TrajectoryView) {
            const results = state.comparisonResults.map(r => r.full);
            TrajectoryView.addComparisonTrajectories(results, state.comparisonLabels);
        }
    }

    function clearComparisons() {
        state.comparisonResults = [];
        state.comparisonLabels = [];
        if (window.TrajectoryView) TrajectoryView.clearComparisons();
    }

    function renderResult(r) {
        const set = (sel, val, digits = 2) => {
            const el = UI.resultGrid.querySelector(sel);
            if (el) el.textContent = val === null || val === undefined || isNaN(val) ? '-' : val.toFixed(digits);
        };
        set('.predictedRange', r.predictedRange || r.predicted_range);
        set('.optimalAngle', r.optimalLaunchAngle || state.config.angle, 1);
        set('.maxHeight', r.maxHeight || r.max_height);
        set('.flightTime', r.flightTime || r.flight_time);
        set('.maxMach', r.maxMach || r.max_mach, 3);
        set('.impactMach', r.impactMach || r.impact_mach, 3);
        set('.impactVelocity', r.impactVelocity || r.impact_velocity, 2);
        set('.compressibilityCorrection', r.compressibilityCorrection || r.compressibility_correction, 3);
        set('.airResistanceFactor', r.airResistanceFactor || r.air_resistance_factor, 3);
        set('.vacuumRange', r.vacuumRange, 1);
    }

    function getConfig() { return { ...state.config }; }
    function setConfig(patch) { Object.assign(state.config, patch); syncControlsFromState(); }
    function getLastResult() { return state.currentResult; }
    function setOnPredict(cb) { UI.onPredict = cb; }

    return {
        init,
        predict,
        findOptimal,
        compare,
        clearComparisons,
        getConfig,
        setConfig,
        getLastResult,
        setOnPredict
    };
})();
