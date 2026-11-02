"""Tes hardware nyata. Hanya jalan dengan: pytest pc_streamer/tests_hw --hardware"""
import pytest

from ht306bridge import discovery as D

pytestmark = pytest.mark.hardware


def test_real_discovery():
    r = D.discover(D.PyUsbBackend(), D.PySerialBackend(), handshake_timeout=3.0)
    assert r.found, f"HT306 tidak ditemukan: {r.rejected}"
    assert r.com_port
    assert D.matches_identity(r.device)
