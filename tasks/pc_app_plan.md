# Rencana Aplikasi PC Companion (HT306 Audio Bridge)

Acuan: `docs/05_PC_Audio_Bridge_PRD_and_Architecture.md`, `docs/decisions/ADR-001_*`, `pc_streamer/design.md`, `pc_streamer/stitch_design_system_implementation/`.

## Temuan Pra-Eksekusi (mengubah asumsi sebelumnya)

| # | Temuan | Dampak | Keputusan |
|---|--------|--------|-----------|
| 1 | Tidak ada compiler C host (`gcc`/`cl`) | Encoder tidak bisa dibuat sebagai ekstensi native dari `lossless_codec.c` | Encoder **numpy vektorisasi** + benchmark. Fallback: paket 96 frame (1 ms) bila CPU tidak cukup |
| 2 | Firmware belum punya BOS / MS OS 2.0 descriptor | Windows tidak otomatis memasang `winusb.sys` ke Interface 2 (user harus Zadig) | Slice firmware: tambah MS OS 2.0 (Compatible ID `WINUSB`) -> **perlu flash ulang RP2350** |
| 3 | `PySide6 Widgets` baseline ~45-60 MB | Target RAM <= 50 MB belum tentu tercapai jika UI & engine satu proses | **Ukur dulu.** Engine headless dipisah dari UI (proses terpisah); UI dipilih setelah pengukuran |
| 4 | Test C firmware tidak bisa dijalankan di host | Tidak ada oracle C | Oracle = referensi Python `tests/verify_end_to_end.py` (sudah terverifikasi 0 bit error) |

## Struktur

```
pc_streamer/
  ht306bridge/
    protocol.py    # konstanta header/flag/CRC (kontrak bersama, SUDAH ADA)
    codec.py       # encoder numpy (+ decoder referensi untuk tes)
    discovery.py   # identifikasi 4-faktor RP2350
    transport.py   # WinUSB bulk writer + auto-reconnect
    capture.py     # WASAPI loopback
    engine.py      # capture -> codec -> transport, state machine
    cli.py
  tests/           # pytest, tanpa hardware (mock)
  tests_hw/        # pytest -m hardware, butuh RP2350 tercolok
```

## Slice

| Slice | Isi | Verifikasi | Perlu RP2350? |
|-------|-----|-----------|---------------|
| A | `protocol.py` + `codec.py` | Round-trip vs oracle, 0 bit error 24/32-bit, benchmark < 25% 1 core | Tidak |
| B | `discovery.py` + `transport.py` | Unit test dengan backend palsu (ESP32/Pico/HT306 asli/palsu) | Tidak |
| C | Firmware MS OS 2.0 descriptor | Build `.uf2` sukses | **Ya (flash + colok)** |
| D | `capture.py` + `engine.py` + `cli.py` | Unit test mock + ukur RAM | Tidak (CLI `--dry-run`) |
| E | Uji hardware end-to-end | `tests_hw/` | **Ya** |
| F | UI (setelah pengukuran RAM) | Screenshot vs Stitch, RAM <= 50 MB | Tidak |

## Kontrak Identifikasi Perangkat (anti salah tebak)

1. VID `0x2E8A` & PID `0x4306`
2. String descriptor Manufacturer/Product/Serial persis seperti `usb_descriptors.c`
3. Interface 2 class `0xFF` dengan endpoint bulk `0x03` OUT
4. Handshake CDC: banner / heartbeat `[HT306]` terbaca dalam timeout

Semua 4 wajib lolos. Satu gagal = ditolak (dengan alasan tercatat).
