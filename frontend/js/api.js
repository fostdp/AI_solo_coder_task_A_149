const ApiClient = (function () {
    const DEFAULT_BASE = 'http://127.0.0.1:8080';
    let baseUrl = DEFAULT_BASE;

    function setBaseUrl(url) { baseUrl = url; }

    async function request(path, options = {}) {
        const url = baseUrl + path;
        const defaultOptions = {
            method: 'GET',
            headers: { 'Accept': 'application/json' }
        };
        const finalOptions = { ...defaultOptions, ...options };
        try {
            const response = await fetch(url, finalOptions);
            const text = await response.text();
            try { return JSON.parse(text); }
            catch { return text; }
        } catch (e) {
            console.error('API请求失败:', e);
            return null;
        }
    }

    async function health() { return request('/api/health'); }

    async function predictRange(params = {}) {
        const query = Object.entries(params)
            .map(([k, v]) => `${k}=${encodeURIComponent(v)}`)
            .join('&');
        return request(`/api/predict-range?${query}`);
    }

    async function springEnergy(angle) {
        return request(`/api/spring-energy?angle=${encodeURIComponent(angle)}`);
    }

    async function getSensorData(machineId = '', limit = 100) {
        const query = [];
        if (machineId) query.push(`machine_id=${encodeURIComponent(machineId)}`);
        query.push(`limit=${limit}`);
        return request(`/api/sensor-data?${query.join('&')}`);
    }

    async function getAlerts(machineId = '', limit = 50) {
        const query = [];
        if (machineId) query.push(`machine_id=${encodeURIComponent(machineId)}`);
        query.push(`limit=${limit}`);
        return request(`/api/alerts?${query.join('&')}`);
    }

    async function getMachineStatus() {
        return request('/api/machine-status');
    }

    return {
        setBaseUrl,
        health,
        predictRange,
        springEnergy,
        getSensorData,
        getAlerts,
        getMachineStatus
    };
})();
