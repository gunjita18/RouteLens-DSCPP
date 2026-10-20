# Build context must include external/Crow, so clone with:
#   git clone --recurse-submodules <repo>   (or: git submodule update --init)

FROM ubuntu:24.04 AS build
# libcurl and cJSON are needed because CMakeLists.txt configures the traffic
# module for every target, even though routelens_server does not link it yet.
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ make libasio-dev libcurl4-openssl-dev libcjson-dev \
        libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY external ./external
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target routelens_server -j2

FROM ubuntu:24.04
# Runtime libraries for the TomTom client: libcurl and cJSON, and CA certificates
# (without them HTTPS calls to TomTom fail inside the container).
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcurl4t64 libcjson1 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home routelens \
    && mkdir -p /var/lib/routelens && chown routelens:routelens /var/lib/routelens
WORKDIR /app
COPY --from=build /src/build/routelens_server /app/routelens_server
COPY web /app/web
USER routelens
# The road network CSVs are not in git or in the image. Mount them. The TomTom
# key is never in the image either: pass it at run time, and keep the daily
# usage counter in a volume:
#   docker run -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" \
#              -v routelens-cache:/var/lib/routelens --env-file .env routelens
ENV ROUTELENS_BIND=0.0.0.0 \
    ROUTELENS_PORT=18080 \
    ROUTELENS_WEB_DIR=/app/web \
    ROUTELENS_DATA_DIR=/app/data/osm \
    ROUTELENS_CACHE_DIR=/var/lib/routelens
EXPOSE 18080
HEALTHCHECK --interval=30s --timeout=3s CMD ["/bin/bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/${ROUTELENS_PORT} && printf 'GET /api/health HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q 200 <&3"]
CMD ["/app/routelens_server"]
