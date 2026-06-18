FROM clickhouse/clickhouse-server:24.3-alpine

COPY docker/clickhouse/config.xml /etc/clickhouse-server/config.d/override_config.xml
COPY docker/clickhouse/users.xml /etc/clickhouse-server/users.d/override_users.xml
COPY docker/clickhouse/init/ /docker-entrypoint-initdb.d/

RUN chown -R clickhouse:clickhouse /etc/clickhouse-server

ENV CLICKHOUSE_DB=trebuchet_sim \
    CLICKHOUSE_USER=default \
    CLICKHOUSE_DEFAULT_ACCESS_MANAGEMENT=1

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=5 \
    CMD wget --no-verbose --tries=1 --spider http://localhost:8123/?query=SELECT%201 || exit 1
