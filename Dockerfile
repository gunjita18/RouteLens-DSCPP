# Build context must include external/Crow, so clone with:
#   git clone --recurse-submodules <repo>   (or: git submodule update --init)

FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake g++ make libasio-dev libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY external ./external
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target routelens_server -j2

FROM ubuntu:24.04
# The server only needs libstdc++/libc, which the base image already has.
RUN useradd --system --no-create-home routelens
WORKDIR /app
COPY --from=build /src/build/routelens_server /app/routelens_server
COPY web /app/web
USER routelens
# The road network CSVs are not in git or in the image. Mount them:
#   docker run -p 18080:18080 -v "$PWD/data/osm:/app/data/osm:ro" routelens
ENV ROUTELENS_BIND=0.0.0.0 \
    ROUTELENS_PORT=18080 \
    ROUTELENS_WEB_DIR=/app/web \
    ROUTELENS_DATA_DIR=/app/data/osm
EXPOSE 18080
HEALTHCHECK --interval=30s --timeout=3s CMD ["/bin/bash", "-c", "exec 3<>/dev/tcp/127.0.0.1/${ROUTELENS_PORT} && printf 'GET /api/health HTTP/1.0\\r\\n\\r\\n' >&3 && grep -q 200 <&3"]
CMD ["/app/routelens_server"]
