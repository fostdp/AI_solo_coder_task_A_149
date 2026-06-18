FROM eclipse-mosquitto:2.0-alpine

COPY docker/mqtt/mosquitto.conf /mosquitto/config/mosquitto.conf

RUN adduser -D -H mosquitto_publisher && \
    adduser -D -H mosquitto_subscriber

EXPOSE 1883 9001

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD mosquitto_pub -t '$$SYS/broker/uptime' -n || exit 1
