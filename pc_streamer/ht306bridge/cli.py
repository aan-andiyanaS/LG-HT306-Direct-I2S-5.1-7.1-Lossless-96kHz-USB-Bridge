"""CLI Antarmuka Pengguna untuk HT306 Audio Bridge.

Dapat dijalankan secara headless untuk background service, dry-run, benchmark,
maupun deteksi perangkat tanpa GUI.
"""
from __future__ import annotations

import argparse
import sys
import time
import os
import psutil

from .protocol import (
    USB_VID,
    USB_PID,
    FLAG_RATE_96KHZ,
    FLAG_BITDEPTH_24,
    FLAG_BITDEPTH_32,
)
from .discovery import PyUsbBackend, PySerialBackend, discover
from .transport import BulkTransport, PyUsbTransportBackend, State
from .capture import SineSource, WasapiLoopbackSource, UnsupportedFormat, list_loopback_devices
from .engine import Engine


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="ht306bridge",
        description="LG HT306 Direct-I2S PC Audio Bridge via RP2350-Zero",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Jalankan engine dengan sinyal sintetis (sine) tanpa membutuhkan hardware nyata",
    )
    parser.add_argument(
        "--sine",
        action="store_true",
        help="Gunakan generator sinus alih-alih menangkap WASAPI loopback nyata",
    )
    parser.add_argument(
        "--bits",
        type=int,
        choices=[24, 32],
        default=24,
        help="Kedalaman bit audio (default: 24, atau 32 untuk True Studio Master)",
    )
    parser.add_argument(
        "--rate",
        type=int,
        choices=[48000, 96000],
        default=96000,
        help="Frekuensi sampling (default: 96000 Hz, atau 48000 Hz)",
    )
    parser.add_argument(
        "--device",
        type=str,
        default=None,
        help="Nama substring perangkat audio Windows untuk loopback capture",
    )
    parser.add_argument(
        "--list-devices",
        action="store_true",
        help="Daftar perangkat loopback audio Windows yang tersedia",
    )
    parser.add_argument(
        "--check-device",
        action="store_true",
        help="Periksa keberadaan hardware RP2350-Zero via 4-Factor Authentication lalu keluar",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.0,
        help="Durasi streaming dalam detik (0 = berjalan terus sampai Ctrl+C)",
    )
    return parser.parse_args(argv)


class DummyTransport:
    """Mock transport untuk mode --dry-run tanpa hardware."""
    def __init__(self):
        self._state = State.STREAMING
        self.bytes_written = 0
        self.packets_written = 0

    def state(self) -> State:
        return self._state

    def open(self) -> None:
        pass

    def write(self, packet: bytes) -> bool:
        self.bytes_written += len(packet)
        self.packets_written += 1
        return True

    def close(self, timeout: float = 1.0) -> None:
        pass


def get_ram_usage_mb() -> float:
    process = psutil.Process(os.getpid())
    return process.memory_info().rss / (1024.0 * 1024.0)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if args.list_devices:
        try:
            devs = list_loopback_devices()
            print("Perangkat WASAPI Loopback yang tersedia:")
            for d in devs:
                print(f"  - {d['name']} ({d['rate']} Hz, {d['channels']} ch)")
            return 0
        except Exception as e:
            print(f"[ERROR] Gagal memindai perangkat audio: {e}")
            return 1

    if args.check_device:
        print("[CHECK] Memindai hardware RP2350-Zero LG HT306...")
        res = discover(PyUsbBackend(), PySerialBackend(), handshake_timeout=1.5)
        if res.found and res.device:
            print(f"[OK] Hardware Ditemukan: {res.device.product}")
            print(f"     VID/PID: 0x{res.device.vid:04X}:0x{res.device.pid:04X}")
            print(f"     COM Port: {res.com_port}")
            return 0
        else:
            print("[GAGAL] Hardware tidak terdeteksi atau ditolak:")
            for r in res.rejected:
                print(f"  - {r.reason_code}: {r.detail}")
            return 2

    # Inisialisasi Audio Source
    source = None
    if args.dry_run or args.sine:
        print(f"[INIT] Menggunakan generator sinus {args.rate} Hz 6-ch...")
        source = SineSource(rate=args.rate, channels=6, amplitude=0.3, realtime=True)
    else:
        try:
            print(f"[INIT] Membuka WASAPI Loopback Capture (Device: {args.device or 'Default'})...")
            source = WasapiLoopbackSource(args.device)
            print(f"[AUDIO] Terhubung ke: {source.device_name} ({source.rate} Hz, {source.channels} ch)")
        except UnsupportedFormat as e:
            print(f"[FORMAT ERROR] {e}")
            return 3
        except Exception as e:
            print(f"[CAPTURE ERROR] Gagal membuka stream WASAPI: {e}")
            print("                Coba gunakan flag --sine atau --dry-run untuk uji mandiri.")
            return 3

    # Inisialisasi Transport
    transport = None
    if args.dry_run:
        print("[TRANSPORT] Mode --dry-run: Menggunakan Dummy Transport...")
        transport = DummyTransport()
    else:
        print("[TRANSPORT] Mencari RP2350-Zero dan membuka WinUSB Bulk Pipe...")
        transport = BulkTransport(PyUsbTransportBackend())
        transport.open()

    # Jalankan Engine
    is_32bit = (args.bits == 32)
    engine = Engine(source, transport, bits32=is_32bit, batch=8)
    engine.start()

    print("\n" + "=" * 65)
    print("  LG HT306 Direct-I2S PC Audio Bridge (Headless Streamer)")
    print(f"  Format: {args.rate} Hz | {'32-bit Studio Master' if is_32bit else '24-bit Lossless'} | 5.1 Surround")
    print("=" * 65)
    print("Tekan Ctrl+C untuk berhenti.\n")

    t0 = time.time()
    try:
        while True:
            time.sleep(1.0)
            elapsed = time.time() - t0
            ram_mb = get_ram_usage_mb()
            raw_st = transport.state
            st_obj = raw_st() if callable(raw_st) else raw_st
            tr_st = st_obj.name if hasattr(st_obj, "name") else str(st_obj)
            st = engine.stats
            throughput_kb = (st.frames * (6 * (4 if is_32bit else 3))) / (1024.0 * max(1.0, elapsed))
            
            print(
                f"\r[STATUS] Uptime: {int(elapsed)}s | "
                f"RAM: {ram_mb:.1f} MB | "
                f"Link: {tr_st} | "
                f"Paket: {st.packets} | "
                f"Drop: {st.dropped} | "
                f"Raw: {throughput_kb:.1f} KB/s",
                end="",
                flush=True
            )

            if args.duration > 0 and elapsed >= args.duration:
                print(f"\n[SELESAI] Durasi pengujian ({args.duration}s) tercapai.")
                break
    except KeyboardInterrupt:
        print("\n[STOP] Menghentikan streaming...")
    finally:
        engine.stop()
        transport.close()
        source.close()
        print(f"[OFF] Engine berhenti dengan aman. Final RAM: {get_ram_usage_mb():.1f} MB.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
