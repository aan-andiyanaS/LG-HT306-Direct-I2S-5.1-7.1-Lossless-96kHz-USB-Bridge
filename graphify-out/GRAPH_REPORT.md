# Graph Report - .  (2026-11-08)

## Corpus Check
- 96 files · ~276,999 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1092 nodes · 2256 edges · 61 communities (59 shown, 2 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 86 edges (avg confidence: 0.68)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Community 0
- Community 1
- Community 2
- Community 3
- Community 4
- Community 5
- Community 6
- Community 7
- Community 8
- Community 9
- Community 10
- Community 11
- Community 12
- Community 13
- Community 14
- Community 15
- Community 16
- Community 17
- Community 18
- Community 19
- Community 20
- Community 21
- Community 22
- Community 23
- Community 24
- Community 25
- Community 26
- Community 27
- Community 28
- Community 29
- Community 30
- Community 31
- Community 32
- Community 33
- Community 34
- Community 35
- Community 36
- Community 37
- Community 38
- Community 39
- Community 40
- Community 41
- Community 42
- Community 43
- Community 44
- Community 45
- Community 46
- Community 47
- Community 48
- Community 49
- Community 50
- Community 52
- Community 53

## God Nodes (most connected - your core abstractions)
1. `engine_base` - 54 edges
2. `win32_edge_engine` - 48 edges
3. `AppState` - 45 edges
4. `UsbTransport` - 41 edges
5. `cocoa_wkwebview_engine` - 40 edges
6. `AudioCapture` - 30 edges
7. `AudioPolicyManager` - 28 edges
8. `webview2_com_handler` - 28 edges
9. `AppSettings` - 25 edges
10. `gtk_webkit_engine` - 25 edges

## Surprising Connections (you probably didn't know these)
- `ProcessAudioFrames` --calls--> `ht306_lossless_encode_frame_flags()`  [INFERRED]
  pc_stream_app/src/audio_capture.h → firmware/src/lossless_codec.c
- `ht306_streamer_process_jriver_block()` --calls--> `ht306_lossless_encode_frame()`  [INFERRED]
  pc_streamer/src/main.c → firmware/src/lossless_codec.c
- `main()` --calls--> `ht306_control_set_pc_mode()`  [INFERRED]
  firmware/src/main.c → firmware/src/ht306_control.c
- `main()` --calls--> `ht306_usb_task()`  [INFERRED]
  firmware/src/main.c → firmware/src/usb_descriptors.c
- `AppRunner` --uses--> `SineSource`  [INFERRED]
  pc_streamer/ht306bridge/app.py → pc_streamer/ht306bridge/capture.py

## Import Cycles
- None detected.

## Communities (61 total, 2 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.06
Nodes (46): Callable, exception_ptr, api_filter(), apply_webkit_dmabuf_workaround(), bad_access, cast_to_webview(), error_info, m_code (+38 more)

### Community 1 - "Community 1"
Cohesion: 0.08
Nodes (22): id, autoreleased(), autoreleasepool, m_pool, cocoa_wkwebview_engine, m_app_delegate, m_debug, m_manager (+14 more)

### Community 2 - "Community 2"
Cohesion: 0.09
Nodes (39): interface, AudioDriverStatus, endpoint_id, endpoint_name, endpoint_visible, installed, is_default, AudioPolicyManager (+31 more)

### Community 3 - "Community 3"
Cohesion: 0.08
Nodes (30): Sinus per kanal berfrekuensi beda, untuk dry-run & tes. Real-time pacing…, SineSource, DummyTransport, get_ram_usage_mb(), main(), Mock transport untuk mode --dry-run tanpa hardware., Engine, EngineStats (+22 more)

### Community 4 - "Community 4"
Cohesion: 0.09
Nodes (42): DWORD, LPVOID, string, atomic, HANDLE, recursive_mutex, string, UsbTransport (+34 more)

### Community 5 - "Community 5"
Cohesion: 0.13
Nodes (25): FakeSerial, FakeUsb, foreign(), good(), parametrize, test_backend_exception_handled(), test_foreign_rejected(), test_handshake_exception_and_no_port() (+17 more)

### Community 6 - "Community 6"
Cohesion: 0.08
Nodes (28): array, basic_string, Container, HKEY, mod_handle_t, client_info_t, dll_path, found (+20 more)

### Community 7 - "Community 7"
Cohesion: 0.09
Nodes (22): delay_cycles(), ht306_control_init(), ht306_control_send_remote_cmd(), ht306_control_set_pc_mode(), ht306_control_set_status_led(), remocon_pulse(), ws2812_write_grb(), ht306_i2s_engine_init() (+14 more)

### Community 8 - "Community 8"
Cohesion: 0.08
Nodes (33): AudioCapture, ApplySoftLimiter, AudioCapture::AudioCapture(), CaptureLoop, CaptureThreadProc, GenerateTestSample, IsRunning, m_b0 (+25 more)

### Community 9 - "Community 9"
Cohesion: 0.10
Nodes (21): com_init_wrapper, m_initialized, DWORD, scale_size(), scale_value_for_dpi(), win32_edge_engine, m_com_handler, m_com_init (+13 more)

### Community 10 - "Community 10"
Cohesion: 0.14
Nodes (28): _bit_length(), _build_crc_tables(), decode_subframe(), encode_block(), _encode_core(), encode_subframe(), ndarray, Encoder lossless subframe (FLAC order-4 modular + Rice adaptif) vektorisasi… (+20 more)

### Community 11 - "Community 11"
Cohesion: 0.15
Nodes (27): HINSTANCE, LPARAM, LPSTR, LRESULT, HWND, string, EnableImmersiveDarkMode(), EscapeJsonString() (+19 more)

### Community 12 - "Community 12"
Cohesion: 0.14
Nodes (22): BitReader_t, BitWriter_t, br_get(), bw_flush(), bw_put(), HT306_CodecStatus_t, ht306_lossless_encode_frame(), ht306_lossless_encode_frame_flags() (+14 more)

### Community 13 - "Community 13"
Cohesion: 0.14
Nodes (13): BridgeRequestHandler, Any, Request handler untuk REST API dan penyajian web UI statis., Minimalisir log HTTP pada stdout; hanya tampilkan jika status code error (>=…, Tangani error logging dan catat ke log buffer server., Mengirim payload JSON terenkode UTF-8 dengan header CORS., Mengirim response error berformat JSON., Routing request HTTP GET. (+5 more)

### Community 14 - "Community 14"
Cohesion: 0.08
Nodes (24): ConnectionState, NavTab, AppState, active_device_index, app_rules, available_devices, current_tab, hw_info (+16 more)

### Community 15 - "Community 15"
Cohesion: 0.09
Nodes (19): map, engine_base, add_user_script_impl, are_user_scripts_equal_impl, bindings, browser_controller_impl, dispatch_impl, eval_impl (+11 more)

### Community 16 - "Community 16"
Cohesion: 0.16
Nodes (17): _check_endpoint(), _check_strings(), _check_vidpid(), DeviceInfo, discover(), DiscoveryResult, matches_identity(), Protocol (+9 more)

### Community 17 - "Community 17"
Cohesion: 0.12
Nodes (13): EndpointInfo, InterfaceInfo, PyUsbBackend, Enumerasi via pyusb + libusb_package (impor malas)., Exception, Protocol, _PyUsbHandle, BulkTransport: kirim paket ke EP 0x03 OUT dengan antrean bounded + thread… (+5 more)

### Community 18 - "Community 18"
Cohesion: 0.09
Nodes (23): AppSettings, active_profile, auto_reconnect, bit_perfect_mode, buffer_batch_frames, buffer_profile, desktop_os_linked, downmix_attenuation_db (+15 more)

### Community 19 - "Community 19"
Cohesion: 0.11
Nodes (13): AudioSource, list_loopback_devices(), Exception, ndarray, Protocol, Sumber audio: WASAPI loopback (nyata) dan sintetis (dry-run/tes). Kontrak…, Format Windows tidak didukung firmware (tidak pernah di-resample diam-diam)., Menangkap mix keluaran perangkat default via pyaudiowpatch (mode shared). (+5 more)

### Community 20 - "Community 20"
Cohesion: 0.12
Nodes (17): BaseHTTPRequestHandler, fixture, Path, BridgeHTTPServer, create_ui_server(), UI Server: REST API dan Static Web Server untuk HT306 Audio Bridge. Menggunakan…, Threading HTTP Server untuk UI HT306 Bridge. Menyimpan referensi ke instance…, Mengambil salinan daftar pesan log terkini secara thread-safe. (+9 more)

### Community 21 - "Community 21"
Cohesion: 0.16
Nodes (14): AppRunner, find_app_browser(), main(), parse_app_args(), Namespace, Aplikasi Desktop Utama LG HT306 Audio Bridge. Menyatukan Audio Engine (WASAPI…, Mencari browser yang mendukung flag --app untuk jendela mandiri tanpa address…, Unit test untuk Application Runner (Desktop Companion Entry Point). (+6 more)

### Community 22 - "Community 22"
Cohesion: 0.19
Nodes (16): BitReader, BitWriter, compute_flac_prediction_24(), compute_flac_prediction_32(), crc8(), decode_lossless_subframe(), encode_lossless_subframe(), estimate_optimal_rice_k() (+8 more)

### Community 23 - "Community 23"
Cohesion: 0.21
Nodes (3): noresult, string, widen_string()

### Community 24 - "Community 24"
Cohesion: 0.16
Nodes (10): error_type, basic_result, m_error, m_value, basic_result<void, Error, Exception>, m_error, optional, m_data (+2 more)

### Community 25 - "Community 25"
Cohesion: 0.12
Nodes (14): function, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2WebMessageReceivedEventHandler, msg_cb_t, webview2_com_handler, m_attempt_handler, m_attempts (+6 more)

### Community 26 - "Community 26"
Cohesion: 0.19
Nodes (17): _http_request(), Any, Unit test untuk ui_server (BridgeHTTPServer dan BridgeRequestHandler).…, Memverifikasi file statis index.html produksi yang disajikan oleh ui_server., Helper untuk melakukan HTTP request murni menggunakan urllib., test_cors_options_preflight(), test_get_logs_endpoint(), test_get_meters_endpoint() (+9 more)

### Community 27 - "Community 27"
Cohesion: 0.21
Nodes (13): apply_window_theme(), compare_os_version(), enable_dpi_awareness(), enable_non_client_dpi_scaling_if_needed(), get(), get_default_window_dpi(), get_window_dpi(), HWND (+5 more)

### Community 28 - "Community 28"
Cohesion: 0.18
Nodes (5): BulkTransport, Dukungan kompatibilitas agar bisa diakses sebagai property maupun fungsi., Non-blocking. Return False bila ada paket lama yang di-drop atau transport…, State, TransportStats

### Community 29 - "Community 29"
Cohesion: 0.21
Nodes (6): impl, list, user_script, m_code, m_impl, unique_ptr

### Community 30 - "Community 30"
Cohesion: 0.14
Nodes (12): AddLog, ClearLogs, GetLogsSnapshot, LogLevel, string, vector, LogLevel, LogEntry (+4 more)

### Community 31 - "Community 31"
Cohesion: 0.14
Nodes (14): HardwareInfo, com_port, detected, dsp_locked, firmware_version, heartbeat_ok, last_heartbeat_text, mux_high (+6 more)

### Community 32 - "Community 32"
Cohesion: 0.16
Nodes (9): HT306 Audio Bridge - PC companion untuk LG HT306 via RP2350-Zero., PyUsbTransportBackend, Backend pyusb nyata; mapping USBError/ENODEV -> UsbDisconnected., Tes hardware nyata. Hanya jalan dengan: pytest pc_streamer/tests_hw --hardware, Tes streaming hardware nyata ke RP2350-Zero. Jalankan dengan: pytest…, Menguji pengiriman stream audio sinus 96 kHz 24-bit nyata ke RP2350-Zero selama…, Menguji pengiriman stream audio sinus 96 kHz 32-bit Studio Master ke…, test_real_hardware_bulk_stream_24bit() (+1 more)

### Community 33 - "Community 33"
Cohesion: 0.29
Nodes (4): gboolean, GtkWidget, GtkWindow, gtk_compat

### Community 34 - "Community 34"
Cohesion: 0.27
Nodes (9): mutex, AppRuleItem, app_name, enabled, process_name, target_profile, atomic, string (+1 more)

### Community 35 - "Community 35"
Cohesion: 0.20
Nodes (9): callback_fn, ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler, LPCWSTR, atomic, webview2_user_script_added_handler, m_cb, m_ref_count, STDMETHODCALLTYPE (+1 more)

### Community 36 - "Community 36"
Cohesion: 0.33
Nodes (9): HRESULT, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, ICoreWebView2EnvironmentOptions, LPWSTR, create_environment_with_options_impl(), find_available_client(), get_available_browser_version_string_impl(), loader (+1 more)

### Community 37 - "Community 37"
Cohesion: 0.17
Nodes (12): StreamTelemetry, bit_depth, buffer_fill_pct, channels, jitter_ms, latency_ms, link_usage_pct, sample_rate (+4 more)

### Community 38 - "Community 38"
Cohesion: 0.24
Nodes (8): From, IID, cast_if_equal_iid(), cast_info_t, iid, LPVOID, REFIID, To

### Community 39 - "Community 39"
Cohesion: 0.18
Nodes (6): gtk_webkit_engine, m_owns_window, m_stop_run_loop, m_user_content_manager, m_webview, m_window

### Community 40 - "Community 40"
Cohesion: 0.31
Nodes (4): binding_ctx_t, binding_t, json_escape(), sync_binding_t

### Community 41 - "Community 41"
Cohesion: 0.24
Nodes (7): gchar, JSCValue, on_script_message_received_t, get_string_from_js_result(), webkitgtk_compat, WebKitJavascriptResult, WebKitUserContentManager

### Community 42 - "Community 42"
Cohesion: 0.20
Nodes (10): ProfileItem, center_clarity_db, description, id, is_active, lipsync_ms, mode, name (+2 more)

### Community 43 - "Community 43"
Cohesion: 0.22
Nodes (5): Menambahkan header Cross-Origin Resource Sharing (CORS)., Menangani preflight request CORS., Melayani file statis dari direktori web atau fallback HTML., Membaca dan mengirim file statis dengan Content-Type yang sesuai., Mengirim HTML fallback sederhana saat index.html belum tersedia.

### Community 45 - "Community 45"
Cohesion: 0.25
Nodes (7): ICoreWebView2, ICoreWebView2Controller, ICoreWebView2Environment, ICoreWebView2PermissionRequestedEventArgs, ICoreWebView2WebMessageReceivedEventArgs, narrow_string(), m_msgCb

### Community 46 - "Community 46"
Cohesion: 0.38
Nodes (5): AudioEndpointHelper, EnumerateDevices, string, wstring, WideToUtf8()

### Community 47 - "Community 47"
Cohesion: 0.47
Nodes (6): json_parse(), json_parse_c(), json_unescape(), std::string json_escape(), std::string json_parse(), WEBVIEW_DEPRECATED_PRIVATE

### Community 48 - "Community 48"
Cohesion: 0.40
Nodes (5): AudioEndpointInfo, id, is_default, name, wstring

### Community 49 - "Community 49"
Cohesion: 0.40
Nodes (5): ChannelMeters, clipped, peak_db, peak_hold_db, rms_db

### Community 52 - "Community 52"
Cohesion: 0.50
Nodes (3): crc8(), Kontrak protokol bersama PC <-> RP2350. Cermin dari…, CRC-8 poly 0x07, init 0x00 (sama dengan ht306_crc8).

### Community 53 - "Community 53"
Cohesion: 0.50
Nodes (3): get_process_ram_mb(), Mengembalikan estimasi memori RSS proses dalam megabyte (MB)., GET /api/status: Status live (state, device, format, stats, config, ram_mb).

## Knowledge Gaps
- **208 isolated node(s):** `id`, `name`, `is_default`, `peak_db`, `rms_db` (+203 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **2 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `start_ui_server_thread()` connect `Community 20` to `Community 26`, `Community 13`, `Community 11`, `Community 21`?**
  _High betweenness centrality (0.383) - this node is a cross-community bridge._
- **Why does `AppState` connect `Community 14` to `Community 34`, `Community 2`, `Community 4`, `Community 37`, `Community 8`, `Community 42`, `Community 46`, `Community 48`, `Community 49`, `Community 18`, `Community 30`, `Community 31`?**
  _High betweenness centrality (0.112) - this node is a cross-community bridge._
- **Why does `engine_base` connect `Community 15` to `Community 0`, `Community 1`, `Community 39`, `Community 40`, `Community 9`, `Community 44`, `Community 23`, `Community 27`, `Community 29`?**
  _High betweenness centrality (0.101) - this node is a cross-community bridge._
- **What connects `id`, `name`, `is_default` to the rest of the system?**
  _208 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.062111801242236024 - nodes in this community are weakly interconnected._
- **Should `Community 1` be split into smaller, more focused modules?**
  _Cohesion score 0.08181818181818182 - nodes in this community are weakly interconnected._
- **Should `Community 2` be split into smaller, more focused modules?**
  _Cohesion score 0.09090909090909091 - nodes in this community are weakly interconnected._