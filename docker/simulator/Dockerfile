FROM python:3.11-slim-bookworm

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    tini \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir --upgrade pip

COPY simulator/requirements.txt .
RUN if [ -f requirements.txt ]; then pip3 install --no-cache-dir -r requirements.txt; fi

COPY simulator/trebuchet_sensor_simulator.py .

RUN chmod +x trebuchet_sensor_simulator.py

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD pgrep -f "trebuchet_sensor_simulator.py" >/dev/null 2>&1 || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["python3", "trebuchet_sensor_simulator.py", "--host", "backend"]
