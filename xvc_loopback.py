#!/usr/bin/env python3
"""Самопроверка Pico без целевой платы: перемычка с выхода на TDO (GP15).

    GP12 (TDI, пин 16) → GP15 (TDO, пин 20):  python3 xvc_loopback.py tdi
    GP13 (TMS, пин 17) → GP15 (TDO, пин 20):  python3 xvc_loopback.py tms

Провод TDO от S9 на время теста отключить.
"""
import random
import socket
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from xvc_test import recv_exact, shift  # noqa: E402


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "tdi"
    host = sys.argv[2] if len(sys.argv) > 2 else "192.168.7.1"
    s = socket.create_connection((host, 2542), timeout=5)
    s.sendall(b"settck:" + struct.pack("<I", 1000))
    recv_exact(s, 4)

    ok = True
    for n in (8, 33, 1000, 16000):
        a = [random.randint(0, 1) for _ in range(n)]
        b = [random.randint(0, 1) for _ in range(n)]
        tms, tdi = (b, a) if which == "tdi" else (a, b)
        tdo = shift(s, tms, tdi)
        # TDO читается на подъёме TCK — это значение, выставленное в этом же такте
        bad = sum(1 for x, y in zip(tdo, a) if x != y)
        print(f"{n:6d} бит: несовпадений {bad}")
        ok &= bad == 0
    print("OK: линия", which.upper(), "работает" if ok else "НЕ совпадает — проблема на стороне Pico")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
