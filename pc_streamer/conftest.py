import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))


def pytest_addoption(parser):
    parser.addoption("--hardware", action="store_true", default=False,
                     help="jalankan tes yang butuh RP2350 sungguhan")


def pytest_configure(config):
    config.addinivalue_line("markers", "hardware: butuh perangkat HT306 nyata (--hardware)")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--hardware"):
        return
    skip = pytest.mark.skip(reason="butuh --hardware")
    for item in items:
        if "hardware" in item.keywords:
            item.add_marker(skip)
