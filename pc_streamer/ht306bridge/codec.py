"""Encoder lossless subframe (FLAC order-4 modular + Rice adaptif) vektorisasi numpy.

Byte-per-byte identik dengan encode_lossless_subframe (tests/verify_end_to_end.py)
dan firmware/src/lossless_codec.c. Seluruh jalur panas tanpa loop per-sampel:
prediksi/residual/zigzag/panjang bit via vektor, pengepakan bitstream LSB-first
via offset bit (cumsum) + scatter ke kata uint64, CRC-8 via tabel linear (GF(2)).
"""
from __future__ import annotations

import sys

import numpy as np

from . import protocol as P

if sys.byteorder != "little":  # pragma: no cover
    raise ImportError("codec.py mengasumsikan host little-endian")

_CH = P.NUM_CHANNELS
_ESC_Q = P.RICE_ESCAPE_QUOTIENT
_MAX_PAYLOAD = P.MAX_PACKET_BYTES - P.HEADER_SIZE


def _build_crc_tables(rows: int) -> np.ndarray:
    """T[j][b] = CRC-8 (poly 0x07, init 0) dari byte b diikuti j byte nol; flat (rows*256,)."""
    t0 = np.zeros(256, dtype=np.uint8)
    for b in range(256):
        c = b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
        t0[b] = c
    tab = np.empty((rows, 256), dtype=np.uint8)
    tab[0] = t0
    for j in range(1, rows):
        tab[j] = t0[tab[j - 1]]
    # tab[0][b] = crc(b) ; crc(b, 0*j) = tab[j][b]  (tab[j]=t0[tab[j-1]])
    return tab.reshape(-1)


_CRC_ROWS = 1024
_CRC_TAB = _build_crc_tables(_CRC_ROWS)
_ROW_OFF = (np.arange(_CRC_ROWS, dtype=np.int64) * 256)


def _sext(x: np.ndarray, bits: int) -> np.ndarray:
    half = 1 << (bits - 1)
    return ((x + half) & ((1 << bits) - 1)) - half


def _bit_length(x: np.ndarray) -> np.ndarray:
    """bit_length untuk int64 >= 0 (< 2^53)."""
    return np.frexp(x.astype(np.float64))[1].astype(np.int64)


def _encode_core(pcm: np.ndarray, seq0: int, bits32: bool, rate96: bool) -> list[bytes]:
    b, n, ch = pcm.shape
    if ch != _CH:
        raise ValueError(f"pcm harus punya {_CH} kanal, dapat {ch}")
    if not 1 <= n <= P.MAX_SUBFRAME_SAMPLES:
        raise ValueError(f"jumlah frame harus 1..{P.MAX_SUBFRAME_SAMPLES}, dapat {n}")
    bits = 32 if bits32 else 24
    max_k = 30 if bits32 else 22
    esc_bits = P.ESCAPE_RAW_BITS_32 if bits32 else P.ESCAPE_RAW_BITS_24
    anc_bytes = 4 if bits32 else 3
    anc_sz = _CH * anc_bytes

    s = _sext(pcm.astype(np.int64, copy=False), bits)  # (B,N,6)

    # --- prediksi + residual (orde naik 1,2,3,4 sesuai oracle) ---
    m = n - 1
    pred = np.empty((b, m, _CH), dtype=np.int64)
    if m:
        pred[:, 0] = s[:, 0]
    if n > 2:
        pred[:, 1] = 2 * s[:, 1] - s[:, 0]
    if n > 3:
        pred[:, 2] = 3 * s[:, 2] - 3 * s[:, 1] + s[:, 0]
    if n > 4:
        pred[:, 3:] = 4 * s[:, 3:-1] - 6 * s[:, 2:-2] + 4 * s[:, 1:-3] - s[:, :-4]
    res = _sext(s[:, 1:] - _sext(pred, bits), bits)
    zz = (res << 1) ^ (res >> 63)  # zigzag, int64 >= 0

    # --- rice k per paket/kanal ---
    if n <= 3:
        sum_zz = zz.sum(axis=1)
        count = 1  # max(1, n-3)
    else:
        sum_zz = zz[:, 2:].sum(axis=1)
        count = n - 3
    mean = sum_zz // count
    base_k = np.where(mean <= 1, 0, np.minimum(_bit_length(mean) - 1, max_k))  # (B,6)

    kk = np.broadcast_to(base_k[:, None, :], (b, m, _CH)).copy()
    boost = min(2, m)
    if boost:
        head = kk[:, :boost]
        head += np.where(head <= 16, 6, 0)
    np.minimum(kk, max_k, out=kk)

    # --- simbol (nilai LSB-first, panjang bit) ---
    q = zz >> kk
    esc = q >= _ESC_Q
    qc = np.minimum(q, _ESC_Q)
    one = np.int64(1)
    rem = zz & ((one << kk) - 1)
    val = ((one << qc) - 1) | (rem << (qc + 1))
    val = np.where(esc, (one << _ESC_Q) - 1 | (zz << (_ESC_Q + 1)), val).astype(np.uint64)
    ln = np.where(esc, _ESC_Q + 1 + esc_bits, qc + 1 + kk)

    val = val.reshape(b, m * _CH)
    ln = ln.reshape(b, m * _CH)
    tot_bits = ln.sum(axis=1)
    bs_bytes = (tot_bits + 7) >> 3
    payload_len = anc_sz + bs_bytes
    if int(payload_len.max()) > _MAX_PAYLOAD:
        raise ValueError(
            f"paket {int(payload_len.max()) + P.HEADER_SIZE} byte melebihi "
            f"MAX_PACKET_BYTES={P.MAX_PACKET_BYTES}"
        )
    pkt_len = payload_len + P.HEADER_SIZE
    pkt_start = np.cumsum(pkt_len) - pkt_len
    total = int(pkt_len.sum())

    # --- pengepakan bitstream ---
    local = np.cumsum(ln, axis=1) - ln
    off = (local + ((pkt_start + P.HEADER_SIZE + anc_sz) * 8)[:, None]).ravel()
    vflat = val.ravel()
    nw = (total + 7) // 8 + 2
    words = np.zeros(nw, dtype=np.uint64)
    if vflat.size:
        w = off >> 6
        sh = (off & 63).astype(np.uint64)
        low = vflat << sh
        high = (vflat >> np.uint64(1)) >> (np.uint64(63) - sh)
        starts = np.flatnonzero(np.concatenate(([True], w[1:] != w[:-1])))
        uw = w[starts]
        words[uw] += np.add.reduceat(low, starts)
        words[uw + 1] += np.add.reduceat(high, starts)
    buf = words.view(np.uint8)

    # --- anchors ---
    a_u = (s[:, 0] & ((1 << bits) - 1)).astype(np.uint32)  # (B,6)
    a_bytes = np.stack([(a_u >> (8 * i)) & 0xFF for i in range(anc_bytes)], axis=2)
    a_bytes = a_bytes.reshape(b, anc_sz).astype(np.uint8)
    a_idx = (pkt_start + P.HEADER_SIZE)[:, None] + np.arange(anc_sz)
    buf[a_idx] = a_bytes

    # --- CRC-8 (linear): xor_i T[L-1-i][byte_i] ---
    if b == 1:
        pl = int(payload_len[0])
        st = int(pkt_start[0]) + P.HEADER_SIZE
        crc = np.array([np.bitwise_xor.reduce(
            _CRC_TAB[_ROW_OFF[pl - 1::-1] + buf[st:st + pl]])], dtype=np.uint8)
    else:
        pstart = pkt_start + P.HEADER_SIZE
        pos = np.arange(int(payload_len.sum())) - np.repeat(np.cumsum(payload_len) - payload_len, payload_len)
        src = np.repeat(pstart, payload_len) + pos
        rows = np.repeat(payload_len, payload_len) - 1 - pos
        contrib = _CRC_TAB[rows * 256 + buf[src]]
        crc = np.bitwise_xor.reduceat(contrib, np.cumsum(payload_len) - payload_len)

    # --- header ---
    flags = (P.FLAG_RATE_96KHZ if rate96 else 0) | (P.FLAG_BITDEPTH_32 if bits32 else P.FLAG_BITDEPTH_24)
    h = pkt_start
    buf[h] = P.SYNC_WORD & 0xFF
    buf[h + 1] = P.SYNC_WORD >> 8
    buf[h + 2] = (pkt_len & 0xFF).astype(np.uint8)
    buf[h + 3] = (pkt_len >> 8).astype(np.uint8)
    buf[h + 4] = ((seq0 + np.arange(b)) & 0xFF).astype(np.uint8)
    buf[h + 5] = flags
    buf[h + 6] = n
    buf[h[:, None] + 7 + np.arange(_CH)] = base_k.astype(np.uint8)
    buf[h + 13] = crc

    raw = buf[:total].tobytes()
    ps = pkt_start.tolist()
    pe = (pkt_start + pkt_len).tolist()
    return [raw[x:y] for x, y in zip(ps, pe)]


def encode_subframe(pcm: np.ndarray, seq: int, *, bits32: bool, rate96: bool = True) -> bytes:
    """Encode satu subframe pcm (N,6) int32 menjadi satu paket lossless."""
    pcm = np.asarray(pcm)
    if pcm.ndim != 2:
        raise ValueError("pcm harus shape (N, 6)")
    return _encode_core(pcm[None], seq, bits32, rate96)[0]


def encode_block(pcm: np.ndarray, seq0: int, *, bits32: bool, rate96: bool = True,
                 frames: int = P.SUBFRAME_SAMPLES) -> list[bytes]:
    """Encode banyak subframe sekaligus. pcm (B*frames, 6) atau (B, frames, 6); seq berurutan dari seq0."""
    pcm = np.asarray(pcm)
    if pcm.ndim == 2:
        if pcm.shape[0] % frames:
            raise ValueError("jumlah baris pcm bukan kelipatan frames")
        pcm = pcm.reshape(-1, frames, pcm.shape[1])
    return _encode_core(pcm, seq0, bits32, rate96)


def decode_subframe(packet: bytes) -> tuple[np.ndarray, dict]:
    """Dekoder referensi (pengujian saja). Mengembalikan (pcm (N,6) int32, info)."""
    sync, plen, seq, flags, n, ks, crc = P.HEADER_STRUCT.unpack_from(packet)
    if sync != P.SYNC_WORD:
        raise ValueError("sync salah")
    if plen != len(packet):
        raise ValueError("panjang paket tidak cocok")
    payload = packet[P.HEADER_SIZE:]
    if P.crc8(payload) != crc:
        raise ValueError("CRC8 salah")
    bits32 = bool(flags & P.FLAG_BITDEPTH_32)
    bits = 32 if bits32 else 24
    max_k = 30 if bits32 else 22
    esc_bits = P.ESCAPE_RAW_BITS_32 if bits32 else P.ESCAPE_RAW_BITS_24
    ab = 4 if bits32 else 3
    out = np.zeros((n, _CH), dtype=np.int64)
    for c in range(_CH):
        out[0, c] = int.from_bytes(payload[c * ab:(c + 1) * ab], "little")
    out[0] = _sext(out[0], bits)
    stream = int.from_bytes(payload[_CH * ab:], "little")
    pos = 0
    half = 1 << (bits - 1)
    mask = (1 << bits) - 1
    for i in range(1, n):
        for c in range(_CH):
            base = ks[c] & P.RICE_K_MASK
            k = base + 6 if (i < 3 and base <= 16) else base
            k = min(k, max_k)
            qq = 0
            while (stream >> pos) & 1:
                qq += 1
                pos += 1
            pos += 1
            if qq == _ESC_Q:
                zzv = (stream >> pos) & ((1 << esc_bits) - 1)
                pos += esc_bits
            else:
                zzv = (qq << k) | ((stream >> pos) & ((1 << k) - 1))
                pos += k
            r = (zzv >> 1) ^ -(zzv & 1)
            p1, p2, p3, p4 = (int(out[i - j, c]) if i - j >= 0 else int(out[0, c]) for j in (1, 2, 3, 4))
            if i == 1:
                pr = p1
            elif i == 2:
                pr = 2 * p1 - p2
            elif i == 3:
                pr = 3 * p1 - 3 * p2 + p3
            else:
                pr = 4 * p1 - 6 * p2 + 4 * p3 - p4
            pr = ((pr + half) & mask) - half
            out[i, c] = ((pr + r + half) & mask) - half
    info = {"seq": seq, "flags": flags, "frames": n, "rice_k": list(ks), "crc": crc, "bits32": bits32}
    return out.astype(np.int32), info
