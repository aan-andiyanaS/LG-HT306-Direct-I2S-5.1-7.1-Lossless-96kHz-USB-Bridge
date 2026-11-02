"""UI Server: REST API dan Static Web Server untuk HT306 Audio Bridge.

Menggunakan Python Standard Library murni (http.server.ThreadingHTTPServer)
tanpa framework eksternal untuk menjaga RAM tetap minimal (< 50 MB total process).
Menyediakan live telemetry status, 6-channel level meters, update konfigurasi DSP,
5.1 acoustic sweep control, serta penyajian web UI statis / fallback HTML.
"""
from __future__ import annotations

import json
import mimetypes
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

import numpy as np

from .protocol import NUM_CHANNELS

CHANNEL_NAMES: list[str] = ["FL", "FR", "C", "LFE", "SL", "SR"]
CHANNEL_MAP: dict[str, int] = {name: idx for idx, name in enumerate(CHANNEL_NAMES)}

FALLBACK_HTML: str = """<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LG HT306 Audio Bridge</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: #131313;
            color: #e5e2e1;
            display: flex;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            margin: 0;
            padding: 1rem;
            box-sizing: border-box;
        }
        .card {
            background: #1c1c1e;
            border: 1px solid #38383a;
            border-radius: 12px;
            padding: 2rem;
            max-width: 540px;
            width: 100%;
            text-align: center;
            box-shadow: 0 8px 30px rgba(0, 0, 0, 0.6);
        }
        .badge {
            display: inline-block;
            background: #262628;
            color: #2fbf71;
            padding: 0.35rem 0.85rem;
            border-radius: 9999px;
            font-size: 0.85rem;
            font-weight: 600;
            letter-spacing: 0.05em;
            margin-bottom: 1.25rem;
            border: 1px solid #2fbf7133;
        }
        h1 {
            color: #ffb2b8;
            font-size: 1.6rem;
            margin: 0 0 0.75rem 0;
            font-weight: 700;
        }
        p {
            color: #b8b8bc;
            font-size: 0.95rem;
            line-height: 1.6;
            margin: 0 0 1.25rem 0;
        }
        .api-box {
            text-align: left;
            background: #131313;
            border: 1px solid #2a2a2c;
            border-radius: 8px;
            padding: 1rem 1.25rem;
            font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
            font-size: 0.85rem;
        }
        .api-box strong {
            color: #ffffff;
            display: block;
            margin-bottom: 0.5rem;
        }
        .api-box ul {
            margin: 0;
            padding-left: 1.25rem;
            color: #a9c7ff;
        }
        .api-box li {
            margin: 0.3rem 0;
        }
    </style>
</head>
<body>
    <div class="card">
        <span class="badge">&#9679; SERVER AKTIF</span>
        <h1>LG HT306 Audio Bridge</h1>
        <p>HT306 Bridge server aktif dan siap melayani permintaan PC Audio Streamer.</p>
        <div class="api-box">
            <strong>Endpoint REST API Aktif:</strong>
            <ul>
                <li><code>GET  /api/status</code> &mdash; Status live telemetry & format</li>
                <li><code>GET  /api/meters</code> &mdash; Peak level meter 6-kanal (dBFS & linear)</li>
                <li><code>GET  /api/logs</code> &mdash; Riwayat log telemetri server</li>
                <li><code>POST /api/config</code> &mdash; Pengaturan mode & mixer trims</li>
                <li><code>POST /api/test_sweep</code> &mdash; Nada generator uji 5.1</li>
            </ul>
        </div>
    </div>
</body>
</html>
"""


def get_process_ram_mb() -> float:
    """Mengembalikan estimasi memori RSS proses dalam megabyte (MB)."""
    try:
        import psutil
        process = psutil.Process(os.getpid())
        return round(process.memory_info().rss / (1024.0 * 1024.0), 2)
    except Exception:
        return 0.0


class BridgeHTTPServer(ThreadingHTTPServer):
    """Threading HTTP Server untuk UI HT306 Bridge.
    
    Menyimpan referensi ke instance audio engine, metadata perangkat, direktori file web,
    serta ring buffer log pesan terakhir.
    """
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        server_address: tuple[str, int],
        RequestHandlerClass: type[BaseHTTPRequestHandler],
        engine: Any = None,
        device_info: dict[str, Any] | None = None,
        web_dir: str | Path | None = None,
    ) -> None:
        super().__init__(server_address, RequestHandlerClass)
        self.engine = engine
        self.device_info = device_info or {}
        if web_dir is not None:
            self.web_dir = Path(web_dir)
        else:
            self.web_dir = Path(__file__).resolve().parent / "web"
        self._logs: list[str] = []
        self._logs_lock = threading.Lock()
        self.add_log("HT306 Bridge HTTP server siap menerima koneksi")

    @property
    def logs(self) -> list[str]:
        """Mengambil salinan daftar pesan log terkini secara thread-safe."""
        with self._logs_lock:
            return list(self._logs)

    def add_log(self, message: str) -> None:
        """Menambahkan pesan log ke buffer ring."""
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        entry = f"[{timestamp}] {message}"
        with self._logs_lock:
            self._logs.append(entry)
            if len(self._logs) > 500:
                self._logs = self._logs[-500:]

    def stop(self) -> None:
        """Menghentikan event loop server dan menutup socket dengan bersih."""
        self.shutdown()
        self.server_close()


class BridgeRequestHandler(BaseHTTPRequestHandler):
    """Request handler untuk REST API dan penyajian web UI statis."""

    server: BridgeHTTPServer

    @property
    def engine(self) -> Any:
        return getattr(self.server, "engine", None)

    @property
    def device_info(self) -> dict[str, Any]:
        return getattr(self.server, "device_info", {})

    def log_message(self, format: str, *args: Any) -> None:
        """Minimalisir log HTTP pada stdout; hanya tampilkan jika status code error (>= 400)."""
        if args and len(args) >= 2:
            code_str = str(args[1])
            if code_str.isdigit() and int(code_str) >= 400:
                super().log_message(format, *args)
                return
        if args and str(args[0]).isdigit() and int(str(args[0])) >= 400:
            super().log_message(format, *args)

    def log_error(self, format: str, *args: Any) -> None:
        """Tangani error logging dan catat ke log buffer server."""
        super().log_error(format, *args)
        if hasattr(self.server, "add_log"):
            msg = format % args if args else format
            self.server.add_log(f"[ERROR] {msg}")

    def _set_cors_headers(self) -> None:
        """Menambahkan header Cross-Origin Resource Sharing (CORS)."""
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")

    def _send_json(self, data: Any, status: int = 200) -> None:
        """Mengirim payload JSON terenkode UTF-8 dengan header CORS."""
        try:
            payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
        except Exception as exc:
            self._send_error_json(500, f"Gagal serialisasi JSON: {exc}")
            return

        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self._set_cors_headers()
        self.end_headers()
        self.wfile.write(payload)

    def _send_error_json(self, status: int, message: str) -> None:
        """Mengirim response error berformat JSON."""
        self._send_json({"error": message, "status": status}, status=status)

    def do_OPTIONS(self) -> None:
        """Menangani preflight request CORS."""
        self.send_response(204)
        self._set_cors_headers()
        self.send_header("Access-Control-Max-Age", "86400")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:
        """Routing request HTTP GET."""
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/status":
            self._handle_get_status()
        elif path == "/api/meters":
            self._handle_get_meters()
        elif path == "/api/logs":
            self._handle_get_logs()
        elif path.startswith("/api/"):
            self._send_error_json(404, f"API endpoint '{path}' tidak ditemukan")
        else:
            self._handle_static_file(path)

    def do_POST(self) -> None:
        """Routing request HTTP POST."""
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        content_length = int(self.headers.get("Content-Length", 0))
        if content_length > 0:
            try:
                body_bytes = self.rfile.read(content_length)
                body = json.loads(body_bytes.decode("utf-8"))
            except Exception:
                self._send_error_json(400, "Body permintaan bukan format JSON yang valid")
                return
        else:
            body = {}

        if path == "/api/config":
            self._handle_post_config(body)
        elif path == "/api/test_sweep":
            self._handle_post_test_sweep(body)
        elif path.startswith("/api/"):
            self._send_error_json(404, f"API endpoint '{path}' tidak ditemukan")
        else:
            self._send_error_json(405, "Method Not Allowed")

    def _handle_get_status(self) -> None:
        """GET /api/status: Status live (state, device, format, stats, config, ram_mb)."""
        engine = self.engine

        state_str = "IDLE"
        if engine is not None:
            transport = getattr(engine, "transport", None)
            if transport is not None and hasattr(transport, "state"):
                raw_st = transport.state
                st = raw_st() if callable(raw_st) else raw_st
                state_str = st.name if hasattr(st, "name") else str(st)
            elif getattr(engine, "_thread", None) and engine._thread.is_alive():
                state_str = "STREAMING"

        source = getattr(engine, "source", None)
        rate = getattr(source, "rate", 96000) if source else 96000
        channels = getattr(source, "channels", NUM_CHANNELS) if source else NUM_CHANNELS
        bits = 32 if (engine and getattr(engine, "bits32", False)) else 24

        fmt = {
            "rate": rate,
            "bits": bits,
            "bitdepth": bits,
            "channels": channels,
            "description": f"{rate} Hz | {bits}-bit | {channels} ch",
        }

        st = getattr(engine, "stats", None)
        if st is not None:
            stats_dict = {
                "packets": getattr(st, "packets", 0),
                "frames": getattr(st, "frames", 0),
                "dropped": getattr(st, "dropped", 0),
                "clipped": getattr(st, "clipped", 0),
                "errors": getattr(st, "errors", 0),
                "last_error": getattr(st, "last_error", ""),
            }
        else:
            stats_dict = {
                "packets": 0,
                "frames": 0,
                "dropped": 0,
                "clipped": 0,
                "errors": 0,
                "last_error": "",
            }

        trims = getattr(engine, "trims_db", None)
        mutes = getattr(engine, "mutes", None)
        config_dict = {
            "mode": getattr(engine, "mode", "desktop"),
            "profile": getattr(engine, "profile", "Movie"),
            "trims_db": trims.tolist() if hasattr(trims, "tolist") else list(trims or []),
            "mutes": mutes.tolist() if hasattr(mutes, "tolist") else list(mutes or []),
            "sweep_active": getattr(engine, "sweep_active", False),
            "sweep_channel": getattr(engine, "sweep_channel", -1),
            "sweep_level_db": getattr(engine, "sweep_level_db", -18.0),
        }

        data = {
            "state": state_str,
            "device": self.device_info,
            "format": fmt,
            "stats": stats_dict,
            "config": config_dict,
            "ram_mb": get_process_ram_mb(),
        }
        self._send_json(data)

    def _handle_get_meters(self) -> None:
        """GET /api/meters: JSON 6 kanal (FL, FR, C, LFE, SL, SR) linear, dBFS, peak_hold_db."""
        engine = self.engine

        levels = getattr(engine, "levels", np.zeros(NUM_CHANNELS, dtype=np.float32))
        peak_db = getattr(engine, "peak_db", np.full(NUM_CHANNELS, -96.0, dtype=np.float32))
        peak_hold_db = getattr(engine, "peak_hold_db", np.full(NUM_CHANNELS, -96.0, dtype=np.float32))

        lin = [round(float(x), 4) for x in levels]
        db = [round(float(x), 2) for x in peak_db]
        hold = [round(float(x), 2) for x in peak_hold_db]

        meters_list = [
            {
                "channel": ch,
                "linear": lin[i],
                "dBFS": db[i],
                "dbfs": db[i],
                "peak_hold_db": hold[i],
            }
            for i, ch in enumerate(CHANNEL_NAMES)
        ]

        data = {
            "channels": CHANNEL_NAMES,
            "linear": lin,
            "dBFS": db,
            "dbfs": db,
            "peak_hold_db": hold,
            "meters": meters_list,
        }
        for i, ch in enumerate(CHANNEL_NAMES):
            data[ch] = {
                "linear": lin[i],
                "dBFS": db[i],
                "dbfs": db[i],
                "peak_hold_db": hold[i],
            }

        self._send_json(data)

    def _handle_get_logs(self) -> None:
        """GET /api/logs: JSON daftar pesan log terakhir."""
        logs = self.server.logs
        self._send_json(logs)

    def _handle_post_config(self, body: dict[str, Any]) -> None:
        """POST /api/config: Update mode, profile, trims_db, mutes."""
        engine = self.engine
        if engine is None:
            self._send_error_json(503, "Audio Engine belum diinisialisasi")
            return

        if "mode" in body:
            mode = str(body["mode"]).lower()
            if mode not in ("desktop", "bitperfect"):
                self._send_error_json(400, "Parameter 'mode' harus berupa 'desktop' atau 'bitperfect'")
                return
            engine.mode = mode
            self.server.add_log(f"Mode operasi diubah ke '{mode}'")

        if "profile" in body:
            engine.profile = str(body["profile"])
            self.server.add_log(f"Profil audio diubah ke '{engine.profile}'")

        if "trims_db" in body:
            trims = body["trims_db"]
            if not isinstance(trims, (list, tuple)):
                self._send_error_json(400, "Parameter 'trims_db' harus berupa list nilai desimal")
                return
            trims_arr = np.zeros(NUM_CHANNELS, dtype=np.float32)
            for i in range(min(len(trims), NUM_CHANNELS)):
                try:
                    trims_arr[i] = float(trims[i])
                except (ValueError, TypeError):
                    self._send_error_json(400, f"Nilai trim pada indeks {i} tidak valid")
                    return
            engine.trims_db = trims_arr

        if "mutes" in body:
            mutes = body["mutes"]
            if not isinstance(mutes, (list, tuple)):
                self._send_error_json(400, "Parameter 'mutes' harus berupa list boolean")
                return
            mutes_arr = np.zeros(NUM_CHANNELS, dtype=bool)
            for i in range(min(len(mutes), NUM_CHANNELS)):
                mutes_arr[i] = bool(mutes[i])
            engine.mutes = mutes_arr

        trims_res = getattr(engine, "trims_db", [])
        mutes_res = getattr(engine, "mutes", [])
        self._send_json({
            "status": "ok",
            "mode": getattr(engine, "mode", "desktop"),
            "profile": getattr(engine, "profile", "Movie"),
            "trims_db": trims_res.tolist() if hasattr(trims_res, "tolist") else list(trims_res),
            "mutes": mutes_res.tolist() if hasattr(mutes_res, "tolist") else list(mutes_res),
        })

    def _handle_post_test_sweep(self, body: dict[str, Any]) -> None:
        """POST /api/test_sweep: Kontrol nada tes akustik 5.1 (start / stop)."""
        engine = self.engine
        if engine is None:
            self._send_error_json(503, "Audio Engine belum diinisialisasi")
            return

        action = str(body.get("action", "")).strip().lower()
        if action not in ("start", "stop"):
            self._send_error_json(400, "Parameter 'action' harus 'start' atau 'stop'")
            return

        raw_ch = body.get("channel", -1)
        ch_idx = -1
        if isinstance(raw_ch, str):
            up = raw_ch.strip().upper()
            if up in CHANNEL_MAP:
                ch_idx = CHANNEL_MAP[up]
            else:
                try:
                    ch_idx = int(raw_ch.strip())
                except ValueError:
                    ch_idx = -1
        elif isinstance(raw_ch, (int, float)):
            ch_idx = int(raw_ch)

        if action == "start" and (ch_idx < 0 or ch_idx >= NUM_CHANNELS):
            self._send_error_json(
                400,
                f"Kanal '{raw_ch}' tidak valid. Pilihan kanal: {', '.join(CHANNEL_NAMES)} atau 0..5"
            )
            return

        try:
            level_db = float(body.get("level_db", -18.0))
        except (ValueError, TypeError):
            self._send_error_json(400, "Parameter 'level_db' harus berupa angka desimal")
            return

        active = (action == "start")
        if hasattr(engine, "set_sweep"):
            engine.set_sweep(active=active, channel=ch_idx if active else -1, level_db=level_db)

        ch_name = CHANNEL_NAMES[ch_idx] if 0 <= ch_idx < NUM_CHANNELS else "None"
        if active:
            self.server.add_log(f"Test sweep dimulai pada kanal {ch_name} ({level_db:.1f} dBFS)")
        else:
            self.server.add_log("Test sweep dihentikan")

        self._send_json({
            "status": "ok",
            "action": action,
            "sweep_active": active,
            "sweep_channel": ch_idx if active else -1,
            "channel_name": ch_name if active else None,
            "level_db": level_db,
        })

    def _handle_static_file(self, path: str) -> None:
        """Melayani file statis dari direktori web atau fallback HTML."""
        web_dir = self.server.web_dir

        if path in ("", "/", "/index.html"):
            index_path = web_dir / "index.html"
            if index_path.is_file():
                self._serve_file(index_path)
            else:
                self._serve_fallback_html()
            return

        # Sanitasi path untuk mencegah traversal directory
        rel_path = path.lstrip("/")
        try:
            target = (web_dir / rel_path).resolve()
            if not target.is_relative_to(web_dir.resolve()):
                self._send_error_json(403, "Akses dilarang")
                return
        except (ValueError, Exception):
            self._send_error_json(403, "Akses dilarang")
            return

        if target.is_file():
            self._serve_file(target)
        else:
            self._send_error_json(404, f"File statis '{path}' tidak ditemukan")

    def _serve_file(self, file_path: Path) -> None:
        """Membaca dan mengirim file statis dengan Content-Type yang sesuai."""
        mime_type, _ = mimetypes.guess_type(str(file_path))
        if mime_type is None:
            mime_type = "application/octet-stream"

        try:
            data = file_path.read_bytes()
        except Exception as exc:
            self._send_error_json(500, f"Gagal membaca file: {exc}")
            return

        self.send_response(200)
        self.send_header("Content-Type", mime_type)
        self.send_header("Content-Length", str(len(data)))
        self._set_cors_headers()
        self.end_headers()
        self.wfile.write(data)

    def _serve_fallback_html(self) -> None:
        """Mengirim HTML fallback sederhana saat index.html belum tersedia."""
        payload = FALLBACK_HTML.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self._set_cors_headers()
        self.end_headers()
        self.wfile.write(payload)


def create_ui_server(
    engine: Any = None,
    host: str = "127.0.0.1",
    port: int = 0,
    device_info: dict[str, Any] | None = None,
    web_dir: str | Path | None = None,
) -> BridgeHTTPServer:
    """Membuat instance BridgeHTTPServer.
    
    Argumen port=0 menggunakan binding dinamis ephemeral port dari OS
    sehingga aman dari konflik port saat testing atau paralel execution.
    """
    return BridgeHTTPServer(
        (host, port),
        BridgeRequestHandler,
        engine=engine,
        device_info=device_info,
        web_dir=web_dir,
    )


def start_ui_server_thread(
    engine: Any = None,
    host: str = "127.0.0.1",
    port: int = 43060,
    device_info: dict[str, Any] | None = None,
    web_dir: str | Path | None = None,
) -> tuple[BridgeHTTPServer, threading.Thread, str]:
    """Menjalankan BridgeHTTPServer pada background daemon thread.
    
    Mengembalikan tuple: (server, thread, url).
    """
    server = create_ui_server(
        engine=engine,
        host=host,
        port=port,
        device_info=device_info,
        web_dir=web_dir,
    )
    thread = threading.Thread(
        target=server.serve_forever,
        name="ht306-ui-server",
        daemon=True,
    )
    thread.start()

    actual_port = server.server_address[1]
    url = f"http://{host}:{actual_port}"
    server.add_log(f"UI Server berjalan pada {url}")
    return server, thread, url
