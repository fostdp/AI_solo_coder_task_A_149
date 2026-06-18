const TrajectoryView = (function () {
    let canvas, ctx;
    let width, height;
    let currentTrajectory = [];
    let comparisonTrajectories = [];
    let projectilePos = null;
    let animationProgress = 0;
    let animating = false;
    let animationId = null;
    let lastConfig = null;

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

    function calculateTrajectory(config) {
        lastConfig = config;
        const projectile = {
            mass: config.mass,
            dragCoefficient: 0.47,
            crossSectionArea: Math.PI * Math.pow(0.1, 2)
        };
        const result = TrebuchetPhysics.calculateFullTrajectory(
            projectile, config.velocity, config.angle, config.dragFactor
        );
        return result;
    }

    function setTrajectory(trajectoryResult, config) {
        lastConfig = config;
        currentTrajectory = trajectoryResult.trajectoryPoints.map(p => ({ x: p.x, y: p.y }));
        comparisonTrajectories = [];
        projectilePos = { x: 0, y: 0 };
        animationProgress = 0;
        animating = true;
    }

    function addComparisonTrajectories(results, labels) {
        comparisonTrajectories = results.map((r, i) => ({
            points: r.trajectoryPoints.map(p => ({ x: p.x, y: p.y })),
            label: labels[i],
            color: [
                '#ffb347', '#ff6b35', '#1e90ff', '#2ed573',
                '#ff4757', '#a29bfe', '#fd79a8', '#00cec9'
            ][i % 8]
        }));
    }

    function clearComparisons() {
        comparisonTrajectories = [];
    }

    function getWorldBounds() {
        let maxX = 100, maxY = 30;
        const allPoints = [...currentTrajectory];
        comparisonTrajectories.forEach(t => allPoints.push(...t.points));
        allPoints.forEach(p => {
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
        });
        maxX = Math.ceil(maxX / 20) * 20 + 20;
        maxY = Math.ceil(maxY / 10) * 10 + 10;
        return { maxX, maxY };
    }

    function worldToScreen(wx, wy, bounds) {
        const paddingX = 70;
        const paddingY = 40;
        const plotW = width - paddingX - 20;
        const plotH = height - paddingY - 30;
        const sx = paddingX + (wx / bounds.maxX) * plotW;
        const sy = height - paddingY - (wy / bounds.maxY) * plotH;
        return { x: sx, y: sy };
    }

    function draw() {
        drawBackground();
        const bounds = getWorldBounds();
        drawAxes(bounds);
        drawGrid(bounds);
        drawGround(bounds);

        comparisonTrajectories.forEach((traj, idx) => {
            drawTrajectoryPath(traj.points, bounds, traj.color, 0.8, idx > 0);
        });

        if (currentTrajectory.length > 0) {
            drawTrajectoryPath(currentTrajectory, bounds, '#ff6b35', 1.0, false);

            if (animating) {
                animationProgress += 0.008;
                if (animationProgress >= 1) {
                    animationProgress = 1;
                    animating = false;
                }
                const idx = Math.floor(animationProgress * (currentTrajectory.length - 1));
                if (idx >= 0 && idx < currentTrajectory.length) {
                    projectilePos = currentTrajectory[idx];
                    drawProjectile(projectilePos.x, projectilePos.y, bounds);
                    drawTrail(currentTrajectory.slice(0, idx + 1), bounds);
                }
            } else if (projectilePos) {
                const last = currentTrajectory[currentTrajectory.length - 1];
                drawProjectile(last.x, 0.1, bounds);
            }
        }

        drawKeyInfo(bounds);
        animationId = requestAnimationFrame(draw);
    }

    function drawBackground() {
        const skyGrad = ctx.createLinearGradient(0, 0, 0, height);
        skyGrad.addColorStop(0, '#0a1628');
        skyGrad.addColorStop(0.6, '#152238');
        skyGrad.addColorStop(1, '#1a2a4a');
        ctx.fillStyle = skyGrad;
        ctx.fillRect(0, 0, width, height);

        ctx.save();
        ctx.globalAlpha = 0.15;
        for (let i = 0; i < 30; i++) {
            const x = (i * 137) % width;
            const y = (i * 89) % (height * 0.5);
            ctx.fillStyle = i % 3 === 0 ? '#ffd700' : '#ffffff';
            ctx.beginPath();
            ctx.arc(x, y, Math.random() * 1.2, 0, Math.PI * 2);
            ctx.fill();
        }
        ctx.restore();
    }

    function drawAxes(bounds) {
        const paddingX = 70;
        const paddingY = 40;
        ctx.strokeStyle = '#4a5a7a';
        ctx.lineWidth = 2;

        ctx.beginPath();
        ctx.moveTo(paddingX, 10);
        ctx.lineTo(paddingX, height - paddingY);
        ctx.lineTo(width - 10, height - paddingY);
        ctx.stroke();

        ctx.fillStyle = '#8a9ab5';
        ctx.font = '12px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('水平距离 (m)', (width + paddingX) / 2, height - 10);

        ctx.save();
        ctx.translate(18, height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.fillText('高度 (m)', 0, 0);
        ctx.restore();
    }

    function drawGrid(bounds) {
        const paddingX = 70;
        const paddingY = 40;
        ctx.strokeStyle = 'rgba(74, 90, 122, 0.25)';
        ctx.lineWidth = 1;

        const xStep = bounds.maxX > 200 ? 50 : (bounds.maxX > 100 ? 20 : 10);
        for (let x = 0; x <= bounds.maxX; x += xStep) {
            const screen = worldToScreen(x, 0, bounds);
            ctx.beginPath();
            ctx.moveTo(screen.x, 10);
            ctx.lineTo(screen.x, height - paddingY);
            ctx.stroke();

            ctx.fillStyle = '#6a7a95';
            ctx.font = '10px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(x.toString(), screen.x, height - paddingY + 14);
        }

        const yStep = bounds.maxY > 80 ? 20 : (bounds.maxY > 40 ? 10 : 5);
        for (let y = 0; y <= bounds.maxY; y += yStep) {
            const screen = worldToScreen(0, y, bounds);
            ctx.beginPath();
            ctx.moveTo(paddingX, screen.y);
            ctx.lineTo(width - 10, screen.y);
            ctx.stroke();

            ctx.fillStyle = '#6a7a95';
            ctx.font = '10px monospace';
            ctx.textAlign = 'right';
            ctx.fillText(y.toString(), paddingX - 6, screen.y + 3);
        }
    }

    function drawGround(bounds) {
        const paddingY = 40;
        const groundGrad = ctx.createLinearGradient(0, height - paddingY, 0, height);
        groundGrad.addColorStop(0, '#2d3d2a');
        groundGrad.addColorStop(1, '#1a2618');
        ctx.fillStyle = groundGrad;
        ctx.fillRect(0, height - paddingY, width, paddingY);

        ctx.strokeStyle = '#3a5a3a';
        ctx.lineWidth = 1;
        for (let i = 0; i < 50; i++) {
            const x = (i / 50) * width + Math.random() * 5;
            ctx.beginPath();
            ctx.moveTo(x, height - paddingY);
            ctx.lineTo(x + Math.random() * 4 - 2, height - paddingY + 4 + Math.random() * 6);
            ctx.stroke();
        }
    }

    function drawTrajectoryPath(points, bounds, color, alpha, dashed) {
        if (points.length < 2) return;
        ctx.save();
        ctx.globalAlpha = alpha;
        ctx.strokeStyle = color;
        ctx.lineWidth = dashed ? 1.5 : 2.5;
        if (dashed) ctx.setLineDash([6, 4]);

        ctx.beginPath();
        for (let i = 0; i < points.length; i++) {
            const screen = worldToScreen(points[i].x, Math.max(0, points[i].y), bounds);
            if (i === 0) ctx.moveTo(screen.x, screen.y);
            else ctx.lineTo(screen.x, screen.y);
        }
        ctx.stroke();
        ctx.setLineDash([]);

        if (!dashed) {
            ctx.shadowColor = color;
            ctx.shadowBlur = 8;
            ctx.beginPath();
            for (let i = 0; i < points.length; i++) {
                const screen = worldToScreen(points[i].x, Math.max(0, points[i].y), bounds);
                if (i === 0) ctx.moveTo(screen.x, screen.y);
                else ctx.lineTo(screen.x, screen.y);
            }
            ctx.stroke();
        }
        ctx.restore();
    }

    function drawTrail(points, bounds) {
        if (points.length < 2) return;
        const trailLength = Math.min(30, points.length);
        const startIdx = Math.max(0, points.length - trailLength);
        for (let i = startIdx; i < points.length - 1; i++) {
            const alpha = (i - startIdx) / trailLength * 0.5;
            const p1 = worldToScreen(points[i].x, points[i].y, bounds);
            const p2 = worldToScreen(points[i + 1].x, points[i + 1].y, bounds);
            ctx.strokeStyle = `rgba(255, 179, 71, ${alpha})`;
            ctx.lineWidth = alpha * 5 + 1;
            ctx.beginPath();
            ctx.moveTo(p1.x, p1.y);
            ctx.lineTo(p2.x, p2.y);
            ctx.stroke();
        }
    }

    function drawProjectile(wx, wy, bounds) {
        const pos = worldToScreen(wx, Math.max(0, wy), bounds);
        ctx.save();

        ctx.shadowColor = '#ff6b35';
        ctx.shadowBlur = 20;
        const grad = ctx.createRadialGradient(pos.x, pos.y, 2, pos.x, pos.y, 12);
        grad.addColorStop(0, '#ffffff');
        grad.addColorStop(0.3, '#ffcc66');
        grad.addColorStop(0.7, '#ff6b35');
        grad.addColorStop(1, 'rgba(255, 107, 53, 0)');
        ctx.fillStyle = grad;
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, 12, 0, Math.PI * 2);
        ctx.fill();

        ctx.shadowBlur = 0;
        ctx.fillStyle = '#6b6b6b';
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, 6, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#888888';
        ctx.beginPath();
        ctx.arc(pos.x - 2, pos.y - 2, 2, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = '#e4e8f0';
        ctx.font = 'bold 11px monospace';
        ctx.textAlign = 'left';
        ctx.fillText(`(${wx.toFixed(1)}m, ${wy.toFixed(1)}m)`, pos.x + 14, pos.y - 6);

        ctx.restore();
    }

    function drawKeyInfo(bounds) {
        if (!lastConfig) return;
        const result = calculateTrajectory(lastConfig);

        const infoX = width - 190;
        const infoY = 20;
        ctx.save();
        ctx.fillStyle = 'rgba(13, 20, 40, 0.9)';
        ctx.strokeStyle = '#2a3a5a';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(infoX, infoY, 180, 130, 6);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#a8b8d5';
        ctx.font = 'bold 12px sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText('📊 弹道参数', infoX + 12, infoY + 22);

        ctx.font = '11px monospace';
        const items = [
            ['初速度', lastConfig.velocity.toFixed(2) + ' m/s', '#e4e8f0'],
            ['发射角', lastConfig.angle.toFixed(1) + '°', '#e4e8f0'],
            ['弹丸质量', lastConfig.mass.toFixed(1) + ' kg', '#e4e8f0'],
            ['预测射程', result.predictedRange.toFixed(2) + ' m', '#ffb347'],
            ['最大高度', result.maxHeight.toFixed(2) + ' m', '#1e90ff'],
            ['飞行时间', result.flightTime.toFixed(2) + ' s', '#2ed573']
        ];
        items.forEach((item, i) => {
            ctx.fillStyle = '#8a9ab5';
            ctx.fillText(item[0] + ':', infoX + 12, infoY + 44 + i * 14);
            ctx.fillStyle = item[2];
            ctx.font = 'bold 11px monospace';
            ctx.fillText(item[1], infoX + 90, infoY + 44 + i * 14);
            ctx.font = '11px monospace';
        });
        ctx.restore();

        if (comparisonTrajectories.length > 0) {
            const legendX = 80;
            const legendY = 20;
            const legendH = 24 + comparisonTrajectories.length * 18;
            ctx.save();
            ctx.fillStyle = 'rgba(13, 20, 40, 0.9)';
            ctx.strokeStyle = '#2a3a5a';
            ctx.beginPath();
            ctx.roundRect(legendX, legendY, 160, legendH, 6);
            ctx.fill();
            ctx.stroke();

            ctx.fillStyle = '#a8b8d5';
            ctx.font = 'bold 12px sans-serif';
            ctx.fillText('📈 对比曲线', legendX + 12, legendY + 20);

            comparisonTrajectories.forEach((traj, i) => {
                ctx.strokeStyle = traj.color;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(legendX + 12, legendY + 34 + i * 18 - 4);
                ctx.lineTo(legendX + 32, legendY + 34 + i * 18 - 4);
                ctx.stroke();

                ctx.fillStyle = '#e4e8f0';
                ctx.font = '11px monospace';
                ctx.fillText(traj.label, legendX + 38, legendY + 34 + i * 18);
            });
            ctx.restore();
        }
    }

    function stop() {
        if (animationId) cancelAnimationFrame(animationId);
    }

    function isAnimating() { return animating; }

    return {
        init,
        calculateTrajectory,
        setTrajectory,
        addComparisonTrajectories,
        clearComparisons,
        isAnimating,
        stop
    };
})();
