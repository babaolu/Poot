# Poot API Reference Guide

This document describes the HTTP API surface of the Poot Orchestrator.

---

## 1. Miner API (C++ Orchestrator, Port 8080/3000)

The miner API endpoints are implemented natively in the C++ custom HTTP server (`services/orchestrator/src/server.cpp`).

### 1.1. List All Registered Miners
* **Endpoint**: `GET /miners`
* **Description**: Returns a JSON array of all registered miner IDs in the network.
* **Response `200 OK`**:
  ```json
  [
    "miner-001",
    "miner-002"
  ]
  ```

### 1.2. List Online Miners Only
* **Endpoint**: `GET /miners/online`
* **Description**: Returns a JSON array of currently active and online miner IDs.
* **Response `200 OK`**:
  ```json
  [
    "miner-001"
  ]
  ```

### 1.3. Get Single Miner Details
* **Endpoint**: `GET /miner/:id`
* **Description**: Returns detailed resource, battery, thermal, and network status for a specific miner.
* **Response `200 OK`**:
  ```json
  {
    "id": "miner-001",
    "wallet_address": "0x4f3e...",
    "storage_bytes_available": 10737418240,
    "storage_bytes_used": 1048576,
    "compute_units_available": 4,
    "compute_units_used": 0,
    "status": "online",
    "battery_percent": 95,
    "cpu_usage_percent": 12.5,
    "thermal_state": "nominal",
    "network_type": "wifi"
  }
  ```
* **Response `404 Not Found`**:
  ```json
  {
    "error": "Miner not found"
  }
  ```

### 1.4. Register a New Miner
* **Endpoint**: `POST /miner/register`
* **Description**: Registers a new mobile miner node in the orchestrator registry.
* **Payload**:
  ```json
  {
    "id": "miner-001",
    "wallet_address": "0x4f3e...",
    "storage_bytes_available": 10737418240
  }
  ```
* **Response `200 OK`**:
  ```json
  {
    "id": "miner-001",
    "status": "registered"
  }
  ```

### 1.5. Miner Heartbeat
* **Endpoint**: `POST /heartbeat`
* **Description**: Receives periodic status and resource updates from active miners.
* **Payload**:
  ```json
  {
    "miner_id": "miner-001",
    "storage_bytes_available": 10737418240,
    "battery_percent": 88,
    "thermal_state": "nominal",
    "network_type": "wifi"
  }
  ```
* **Response `200 OK`**:
  ```json
  {
    "accepted": true
  }
  ```

### 1.6. Check Offline Miners
* **Endpoint**: `POST /check-offline`
* **Description**: Manually triggers a stale heartbeat sweep, marking silent miners offline and returning their IDs.
* **Response `200 OK`**:
  ```json
  [
    "miner-002"
  ]
  ```

---

## 2. Planned Customer & Deployment APIs (Phase 3 Spec)

These endpoints are part of the upcoming deployment layer to be managed by `apps/customer-dashboard` and `services/instance-runner`.

### 2.1. Deploy Customer Instance
* **Endpoint**: `POST /instances/deploy`
* **Description**: Accepts a deployment manifest, initiates file sharding, and instructs the orchestrator to assign shards.
* **Payload**:
  ```json
  {
    "customer_id": "cust-999",
    "deployment_type": "static_site",
    "source_package_sha256": "8f3c...",
    "replication_factor": 3
  }
  ```
* **Response `202 Accepted`**:
  ```json
  {
    "instance_id": "inst-111",
    "status": "sharding_initiated",
    "manifest": {
      "data_shards": 6,
      "parity_shards": 3,
      "total_shards": 9
    }
  }
  ```

### 2.2. Get Instance Live Status
* **Endpoint**: `GET /api/instances/:id`
* **Description**: Fetches deployment status, replica counts, active storage nodes, and latency metrics.
* **Response `200 OK`**:
  ```json
  {
    "instance_id": "inst-111",
    "status": "running",
    "replicas_online": 9,
    "replicas_healthy": 9,
    "replicas_assigned": [
      { "shard_id": "shard-0", "miner_id": "miner-001", "status": "active" },
      { "shard_id": "shard-1", "miner_id": "miner-003", "status": "active" }
    ]
  }
  ```
