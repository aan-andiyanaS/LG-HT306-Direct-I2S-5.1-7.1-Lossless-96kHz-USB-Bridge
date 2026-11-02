"""Penemuan perangkat HT306 dengan identifikasi 4-faktor (anti salah tebak).

Faktor: (1) VID/PID, (2) string Manufacturer/Product/Serial, (3) interface vendor
class 0xFF dengan endpoint bulk OUT 0x03, (4) handshake CDC (marker heartbeat).
Backend USB/serial diinjeksi agar bisa diuji tanpa hardware.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Optional, Protocol

from . import protocol as P

E_VIDPID = "E-USB-VIDPID"
E_STRINGS = "E-USB-STRINGS"
E_ENDPOINT = "E-USB-ENDPOINT"
E_HANDSHAKE = "E-USB-HANDSHAKE"
E_BACKEND = "E-USB-BACKEND"

BULK_TRANSFER_TYPE = 0x02


@dataclass(frozen=True)
class EndpointInfo:
    address: int
    attributes: int  # bit0-1 = tipe transfer (2 = bulk)


@dataclass(frozen=True)
class InterfaceInfo:
    number: int
    interface_class: int
    endpoints: tuple = ()  # tuple[EndpointInfo]


@dataclass(frozen=True)
class DeviceInfo:
    vid: int
    pid: int
    manufacturer: Optional[str] = None
    product: Optional[str] = None
    serial: Optional[str] = None
    interfaces: tuple = ()  # tuple[InterfaceInfo]
    bus: Optional[int] = None
    address: Optional[int] = None


@dataclass(frozen=True)
class Rejection:
    reason_code: str
    detail: str


@dataclass
class DiscoveryResult:
    found: bool = False
    device: Optional[DeviceInfo] = None
    com_port: Optional[str] = None
    ambiguous: bool = False
    candidates: list = field(default_factory=list)  # list[(DeviceInfo, com_port)]
    rejected: list = field(default_factory=list)  # list[Rejection]


class UsbBackend(Protocol):
    def enumerate(self) -> list:  # list[DeviceInfo]
        ...


class SerialBackend(Protocol):
    def find_port(self, vid: int, pid: int, serial: Optional[str]) -> Optional[str]:
        ...

    def handshake(self, port: str, timeout: float) -> bool:
        ...


# ---------------------------------------------------------------- pemeriksaan murni

def _check_vidpid(dev: DeviceInfo) -> bool:
    return dev.vid == P.USB_VID and dev.pid == P.USB_PID


def _check_strings(dev: DeviceInfo) -> bool:
    return (dev.manufacturer == P.USB_MANUFACTURER and dev.product == P.USB_PRODUCT
            and dev.serial == P.USB_SERIAL)


def _check_endpoint(dev: DeviceInfo) -> bool:
    for itf in dev.interfaces:
        if itf.number == P.VENDOR_INTERFACE and itf.interface_class == P.VENDOR_CLASS:
            for ep in itf.endpoints:
                if ep.address == P.VENDOR_EP_OUT and (ep.attributes & 0x03) == BULK_TRANSFER_TYPE:
                    return True
    return False


def matches_identity(dev: DeviceInfo) -> bool:
    """Faktor 1-3 (murni, cepat, tanpa I/O) untuk poller hot-plug."""
    return _check_vidpid(dev) and _check_strings(dev) and _check_endpoint(dev)


def _static_rejection(dev: DeviceInfo) -> Optional[Rejection]:
    tag = f"{dev.vid:04x}:{dev.pid:04x}"
    if not _check_vidpid(dev):
        return Rejection(E_VIDPID, f"{tag} bukan {P.USB_VID:04x}:{P.USB_PID:04x}")
    if not _check_strings(dev):
        return Rejection(E_STRINGS, f"{tag} string tidak cocok "
                         f"(mfr={dev.manufacturer!r}, prod={dev.product!r}, sn={dev.serial!r})")
    if not _check_endpoint(dev):
        return Rejection(E_ENDPOINT, f"{tag} tanpa interface {P.VENDOR_INTERFACE} "
                         f"class 0xFF + bulk OUT 0x{P.VENDOR_EP_OUT:02X}")
    return None


# ---------------------------------------------------------------- discovery

def discover(usb: UsbBackend, serial: SerialBackend, handshake_timeout: float = 2.0) -> DiscoveryResult:
    """Cari perangkat HT306 sah. Tidak pernah melempar exception."""
    result = DiscoveryResult()
    try:
        devices = list(usb.enumerate())
    except Exception as exc:  # backend rusak -> hasil kosong + alasan
        result.rejected.append(Rejection(E_BACKEND, f"enumerate gagal: {exc!r}"))
        return result

    valid = []
    for dev in devices:
        rej = _static_rejection(dev)
        if rej is not None:
            # Perangkat asing (VID/PID salah) dicatat tapi hanya sebagai informasi.
            result.rejected.append(rej)
            continue
        try:
            port = serial.find_port(dev.vid, dev.pid, dev.serial)
            if port is None:
                result.rejected.append(Rejection(E_HANDSHAKE, "COM port perangkat tidak ditemukan"))
                continue
            if not serial.handshake(port, handshake_timeout):
                result.rejected.append(Rejection(E_HANDSHAKE, f"{port}: marker "
                                                 f"{P.HEARTBEAT_MARKER} tidak terlihat"))
                continue
        except Exception as exc:
            result.rejected.append(Rejection(E_HANDSHAKE, f"handshake error: {exc!r}"))
            continue
        valid.append((dev, port))

    if valid:
        valid.sort(key=lambda dp: (dp[0].serial or "", dp[0].bus or 0, dp[0].address or 0))
        result.found = True
        result.device, result.com_port = valid[0]
        result.candidates = valid
        result.ambiguous = len(valid) > 1
    return result


# ---------------------------------------------------------------- backend nyata

class PyUsbBackend:
    """Enumerasi via pyusb + libusb_package (impor malas)."""

    def enumerate(self) -> list:
        import libusb_package
        import usb.core
        import usb.util

        backend = libusb_package.get_libusb1_backend()
        out = []
        for d in usb.core.find(find_all=True, backend=backend, idVendor=P.USB_VID, idProduct=P.USB_PID) or []:
            out.append(self._describe(d, usb.util))
        # perangkat lain juga dilaporkan agar alasan penolakan jelas
        for d in usb.core.find(find_all=True, backend=backend) or []:
            if d.idVendor == P.USB_VID and d.idProduct == P.USB_PID:
                continue
            out.append(DeviceInfo(vid=d.idVendor, pid=d.idProduct, bus=d.bus, address=d.address))
        return out

    @staticmethod
    def _describe(d, util) -> DeviceInfo:
        def s(fn):
            try:
                return fn()
            except Exception:
                return None

        interfaces = []
        try:
            for cfg in d:
                for itf in cfg:
                    eps = tuple(EndpointInfo(e.bEndpointAddress, e.bmAttributes) for e in itf)
                    interfaces.append(InterfaceInfo(itf.bInterfaceNumber, itf.bInterfaceClass, eps))
        except Exception:
            pass
        return DeviceInfo(
            vid=d.idVendor, pid=d.idProduct,
            manufacturer=s(lambda: util.get_string(d, d.iManufacturer)),
            product=s(lambda: util.get_string(d, d.iProduct)),
            serial=s(lambda: util.get_string(d, d.iSerialNumber)),
            interfaces=tuple(interfaces), bus=d.bus, address=d.address)


class PySerialBackend:
    """Cari COM port via VID/PID/serial dan lakukan handshake marker."""

    def find_port(self, vid: int, pid: int, serial: Optional[str]) -> Optional[str]:
        from serial.tools import list_ports

        matches = sorted(
            (p for p in list_ports.comports()
             if p.vid == vid and p.pid == pid and (serial is None or p.serial_number in (None, serial))),
            key=lambda p: p.device)
        return matches[0].device if matches else None

    def handshake(self, port: str, timeout: float) -> bool:
        import serial as pyserial

        deadline = time.monotonic() + timeout
        marker = P.HEARTBEAT_MARKER.encode()
        buf = b""
        with pyserial.Serial(port, P.CDC_BAUD, timeout=0.1, write_timeout=0.5) as ser:
            ser.write(b"?")  # picu status
            while time.monotonic() < deadline:
                buf = (buf + ser.read(256))[-512:]
                if marker in buf:
                    return True
        return False
