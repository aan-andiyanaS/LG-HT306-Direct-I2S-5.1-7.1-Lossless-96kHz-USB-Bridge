"""Kontrak protokol bersama PC <-> RP2350. Cermin dari firmware/include/lossless_codec.h.

Setiap perubahan di sini WAJIB mengikuti header C (dan sebaliknya).
"""
from __future__ import annotations

import struct

SYNC_WORD = 0xA55A
NUM_CHANNELS = 6
SUBFRAME_SAMPLES = 48
MAX_SUBFRAME_SAMPLES = 96
RICE_ESCAPE_QUOTIENT = 15
ESCAPE_RAW_BITS_24 = 25
ESCAPE_RAW_BITS_32 = 32
MAX_PACKET_BYTES = 1020
RICE_K_MASK = 0x1F

FLAG_RATE_96KHZ = 1 << 0
FLAG_BITDEPTH_24 = 1 << 1
FLAG_CHANNELS_8 = 1 << 2
FLAG_BITDEPTH_32 = 1 << 3

# sync(2) len(2) seq(1) flags(1) frames(1) rice_k(6) crc8(1) = 14 byte
HEADER_STRUCT = struct.Struct("<HHBBB6sB")
HEADER_SIZE = HEADER_STRUCT.size
assert HEADER_SIZE == 14

# Identitas USB (firmware/src/usb_descriptors.c)
USB_VID = 0x2E8A
USB_PID = 0x4306
USB_MANUFACTURER = "LG HT306 Custom Digital Mod"
USB_PRODUCT = "LG HT306 5.1 Surround Direct-I2S (RP2350-Zero)"
USB_SERIAL = "HT306-RP2350Z-96K"
VENDOR_INTERFACE = 2
VENDOR_EP_OUT = 0x03
VENDOR_CLASS = 0xFF
CDC_BAUD = 115200
HEARTBEAT_MARKER = "[HT306]"


def crc8(data: bytes) -> int:
    """CRC-8 poly 0x07, init 0x00 (sama dengan ht306_crc8)."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc
