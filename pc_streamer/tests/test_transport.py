import random
import threading
import time

from ht306bridge import transport as T
from tests.test_discovery import good


def wait_for(cond, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.01)
    return False


class FakeHandle:
    def __init__(self, be):
        self.be = be

    def write(self, ep, data, timeout_ms):
        be = self.be
        if be.gate is not None:
            be.gate.wait(5)
        if be.unplugged:
            raise T.UsbDisconnected("ENODEV")
        if be.random_err and random.random() < 0.3:
            raise RuntimeError("random")
        assert ep == 0x03
        be.sent.append(bytes(data))
        return len(data)

    def close(self):
        pass


class FakeBackend:
    def __init__(self):
        self.sent, self.unplugged, self.gate, self.random_err = [], False, None, False
        self.opens = 0

    def find_device(self):
        return None if self.unplugged else good()

    def open(self, dev):
        self.opens += 1
        return FakeHandle(self)


def mk(be, **kw):
    return T.BulkTransport(be, backoff=(0.02, 0.02, 0.05), **kw)


def test_sequential_writes_intact():
    be = FakeBackend()
    tr = mk(be, queue_size=64)
    tr.open()
    pkts = [bytes([i]) * (i + 1) for i in range(30)]
    for p in pkts:
        tr.write(p)
    assert wait_for(lambda: len(be.sent) == 30)
    assert be.sent == pkts
    s = tr.stats()
    assert s.packets == 30 and s.bytes == sum(map(len, pkts)) and s.dropped == 0
    assert tr.state is T.State.STREAMING
    tr.close()


def test_queue_full_drops_oldest_bounded():
    be = FakeBackend()
    be.gate = threading.Event()
    tr = mk(be, queue_size=4)
    tr.open()
    assert wait_for(lambda: tr.state is T.State.READY)
    for i in range(100):
        tr.write(bytes([i]))
        assert tr.stats().queue_depth <= 4
    assert tr.stats().dropped >= 90
    be.gate.set()
    assert wait_for(lambda: be.sent and be.sent[-1] == bytes([99]))
    tr.close()


def test_unplug_then_reconnect():
    be = FakeBackend()
    tr = mk(be)
    tr.open()
    tr.write(b"a")
    assert wait_for(lambda: be.sent == [b"a"])
    be.unplugged = True
    tr.write(b"lost")
    assert wait_for(lambda: tr.state is T.State.DISCONNECTED)
    be.unplugged = False
    assert wait_for(lambda: tr.state is T.State.READY)
    tr.write(b"b")
    assert wait_for(lambda: b"b" in be.sent)
    assert tr.stats().reconnects == 1 and tr.stats().errors >= 1
    tr.close()


def test_close_stops_thread():
    be = FakeBackend()
    tr = mk(be)
    tr.open()
    tr.write(b"x")
    tr.close()
    assert not [t for t in threading.enumerate() if t.name == "ht306-bulk-writer"]
    assert tr.state is T.State.DISCONNECTED
    assert tr.write(b"y") is False


def test_close_while_disconnected_no_device():
    be = FakeBackend()
    be.unplugged = True
    tr = T.BulkTransport(be, backoff=(5.0,))
    tr.open()
    time.sleep(0.1)
    t0 = time.monotonic()
    tr.close()
    assert time.monotonic() - t0 < 2
    assert not [t for t in threading.enumerate() if t.name == "ht306-bulk-writer"]


def test_random_errors_no_deadlock():
    be = FakeBackend()
    be.random_err = True
    tr = mk(be, queue_size=8)
    tr.open()
    for i in range(200):
        tr.write(bytes([i % 256]))
        time.sleep(0.001)
    time.sleep(0.3)
    tr.close()
    assert not [t for t in threading.enumerate() if t.name == "ht306-bulk-writer"]
    assert tr.stats().errors > 0
