# Dockerfile for Poot Orchestrator — C++ server (services/orchestrator)
# Builds the C++23 HTTP server via CMake and runs orchestrator-server on container port 8080

FROM gcc:13

SHELL ["/bin/bash", "-lc"]

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    git \
    ca-certificates \
    libpq-dev \
    pkg-config \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only CMake sources first for cache efficiency
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/

RUN cmake -B build-native -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build-native --parallel

EXPOSE 8080

CMD ["build-native/orchestrator-server"]
