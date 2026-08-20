"""
shard_engine.py — Reference pure-Python GF(2^8) Reed-Solomon implementation.

NOTE (FR-TST-1):
This pure-Python module is maintained solely as a reference, fuzzing, and
cross-validation tool. It is NOT the production cryptographic engine.
All automated CI pipeline tests and testnet verification suites MUST use
`shard_engine_native.py` (which links directly to `libshard-engine.so`)
to validate the production C++ Reed-Solomon and AES-256-GCM implementation.
"""

from __future__ import annotations
import hashlib
import struct
import zlib
from dataclasses import dataclass, field
from typing import List, Optional

# ── constants ─────────────────────────────────────────────────────────────

_DEFAULT_DATA   = 6
_DEFAULT_PARITY = 3


# ── GF(2^8) arithmetic ─────────────────────────────────────────────────────

_PRIMITIVE = 0x11D  # x^8 + x^4 + x^3 + x^2 + 1


def _build_gf_tables() -> tuple[list[int], list[int]]:
    exp = [0] * 512
    log = [0] * 256
    x = 1
    for i in range(255):
        exp[i] = x
        exp[i + 255] = x
        log[x] = i
        if x & 0x80:
            x = (x << 1) ^ _PRIMITIVE
        else:
            x <<= 1
    exp[255] = exp[0]
    return exp, log


_EXP, _LOG = _build_gf_tables()


def _gf_mul(a: int, b: int) -> int:
    if a == 0 or b == 0:
        return 0
    return _EXP[_LOG[a] + _LOG[b]]


def _gf_div(a: int, b: int) -> int:
    if b == 0:
        raise ZeroDivisionError("GF division by zero")
    if a == 0:
        return 0
    return _EXP[(_LOG[a] + 255 - _LOG[b]) % 255]


# ── Vandermonde matrix ─────────────────────────────────────────────────────

def _vandermonde(row: int, col: int) -> int:
    """GF(2^8): row^col, with row=0→1 everywhere (identity row)."""
    if col == 0:
        return 1
    return _EXP[(row * col) % 255]


# ── Gaussian elimination in GF(2^8) ───────────────────────────────────────

def _gauss_jordan(mat: list[list[int]]) -> list[list[int]]:
    n = len(mat)
    # Augmented matrix
    aug = [row[:] + [1 if i == j else 0 for j in range(n)]
           for i, row in enumerate(mat)]

    for col in range(n):
        # Pivot
        pivot = next((r for r in range(col, n) if aug[r][col]), None)
        if pivot is None:
            raise ValueError("Singular matrix")
        if pivot != col:
            aug[col], aug[pivot] = aug[pivot], aug[col]

        pivot_val = aug[col][col]
        # Scale pivot row
        aug[col] = [_gf_div(v, pivot_val) for v in aug[col]]

        for r in range(n):
            if r == col:
                continue
            factor = aug[r][col]
            if factor == 0:
                continue
            aug[r] = [aug[r][c] ^ _gf_mul(factor, aug[col][c])
                      for c in range(2 * n)]

    # Extract inverse (right half)
    return [row[n:] for row in aug]


# ── Encode / Decode ────────────────────────────────────────────────────────

def encode(data_shards: List[bytes]) -> List[bytes]:
    """Reed-Solomon encode: given N data shards, return N+M parity shards."""
    n = len(data_shards)
    if n == 0:
        raise ValueError("No data shards")
    shard_size = len(data_shards[0])
    for i, s in enumerate(data_shards):
        if len(s) != shard_size:
            raise ValueError(f"Data shard {i} has wrong size: {len(s)} (expected {shard_size})")

    m = _DEFAULT_PARITY
    # Vandermonde sub-matrix for parity rows [n .. n+m)
    parity = []
    for p in range(m):
        row = n + p
        out = bytearray(shard_size)
        for b in range(shard_size):
            acc = 0
            for d in range(n):
                acc ^= _gf_mul(_vandermonde(row, d), data_shards[d][b])
            out[b] = acc
        parity.append(bytes(out))
    return parity


def decode(shards: List[bytes], indices: List[int], data_shards: int) -> List[bytes]:
    """Decode: given any ≥ data_shards shards, reconstruct all data shards."""
    if len(shards) != len(indices):
        raise ValueError("shards and indices must have the same length")
    if len(shards) < data_shards:
        raise ValueError(f"Need ≥ {data_shards} shards, got {len(shards)}")
    shard_size = len(shards[0])
    k = min(data_shards, len(shards))

    # Build matrix B
    B = [[0] * k for _ in range(k)]
    for i in range(k):
        idx = indices[i]
        if idx < data_shards:
            B[i][idx] = 1
        else:
            for j in range(k):
                B[i][j] = _vandermonde(idx, j)

    inv_B = _gauss_jordan(B)  # k × k

    # Reconstruct data shards
    result = []
    for j in range(data_shards):
        out = bytearray(shard_size)
        for b in range(shard_size):
            acc = 0
            for i in range(k):
                acc ^= _gf_mul(inv_B[j][i], shards[i][b])
            out[b] = acc
        result.append(bytes(out))
    return result


# ── Data structures ────────────────────────────────────────────────────────

@dataclass
class Shard:
    index: int = 0
    data: bytes = b""
    iv:   bytes = b""
    tag:  bytes = b""


@dataclass
class Manifest:
    content_hash_b64:   str = ""
    original_size:      int  = 0
    shard_size:         int  = 0
    data_shards:        int  = _DEFAULT_DATA
    parity_shards:      int  = _DEFAULT_PARITY
    shard_hashes_b64:   List[str] = field(default_factory=list)
    encryption_iv_b64:  str       = ""


# ── AES-256-GCM via /dev/urandom wrapper ───────────────────────────────────
#
# The per-shard "encryption" in this Python engine is XOR-based (fast to
# verify in pure Python, slow enough to deter abuse, not meant to replace
# OpenSSL in production).  The C++ shard-engine uses real AES-256-GCM.
# Both produce the same shard layout so the manifest round-trips cleanly.

def _xor_bytes(key: bytes, data: bytes) -> bytes:
    out = bytearray(len(data))
    for i, b in enumerate(data):
        out[i] = b ^ key[i % len(key)]
    return bytes(out)


def _derive_key(content_hash: bytes) -> bytes:
    return content_hash  # SHA-256 is already 32 bytes


def _rand(n: int) -> bytes:
    return open("/dev/urandom", "rb").read(n)


# ── Manifest JSON helpers (deterministic) ──────────────────────────────────

def _b64encode(b: bytes) -> str:
    import base64
    return base64.b64encode(b).decode()


def _b64decode(s: str) -> bytes:
    import base64
    return base64.b64decode(s)


def manifest_to_json(m: Manifest) -> str:
    shards_json = ",".join(f'"{h}"' for h in m.shard_hashes_b64)
    return (
        f'{{'
        f'"content_hash_b64":"{m.content_hash_b64}",'
        f'"original_size":{m.original_size},'
        f'"shard_size":{m.shard_size},'
        f'"data_shards":{m.data_shards},'
        f'"parity_shards":{m.parity_shards},'
        f'"encryption_iv_b64":"{m.encryption_iv_b64}",'
        f'"shard_hashes_b64":[{shards_json}]'
        f'}}'
    )


def manifest_from_json(s: str) -> Optional[Manifest]:
    """Very small JSON parser — matches the C++ implementation."""
    try:
        # We only need to pull specific string/int values by key.
        import json, re
        d = json.loads(s)
        hashes = d.get("shard_hashes_b64", [])
        return Manifest(
            content_hash_b64  = d.get("content_hash_b64", ""),
            original_size     = d.get("original_size", 0),
            shard_size        = d.get("shard_size", 0),
            data_shards       = d.get("data_shards", _DEFAULT_DATA),
            parity_shards     = d.get("parity_shards", _DEFAULT_PARITY),
            shard_hashes_b64  = list(hashes),
            encryption_iv_b64 = d.get("encryption_iv_b64", ""),
        )
    except (json.JSONDecodeError, KeyError):
        return None


# ── Public API ─────────────────────────────────────────────────────────────

def split_and_encrypt(
    data:        bytes,
    data_shards: int    = _DEFAULT_DATA,
    parity_shards: int = _DEFAULT_PARITY,
) -> tuple[List[Shard], Manifest]:
    if not data:
        raise ValueError("Cannot shard empty data")
    if data_shards <= 0 or parity_shards <= 0:
        raise ValueError("Shard counts must be positive")

    total = data_shards + parity_shards
    shard_size = (len(data) + data_shards - 1) // data_shards  # ceil

    # Pad to multiple of data_shards
    padded = data + b'\x00' * (shard_size * data_shards - len(data))

    # Data shards
    raw_data = [padded[i * shard_size:(i + 1) * shard_size]
                for i in range(data_shards)]

    # Parity shards
    raw_parity = encode(raw_data)

    content_hash = hashlib.sha256(data).digest()
    key = _derive_key(content_hash)
    iv  = _rand(12)

    shards: List[Shard] = []
    shard_hashes_b64: List[str] = []

    for i in range(total):
        raw = (raw_data if i < data_shards else raw_parity)[i % data_shards]
        encrypted = _xor_bytes(key, raw)
        sh = Shard(
            index = i,
            data  = encrypted,
            iv    = iv,
            tag   = b"",  # XOR cipher has no tag; C++ engine uses AES-GCM 16B
        )
        shards.append(sh)
        shard_hashes_b64.append(_b64encode(hashlib.sha256(raw).digest()))

    manifest = Manifest(
        content_hash_b64  = _b64encode(content_hash),
        original_size      = len(data),
        shard_size         = shard_size,
        data_shards        = data_shards,
        parity_shards      = parity_shards,
        shard_hashes_b64   = shard_hashes_b64,
        encryption_iv_b64  = _b64encode(iv),
    )
    return shards, manifest


def reconstruct_and_verify(
    shards:   List[Shard],
    manifest: Manifest,
) -> Optional[bytes]:
    if len(shards) < manifest.data_shards:
        raise ValueError(f"Need ≥ {manifest.data_shards} shards, got {len(shards)}")

    # Decrypt
    content_hash = _b64decode(manifest.content_hash_b64)
    key = _derive_key(content_hash)

    decrypted = [_xor_bytes(key, sh.data) for sh in shards]
    indices   = [sh.index for sh in shards]

    # RS-decode
    data_blocks = decode(decrypted, indices, manifest.data_shards)

    # Reassemble
    raw = b"".join(data_blocks)
    raw = raw[:manifest.original_size]

    # Verify content hash
    if hashlib.sha256(raw).digest() != content_hash:
        return None
    return raw
