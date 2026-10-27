# ---- build the C server ----
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY backend ./backend
RUN gcc -std=c11 -O2 -o echo_server backend/server.c backend/music.c backend/json.c

# ---- tiny runtime image ----
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/echo_server ./echo_server
COPY static ./static
COPY docker-entrypoint.sh ./docker-entrypoint.sh
RUN chmod +x docker-entrypoint.sh
# Render injects PORT (default 10000). HOST=0.0.0.0 makes the server reachable from outside the container.
ENV HOST=0.0.0.0
# STORAGE_DIR must be the path where the Render persistent disk is mounted.
ENV STORAGE_DIR=/var/data
EXPOSE 10000
CMD ["./docker-entrypoint.sh"]
