# Dockerfile — simulated Poot miner for tools/testnet
# Minimal Python 3.12 runtime exposing POST /shard?shard_id=<id> and GET /shard?shard_id=<id>

FROM python:3.12-slim

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    openssl \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir requests numpy 2>&1 | tail -3

COPY miner_server.py ./
COPY shard_engine.py ./

ENV MINER_ID=miner-001
ENV ORCHESTRATOR_URL=http://orchestrator:8080
ENV MINER_PORT=9100
ENV SHARD_DIR=/shards

RUN mkdir -p /shards

EXPOSE 9100

CMD ["python", "-u", "miner_server.py"]
