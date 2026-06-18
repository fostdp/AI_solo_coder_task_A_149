const App = (function () {
    let selectedMachineId = '';
    let machineCache = [];

    function init() {
        initTabs();
        initTimeDisplay();
        initSpringView();
        initTrajectoryView();
        initThreeDView();
        initChartControls();
        initHistoryQuery();

        TrebuchetModel.init('three-canvas');
        SpringAnimation.init('spring-canvas');
        TrajectoryView.init('trajectory-canvas');
        DataChart.init('data-chart-canvas');

        TrebuchetModel.setArmAngle(-Math.PI / 6);
        TrebuchetModel.resetProjectile();

        updateSpringEnergyUI();
        calculateTrajectory();

        startDataPolling();
        setInterval(updateTimeDisplay, 1000);
    }

    function initTabs() {
        const tabs = document.querySelectorAll('.tab-btn');
        const contents = document.querySelectorAll('.tab-content');
        tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                tabs.forEach(t => t.classList.remove('active'));
                contents.forEach(c => c.classList.remove('active'));
                tab.classList.add('active');
                document.getElementById(tab.dataset.tab).classList.add('active');
                setTimeout(() => window.dispatchEvent(new Event('resize')), 50);
            });
        });
    }

    function initTimeDisplay() { updateTimeDisplay(); }

    function updateTimeDisplay() {
        const now = new Date();
        const pad = n => n.toString().padStart(2, '0');
        document.getElementById('current-time').textContent =
            `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())} ` +
            `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
    }

    function initThreeDView() {
        const btnFire = document.getElementById('btn-fire');
        const btnReset = document.getElementById('btn-reset');
        const selMachine = document.getElementById('selected-machine');

        btnFire.addEventListener('click', () => {
            if (TrebuchetModel.isFiring()) return;
            const mass = parseFloat(document.getElementById('proj-mass').value) || 10;
            const velocity = parseFloat(document.getElementById('proj-velocity').value) || 35;
            const angle = parseFloat(document.getElementById('angle-slider').value) || 45;
            TrebuchetModel.fireProjectile(velocity, angle, mass, () => {
                document.getElementById('animation-status').textContent = '发射完成';
            });
            document.getElementById('animation-status').textContent = '发射中...';
        });

        btnReset.addEventListener('click', () => {
            TrebuchetModel.reset();
            document.getElementById('animation-status').textContent = '待机';
        });

        selMachine.addEventListener('change', (e) => {
            selectedMachineId = e.target.value;
            document.getElementById('chart-machine').value = selectedMachineId;
            document.getElementById('history-machine').value = selectedMachineId;
            refreshChartData();
        });
    }

    function initSpringView() {
        const wireD = document.getElementById('spring-wire-d');
        const meanD = document.getElementById('spring-mean-d');
        const coils = document.getElementById('spring-coils');
        const torsionSlider = document.getElementById('torsion-slider');
        const torsionValue = document.getElementById('torsion-value');

        const update = () => {
            const config = {
                wireDiameter: (parseFloat(wireD.value) || 20) / 1000,
                coilMeanDiameter: (parseFloat(meanD.value) || 150) / 1000,
                activeCoils: parseInt(coils.value) || 12,
                material: TrebuchetPhysics.MATERIALS.steel65mn
            };
            SpringAnimation.setConfig(config);
            const angle = parseFloat(torsionSlider.value) || 0;
            torsionValue.textContent = angle + '°';
            SpringAnimation.setTorsion(angle);
            updateSpringEnergyUI();
        };

        wireD.addEventListener('input', update);
        meanD.addEventListener('input', update);
        coils.addEventListener('input', update);
        torsionSlider.addEventListener('input', update);
    }

    function updateSpringEnergyUI() {
        const torsionDeg = parseFloat(document.getElementById('torsion-slider').value) || 0;
        const config = {
            wireDiameter: (parseFloat(document.getElementById('spring-wire-d').value) || 20) / 1000,
            coilMeanDiameter: (parseFloat(document.getElementById('spring-mean-d').value) || 150) / 1000,
            activeCoils: parseInt(document.getElementById('spring-coils').value) || 12,
            material: TrebuchetPhysics.MATERIALS.steel65mn
        };
        const result = TrebuchetPhysics.calculateSpringEnergy(
            config, TrebuchetPhysics.degToRad(torsionDeg)
        );

        document.getElementById('spring-constant').textContent =
            result.springConstant.toFixed(1) + ' N·m/rad';
        document.getElementById('spring-energy').textContent =
            (result.storedEnergy / 1000).toFixed(2) + ' kJ';
        document.getElementById('shear-stress').textContent =
            (result.shearStress / 1e6).toFixed(1) + ' MPa';
        document.getElementById('elastic-stress').textContent =
            ((result.elasticStress || 0) / 1e6).toFixed(1) + ' MPa';
        document.getElementById('plastic-strain').textContent =
            (result.plasticStrain || 0).toExponential(3);
        document.getElementById('yield-ratio').textContent =
            (result.yieldStrengthRatio * 100).toFixed(1) + '%';
        document.getElementById('spring-efficiency').textContent =
            (result.efficiency * 100).toFixed(1) + '%';
        document.getElementById('cycle-count').textContent =
            result.cycleCount || 0;
        document.getElementById('damage-ratio').textContent =
            ((result.cyclicDamageRatio || 0) * 100).toFixed(2) + '%';
        document.getElementById('modulus-reduction').textContent =
            (result.modulusReduction || 1.0).toFixed(3);

        const riskEl = document.getElementById('spring-risk');
        riskEl.textContent = result.riskLevel === 'normal' ? '正常'
            : result.riskLevel === 'warning' ? '警告' : '危险';
        riskEl.className = 'result-value risk-level ' + result.riskLevel;

        const fatigueEl = document.getElementById('fatigue-risk');
        fatigueEl.textContent = result.fatigueRisk ? '告警' : '正常';
        fatigueEl.className = 'result-value risk-level ' + (result.fatigueRisk ? 'critical' : 'normal');
    }

    function initTrajectoryView() {
        const calcBtn = document.getElementById('btn-calc-trajectory');
        const angleSlider = document.getElementById('angle-slider');
        const angleValue = document.getElementById('angle-value');
        const compareBtn = document.getElementById('btn-compare');

        angleSlider.addEventListener('input', () => {
            angleValue.textContent = angleSlider.value + '°';
        });

        calcBtn.addEventListener('click', calculateTrajectory);

        ['proj-mass', 'proj-velocity', 'drag-factor'].forEach(id => {
            document.getElementById(id).addEventListener('change', calculateTrajectory);
        });

        compareBtn.addEventListener('click', generateComparison);
    }

    function calculateTrajectory() {
        const mass = parseFloat(document.getElementById('proj-mass').value) || 10;
        const velocity = parseFloat(document.getElementById('proj-velocity').value) || 35;
        const angle = parseFloat(document.getElementById('angle-slider').value) || 45;
        const dragFactor = parseFloat(document.getElementById('drag-factor').value) || 1.0;

        const config = { mass, velocity, angle, dragFactor };
        const result = TrajectoryView.calculateTrajectory(config);

        TrajectoryView.setTrajectory(result, config);
        TrajectoryView.clearComparisons();

        document.getElementById('pred-range').textContent =
            result.predictedRange.toFixed(2) + ' m';
        document.getElementById('pred-height').textContent =
            result.maxHeight.toFixed(2) + ' m';
        document.getElementById('pred-time').textContent =
            result.flightTime.toFixed(2) + ' s';
        document.getElementById('opt-angle').textContent =
            result.launchAngleOptimal.toFixed(1) + '°';
        document.getElementById('impact-vel').textContent =
            result.impactVelocity.toFixed(2) + ' m/s';
        document.getElementById('max-mach').textContent =
            (result.maxMach || TrebuchetPhysics.calculateMachNumber(velocity)).toFixed(3);
        document.getElementById('impact-mach').textContent =
            (result.impactMach || TrebuchetPhysics.calculateMachNumber(result.impactVelocity)).toFixed(3);
        document.getElementById('compressibility-correction').textContent =
            (result.compressibilityCorrection || 1.0).toFixed(3);
    }

    function generateComparison() {
        const mode = document.getElementById('compare-mode').value;
        const mass = parseFloat(document.getElementById('proj-mass').value) || 10;
        const velocity = parseFloat(document.getElementById('proj-velocity').value) || 35;
        const angle = parseFloat(document.getElementById('angle-slider').value) || 45;
        const dragFactor = parseFloat(document.getElementById('drag-factor').value) || 1.0;

        const results = [];
        const labels = [];
        let values = [];

        if (mode === 'angle') {
            values = [20, 30, 40, 45, 50, 60, 70];
            values.forEach(a => {
                const cfg = { mass, velocity, angle: a, dragFactor };
                results.push(TrajectoryView.calculateTrajectory(cfg));
                labels.push(`${a}°`);
            });
        } else if (mode === 'velocity') {
            values = [20, 25, 30, 35, 40, 45, 50];
            values.forEach(v => {
                const cfg = { mass, velocity: v, angle, dragFactor };
                results.push(TrajectoryView.calculateTrajectory(cfg));
                labels.push(`${v}m/s`);
            });
        } else if (mode === 'mass') {
            values = [5, 8, 10, 15, 20, 30, 50];
            values.forEach(m => {
                const cfg = { mass: m, velocity, angle, dragFactor };
                results.push(TrajectoryView.calculateTrajectory(cfg));
                labels.push(`${m}kg`);
            });
        }

        TrajectoryView.addComparisonTrajectories(results, labels);
    }

    function initChartControls() {
        const typeSel = document.getElementById('chart-type');
        const machineSel = document.getElementById('chart-machine');
        const refreshBtn = document.getElementById('btn-refresh-data');

        typeSel.addEventListener('change', () => {
            DataChart.setType(typeSel.value);
            refreshChartData();
        });

        machineSel.addEventListener('change', () => {
            selectedMachineId = machineSel.value;
            document.getElementById('selected-machine').value = selectedMachineId;
            document.getElementById('history-machine').value = selectedMachineId;
            refreshChartData();
        });

        refreshBtn.addEventListener('click', refreshChartData);
    }

    function initHistoryQuery() {
        const btn = document.getElementById('btn-query-history');
        btn.addEventListener('click', async () => {
            const machineId = document.getElementById('history-machine').value;
            const limit = parseInt(document.getElementById('history-limit').value) || 50;
            const data = await ApiClient.getSensorData(machineId, limit);
            const container = document.getElementById('history-result');

            if (!data || !data.data || data.data.length === 0) {
                container.innerHTML = '<div class="loading">无数据</div>';
                return;
            }

            let html = '<table><thead><tr>';
            html += '<th>设备</th><th>循环</th><th>扭转角</th><th>储能</th><th>损伤比</th><th>最大Ma</th><th>射程</th><th>风险</th>';
            html += '</tr></thead><tbody>';
            data.data.forEach(d => {
                const riskCls = d.risk_level || 'normal';
                html += `<tr>
                    <td>${d.machine_id}</td>
                    <td>${d.cycle_count || 0}</td>
                    <td>${(d.torsion_angle || 0).toFixed(2)}</td>
                    <td>${(d.stored_energy || 0).toFixed(0)}</td>
                    <td>${((d.cyclic_damage_ratio || 0) * 100).toFixed(1)}%</td>
                    <td>${(d.max_mach || 0).toFixed(2)}</td>
                    <td>${(d.actual_range || 0).toFixed(1)}m</td>
                    <td>${d.risk_level || '-'}</td>
                </tr>`;
            });
            html += '</tbody></table>';
            container.innerHTML = html;
        });
    }

    async function startDataPolling() {
        await refreshMachineStatus();
        await refreshAlerts();
        await refreshChartData();
        setInterval(refreshMachineStatus, 5000);
        setInterval(refreshAlerts, 8000);
        setInterval(refreshChartData, 10000);
    }

    async function refreshMachineStatus() {
        const data = await ApiClient.getMachineStatus();
        const listEl = document.getElementById('machine-status-list');
        const statusBadge = document.getElementById('system-status');
        const realtimeEl = document.getElementById('realtime-stats');
        const sel1 = document.getElementById('selected-machine');
        const sel2 = document.getElementById('chart-machine');
        const sel3 = document.getElementById('history-machine');

        if (!data || !data.data || data.data.length === 0) {
            listEl.innerHTML = '<div class="loading">等待设备数据...</div>';
            statusBadge.className = 'status-badge status-warning';
            statusBadge.textContent = '等待连接';
            realtimeEl.innerHTML = '<div class="loading">等待数据...</div>';
            return;
        }

        machineCache = data.data;
        const opts = data.data.map(d =>
            `<option value="${d.machine_id}" ${d.machine_id === selectedMachineId ? 'selected' : ''}>${d.machine_id}</option>`
        ).join('');
        const selHtml = '<option value="">-- 请选择 --</option>' + opts;
        sel1.innerHTML = selHtml;
        sel2.innerHTML = '<option value="">全部设备</option>' + opts;
        sel3.innerHTML = '<option value="">全部</option>' + opts;

        let html = '';
        let hasCritical = false, hasWarning = false;
        data.data.forEach(d => {
            if (d.current_risk_level === 'critical') hasCritical = true;
            else if (d.current_risk_level === 'warning') hasWarning = true;
            const selected = d.machine_id === selectedMachineId ? 'selected' : '';
            html += `<div class="machine-item ${selected}" data-id="${d.machine_id}">
                <div class="machine-name">
                    <span>${d.machine_id}</span>
                    <span class="risk-indicator ${d.current_risk_level || 'normal'}">
                        ${d.current_risk_level === 'critical' ? '危险' : d.current_risk_level === 'warning' ? '警告' : '正常'}
                    </span>
                </div>
                <div class="machine-metrics">
                    <span>循环: <b>${d.total_cycles || 0}</b></span>
                    <span>损伤: <b>${((d.current_damage_ratio || 0) * 100).toFixed(1)}%</b></span>
                    <span>最大Ma: <b>${(d.last_max_mach || 0).toFixed(2)}</b></span>
                    <span>扭转角: <b>${(d.last_torsion_angle || 0).toFixed(2)}rad</b></span>
                    <span>储能: <b>${(d.last_stored_energy || 0).toFixed(0)}J</b></span>
                    <span>射程: <b>${(d.last_actual_range || 0).toFixed(1)}m</b></span>
                    <span>告警: <b>${d.unacknowledged_alerts || 0}</b></span>
                </div>
            </div>`;
        });
        listEl.innerHTML = html;

        document.querySelectorAll('.machine-item').forEach(el => {
            el.addEventListener('click', () => {
                selectedMachineId = el.dataset.id;
                refreshMachineStatus();
                refreshChartData();
            });
        });

        if (hasCritical) {
            statusBadge.className = 'status-badge status-error';
            statusBadge.textContent = '存在危险告警';
        } else if (hasWarning) {
            statusBadge.className = 'status-badge status-warning';
            statusBadge.textContent = '存在警告';
        } else {
            statusBadge.className = 'status-badge status-ok';
            statusBadge.textContent = '系统运行中';
        }

        if (selectedMachineId) {
            const sel = data.data.find(d => d.machine_id === selectedMachineId);
            if (sel) {
                realtimeEl.innerHTML = `
                    <div class="stat-item"><span class="stat-label">设备ID</span><span class="stat-value">${sel.machine_id}</span></div>
                    <div class="stat-item"><span class="stat-label">循环次数</span><span class="stat-value">${sel.total_cycles || 0}</span></div>
                    <div class="stat-item"><span class="stat-label">累积损伤比</span><span class="stat-value">${((sel.current_damage_ratio || 0) * 100).toFixed(2)}%</span></div>
                    <div class="stat-item"><span class="stat-label">最近最大马赫</span><span class="stat-value">${(sel.last_max_mach || 0).toFixed(3)} Ma</span></div>
                    <div class="stat-item"><span class="stat-label">扭转角</span><span class="stat-value">${(sel.last_torsion_angle || 0).toFixed(3)} rad</span></div>
                    <div class="stat-item"><span class="stat-label">弹簧储能</span><span class="stat-value">${(sel.last_stored_energy || 0).toFixed(0)} J</span></div>
                    <div class="stat-item"><span class="stat-label">释放速度</span><span class="stat-value">${(sel.last_release_velocity || 0).toFixed(2)} m/s</span></div>
                    <div class="stat-item"><span class="stat-label">实际射程</span><span class="stat-value">${(sel.last_actual_range || 0).toFixed(2)} m</span></div>
                    <div class="stat-item"><span class="stat-label">预测射程</span><span class="stat-value">${(sel.last_predicted_range || 0).toFixed(2)} m</span></div>
                    <div class="stat-item"><span class="stat-label">风险等级</span><span class="stat-value risk-level ${sel.current_risk_level || 'normal'}">${sel.current_risk_level || 'normal'}</span></div>
                `;
            }
        } else {
            realtimeEl.innerHTML = '<div class="loading">请选择一台设备查看详情</div>';
        }
    }

    async function refreshAlerts() {
        const data = await ApiClient.getAlerts(selectedMachineId, 20);
        const listEl = document.getElementById('alert-list');
        if (!data || !data.data || data.data.length === 0) {
            listEl.innerHTML = '<div class="loading">暂无告警</div>';
            return;
        }
        let html = '';
        data.data.slice(0, 15).forEach(a => {
            const level = a.alert_level === 'critical' || a.alert_level === 'warning' ? a.alert_level : 'info';
            const typeText = a.alert_type === 'spring_fracture_risk' ? '弹簧断裂风险'
                : a.alert_type === 'insufficient_range' ? '射程不足'
                : a.alert_type === 'efficiency_low' ? '效率偏低'
                : a.alert_type === 'cyclic_fatigue_risk' ? '循环疲劳风险' : a.alert_type;
            html += `<div class="alert-item ${level}">
                <div class="alert-header">
                    <span class="alert-type">${typeText}</span>
                    <span class="alert-time">${(a.timestamp || '').split('T').pop() || ''}</span>
                </div>
                <div class="alert-message">${a.message || ''}</div>
            </div>`;
        });
        listEl.innerHTML = html;
    }

    async function refreshChartData() {
        const type = document.getElementById('chart-type').value;
        const data = await ApiClient.getSensorData(selectedMachineId, 100);
        if (data && data.data) {
            DataChart.setType(type);
            DataChart.setData(data.data);
        }
    }

    return { init };
})();

if (typeof THREE !== 'undefined') {
    document.addEventListener('DOMContentLoaded', App.init);
} else {
    window.addEventListener('load', () => {
        if (typeof THREE !== 'undefined') App.init();
        else document.addEventListener('DOMContentLoaded', App.init);
    });
}
