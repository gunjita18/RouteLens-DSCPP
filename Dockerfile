# Build context must include external/Crow, so clone with:
#   git clone --recurse-submodules <repo>   (or: git submodule update --init)

FROM ubuntu:24.04 AS build
# cJSON is needed by the traffic module, which routelens_server links (it parses
# TomTom segment JSON and reads the major junctions).
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ make libasio-dev libcjson-dev \
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
# The server never calls TomTom (it reads a saved snapshot), so it needs no
# libcurl and no CA certificates. Only cJSON is needed at run time.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcjson1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home routelens
WORKDIR /app
COPY --from=build /src/build/routelens_server /app/routelens_server
COPY web /app/web
# The major-junction list is small and in git, so it is part of the image.
COPY data/junctions/major_junctions.csv /app/data/junctions/major_junctions.csv
USER routelens
# The road network CSVs and the traffic snapshot are not in the image. Mount them
# (the snapshot is optional: without it the server uses free-flow speeds and says
# so on the page). No API key is needed at run time; the snapshot is created
# beforehand with preprocess/fetch_traffic.py:
#   docker run -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" \
#              -v "$PWD/data/traffic:/app/data/traffic:ro" routelens
ENV ROUTELENS_BIND=0.0.0.0 \
    ROUTELENS_PORT=18080 \
    ROUTELENS_WEB_DIR=/app/web \
    ROUTELENS_DATA_DIR=/app/data/osm \
    ROUTELENS_JUNCTIONS_FILE=/app/data/junctions/major_junctions.csv \
    ROUTELENS_TRAFFIC_FILE=/app/data/traffic/traffic.csv
EXPOSE 18080
HEALTHCHECK --interval=30s --timeout=3s CMD ["/bin/bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/${ROUTELENS_PORT} && printf 'GET /api/health HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q 200 <&3"]
CMD ["/app/routelens_server"]
