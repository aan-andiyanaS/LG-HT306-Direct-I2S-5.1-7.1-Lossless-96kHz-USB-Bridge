"""Sumber audio: WASAPI loopback (nyata) dan sintetis (dry-run/tes).

Kontrak AudioSource: `read(frames)` memblok sampai `frames` frame siap, mengembalikan
float32 (frames, channels) dalam [-1, 1]. `rate` dan `channels` tetap selama hidup objek.
"""
from __future__ import annotations

from typing import Protocol

import numpy as np

SUPPORTED_RATES = (48000, 96000)


class AudioSource(Protocol):
    rate: int
    channels: int

    def read(self, frames: int) -> np.ndarray: ...
    def close(self) -> None: ...


class UnsupportedFormat(Exception):
    """Format Windows tidak didukung firmware (tidak pernah di-resample diam-diam)."""

    code = "E-AUD-FORMAT"


class SineSource:
    """Sinus per kanal berfrekuensi beda, untuk dry-run & tes. Real-time pacing opsional."""

    def __init__(self, rate: int = 96000, channels: int = 6, amplitude: float = 0.5,
                 realtime: bool = False):
        if rate not in SUPPORTED_RATES:
            raise UnsupportedFormat(f"rate {rate} tidak didukung")
        self.rate, self.channels, self.realtime = rate, channels, realtime
        self._amp = amplitude
        self._n = 0
        self._freqs = 220.0 * (1.0 + np.arange(channels))
        self._t0 = None

    def read(self, frames: int) -> np.ndarray:
        t = (self._n + np.arange(frames)) / self.rate
        out = (self._amp * np.sin(2 * np.pi * np.outer(t, self._freqs))).astype(np.float32)
        self._n += frames
        if self.realtime:
            import time
            if self._t0 is None:
                self._t0 = time.perf_counter()
            wait = self._t0 + self._n / self.rate - time.perf_counter()
            if wait > 0:
                time.sleep(wait)
        return out

    def close(self) -> None:
        pass


class WasapiLoopbackSource:
    """Menangkap mix keluaran perangkat default via pyaudiowpatch (mode shared)."""

    def __init__(self, device_name: str | None = None):
        import pyaudiowpatch as pa

        self._pa = pa.PyAudio()
        info = self._pick(device_name)
        self.rate = int(info["defaultSampleRate"])
        self.channels = int(info["maxInputChannels"])
        self.device_name = info["name"]
        if self.rate not in SUPPORTED_RATES:
            self._pa.terminate()
            raise UnsupportedFormat(
                f"Windows memakai {self.rate} Hz; set perangkat ke 48000 atau 96000 Hz")
        if self.channels not in (2, 6, 8):
            self._pa.terminate()
            raise UnsupportedFormat(f"{self.channels} kanal tidak didukung (2/6/8)")
        self._stream = self._pa.open(
            format=pa.paFloat32, channels=self.channels, rate=self.rate, input=True,
            input_device_index=info["index"], frames_per_buffer=480)

    def _pick(self, name):
        pa = self._pa
        default = pa.get_default_wasapi_loopback()
        if name is None:
            return default
        for lb in pa.get_loopback_device_info_generator():
            if name.lower() in lb["name"].lower():
                return lb
        raise UnsupportedFormat(f"perangkat '{name}' tidak ditemukan")

    def read(self, frames: int) -> np.ndarray:
        raw = self._stream.read(frames, exception_on_overflow=False)
        return np.frombuffer(raw, dtype=np.float32).reshape(-1, self.channels)

    def close(self) -> None:
        try:
            self._stream.stop_stream()
            self._stream.close()
        finally:
            self._pa.terminate()


def list_loopback_devices() -> list[dict]:
    import pyaudiowpatch as pa

    p = pa.PyAudio()
    try:
        return [
            {"name": d["name"], "rate": int(d["defaultSampleRate"]),
             "channels": int(d["maxInputChannels"])}
            for d in p.get_loopback_device_info_generator()
        ]
    finally:
        p.terminate()
