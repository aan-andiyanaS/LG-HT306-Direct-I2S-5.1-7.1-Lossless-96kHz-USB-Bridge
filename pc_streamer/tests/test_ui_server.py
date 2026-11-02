"""Unit test untuk ui_server (BridgeHTTPServer dan BridgeRequestHandler).

Memverifikasi endpoint REST API:
- GET /api/status
- GET /api/meters
- GET /api/logs
- POST /api/config
- POST /api/test_sweep
- OPTIONS preflight (CORS)
- Static file serving & fallback HTML
- Siklus hidup server & shutdown bersih tanpa hanging thread.
"""
from __future__ import annotations

import json
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from ht306bridge.capture import SineSource
from ht306bridge.cli import DummyTransport
from ht306bridge.engine import Engine
from ht306bridge.ui_server import (
    CHANNEL_NAMES,
    BridgeHTTPServer,
    create_ui_server,
    start_ui_server_thread,
)


def _http_request(
    url: str,
    method: str = "GET",
    json_data: dict[str, Any] | None = None,
    headers: dict[str, str] | None = None,
) -> tuple[int, dict[str, str], bytes]:
    """Helper untuk melakukan HTTP request murni menggunakan urllib."""
    req_headers = headers.copy() if headers else {}
    body_bytes = None

    if json_data is not None:
        body_bytes = json.dumps(json_data).encode("utf-8")
        if "Content-Type" not in req_headers:
            req_headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=body_bytes, headers=req_headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5.0) as resp:
            resp_headers = {k: v for k, v in resp.headers.items()}
            return resp.status, resp_headers, resp.read()
    except urllib.error.HTTPError as err:
        resp_headers = {k: v for k, v in err.headers.items()}
        return err.code, resp_headers, err.read()


@pytest.fixture
def mock_engine():
    """Fixture membuat Engine dengan SineSource dan DummyTransport."""
    src = SineSource(rate=96000, channels=6, amplitude=0.3, realtime=False)
    tr = DummyTransport()
    engine = Engine(src, tr, bits32=False, batch=4)
    yield engine
    src.close()


@pytest.fixture
def test_server(mock_engine, tmp_path):
    """Fixture menjalankan UI server pada port dinamis (port=0) dengan direktori web sementara."""
    device_info = {
        "product": "LG HT306 5.1 Surround Direct-I2S (RP2350-Zero)",
        "vid": 0x2E8A,
        "pid": 0x4306,
        "com_port": "COM4",
    }
    server, thread, base_url = start_ui_server_thread(
        engine=mock_engine,
        host="127.0.0.1",
        port=0,
        device_info=device_info,
        web_dir=tmp_path,
    )
    yield server, mock_engine, base_url, tmp_path
    server.stop()
    thread.join(timeout=3.0)


def test_get_status_endpoint(test_server):
    server, engine, base_url, _ = test_server

    status, headers, body = _http_request(f"{base_url}/api/status")
    assert status == 200
    assert "application/json" in headers.get("Content-Type", "")
    assert headers.get("Access-Control-Allow-Origin") == "*"

    data = json.loads(body.decode("utf-8"))
    assert "state" in data
    assert "device" in data
    assert "format" in data
    assert "stats" in data
    assert "config" in data
    assert "ram_mb" in data

    # Verifikasi rincian format
    fmt = data["format"]
    assert fmt["rate"] == 96000
    assert fmt["channels"] == 6
    assert fmt["bits"] == 24
    assert "96000 Hz" in fmt["description"]

    # Verifikasi metadata perangkat
    dev = data["device"]
    assert dev["vid"] == 0x2E8A
    assert dev["pid"] == 0x4306
    assert dev["com_port"] == "COM4"

    # Verifikasi konfigurasi awal engine
    cfg = data["config"]
    assert cfg["mode"] == "desktop"
    assert cfg["profile"] == "Movie"
    assert len(cfg["trims_db"]) == 6
    assert len(cfg["mutes"]) == 6
    assert cfg["sweep_active"] is False

    # Verifikasi RAM
    assert isinstance(data["ram_mb"], (int, float))
    assert data["ram_mb"] >= 0.0


def test_get_meters_endpoint(test_server):
    server, engine, base_url, _ = test_server

    # Lakukan 1 step audio agar meter terisi nilai terukur
    engine.step()

    status, headers, body = _http_request(f"{base_url}/api/meters")
    assert status == 200
    assert "application/json" in headers.get("Content-Type", "")

    data = json.loads(body.decode("utf-8"))
    assert data["channels"] == CHANNEL_NAMES
    assert len(data["linear"]) == 6
    assert len(data["dBFS"]) == 6
    assert len(data["peak_hold_db"]) == 6
    assert len(data["meters"]) == 6

    # Cek setiap elemen di meters list
    for item in data["meters"]:
        assert item["channel"] in CHANNEL_NAMES
        assert isinstance(item["linear"], (int, float))
        assert isinstance(item["dBFS"], (int, float))
        assert isinstance(item["peak_hold_db"], (int, float))

    # Cek akses langsung via nama kanal (FL, FR, dll.)
    assert "FL" in data
    assert "FR" in data
    assert "C" in data
    assert "LFE" in data
    assert "SL" in data
    assert "SR" in data
    assert data["FL"]["linear"] >= 0.0


def test_get_logs_endpoint(test_server):
    server, engine, base_url, _ = test_server

    # Tambahkan pesan log khusus
    server.add_log("Pesan pengujian log kustom #1")

    status, headers, body = _http_request(f"{base_url}/api/logs")
    assert status == 200
    assert "application/json" in headers.get("Content-Type", "")

    logs = json.loads(body.decode("utf-8"))
    assert isinstance(logs, list)
    assert len(logs) >= 2  # Log init + log kustom

    # Pastikan pesan log yang ditambahkan ada di dalam daftar
    found = any("Pesan pengujian log kustom #1" in entry for entry in logs)
    assert found, "Pesan log kustom tidak ditemukan dalam riwayat log"


def test_post_config_mode_and_profile(test_server):
    server, engine, base_url, _ = test_server

    # 1. Update mode ke bitperfect
    status, _, body = _http_request(
        f"{base_url}/api/config",
        method="POST",
        json_data={"mode": "bitperfect", "profile": "Direct Master"},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["status"] == "ok"
    assert res["mode"] == "bitperfect"
    assert res["profile"] == "Direct Master"
    assert engine.mode == "bitperfect"
    assert engine.profile == "Direct Master"

    # 2. Kembalikan mode ke desktop
    status, _, body = _http_request(
        f"{base_url}/api/config",
        method="POST",
        json_data={"mode": "desktop"},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["mode"] == "desktop"
    assert engine.mode == "desktop"


def test_post_config_trims_and_mutes(test_server):
    server, engine, base_url, _ = test_server

    new_trims = [2.0, -1.5, 0.5, 4.0, -2.0, -1.0]
    new_mutes = [False, False, True, False, True, False]

    status, _, body = _http_request(
        f"{base_url}/api/config",
        method="POST",
        json_data={"trims_db": new_trims, "mutes": new_mutes},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["status"] == "ok"
    assert res["trims_db"] == new_trims
    assert res["mutes"] == new_mutes

    assert np.allclose(engine.trims_db, new_trims)
    assert np.array_equal(engine.mutes, new_mutes)


def test_post_config_validation(test_server):
    server, engine, base_url, _ = test_server

    # Mode tidak valid
    status, _, body = _http_request(
        f"{base_url}/api/config",
        method="POST",
        json_data={"mode": "ultra_audio"},
    )
    assert status == 400
    data = json.loads(body.decode("utf-8"))
    assert "error" in data

    # Trims bukan list
    status, _, body = _http_request(
        f"{base_url}/api/config",
        method="POST",
        json_data={"trims_db": "bukan_list"},
    )
    assert status == 400


def test_post_test_sweep_start_and_stop(test_server):
    server, engine, base_url, _ = test_server

    # 1. Mulai sweep pada kanal 'C' (Center = indeks 2)
    status, _, body = _http_request(
        f"{base_url}/api/test_sweep",
        method="POST",
        json_data={"action": "start", "channel": "C", "level_db": -12.0},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["status"] == "ok"
    assert res["sweep_active"] is True
    assert res["sweep_channel"] == 2
    assert res["channel_name"] == "C"
    assert res["level_db"] == -12.0

    assert engine.sweep_active is True
    assert engine.sweep_channel == 2
    assert engine.sweep_level_db == -12.0

    # 2. Hentikan sweep
    status, _, body = _http_request(
        f"{base_url}/api/test_sweep",
        method="POST",
        json_data={"action": "stop"},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["status"] == "ok"
    assert res["sweep_active"] is False
    assert engine.sweep_active is False

    # 3. Mulai sweep dengan indeks integer kanal (LFE = 3)
    status, _, body = _http_request(
        f"{base_url}/api/test_sweep",
        method="POST",
        json_data={"action": "start", "channel": 3, "level_db": -24.0},
    )
    assert status == 200
    res = json.loads(body.decode("utf-8"))
    assert res["sweep_active"] is True
    assert res["sweep_channel"] == 3
    assert res["channel_name"] == "LFE"
    assert engine.sweep_channel == 3


def test_post_test_sweep_validation(test_server):
    server, engine, base_url, _ = test_server

    # Action tidak dikenal
    status, _, body = _http_request(
        f"{base_url}/api/test_sweep",
        method="POST",
        json_data={"action": "pause"},
    )
    assert status == 400

    # Kanal tidak valid
    status, _, body = _http_request(
        f"{base_url}/api/test_sweep",
        method="POST",
        json_data={"action": "start", "channel": "SUB_REAR"},
    )
    assert status == 400


def test_cors_options_preflight(test_server):
    server, engine, base_url, _ = test_server

    status, headers, body = _http_request(
        f"{base_url}/api/config",
        method="OPTIONS",
    )
    assert status == 204
    assert headers.get("Access-Control-Allow-Origin") == "*"
    methods = headers.get("Access-Control-Allow-Methods", "")
    assert "GET" in methods
    assert "POST" in methods
    assert "OPTIONS" in methods
    allow_headers = headers.get("Access-Control-Allow-Headers", "")
    assert "Content-Type" in allow_headers


def test_static_fallback_html_when_no_index(test_server):
    server, engine, base_url, tmp_path = test_server

    # Pastikan tidak ada index.html di tmp_path
    assert not (tmp_path / "index.html").exists()

    status, headers, body = _http_request(f"{base_url}/")
    assert status == 200
    assert "text/html" in headers.get("Content-Type", "")
    content = body.decode("utf-8")
    assert "SERVER AKTIF" in content
    assert "HT306 Audio Bridge" in content


def test_static_file_serving(test_server):
    server, engine, base_url, tmp_path = test_server

    # 1. Buat index.html di tmp_path
    custom_html = "<html><body><h1>Custom HT306 UI</h1></body></html>"
    (tmp_path / "index.html").write_text(custom_html, encoding="utf-8")

    status, headers, body = _http_request(f"{base_url}/index.html")
    assert status == 200
    assert "text/html" in headers.get("Content-Type", "")
    assert body.decode("utf-8") == custom_html

    # 2. Buat file CSS statis
    css_content = "body { background: #000; }"
    (tmp_path / "theme.css").write_text(css_content, encoding="utf-8")

    status, headers, body = _http_request(f"{base_url}/theme.css")
    assert status == 200
    assert "text/css" in headers.get("Content-Type", "")
    assert body.decode("utf-8") == css_content

    # 3. File tidak ditemukan -> 404
    status, _, body = _http_request(f"{base_url}/not_found.js")
    assert status == 404


def test_server_clean_lifecycle_and_shutdown():
    src = SineSource(rate=48000, channels=6, amplitude=0.1, realtime=False)
    tr = DummyTransport()
    engine = Engine(src, tr, bits32=False, batch=2)

    server, thread, url = start_ui_server_thread(engine=engine, host="127.0.0.1", port=0)
    assert thread.is_alive()

    # Pastikan server merespons
    status, _, _ = _http_request(f"{url}/api/status")
    assert status == 200

    # Hentikan server
    server.stop()
    thread.join(timeout=3.0)

    # Pastikan thread telah berhenti tanpa hang
    assert not thread.is_alive()
    src.close()


def test_serve_real_web_index_html():
    """Memverifikasi file statis index.html produksi yang disajikan oleh ui_server."""
    src = SineSource(rate=96000, channels=6, amplitude=0.1, realtime=False)
    tr = DummyTransport()
    engine = Engine(src, tr, bits32=False, batch=2)

    # Server berjalan dengan default web_dir (pc_streamer/ht306bridge/web)
    server, thread, base_url = start_ui_server_thread(engine=engine, host="127.0.0.1", port=0)

    try:
        # 1. Request root /
        status, headers, body = _http_request(f"{base_url}/")
        assert status == 200
        assert "text/html" in headers.get("Content-Type", "")
        content = body.decode("utf-8")

        assert "<title>LG HT306 Audio Bridge</title>" in content
        assert 'id="tab-dashboard"' in content
        assert 'id="tab-mixer"' in content
        assert 'id="tab-routing"' in content
        assert 'id="tab-profiles"' in content
        assert 'id="tab-test"' in content
        assert 'id="tab-log"' in content
        assert 'id="tab-settings"' in content

        # Verifikasi navigasi data-tab
        assert 'data-tab="dashboard"' in content
        assert 'data-tab="mixer"' in content
        assert 'data-tab="routing"' in content
        assert 'data-tab="profiles"' in content
        assert 'data-tab="test"' in content
        assert 'data-tab="log"' in content
        assert 'data-tab="settings"' in content

        # 2. Request /index.html eksplisit
        status2, headers2, body2 = _http_request(f"{base_url}/index.html")
        assert status2 == 200
        assert "text/html" in headers2.get("Content-Type", "")
        assert body2 == body

    finally:
        server.stop()
        thread.join(timeout=3.0)
        src.close()

