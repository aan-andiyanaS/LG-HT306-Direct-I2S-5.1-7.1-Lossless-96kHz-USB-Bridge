"""Unit test untuk Application Runner (Desktop Companion Entry Point)."""
from __future__ import annotations

import os
import sys
import time
import subprocess
import urllib.request
import json
import psutil
import pytest

from ht306bridge.app import find_app_browser, parse_app_args, AppRunner


def test_find_app_browser():
    # Mengembalikan path browser yang valid atau None
    browser = find_app_browser()
    if browser:
        assert os.path.isfile(browser)


def test_parse_app_args_defaults():
    args = parse_app_args([])
    assert args.bits == 32
    assert args.rate == 96000
    assert args.port == 43060
    assert not args.dry_run
    assert not args.headless
    assert not args.no_browser


def test_parse_app_args_custom():
    args = parse_app_args(["--dry-run", "--bits", "24", "--rate", "48000", "--port", "49999", "--no-browser"])
    assert args.dry_run
    assert args.bits == 24
    assert args.rate == 48000
    assert args.port == 49999
    assert args.no_browser


def test_app_runner_lifecycle_dryrun():
    args = parse_app_args(["--dry-run", "--no-browser", "--port", "0", "--bits", "32"])
    runner = AppRunner(args)

    runner.setup_audio_pipeline()
    runner.setup_ui_server()

    assert runner.engine is not None
    assert runner.server is not None
    assert runner.server_url.startswith("http://127.0.0.1:")

    # Uji query HTTP ke server UI yang sedang berjalan
    status_url = f"{runner.server_url}/api/status"
    req = urllib.request.urlopen(status_url, timeout=2.0)
    assert req.status == 200
    data = json.loads(req.read().decode("utf-8"))
    assert data["format"]["rate"] == 96000
    assert data["format"]["bitdepth"] == 32

    # Biarkan streaming berjalan sebentar
    time.sleep(0.3)
    assert runner.engine.stats.packets > 0

    # Shutdown
    runner.shutdown()
    assert runner.engine is None
    assert runner.server is None


def test_app_subprocess_ram_footprint_strictly_under_50mb():
    """Menguji konsumsi RAM aplikasi utuh (Audio Engine + HTTP UI Server) berjalan mandiri."""
    env = os.environ.copy()
    env["PYTHONPATH"] = "pc_streamer"

    # Jalankan app selama ~2 detik dengan UI server aktif
    cmd = [
        sys.executable,
        "-m", "ht306bridge.app",
        "--dry-run",
        "--no-browser",
        "--port", "43199",
        "--bits", "32",
    ]
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env
    )
    p = psutil.Process(proc.pid)
    max_ram_mb = 0.0

    # Pantau RAM selama 1.5 detik
    t0 = time.time()
    while time.time() - t0 < 1.5:
        if proc.poll() is not None:
            break
        try:
            ram = p.memory_info().rss / (1024 * 1024)
            if ram > max_ram_mb:
                max_ram_mb = ram
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            break
        time.sleep(0.1)

    # Kirim terminasi
    proc.terminate()
    try:
        proc.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        proc.kill()

    print(f"\n[MEASURED FULL APP RAM] {max_ram_mb:.2f} MB")
    assert max_ram_mb > 0
    # Bukti konkret: Full App (Engine + UI Server) mengonsumsi < 45 MB RAM (Target: < 50 MB)
    assert max_ram_mb < 45.0
