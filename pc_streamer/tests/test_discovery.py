import pytest

from ht306bridge import protocol as P
from ht306bridge import discovery as D


def good(serial=P.USB_SERIAL, bus=1, addr=1):
    return D.DeviceInfo(
        P.USB_VID, P.USB_PID, P.USB_MANUFACTURER, P.USB_PRODUCT, serial,
        (D.InterfaceInfo(P.VENDOR_INTERFACE, 0xFF, (D.EndpointInfo(0x03, 0x02),)),), bus, addr)


class FakeUsb:
    def __init__(self, devs=(), exc=None):
        self.devs, self.exc = list(devs), exc

    def enumerate(self):
        if self.exc:
            raise self.exc
        return self.devs


class FakeSerial:
    def __init__(self, port="COM9", ok=True, exc=None):
        self.port, self.ok, self.exc = port, ok, exc

    def find_port(self, vid, pid, serial):
        return self.port

    def handshake(self, port, timeout):
        if self.exc:
            raise self.exc
        return self.ok


def foreign(vid, pid, mfr="x", prod="y"):
    return D.DeviceInfo(vid, pid, mfr, prod, "1")


def test_valid_accepted():
    r = D.discover(FakeUsb([good()]), FakeSerial())
    assert r.found and r.com_port == "COM9" and not r.ambiguous and not r.rejected


@pytest.mark.parametrize("dev", [
    foreign(0x303A, 0x1001), foreign(0x2E8A, 0x0005), foreign(0x2E8A, 0x000A),
    foreign(0x1A86, 0x7523), foreign(0x10C4, 0xEA60), foreign(0x2341, 0x0043)])
def test_foreign_rejected(dev):
    r = D.discover(FakeUsb([dev]), FakeSerial())
    assert not r.found and r.rejected[0].reason_code == D.E_VIDPID


def test_wrong_strings():
    d = good()
    d = D.DeviceInfo(d.vid, d.pid, "Raspberry Pi", d.product, d.serial, d.interfaces)
    r = D.discover(FakeUsb([d]), FakeSerial())
    assert not r.found and r.rejected[0].reason_code == D.E_STRINGS


def test_missing_endpoint():
    d = good()
    d = D.DeviceInfo(d.vid, d.pid, d.manufacturer, d.product, d.serial,
                     (D.InterfaceInfo(2, 0xFF, (D.EndpointInfo(0x04, 0x02),)),))
    r = D.discover(FakeUsb([d]), FakeSerial())
    assert not r.found and r.rejected[0].reason_code == D.E_ENDPOINT
    assert not D.matches_identity(d)


def test_handshake_timeout():
    r = D.discover(FakeUsb([good()]), FakeSerial(ok=False))
    assert not r.found and r.rejected[0].reason_code == D.E_HANDSHAKE


def test_handshake_exception_and_no_port():
    r = D.discover(FakeUsb([good()]), FakeSerial(exc=OSError("busy")))
    assert not r.found and r.rejected[0].reason_code == D.E_HANDSHAKE
    r = D.discover(FakeUsb([good()]), FakeSerial(port=None))
    assert not r.found and r.rejected[0].reason_code == D.E_HANDSHAKE


def test_two_valid_ambiguous_deterministic():
    # serial wajib persis sama -> urutan deterministik jatuh ke (bus, address)
    a, b = good(bus=1, addr=7), good(bus=1, addr=3)
    r = D.discover(FakeUsb([a, b]), FakeSerial())
    assert r.found and r.ambiguous and len(r.candidates) == 2
    assert r.device.address == 3


def test_none_found():
    r = D.discover(FakeUsb([]), FakeSerial())
    assert not r.found and r.device is None


def test_backend_exception_handled():
    r = D.discover(FakeUsb(exc=RuntimeError("boom")), FakeSerial())
    assert not r.found and r.rejected[0].reason_code == D.E_BACKEND


def test_matches_identity_pure():
    assert D.matches_identity(good())
    assert not D.matches_identity(foreign(P.USB_VID, P.USB_PID))
