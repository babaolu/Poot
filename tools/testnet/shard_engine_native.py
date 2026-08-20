"""
shard_engine_native.py — Python ctypes bindings to the production C++ shard-engine shared library.
Enforces FR-TST-1: testnet exercises the real production C++ shard engine, not a mock or re-implementation.
"""

from __future__ import annotations

import ctypes
import json
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

# ── Locate and load libshard-engine.so ──────────────────────────────────────

_POSSIBLE_LIB_PATHS = [
    Path(__file__).parent.parent.parent / "packages" / "shard-engine" / "build-native" / "libshard-engine.so",
    Path(__file__).parent.parent.parent / "packages" / "shard-engine" / "build" / "libshard-engine.so",
    Path("/usr/local/lib/libshard-engine.so"),
    Path("/usr/lib/libshard-engine.so"),
]

_LIB_PATH = os.environ.get("SHARD_ENGINE_LIB")
_lib = None

if _LIB_PATH and Path(_LIB_PATH).exists():
    _lib = ctypes.CDLL(str(Path(_LIB_PATH).resolve()))
else:
    for p in _POSSIBLE_LIB_PATHS:
        if p.exists():
            _lib = ctypes.CDLL(str(p.resolve()))
            break

if _lib is None:
    raise RuntimeError(
        "Could not locate libshard-engine.so. Please build packages/shard-engine via CMake first, "
        "or set SHARD_ENGINE_LIB=/path/to/libshard-engine.so"
    )

# ── Function Signatures ────────────────────────────────────────────────────

_lib.shard_split.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.shard_split.restype = ctypes.c_void_p

_lib.shard_get_data.argtypes = [
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.shard_get_data.restype = ctypes.POINTER(ctypes.c_uint8)

_lib.shard_get_iv.argtypes = [
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.shard_get_iv.restype = ctypes.POINTER(ctypes.c_uint8)

_lib.shard_get_tag.argtypes = [
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.shard_get_tag.restype = ctypes.POINTER(ctypes.c_uint8)

_lib.shard_get_index.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.shard_get_index.restype = ctypes.c_size_t

_lib.shard_get_manifest_json.argtypes = [ctypes.c_void_p]
_lib.shard_get_manifest_json.restype = ctypes.c_char_p

_lib.shard_reconstruct_with_tags.argtypes = [
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.shard_reconstruct_with_tags.restype = ctypes.POINTER(ctypes.c_uint8)

_lib.shard_free.argtypes = [ctypes.c_void_p]
_lib.shard_free.restype = None

_lib.shard_free_string.argtypes = [ctypes.c_char_p]
_lib.shard_free_string.restype = None

_lib.shard_free_data.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
_lib.shard_free_data.restype = None


# ── Data Structures ────────────────────────────────────────────────────────

@dataclass
class Shard:
    index: int = 0
    data: bytes = b""
    iv: bytes = b""
    tag: bytes = b""


@dataclass
class Manifest:
    content_hash_b64: str = ""
    original_size: int = 0
    shard_size: int = 0
    data_shards: int = 6
    parity_shards: int = 3
    shard_hashes_b64: List[str] = field(default_factory=list)
    shard_ivs_b64: List[str] = field(default_factory=list)
    wrapped_key_b64: str = ""
    key_iv_b64: str = ""
    key_tag_b64: str = ""


# ── Manifest Helpers ───────────────────────────────────────────────────────

def manifest_to_json(m: Manifest) -> str:
    return json.dumps({
        "content_hash_b64": m.content_hash_b64,
        "original_size": m.original_size,
        "shard_size": m.shard_size,
        "data_shards": m.data_shards,
        "parity_shards": m.parity_shards,
        "shard_hashes_b64": m.shard_hashes_b64,
        "shard_ivs_b64": m.shard_ivs_b64,
        "wrapped_key_b64": m.wrapped_key_b64,
        "key_iv_b64": m.key_iv_b64,
        "key_tag_b64": m.key_tag_b64,
    }, indent=2)


def manifest_from_json(s: str) -> Optional[Manifest]:
    try:
        d = json.loads(s)
        return Manifest(
            content_hash_b64=d.get("content_hash_b64", ""),
            original_size=d.get("original_size", 0),
            shard_size=d.get("shard_size", 0),
            data_shards=d.get("data_shards", 6),
            parity_shards=d.get("parity_shards", 3),
            shard_hashes_b64=list(d.get("shard_hashes_b64", [])),
            shard_ivs_b64=list(d.get("shard_ivs_b64", [])),
            wrapped_key_b64=d.get("wrapped_key_b64", ""),
            key_iv_b64=d.get("key_iv_b64", ""),
            key_tag_b64=d.get("key_tag_b64", ""),
        )
    except Exception:
        return None


# ── Python High-Level API ──────────────────────────────────────────────────

def split_and_encrypt(
    data: bytes,
    data_shards: int = 6,
    parity_shards: int = 3,
) -> tuple[List[Shard], Manifest]:
    if not data:
        raise ValueError("Cannot shard empty data")

    data_bytes = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    out_shard_count = ctypes.c_size_t(0)

    handle = _lib.shard_split(data_bytes, len(data), ctypes.byref(out_shard_count))
    if not handle:
        raise RuntimeError("shard_split failed in native library")

    try:
        manifest_raw = _lib.shard_get_manifest_json(handle)
        if not manifest_raw:
            raise RuntimeError("Failed to retrieve manifest JSON from native handle")
        
        manifest_str = manifest_raw.decode("utf-8")
        manifest = manifest_from_json(manifest_str)
        if not manifest:
            raise RuntimeError("Failed to parse native manifest JSON")

        shards: List[Shard] = []
        for i in range(out_shard_count.value):
            out_len = ctypes.c_size_t(0)
            data_ptr = _lib.shard_get_data(handle, i, ctypes.byref(out_len))
            shard_data = bytes(ctypes.string_at(data_ptr, out_len.value))

            iv_len = ctypes.c_size_t(0)
            iv_ptr = _lib.shard_get_iv(handle, i, ctypes.byref(iv_len))
            shard_iv = bytes(ctypes.string_at(iv_ptr, iv_len.value))

            tag_len = ctypes.c_size_t(0)
            tag_ptr = _lib.shard_get_tag(handle, i, ctypes.byref(tag_len))
            shard_tag = bytes(ctypes.string_at(tag_ptr, tag_len.value))

            orig_index = _lib.shard_get_index(handle, i)

            shards.append(Shard(index=orig_index, data=shard_data, iv=shard_iv, tag=shard_tag))

        return shards, manifest
    finally:
        _lib.shard_free(handle)


def reconstruct_and_verify(
    shards: List[Shard],
    manifest: Manifest,
) -> Optional[bytes]:
    if len(shards) < manifest.data_shards:
        raise ValueError(f"Need >= {manifest.data_shards} shards, got {len(shards)}")

    manifest_json = manifest_to_json(manifest).encode("utf-8")
    count = len(shards)

    # Prepare C pointer arrays
    c_data_buffers = []
    c_data_ptrs = (ctypes.POINTER(ctypes.c_uint8) * count)()
    c_lens = (ctypes.c_size_t * count)()
    c_tag_buffers = []
    c_tag_ptrs = (ctypes.POINTER(ctypes.c_uint8) * count)()
    c_indices = (ctypes.c_size_t * count)()

    for i, sh in enumerate(shards):
        buf = (ctypes.c_uint8 * len(sh.data)).from_buffer_copy(sh.data)
        c_data_buffers.append(buf)
        c_data_ptrs[i] = ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8))
        c_lens[i] = len(sh.data)

        if sh.tag:
            tag_buf = (ctypes.c_uint8 * len(sh.tag)).from_buffer_copy(sh.tag)
            c_tag_buffers.append(tag_buf)
            c_tag_ptrs[i] = ctypes.cast(tag_buf, ctypes.POINTER(ctypes.c_uint8))
        else:
            c_tag_ptrs[i] = None

        c_indices[i] = sh.index

    out_len = ctypes.c_size_t(0)
    res_ptr = _lib.shard_reconstruct_with_tags(
        manifest_json,
        c_data_ptrs,
        c_lens,
        c_tag_ptrs,
        c_indices,
        count,
        ctypes.byref(out_len),
    )

    if not res_ptr:
        return None

    try:
        return bytes(ctypes.string_at(res_ptr, out_len.value))
    finally:
        _lib.shard_free_data(res_ptr)
