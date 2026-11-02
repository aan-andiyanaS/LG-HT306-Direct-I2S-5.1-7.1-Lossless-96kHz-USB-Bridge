"""Tes codec.py terhadap oracle tests/verify_end_to_end.py (tanpa hardware)."""
from __future__ import annotations

import importlib.util
import time
from pathlib import Path

import numpy as np
import pytest

from ht306bridge import codec, protocol as P

REPO = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("verify_e2e_oracle", REPO / "tests" / "verify_end_to_end.py")
oracle = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(oracle)

MODES = [False, True]  # bits32
SIZES = [48, 96]
LO = {False: -(1 << 23), True: -(1 << 31)}
HI = {False: (1 << 23) - 1, True: (1 << 31) - 1}


def ref(pcm, seq, bits32):
    return oracle.encode_lossless_subframe(pcm.tolist(), seq, is_32bit=bits32)


def gen_cases(bits32, n, rng):
    lo, hi = LO[bits32], HI[bits32]
    cases = []
    for _ in range(120):
        amp = int(rng.integers(1, hi))
        cases.append(rng.integers(-amp, amp, size=(n, 6)))
    t = np.arange(n)[:, None]
    for f in (0.01, 0.07, 0.3):
        cases.append(np.round(hi * 0.9 * np.sin(2 * np.pi * f * t + np.arange(6))).astype(np.int64))
    for _ in range(20):  # sinus + transien full-scale
        x = np.round(hi * 0.5 * np.sin(2 * np.pi * 0.02 * t + np.arange(6)))
        i = int(rng.integers(1, n - 1))
        x[i] = hi
        x[i + 1] = lo
        cases.append(x.astype(np.int64))
    for _ in range(60):  # amplitudo kecil: muat di paket untuk N=96
        cases.append(rng.integers(-(1 << int(rng.integers(2, 8))), 1 << 7, size=(n, 6)))
    cases.append(np.zeros((n, 6), dtype=np.int64))
    cases.append(np.full((n, 6), hi, dtype=np.int64))
    cases.append(np.full((n, 6), lo, dtype=np.int64))
    sq = np.where((np.arange(n) % 2)[:, None] == 0, hi, lo) * np.ones((1, 6), dtype=np.int64)
    cases.append(sq)
    cases.append(np.clip(np.round(hi * 3 * np.sin(2 * np.pi * 0.05 * t + np.arange(6))), lo, hi).astype(np.int64))
    for _ in range(80):
        cases.append(rng.integers(lo, hi + 1, size=(n, 6), dtype=np.int64))
    return [c.astype(np.int32) for c in cases]


def encode_or_skip_oversize(pcm, seq, bits32):
    """Oracle tidak membatasi ukuran; codec wajib raise ValueError bila > MAX."""
    r = ref(pcm, seq, bits32)
    if len(r) > P.MAX_PACKET_BYTES:
        with pytest.raises(ValueError):
            codec.encode_subframe(pcm, seq, bits32=bits32)
        return None, None
    return codec.encode_subframe(pcm, seq, bits32=bits32), r


@pytest.mark.parametrize("n", SIZES)
@pytest.mark.parametrize("bits32", MODES)
def test_bytes_identical_and_roundtrip(bits32, n):
    rng = np.random.default_rng(1234 + n + bits32)
    cases = gen_cases(bits32, n, rng)
    assert len(cases) >= 200
    done = 0
    for i, pcm in enumerate(cases):
        got, r = encode_or_skip_oversize(pcm, i, bits32)
        if got is None:
            continue
        assert got == r, f"case {i}"
        dec, info = codec.decode_subframe(got)
        assert np.array_equal(dec, pcm)
        assert oracle.decode_lossless_subframe(got) == pcm.tolist()
        done += 1
    assert done >= 40


@pytest.mark.parametrize("bits32", MODES)
def test_block_matches_single(bits32):
    rng = np.random.default_rng(7)
    amp = 1 << (20 if not bits32 else 14)
    blocks = [rng.integers(-amp, amp, size=(48, 6)).astype(np.int32) for _ in range(9)]
    blocks[3][:] = 0
    got = codec.encode_block(np.concatenate(blocks), 250, bits32=bits32)
    assert len(got) == 9
    for i, (g, b) in enumerate(zip(got, blocks)):
        assert g == ref(b, (250 + i) & 0xFF, bits32)  # seq wrap 8-bit


@pytest.mark.parametrize("bits32", MODES)
def test_header_and_crc(bits32):
    rng = np.random.default_rng(3)
    pcm = rng.integers(-1000, 1000, size=(48, 6)).astype(np.int32)
    pkt = codec.encode_subframe(pcm, 300, bits32=bits32)
    sync, plen, seq, flags, n, ks, crc = P.HEADER_STRUCT.unpack_from(pkt)
    assert sync == P.SYNC_WORD and plen == len(pkt) <= P.MAX_PACKET_BYTES
    assert seq == 300 & 0xFF and n == 48 and len(ks) == 6
    assert flags == (P.FLAG_RATE_96KHZ | (P.FLAG_BITDEPTH_32 if bits32 else P.FLAG_BITDEPTH_24))
    assert crc == P.crc8(pkt[P.HEADER_SIZE:])
    p48 = codec.encode_subframe(pcm, 1, bits32=bits32, rate96=False)
    assert not P.HEADER_STRUCT.unpack_from(p48)[3] & P.FLAG_RATE_96KHZ


@pytest.mark.parametrize("n", [48, 96])
@pytest.mark.parametrize("bits32", MODES)
def test_edge_values(bits32, n):
    lo, hi = LO[bits32], HI[bits32]
    cases = [np.zeros((n, 6)), np.full((n, 6), lo), np.full((n, 6), hi)]
    alt = np.zeros((n, 6), dtype=np.int64)
    alt[1::2] = lo
    alt[0::2] = hi
    cases.append(alt)
    cases.append(-alt - 1)
    for c in cases:
        pcm = c.astype(np.int32)
        got, r = encode_or_skip_oversize(pcm, 5, bits32)
        if got is not None:
            assert got == r
            assert np.array_equal(codec.decode_subframe(got)[0], pcm)
    # derau penuh -> escape dipicu (Rice k kecil di awal lalu lonjakan)
    rng = np.random.default_rng(99)
    pcm = np.zeros((n, 6), dtype=np.int64)
    pcm[n // 2:] = rng.integers(lo, hi + 1, size=(n - n // 2, 6))
    pcm = pcm.astype(np.int32)
    got, r = encode_or_skip_oversize(pcm, 9, bits32)
    if got is not None:
        assert got == r
        assert np.array_equal(codec.decode_subframe(got)[0], pcm)


def test_short_frames():
    for n in (1, 2, 3, 4, 5):
        for bits32 in MODES:
            pcm = np.random.default_rng(n).integers(-5000, 5000, size=(n, 6)).astype(np.int32)
            got = codec.encode_subframe(pcm, 1, bits32=bits32)
            assert got == ref(pcm, 1, bits32)
            assert np.array_equal(codec.decode_subframe(got)[0], pcm)


def test_oversize_raises():
    rng = np.random.default_rng(5)
    pcm = rng.integers(-(1 << 31), 1 << 31, size=(96, 6), dtype=np.int64).astype(np.int32)
    with pytest.raises(ValueError):
        codec.encode_subframe(pcm, 0, bits32=True)


@pytest.mark.slow
@pytest.mark.parametrize("bits32", MODES)
def test_benchmark(bits32, capsys):
    t = np.arange(48)[:, None]
    amp = (1 << 30) if bits32 else (1 << 22)
    pcm = np.round(amp * np.sin(2 * np.pi * 0.01 * t + np.arange(6)) +
                   np.random.default_rng(0).normal(0, amp / 4000, (48, 6))).astype(np.int32)
    n = 4000
    for _ in range(200):
        codec.encode_subframe(pcm, 0, bits32=bits32)
    t0 = time.perf_counter()
    for i in range(n):
        codec.encode_subframe(pcm, i, bits32=bits32)
    us = (time.perf_counter() - t0) / n * 1e6
    blk = np.concatenate([pcm] * 16)
    t0 = time.perf_counter()
    for _ in range(n // 16):
        codec.encode_block(blk, 0, bits32=bits32)
    usb = (time.perf_counter() - t0) / n * 1e6
    with capsys.disabled():
        print(f"\n[bench {'32' if bits32 else '24'}-bit] single: {us:.1f} us/pkt = {us * 2000 / 1e4:.1f}% core @2000 pkt/s | "
              f"block(16): {usb:.1f} us/pkt = {usb * 2000 / 1e4:.1f}%")
