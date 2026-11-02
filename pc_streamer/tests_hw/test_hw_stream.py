"""Tes streaming hardware nyata ke RP2350-Zero.
Jalankan dengan: pytest pc_streamer/tests_hw --hardware
"""
import time
import pytest

from ht306bridge import discovery as D
from ht306bridge.transport import BulkTransport, PyUsbTransportBackend, State
from ht306bridge.capture import SineSource
from ht306bridge.engine import Engine

pytestmark = pytest.mark.hardware


def test_real_hardware_bulk_stream_24bit():
    """Menguji pengiriman stream audio sinus 96 kHz 24-bit nyata ke RP2350-Zero selama 2 detik."""
    r = D.discover(D.PyUsbBackend(), D.PySerialBackend(), handshake_timeout=2.0)
    assert r.found, f"Hardware tidak terdeteksi: {r.rejected}"

    transport = BulkTransport(PyUsbTransportBackend())
    transport.open()

    source = SineSource(rate=96000, channels=6, amplitude=0.4, realtime=True)
    engine = Engine(source, transport, bits32=False, batch=8)
    engine.start()

    time.sleep(2.0)
    assert transport.state in (State.STREAMING, State.READY)

    engine.stop()
    transport.close()
    source.close()

    assert engine.stats.packets > 1000
    assert engine.stats.dropped == 0
    assert engine.stats.errors == 0


def test_real_hardware_bulk_stream_32bit():
    """Menguji pengiriman stream audio sinus 96 kHz 32-bit Studio Master ke RP2350-Zero selama 2 detik."""
    r = D.discover(D.PyUsbBackend(), D.PySerialBackend(), handshake_timeout=2.0)
    assert r.found, f"Hardware tidak terdeteksi: {r.rejected}"

    transport = BulkTransport(PyUsbTransportBackend())
    transport.open()

    source = SineSource(rate=96000, channels=6, amplitude=0.4, realtime=True)
    engine = Engine(source, transport, bits32=True, batch=8)
    engine.start()

    time.sleep(2.0)
    assert transport.state in (State.STREAMING, State.READY)

    engine.stop()
    transport.close()
    source.close()

    assert engine.stats.packets > 1000
    assert engine.stats.dropped == 0
    assert engine.stats.errors == 0
