#!/usr/bin/env python3
"""
miner_server.py — Simulated miner shard store for the Poot testnet.
Stores encrypted shard blobs on disk and serves them back for
re-assembly.  Designed to be run inside a Docker container; the
container name or MINER_ID env var is the miner's identity.

Endpoints:
  GET  /health                  → {"miner_id": "...", "shards_stored": N}
  POST /shard                   body: raw shard bytes
                                  query: ?shard_id=<id>
                                  → {"ok": true}
  GET  /shard?shard_id=<id>     → raw shard bytes (200) or 404
                                  (legacy alias for /shard via GET)
"""

import os
import sys
import hashlib
import logging
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

logging.basicConfig(
    stream=sys.stdout,
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("miner-server")

SHARD_DIR = os.getenv("SHARD_DIR", "/shards")
MINER_ID  = os.getenv("MINER_ID",  "miner-001")


class MinerHandler(BaseHTTPRequestHandler):

    # ── helpers ─────────────────────────────────────────────────────────

    def _json(self, status: int, payload: dict) -> None:
        body = str(payload).replace("'", '"').encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _store_path(self, shard_id: str) -> str:
        return os.path.join(SHARD_DIR, shard_id)

    # ── routes ──────────────────────────────────────────────────────────

    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/health":
            shards = [f for f in os.listdir(SHARD_DIR) if os.path.isfile(os.path.join(SHARD_DIR, f))]
            self._json(200, {"miner_id": MINER_ID, "shards_stored": len(shards)})
            return

        if parsed.path == "/shard":
            qs = parse_qs(parsed.query)
            shard_id = qs.get("shard_id", [""])[0]
            if not shard_id:
                self._json(400, {"error": "missing shard_id"})
                return
            path = self._store_path(shard_id)
            if not os.path.exists(path):
                self.send_response(404)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            with open(path, "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""

        if parsed.path == "/shard":
            qs = parse_qs(parsed.query)
            shard_id = qs.get("shard_id", [""])[0]
            if not shard_id:
                self._json(400, {"error": "missing shard_id"})
                return
            path = self._store_path(shard_id)
            with open(path, "wb") as f:
                f.write(body)
            log.info("stored shard %s (%d bytes)", shard_id, len(body))
            self._json(200, {"ok": True, "shard_id": shard_id, "bytes": len(body)})
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, fmt, *args):
        log.info(fmt, *args)


def main():
    port = int(os.getenv("MINER_PORT", "9100"))
    srv = HTTPServer(("0.0.0.0", port), MinerHandler)
    log.info("Miner %s: listening on port %d (shard dir=%s)", MINER_ID, port, SHARD_DIR)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    srv.server_close()


if __name__ == "__main__":
    main()
