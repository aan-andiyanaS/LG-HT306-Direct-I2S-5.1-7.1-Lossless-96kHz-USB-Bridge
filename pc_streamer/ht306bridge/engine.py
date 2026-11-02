"""Engine: AudioSource -> konversi int -> codec (batch) -> BulkTransport.

Jalur panas tanpa logging/alokasi berlebih. Tidak ada mixing/upmix/resample:
kanal sumber dipetakan langsung ke slot 5.1 [FL, FR, C, LFE, SL, SR]; kanal yang tidak ada = nol.
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass

import numpy as np

from . import codec
from .protocol import NUM_CHANNELS, SUBFRAME_SAMPLES

# 8 subframe x 0,5 ms = 4 ms latensi batch @96 kHz; encode ~9% satu core (terukur, Slice A)
DEFAULT_BATCH = 8


@dataclass
class EngineStats:
    packets: int = 0
    frames: int = 0
    dropped: int = 0
    clipped: int = 0
    errors: int = 0
    last_error: str = ""


def to_pcm(block: np.ndarray, bits32: bool, stats: EngineStats | None = None) -> np.ndarray:
    """float32 [-1,1] (N,6) -> int32. 24-bit: +-2^23; 32-bit: +-2^31. Clip dihitung."""
    scale = 2147483648.0 if bits32 else 8388608.0
    hi = 2147483647 if bits32 else 8388607
    lo = -hi - 1
    v = np.rint(block.astype(np.float64) * scale)
    if stats is not None:
        stats.clipped += int(np.count_nonzero((v > hi) | (v < lo)))
    return np.clip(v, lo, hi).astype(np.int32)


def map_channels(block: np.ndarray) -> np.ndarray:
    """(N,c) -> (N,6). Direct 1:1, kanal ekstra (7.1) dibuang, kurang = senyap. Tanpa mixing."""
    n, c = block.shape
    if c == NUM_CHANNELS:
        return block
    out = np.zeros((n, NUM_CHANNELS), dtype=block.dtype)
    k = min(c, NUM_CHANNELS)
    out[:, :k] = block[:, :k]
    return out


class Engine:
    def __init__(self, source, transport, *, bits32: bool = False, batch: int = DEFAULT_BATCH,
                 frames: int = SUBFRAME_SAMPLES):
        self.source, self.transport = source, transport
        self.bits32, self.batch, self.frames = bits32, batch, frames
        self.stats = EngineStats()
        self._seq = 0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

        # Level metering (0.0 .. 1.0 linear dan -96.0 .. 0.0 dBFS)
        self.levels = np.zeros(NUM_CHANNELS, dtype=np.float32)
        self.peak_db = np.full(NUM_CHANNELS, -96.0, dtype=np.float32)
        self.peak_hold_db = np.full(NUM_CHANNELS, -96.0, dtype=np.float32)

        # DSP & Operating Mode
        self.mode: str = "desktop"  # "desktop" | "bitperfect"
        self.profile: str = "Movie"  # "Movie" | "Music" | "Game"
        self.trims_db = np.zeros(NUM_CHANNELS, dtype=np.float32)
        self.mutes = np.zeros(NUM_CHANNELS, dtype=bool)

        # 5.1 Acoustic Sweep Generator
        self.sweep_active: bool = False
        self.sweep_channel: int = -1  # 0..5 (FL, FR, C, LFE, SL, SR)
        self.sweep_level_db: float = -18.0
        self._sweep_phase: float = 0.0

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="ht306-engine", daemon=True)
        self._thread.start()

    def stop(self, timeout: float = 5.0) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout)

    def set_sweep(self, active: bool, channel: int = -1, level_db: float = -18.0) -> None:
        """Mengaktifkan generator nada akustik 5.1 untuk tes speaker."""
        self.sweep_active = active
        self.sweep_channel = channel
        self.sweep_level_db = level_db
        self._sweep_phase = 0.0

    def _generate_sweep_pcm(self, n: int) -> np.ndarray:
        """Menghasilkan blok PCM nada sinus khusus untuk kanal sweep yang aktif."""
        pcm = np.zeros((n, NUM_CHANNELS), dtype=np.int32)
        if self.sweep_channel < 0 or self.sweep_channel >= NUM_CHANNELS:
            return pcm

        rate = getattr(self.source, "rate", 96000)
        # Kanal 3 adalah LFE (Subwoofer) -> 60 Hz. Kanal lain 1000 Hz.
        freq = 60.0 if self.sweep_channel == 3 else 1000.0
        amp = 10.0 ** (self.sweep_level_db / 20.0)
        t = np.arange(n, dtype=np.float64) / rate
        sine = np.sin(2.0 * np.pi * freq * t + self._sweep_phase)
        self._sweep_phase = (self._sweep_phase + 2.0 * np.pi * freq * (n / rate)) % (2.0 * np.pi)

        scale = 2147483647.0 if self.bits32 else 8388607.0
        tone = np.rint(sine * amp * scale).astype(np.int32)
        pcm[:, self.sweep_channel] = tone
        return pcm

    def step(self) -> int:
        """Satu batch: baca -> DSP/Meter -> encode -> kirim. Mengembalikan jumlah paket terkirim."""
        n = self.batch * self.frames

        if self.sweep_active:
            pcm = self._generate_sweep_pcm(n)
        else:
            raw = self.source.read(n)
            pcm = to_pcm(map_channels(raw), self.bits32, self.stats)
            if len(pcm) < n:  # sumber memberi kurang: buang sisa tidak penuh
                return 0

            # DSP Processing (hanya aktif di mode desktop, bypass total di bit-perfect)
            if self.mode == "desktop":
                has_trims = np.any(self.trims_db != 0.0)
                has_mutes = np.any(self.mutes)
                if has_trims or has_mutes:
                    gains = np.where(self.mutes, 0.0, 10.0 ** (self.trims_db / 20.0))
                    pcm = np.rint(pcm.astype(np.float64) * gains).astype(np.int32)

        # Hitung Peak Level Metering (Normalized & dBFS)
        scale = 2147483648.0 if self.bits32 else 8388608.0
        cur_peaks = np.max(np.abs(pcm), axis=0) / scale
        self.levels = np.maximum(cur_peaks, self.levels * 0.85)
        safe_levels = np.maximum(self.levels, 1e-5)
        self.peak_db = np.clip(20.0 * np.log10(safe_levels), -96.0, 0.0)
        self.peak_hold_db = np.maximum(self.peak_db, self.peak_hold_db - 0.4)

        # Encode ke paket USB Lossless
        rate96 = getattr(self.source, "rate", 96000) == 96000
        packets = codec.encode_block(pcm, self._seq, bits32=self.bits32,
                                     rate96=rate96, frames=self.frames)
        self._seq = (self._seq + len(packets)) & 0xFF
        for p in packets:
            if not self.transport.write(p):
                self.stats.dropped += 1
        self.stats.packets += len(packets)
        self.stats.frames += n
        return len(packets)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self.step()
            except Exception as exc:  # satu error tidak boleh membunuh stream
                self.stats.errors += 1
                self.stats.last_error = f"{type(exc).__name__}: {exc}"
                time.sleep(0.05)
