#!/usr/bin/env python3
"""
run_testnet.py — Phase 1D end-to-end testnet runner.

Steps:
  1. Wait for orchestrator + POST endpoints to be ready
  2. Register 10 miners via orchestrator API
  3. Heartbeat all miners Online
  4. Generate a random 10 MB test file
  5. Split & encrypt the file into 9 shards (6 data + 3 parity)
  6. Write each shard to 3 distinct miner storage dirs on the host volume
  7. Kill 3 miners (remove their containers)
  8. Collect surviving shard files from the 7 alive storage dirs
  9. Reconstruct the original file and verify SHA-256 + size

Usage (from tools/testnet/):
  python3 run_testnet.py
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

# ── Configuration ──────────────────────────────────────────────────────────

SCRIPT_DIR      = Path(__file__).parent.resolve()
TEST_DATA_DIR   = SCRIPT_DIR / "test_data"
TEST_FILE       = TEST_DATA_DIR / "sample_10mb.bin"
MANIFEST_FILE   = TEST_DATA_DIR / "manifest.json"
RESULT_FILE     = SCRIPT_DIR / "test_result.txt"
FAIL_FILE       = SCRIPT_DIR / "fail_reason.txt"
MAGIC_TAIL      = b"\xca\xfe\xba\xbe"

ORCH_URL        = os.environ.get("ORCH_URL", "http://localhost:3000")

MINERS   = [f"miner-{i:03d}" for i in range(1, 11)]
VICTIMS  = ["miner-001", "miner-005", "miner-009"]

DATA_SHARDS   = 6
PARITY_SHARDS = 3
REPLICATION   = 3
FILE_MB       = 10
FILE_BYTES    = FILE_MB * 1024 * 1024

# ── Import shard engine ────────────────────────────────────────────────────

sys.path.insert(0, str(SCRIPT_DIR))
from shard_engine_native import (          # noqa: E402
    split_and_encrypt,
    reconstruct_and_verify,
    Shard,
    Manifest,
    manifest_from_json,
    manifest_to_json,
)

# ── Logging helpers ────────────────────────────────────────────────────────

def info(msg: str) -> None:
    print(f"[INFO] {msg}", flush=True)


def ok(msg: str) -> None:
    print(f"  ✓  {msg}", flush=True)


def die(msg: str, rc: int = 1) -> None:
    print(f"  ✗  {msg}", flush=True)
    FAIL_FILE.write_text(msg + "\n")
    sys.exit(rc)


def wait_for(url: str, label: str, timeout: int = 90) -> None:
    info(f"Waiting for {label} ({url}) …")
    deadline = time.monotonic() + timeout
    last_err: str | None = None
    while time.monotonic() < deadline:
        try:
            import requests  # local import to keep module-level clean
            r = requests.get(url, timeout=2)
            if r.status_code == 200:
                ok(f"{label} ready")
                return
            last_err = f"HTTP {r.status_code}"
        except Exception as e:
            last_err = str(e)
        time.sleep(1)
    die(f"{label} not ready after {timeout}s: {last_err}")


def wait_for_post(url: str, label: str, timeout: int = 90) -> None:
    info(f"Waiting for POST {label} ({url}) …")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            import requests
            r = requests.post(url, timeout=2, json={})
            if r.status_code in (200, 400, 404):
                ok(f"{label} ready (HTTP {r.status_code})")
                return
        except Exception:
            pass
        time.sleep(1)
    die(f"{label} POST not ready after {timeout}s")


# ── Steps ──────────────────────────────────────────────────────────────────

def step_wait() -> None:
    wait_for(f"{ORCH_URL}/health",                              "orchestrator /health")
    wait_for_post(f"{ORCH_URL}/miner/register",                 "orchestrator /miner/register")
    wait_for_post(f"{ORCH_URL}/heartbeat",                      "orchestrator /heartbeat")


def step_register() -> None:
    import requests
    info("Registering 10 miners …")
    for m in MINERS:
        r = requests.post(
            f"{ORCH_URL}/miner/register",
            json={"id": m, "wallet_address": "0xdeadbeef",
                  "storage_bytes_available": 1 << 30},
            timeout=5,
        )
        if r.status_code != 200:
            die(f"register {m}: HTTP {r.status_code}: {r.text[:200]}")
    ok("10 miners registered")


def step_heartbeat() -> None:
    import requests
    info("Heartbeating 10 miners Online …")
    for m in MINERS:
        r = requests.post(
            f"{ORCH_URL}/heartbeat",
            json={"miner_id": m,
                  "storage_bytes_available": 1 << 30,
                  "battery_percent": 80, "is_charging": True,
                  "thermal_state": "nominal", "network_type": "wifi"},
            timeout=5,
        )
        if r.status_code != 200:
            die(f"heartbeat {m}: HTTP {r.status_code}: {r.text[:200]}")
    online = requests.get(f"{ORCH_URL}/miners/online", timeout=5).json()
    n = len(online) if isinstance(online, list) else 0
    if n < 10:
        die(f"only {n} miners Online; expected 10")
    ok("all 10 miners Online")


def step_generate_file() -> tuple[str, int]:
    info(f"Generating {FILE_MB} MB test file …")
    TEST_DATA_DIR.mkdir(exist_ok=True)
    written = 0
    with open(TEST_FILE, "wb") as f:
        urandom = open("/dev/urandom", "rb")
        while written < FILE_BYTES:
            chunk = urandom.read(min(65536, FILE_BYTES - written))
            f.write(chunk)
            written += len(chunk)
        urandom.close()
    # Stamp magic tail so each invocation is unique
    with open(TEST_FILE, "ab") as f:
        f.write(MAGIC_TAIL)
    file_hash = hashlib.sha256(TEST_FILE.read_bytes()).hexdigest()
    file_size = TEST_FILE.stat().st_size
    ok(f"{file_size:,} bytes  SHA-256 {file_hash}")
    return file_hash, file_size


def step_shard_distribute() -> Manifest:
    info("Splitting file into Reed-Solomon shards (Production C++ engine) …")
    data = TEST_FILE.read_bytes()
    shards, manifest = split_and_encrypt(data, DATA_SHARDS, PARITY_SHARDS)
    MANIFEST_FILE.write_text(manifest_to_json(manifest))
    info(f"  {DATA_SHARDS} data + {PARITY_SHARDS} parity = {len(shards)} shards"
         f"  ({manifest.shard_size} B each)")

    info(f"Distributing {len(shards) * REPLICATION} copies across {len(MINERS)} miners …")
    for si, sh in enumerate(shards):
        for r in range(REPLICATION):
            miner     = MINERS[(si + r) % len(MINERS)]
            shard_id  = f"shard-{si:03d}-{r}"
            miner_dir = TEST_DATA_DIR / miner.replace("-", "_")
            miner_dir.mkdir(parents=True, exist_ok=True)
            # Store shard payload (ciphertext + 16-byte authentication tag)
            (miner_dir / shard_id).write_bytes(sh.data + sh.tag)
    ok(f"Distribution complete  ({len(shards) * REPLICATION} shard copies)")

    return manifest


def step_kill() -> None:
    info(f"Killing {len(VICTIMS)} miners: {', '.join(VICTIMS)} …")
    for name in VICTIMS:
        container = f"{SCRIPT_DIR.name}_{name}_1"
        try:
            subprocess.run(["docker", "rm", "-f", container],
                           capture_output=True, timeout=10)
        except Exception:
            pass
        time.sleep(0.3)
    FAIL_FILE.unlink(missing_ok=True)
    time.sleep(2)
    ok(f"{len(VICTIMS)} miners killed")


def step_collect_shards() -> list[tuple[int, bytes]]:
    info("Collecting surviving shards from alive miners …")

    survived: dict[int, bytes] = {}

    for miner_name in MINERS:
        if miner_name in VICTIMS:
            continue
        storage_dir = TEST_DATA_DIR / miner_name.replace("-", "_")
        if not storage_dir.is_dir():
            continue
        for entry in sorted(storage_dir.iterdir()):
            if not entry.is_file() or not entry.name.startswith("shard-"):
                continue
            try:
                shard_idx = int(entry.name.split("-")[1])
            except (IndexError, ValueError):
                continue
            if shard_idx not in survived:
                survived[shard_idx] = entry.read_bytes()

    if len(survived) < DATA_SHARDS:
        die(f"only {len(survived)}/{DATA_SHARDS} required shard indices survived: "
            f"{sorted(survived)}")
    ok(f"{len(survived)}/{DATA_SHARDS} required indices: {sorted(survived)}")
    return sorted(survived.items())


def step_reconstruct(survived: list[tuple[int, bytes]],
                     orig_hash: str, orig_size: int) -> None:
    info("Reconstructing …")
    manifest = manifest_from_json(MANIFEST_FILE.read_text())
    if manifest is None:
        die("failed to parse manifest.json")

    shard_objs = [Shard(index=i, data=raw, iv=b"", tag=b"")
                  for i, raw in survived]
    recovered = reconstruct_and_verify(shard_objs, manifest)
    if recovered is None:
        die("reconstruction failed: content hash mismatch")

    actual_hash = hashlib.sha256(recovered).hexdigest()
    actual_size = len(recovered)
    if actual_hash != orig_hash:
        die(f"hash mismatch: {actual_hash} != {orig_hash}")
    if actual_size != orig_size:
        die(f"size mismatch: {actual_size} != {orig_size}")
    ok(f"File intact — {actual_size:,} bytes  SHA-256 {actual_hash}")


# ── Main ───────────────────────────────────────────────────────────────────

def main() -> None:
    step_wait()
    step_register()
    step_heartbeat()
    orig_hash, orig_size = step_generate_file()
    step_shard_distribute()
    step_kill()
    survived = step_collect_shards()
    step_reconstruct(survived, orig_hash, orig_size)

    RESULT_FILE.write_text("PASS")
    print()
    print("═" * 60)
    print("  Phase 1D testnet: PASS")
    print(f"  File:          {orig_size:,} bytes")
    print(f"  SHA-256:       {orig_hash}")
    print(f"  Miners online: {len(MINERS)}")
    print(f"  Miners killed: {len(VICTIMS)}  ({', '.join(VICTIMS)})")
    print(f"  Survivors:     {len(MINERS) - len(VICTIMS)}")
    print("═" * 60)


if __name__ == "__main__":
    main()
