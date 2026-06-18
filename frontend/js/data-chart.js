const DataChart = (function () {
    let canvas, ctx;
    let width, height;
    let currentData = [];
    let chartType = 'range';

    const typeConfig = {
        range: { label: '射程', color: '#ff6b35', unit: 'm' },
        energy: { label: '储能', color: '#ffb347', unit: 'J' },
        angle: { label: '扭转角', color: '#1e90ff', unit: 'rad' },
        efficiency: { label: '效率', color: '#2ed573', unit: '%' }
    };

    function init(canvasId) {
        canvas = document.getElementById(canvasId);
        ctx = canvas.getContext('2d');
        resize();
        window.addEventListener('resize', () => { resize(); draw(); });
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

    function setType(type) {
        chartType = type;
        draw();
    }

    function setData(data) {
        currentData = data.slice().reverse();
        draw();
    }

    function getValue(d) {
        switch (chartType) {
            case 'range': return d.actual_range || d.predicted_range || 0;
            case 'energy': return d.stored_energy || 0;
            case 'angle': return d.torsion_angle || 0;
            case 'efficiency': return (d.efficiency || 0) * 100;
            default: return 0;
        }
    }

    function draw() {
        const padding = { top: 20, right: 60, bottom: 28, left: 55 };
        const chartW = width - padding.left - padding.right;
        const chartH = height - padding.top - padding.bottom;

        ctx.fillStyle = 'rgba(10, 14, 26, 0.4)';
        ctx.fillRect(0, 0, width, height);

        if (currentData.length < 2) {
            ctx.fillStyle = '#6a7a95';
            ctx.font = '13px sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText('暂无数据，请等待传感器上报或刷新', width / 2, height / 2);
            return;
        }

        let minVal = Infinity, maxVal = -Infinity;
        currentData.forEach(d => {
            const v = getValue(d);
            minVal = Math.min(minVal, v);
            maxVal = Math.max(maxVal, v);
        });
        const range = maxVal - minVal || 1;
        minVal -= range * 0.1;
        maxVal += range * 0.1;

        ctx.strokeStyle = 'rgba(74, 90, 122, 0.3)';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 5; i++) {
            const y = padding.top + (i / 5) * chartH;
            ctx.beginPath();
            ctx.moveTo(padding.left, y);
            ctx.lineTo(width - padding.right, y);
            ctx.stroke();

            const val = maxVal - (i / 5) * (maxVal - minVal);
            ctx.fillStyle = '#8a9ab5';
            ctx.font = '10px monospace';
            ctx.textAlign = 'right';
            ctx.fillText(val.toFixed(1), padding.left - 6, y + 3);
        }

        const cfg = typeConfig[chartType];
        ctx.fillStyle = '#8a9ab5';
        ctx.font = '11px sans-serif';
        ctx.textAlign = 'center';
        ctx.save();
        ctx.translate(14, height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.fillText(`${cfg.label} (${cfg.unit})`, 0, 0);
        ctx.restore();

        const points = currentData.map((d, i) => ({
            x: padding.left + (i / (currentData.length - 1)) * chartW,
            y: padding.top + (1 - (getValue(d) - minVal) / (maxVal - minVal)) * chartH,
            d
        }));

        const gradient = ctx.createLinearGradient(0, padding.top, 0, height - padding.bottom);
        gradient.addColorStop(0, cfg.color + '66');
        gradient.addColorStop(1, cfg.color + '00');
        ctx.fillStyle = gradient;
        ctx.beginPath();
        ctx.moveTo(points[0].x, height - padding.bottom);
        points.forEach(p => ctx.lineTo(p.x, p.y));
        ctx.lineTo(points[points.length - 1].x, height - padding.bottom);
        ctx.closePath();
        ctx.fill();

        ctx.strokeStyle = cfg.color;
        ctx.lineWidth = 2;
        ctx.shadowColor = cfg.color;
        ctx.shadowBlur = 6;
        ctx.beginPath();
        points.forEach((p, i) => {
            if (i === 0) ctx.moveTo(p.x, p.y);
            else ctx.lineTo(p.x, p.y);
        });
        ctx.stroke();
        ctx.shadowBlur = 0;

        points.forEach((p, i) => {
            if (i % Math.ceil(points.length / 15) === 0 || i === points.length - 1) {
                ctx.fillStyle = cfg.color;
                ctx.beginPath();
                ctx.arc(p.x, p.y, 3, 0, Math.PI * 2);
                ctx.fill();

                ctx.fillStyle = '#e4e8f0';
                ctx.beginPath();
                ctx.arc(p.x, p.y, 1.5, 0, Math.PI * 2);
                ctx.fill();
            }
        });

        ctx.fillStyle = '#6a7a95';
        ctx.font = '10px monospace';
        ctx.textAlign = 'center';
        for (let i = 0; i < points.length; i += Math.ceil(points.length / 8)) {
            ctx.fillText(`#${i + 1}`, points[i].x, height - padding.bottom + 14);
        }

        const latest = getValue(currentData[currentData.length - 1]);
        const avg = currentData.reduce((s, d) => s + getValue(d), 0) / currentData.length;

        ctx.fillStyle = 'rgba(13, 20, 40, 0.9)';
        ctx.strokeStyle = '#2a3a5a';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(width - padding.right + 8, padding.top, 48, 52, 4);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = cfg.color;
        ctx.font = 'bold 12px monospace';
        ctx.textAlign = 'left';
        ctx.fillText(latest.toFixed(1), width - padding.right + 14, padding.top + 18);
        ctx.fillStyle = '#8a9ab5';
        ctx.font = '9px sans-serif';
        ctx.fillText('最新', width - padding.right + 14, padding.top + 30);
        ctx.fillStyle = '#e4e8f0';
        ctx.font = 'bold 10px monospace';
        ctx.fillText(avg.toFixed(1) + cfg.unit, width - padding.right + 14, padding.top + 46);
    }

    return { init, setData, setType };
})();
