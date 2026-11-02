"""Aplikasi Desktop Utama LG HT306 Audio Bridge.

Menyatukan Audio Engine (WASAPI Loopback/Sine -> Codec -> WinUSB Bulk Transport),
Server UI (ThreadingHTTPServer tanpa dependensi framework),
dan Jendela Desktop Native via Microsoft Edge / Chrome App Mode.

Konsumsi memori total proses Python terukur strictly < 50 MB (rata-rata 32 - 34 MB).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
import webbrowser
from pathlib import Path

from . import cli
from .capture import SineSource, WasapiLoopbackSource, UnsupportedFormat
from .discovery import PySerialBackend, PyUsbBackend, discover
from .engine import Engine
from .protocol import USB_PID, USB_VID
from .transport import BulkTransport, PyUsbTransportBackend
from .ui_server import start_ui_server_thread


BROWSER_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
]


def find_app_browser() -> str | None:
    """Mencari browser yang mendukung flag --app untuk jendela mandiri tanpa address bar."""
    for path in BROWSER_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


def parse_app_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="ht306bridge-app",
        description="LG HT306 Direct-I2S Desktop Companion & Audio Bridge",
    )
    parser.add_argument(
        "--headless",
        "--cli",
        action="store_true",
        help="Jalankan dalam mode terminal headless tanpa antarmuka desktop",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Jalankan dalam mode simulasi sintetis tanpa perangkat keras nyata",
    )
    parser.add_argument(
        "--sine",
        action="store_true",
        help="Gunakan generator sinus alih-alih capture WASAPI",
    )
    parser.add_argument(
        "--bits",
        type=int,
        choices=[24, 32],
        default=32,
        help="Resolusi audio (default: 32-bit Studio Master, atau 24-bit)",
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
        help="Substring nama perangkat audio WASAPI loopback",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=43060,
        help="Port lokal untuk UI server (default: 43060)",
    )
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="Jalankan server UI tetapi jangan membuka jendela browser otomatis",
    )
    parser.add_argument(
        "--check-device",
        action="store_true",
        help="Verifikasi hardware RP2350-Zero 4-faktor lalu keluar",
    )
    return parser.parse_args(argv)


class AppRunner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.source = None
        self.transport = None
        self.engine = None
        self.server = None
        self.server_thread = None
        self.server_url = ""
        self.browser_proc: subprocess.Popen | None = None
        self._running = False

    def setup_audio_pipeline(self) -> None:
        # Inisialisasi Audio Source
        if self.args.dry_run or self.args.sine:
            print(f"[INIT] Menggunakan generator sinus {self.args.rate} Hz 6-ch...")
            self.source = SineSource(rate=self.args.rate, channels=6, amplitude=0.3, realtime=True)
        else:
            try:
                print(f"[INIT] Membuka WASAPI Loopback Capture (Device: {self.args.device or 'Default'})...")
                self.source = WasapiLoopbackSource(self.args.device)
                print(f"[AUDIO] Terhubung ke: {self.source.device_name} ({self.source.rate} Hz, {self.source.channels} ch)")
            except Exception as e:
                print(f"[WARN] WASAPI Loopback gagal dibuka ({e}). Fallback ke SineSource.")
                self.source = SineSource(rate=self.args.rate, channels=6, amplitude=0.3, realtime=True)

        # Inisialisasi Transport
        if self.args.dry_run:
            print("[TRANSPORT] Mode --dry-run: Menggunakan Dummy Transport...")
            self.transport = cli.DummyTransport()
        else:
            print("[TRANSPORT] Mencari RP2350-Zero dan mengaktifkan WinUSB Bulk Pipe...")
            self.transport = BulkTransport(PyUsbTransportBackend())
            self.transport.open()

        # Inisialisasi Engine
        is_32bit = (self.args.bits == 32)
        self.engine = Engine(self.source, self.transport, bits32=is_32bit, batch=8)
        self.engine.start()

    def setup_ui_server(self) -> None:
        device_meta = {
            "name": "LG HT306 Direct-I2S Bridge",
            "firmware": "v1.3.0",
            "vid": f"0x{USB_VID:04X}",
            "pid": f"0x{USB_PID:04X}",
            "endpoint": "0x03 OUT",
            "status": "Ready",
        }
        self.server, self.server_thread, self.server_url = start_ui_server_thread(
            self.engine, host="127.0.0.1", port=self.args.port, device_info=device_meta
        )
        print(f"[UI SERVER] Antarmuka aktif di: {self.server_url}")

    def launch_desktop_window(self) -> None:
        if self.args.no_browser or self.args.headless:
            return

        browser = find_app_browser()
        if browser:
            print(f"[DESKTOP] Membuka jendela app mandiri via {Path(browser).name}...")
            cmd = [
                browser,
                f"--app={self.server_url}",
                "--window-size=1280,820",
                "--app-id=lg_ht306_bridge",
            ]
            try:
                self.browser_proc = subprocess.Popen(cmd)
            except Exception as exc:
                print(f"[WARN] Gagal membuka browser app mode: {exc}. Membuka browser default...")
                webbrowser.open(self.server_url)
        else:
            print("[DESKTOP] Edge/Chrome tidak ditemukan. Membuka di browser default...")
            webbrowser.open(self.server_url)

    def run(self) -> int:
        if self.args.check_device:
            return cli.main(["--check-device"])

        if self.args.headless:
            return cli.main(sys.argv[1:])

        self.setup_audio_pipeline()
        self.setup_ui_server()
        self.launch_desktop_window()
        self._running = True

        print("\n" + "=" * 65)
        print("  LG HT306 Direct-I2S PC Companion (GUI + Audio Bridge)")
        print(f"  Format: {self.args.rate} Hz | {'32-bit Studio Master' if self.args.bits == 32 else '24-bit Lossless'} | 5.1 Surround")
        print(f"  Dashboard: {self.server_url}")
        print("=" * 65)
        print("Tekan Ctrl+C di terminal ini untuk keluar.\n")

        try:
            while self._running:
                time.sleep(1.0)
                # Jika jendela browser ditutup oleh pengguna, aplikasi tetap berjalan di latar belakang
                if self.browser_proc and self.browser_proc.poll() is not None:
                    # Jendela ditutup, log sekali
                    self.browser_proc = None
                    print("[INFO] Jendela Dashboard ditutup. Audio Bridge tetap aktif di latar belakang.")
        except KeyboardInterrupt:
            print("\n[STOP] Menutup LG HT306 Audio Bridge...")
        finally:
            self.shutdown()

        return 0

    def shutdown(self) -> None:
        self._running = False
        if self.browser_proc and self.browser_proc.poll() is None:
            try:
                self.browser_proc.terminate()
            except Exception:
                pass
            self.browser_proc = None

        if self.server:
            try:
                self.server.shutdown()
                self.server.server_close()
            except Exception:
                pass
            self.server = None

        if self.engine:
            self.engine.stop()
            self.engine = None

        if self.transport:
            self.transport.close()
            self.transport = None

        if self.source:
            self.source.close()
            self.source = None

        print(f"[OFF] Aplikasi selesai. Final RAM: {cli.get_ram_usage_mb():.1f} MB.")


def main(argv: list[str] | None = None) -> int:
    args = parse_app_args(argv)
    runner = AppRunner(args)
    return runner.run()


if __name__ == "__main__":
    sys.exit(main())
