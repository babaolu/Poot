#!/usr/bin/env bash
# run_testnet.sh — Phase 1D end-to-end CI wrapper
#
# Usage (from tools/testnet/):
#   ./run_testnet.sh               # starts docker-compose then runs
#   ./run_testnet.sh skip-start    # skip docker-compose up (already running)
#
# Exit codes:
#   0  success
#   1  failure (failure reason in fail_reason.txt)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_START=false
[[ "${1:-}" == "skip-start" ]] && SKIP_START=true

if ! $SKIP_START; then
    echo "=== Starting testnet stack ==="
    cd "$SCRIPT_DIR"
    docker-compose up --build -d 2>&1
    echo ""
fi

echo "=== Running Phase 1D testnet pipeline ==="
cd "$SCRIPT_DIR"
export ORCH_URL="${ORCH_URL:-http://localhost:3000}"
python3 run_testnet.py
rc=$?

if [[ $rc -ne 0 ]]; then
    echo ""
    echo "FAIL — see ${SCRIPT_DIR}/fail_reason.txt"
    echo "(containers left running for inspection — run 'docker-compose down' when done)"
    exit $rc
fi

echo ""
echo "All good.  Tear down with: cd tools/testnet && docker-compose down"
exit 0
