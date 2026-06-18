const SpringAnimation = (function () {
    let canvas, ctx;
    let width, height;
    let currentTorsion = 0;
    let targetTorsion = 0;
    let animationId = null;
    let springConfig = {
        wireDiameter: 0.02,
        coilMeanDiameter: 0.15,
        activeCoils: 12,
        material: TrebuchetPhysics.MATERIALS.steel65mn
    };

    function init(canvasId) {
        canvas = document.getElementById(canvasId);
        ctx = canvas.getContext('2d');
        resize();
        window.addEventListener('resize', resize);
        draw();
    }

    function resize() {
        const rect = canvas.parentElement.getBoundingClientRect();
        width = rect.width;
        height = rect.height;
        canvas.width = width * window.devicePixelRatio;
        canvas.height = height * window.devicePixelRatio;
        canvas.style.width = width + 'px';
        canvas.style.height = height + 'px';
        ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
    }

    function setConfig(config) {
        springConfig = { ...springConfig, ...config };
        if (springConfig.material) {
            springConfig.material = config.material;
        }
    }

    function setTorsion(angleDeg) {
        targetTorsion = angleDeg;
    }

    function getConfig() {
        return springConfig;
    }

    function drawBackground() {
        const gradient = ctx.createLinearGradient(0, 0, 0, height);
        gradient.addColorStop(0, '#0a0e1a');
        gradient.addColorStop(1, '#141a2e');
        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, width, height);

        ctx.strokeStyle = 'rgba(42, 58, 90, 0.3)';
        ctx.lineWidth = 1;
        const gridSize = 40;
        for (let x = 0; x < width; x += gridSize) {
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, height);
            ctx.stroke();
        }
        for (let y = 0; y < height; y += gridSize) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }
    }

    function drawSpring(torsionDeg) {
        const centerX = width / 2;
        const centerY = height / 2 + 20;
        const springRadius = Math.min(width, height) * 0.28;
        const turns = springConfig.activeCoils;
        const wireThickness = Math.max(3, springConfig.wireDiameter / 0.02 * 4);
        const segments = turns * 40;

        const torsionRad = TrebuchetPhysics.degToRad(torsionDeg);
        const energyResult = TrebuchetPhysics.calculateSpringEnergy(springConfig, torsionRad);

        let springColor = '#4a5568';
        let glowColor = 'rgba(100, 120, 160, 0.3)';
        if (energyResult.riskLevel === 'warning') {
            springColor = '#ffc107';
            glowColor = 'rgba(255, 193, 7, 0.4)';
        } else if (energyResult.riskLevel === 'critical') {
            springColor = '#ff4757';
            glowColor = 'rgba(255, 71, 87, 0.5)';
        } else if (torsionDeg > 60) {
            springColor = '#7a9acc';
            glowColor = 'rgba(122, 154, 204, 0.4)';
        }

        const depthFactor = 0.4;
        const springHeight = springRadius * 1.8;
        const springStartY = centerY - springHeight / 2;

        const totalRotation = torsionDeg / 360 * Math.PI * 2;

        ctx.save();
        ctx.shadowColor = glowColor;
        ctx.shadowBlur = 20;

        for (let side = 0; side < 2; side++) {
            ctx.beginPath();
            for (let i = 0; i <= segments; i++) {
                const t = i / segments;
                const angle = t * turns * Math.PI * 2 + totalRotation * t;
                const coilRadius = springRadius * (1 + Math.sin(t * Math.PI) * 0.05);
                const zAngle = Math.cos(angle + (side === 0 ? 0 : Math.PI));
                const x = centerX + Math.cos(angle) * coilRadius * (side === 0 ? 1 : 0.3);
                const y = springStartY + t * springHeight;
                const thickness = wireThickness * (0.5 + 0.5 * (zAngle * 0.5 + 0.5));

                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.strokeStyle = side === 0 ? springColor : adjustColor(springColor, -40);
            ctx.lineWidth = side === 0 ? wireThickness : wireThickness * 0.6;
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';
            ctx.stroke();
        }

        for (let layer = 0; layer < 2; layer++) {
            ctx.beginPath();
            for (let i = 0; i <= segments; i++) {
                const t = i / segments;
                const angle = t * turns * Math.PI * 2 + totalRotation * t;
                const perspective = layer === 0 ? 1 : 0.92;
                const x = centerX + Math.cos(angle) * springRadius * perspective;
                const y = springStartY + t * springHeight;
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.strokeStyle = layer === 0 ? springColor : adjustColor(springColor, 20);
            ctx.lineWidth = wireThickness * (layer === 0 ? 1 : 0.8);
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';
            ctx.stroke();
        }
        ctx.restore();

        drawEnds(springStartY, springHeight, springRadius, centerX, torsionDeg);
        drawEnergyIndicator(torsionDeg, energyResult);
        drawInfoPanel(centerX, springStartY, springRadius, torsionDeg, energyResult);
    }

    function drawEnds(startY, springH, radius, centerX, torsionDeg) {
        const endWidth = radius * 0.8;
        const rot = TrebuchetPhysics.degToRad(torsionDeg);

        ctx.save();
        ctx.fillStyle = '#3a4a6a';
        ctx.strokeStyle = '#5a6a8a';
        ctx.lineWidth = 2;

        ctx.beginPath();
        ctx.roundRect(centerX - endWidth / 2, startY - 25, endWidth, 20, 4);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#8b7355';
        ctx.fillRect(centerX - radius * 0.4, startY - 30, radius * 0.8, 6);

        ctx.translate(centerX, startY + springH + 10);
        ctx.rotate(rot * 0.3);

        ctx.fillStyle = '#3a4a6a';
        ctx.strokeStyle = '#5a6a8a';
        ctx.beginPath();
        ctx.roundRect(-endWidth / 2, 0, endWidth, 20, 4);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#ffb347';
        ctx.beginPath();
        ctx.moveTo(endWidth / 2, 10);
        ctx.lineTo(endWidth / 2 + 15 + Math.abs(rot) * 30, 10);
        ctx.lineTo(endWidth / 2 + Math.abs(rot) * 30, 4);
        ctx.lineTo(endWidth / 2 + Math.abs(rot) * 30, 16);
        ctx.closePath();
        ctx.fill();
        ctx.restore();
    }

    function drawEnergyIndicator(torsionDeg, energyResult) {
        const barWidth = 30;
        const barHeight = height * 0.5;
        const barX = 30;
        const barY = (height - barHeight) / 2;

        ctx.save();
        ctx.fillStyle = 'rgba(10, 14, 26, 0.8)';
        ctx.strokeStyle = '#2a3a5a';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(barX - 5, barY - 5, barWidth + 10, barHeight + 40, 4);
        ctx.fill();
        ctx.stroke();

        const maxAngle = 180;
        const fillHeight = (Math.min(torsionDeg, maxAngle) / maxAngle) * barHeight;
        const fillY = barY + barHeight - fillHeight;

        let gradColor1 = '#2ed573';
        let gradColor2 = '#7bed9f';
        if (energyResult.riskLevel === 'warning') {
            gradColor1 = '#ffc107';
            gradColor2 = '#ffd54f';
        } else if (energyResult.riskLevel === 'critical') {
            gradColor1 = '#ff4757';
            gradColor2 = '#ff6b81';
        }

        const gradient = ctx.createLinearGradient(0, fillY, 0, barY + barHeight);
        gradient.addColorStop(0, gradColor1);
        gradient.addColorStop(1, gradColor2);

        ctx.fillStyle = gradient;
        ctx.beginPath();
        ctx.roundRect(barX, fillY, barWidth, fillHeight, 3);
        ctx.fill();

        for (let i = 0; i <= 10; i++) {
            const y = barY + (i / 10) * barHeight;
            ctx.strokeStyle = i % 5 === 0 ? '#8a9ab5' : '#4a5a7a';
            ctx.lineWidth = i % 5 === 0 ? 2 : 1;
            ctx.beginPath();
            ctx.moveTo(barX + barWidth + 3, y);
            ctx.lineTo(barX + barWidth + (i % 5 === 0 ? 10 : 6), y);
            ctx.stroke();
            if (i % 5 === 0) {
                ctx.fillStyle = '#8a9ab5';
                ctx.font = '10px monospace';
                ctx.textAlign = 'left';
                ctx.fillText((10 - i) * 18 + '°', barX + barWidth + 14, y + 3);
            }
        }

        ctx.fillStyle = '#e4e8f0';
        ctx.font = 'bold 11px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('扭转', barX + barWidth / 2, barY + barHeight + 18);
        ctx.fillStyle = '#ffb347';
        ctx.font = 'bold 12px monospace';
        ctx.fillText(torsionDeg.toFixed(0) + '°', barX + barWidth / 2, barY + barHeight + 32);
        ctx.restore();
    }

    function drawInfoPanel(centerX, startY, radius, torsionDeg, energyResult) {
        ctx.save();
        ctx.fillStyle = 'rgba(13, 20, 40, 0.9)';
        ctx.strokeStyle = '#2a3a5a';
        ctx.lineWidth = 1;
        const panelX = width - 210;
        const panelY = 20;
        ctx.beginPath();
        ctx.roundRect(panelX, panelY, 190, 110, 6);
        ctx.fill();
        ctx.stroke();

        ctx.font = 'bold 12px sans-serif';
        ctx.fillStyle = '#a8b8d5';
        ctx.textAlign = 'left';
        ctx.fillText('⚡ 实时储能状态', panelX + 12, panelY + 22);

        ctx.font = '11px monospace';
        ctx.fillStyle = '#8a9ab5';
        ctx.fillText('储能:', panelX + 12, panelY + 44);
        ctx.fillStyle = '#ffb347';
        ctx.font = 'bold 14px monospace';
        ctx.fillText((energyResult.stored_energy / 1000).toFixed(2) + ' kJ', panelX + 70, panelY + 44);

        ctx.font = '11px monospace';
        ctx.fillStyle = '#8a9ab5';
        ctx.fillText('效率:', panelX + 12, panelY + 66);
        ctx.fillStyle = '#e4e8f0';
        ctx.fillText((energyResult.efficiency * 100).toFixed(1) + '%', panelX + 70, panelY + 66);

        ctx.fillStyle = '#8a9ab5';
        ctx.fillText('应力比:', panelX + 12, panelY + 88);
        let stressColor = '#2ed573';
        if (energyResult.riskLevel === 'warning') stressColor = '#ffc107';
        if (energyResult.riskLevel === 'critical') stressColor = '#ff4757';
        ctx.fillStyle = stressColor;
        ctx.fillText((energyResult.yieldStrengthRatio * 100).toFixed(1) + '%', panelX + 70, panelY + 88);

        ctx.fillStyle = '#8a9ab5';
        ctx.fillText('风险:', panelX + 12, panelY + 106);
        let riskText = '正常';
        if (energyResult.riskLevel === 'warning') riskText = '警告';
        if (energyResult.riskLevel === 'critical') riskText = '危险';
        ctx.fillStyle = stressColor;
        ctx.font = 'bold 12px sans-serif';
        ctx.fillText(riskText, panelX + 70, panelY + 106);

        ctx.restore();
    }

    function adjustColor(color, amount) {
        const hex = color.replace('#', '');
        let r = parseInt(hex.substr(0, 2), 16);
        let g = parseInt(hex.substr(2, 2), 16);
        let b = parseInt(hex.substr(4, 2), 16);
        r = Math.max(0, Math.min(255, r + amount));
        g = Math.max(0, Math.min(255, g + amount));
        b = Math.max(0, Math.min(255, b + amount));
        return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
    }

    function draw() {
        currentTorsion += (targetTorsion - currentTorsion) * 0.08;
        drawBackground();
        drawSpring(currentTorsion);
        animationId = requestAnimationFrame(draw);
    }

    function stop() {
        if (animationId) cancelAnimationFrame(animationId);
    }

    function getCurrentEnergy() {
        const torsionRad = TrebuchetPhysics.degToRad(currentTorsion);
        return TrebuchetPhysics.calculateSpringEnergy(springConfig, torsionRad);
    }

    return {
        init,
        setConfig,
        setTorsion,
        getConfig,
        getCurrentEnergy,
        stop
    };
})();
