"""BulkTransport: kirim paket ke EP 0x03 OUT dengan antrean bounded + thread writer.

Keputusan: paket ditulis utuh (tidak dipotong 64 byte) — host controller USB
memecah ke packet size FS 64 byte sendiri; pemotongan manual hanya menambah
syscall. Antrean penuh -> drop-oldest (audio realtime: data lama lebih basi),
dihitung di `dropped`. Tidak ada logging per paket.
"""
from __future__ import annotations

import collections
import enum
import threading
from dataclasses import dataclass
from typing import Callable, Optional, Protocol

from . import protocol as P
from .discovery import DeviceInfo, matches_identity

BACKOFF_STEPS = (0.5, 1.0, 2.0, 5.0)


class State(enum.Enum):
    DISCONNECTED = "DISCONNECTED"
    CONNECTING = "CONNECTING"
    READY = "READY"
    STREAMING = "STREAMING"
    ERROR = "ERROR"

    def __call__(self):
        """Dukungan kompatibilitas agar bisa diakses sebagai property maupun fungsi."""
        return self


class UsbDisconnected(Exception):
    """Perangkat tercabut / ENODEV."""


class UsbHandle(Protocol):
    def write(self, endpoint: int, data: bytes, timeout_ms: int) -> int: ...
    def close(self) -> None: ...


class TransportBackend(Protocol):
    def find_device(self) -> Optional[DeviceInfo]:
        """Kembalikan DeviceInfo yang ada saat ini (atau None)."""

    def open(self, device: DeviceInfo) -> UsbHandle: ...


@dataclass
class TransportStats:
    bytes: int = 0
    packets: int = 0
    errors: int = 0
    reconnects: int = 0
    dropped: int = 0
    queue_depth: int = 0


class BulkTransport:
    def __init__(self, backend: TransportBackend, queue_size: int = 32,
                 backoff: tuple = BACKOFF_STEPS, write_timeout_ms: int = 500,
                 on_state: Optional[Callable[[State], None]] = None):
        self._backend = backend
        self._q: collections.deque = collections.deque()
        self._qmax = max(1, queue_size)
        self._cv = threading.Condition()
        self._backoff = backoff
        self._wto = write_timeout_ms
        self._on_state = on_state
        self._state = State.DISCONNECTED
        self._stats = TransportStats()
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._handle: Optional[UsbHandle] = None
        self._reconnected_once = False

    # ------------------------------------------------------------ API publik
    @property
    def state(self) -> State:
        return self._state

    def stats(self) -> TransportStats:
        with self._cv:
            s = self._stats
            return TransportStats(s.bytes, s.packets, s.errors, s.reconnects, s.dropped, len(self._q))

    def open(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="ht306-bulk-writer", daemon=True)
        self._thread.start()

    def write(self, packet: bytes) -> bool:
        """Non-blocking. Return False bila ada paket lama yang di-drop atau transport tidak aktif."""
        if self._stop.is_set() or self._thread is None:
            return False
        ok = True
        with self._cv:
            if len(self._q) >= self._qmax:
                self._q.popleft()
                self._stats.dropped += 1
                ok = False
            self._q.append(bytes(packet))
            self._cv.notify()
        return ok

    def close(self, timeout: float = 5.0) -> None:
        self._stop.set()
        with self._cv:
            self._cv.notify_all()
        t = self._thread
        if t is not None and t is not threading.current_thread():
            t.join(timeout)
        self._thread = None
        self._close_handle()
        with self._cv:
            self._q.clear()
        self._set_state(State.DISCONNECTED)

    # ------------------------------------------------------------ internal
    def _set_state(self, st: State) -> None:
        if st is self._state:
            return
        self._state = st
        if self._on_state:
            try:
                self._on_state(st)
            except Exception:
                pass

    def _close_handle(self) -> None:
        h, self._handle = self._handle, None
        if h is not None:
            try:
                h.close()
            except Exception:
                pass

    def _connect(self) -> bool:
        self._set_state(State.CONNECTING)
        try:
            dev = self._backend.find_device()
            if dev is None or not matches_identity(dev):
                return False
            self._handle = self._backend.open(dev)
        except Exception:
            self._stats.errors += 1
            self._set_state(State.ERROR)
            return False
        if self._reconnected_once:
            self._stats.reconnects += 1
        self._reconnected_once = True
        self._set_state(State.READY)
        return True

    def _run(self) -> None:
        attempt = 0
        while not self._stop.is_set():
            if self._handle is None:
                if self._connect():
                    attempt = 0
                else:
                    if self._state is not State.ERROR:
                        self._set_state(State.DISCONNECTED)
                    delay = self._backoff[min(attempt, len(self._backoff) - 1)]
                    attempt += 1
                    self._stop.wait(delay)
                    continue
            with self._cv:
                while not self._q and not self._stop.is_set():
                    self._cv.wait(0.2)
                if self._stop.is_set():
                    break
                pkt = self._q.popleft()
            try:
                self._handle.write(P.VENDOR_EP_OUT, pkt, self._wto)
            except Exception as exc:
                with self._cv:
                    self._stats.errors += 1
                self._close_handle()
                self._set_state(State.DISCONNECTED if isinstance(exc, UsbDisconnected)
                                else State.ERROR)
                continue
            with self._cv:
                self._stats.bytes += len(pkt)
                self._stats.packets += 1
            self._set_state(State.STREAMING)


# ---------------------------------------------------------------- backend nyata

class PyUsbTransportBackend:
    """Backend pyusb nyata; mapping USBError/ENODEV -> UsbDisconnected."""

    def __init__(self):
        from .discovery import PyUsbBackend
        self._enum = PyUsbBackend()

    def find_device(self) -> Optional[DeviceInfo]:
        for d in sorted(self._enum.enumerate(), key=lambda x: x.serial or ""):
            if matches_identity(d):
                return d
        return None

    def open(self, device: DeviceInfo) -> UsbHandle:
        import libusb_package
        import usb.core
        import usb.util

        dev = usb.core.find(backend=libusb_package.get_libusb1_backend(),
                            idVendor=device.vid, idProduct=device.pid, bus=device.bus,
                            address=device.address)
        if dev is None:
            raise UsbDisconnected("perangkat hilang")
        import sys
        if sys.platform != "win32":
            try:
                dev.set_configuration()
            except Exception:
                pass
        usb.util.claim_interface(dev, P.VENDOR_INTERFACE)
        return _PyUsbHandle(dev, usb)


class _PyUsbHandle:
    def __init__(self, dev, usb_mod):
        self._dev, self._usb = dev, usb_mod

    def write(self, endpoint: int, data: bytes, timeout_ms: int) -> int:
        try:
            return self._dev.write(endpoint, data, timeout_ms)
        except self._usb.core.USBError as exc:
            if exc.errno in (19, 5, 32) or "no such device" in str(exc).lower():
                raise UsbDisconnected(str(exc)) from exc
            raise

    def close(self) -> None:
        self._usb.util.dispose_resources(self._dev)
