#!/usr/bin/env python3
"""Отладка pico-xvc: выставить TDI/TMS и прочитать реальные уровни на ножках.

    python3 xvc_pins.py [host]

Pico читает уровень прямо с контакта, поэтому видно, выдаёт ли выход сигнал.
"""
import os
import socket
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xvc_test import recv_exact, shift  # noqa: E402

PINS = {"TDI": 12, "TMS": 13, "TCK": 14, "TDO": 15}


def read_pins(s):
    s.sendall(b"dbgpins:")
    v = struct.unpack("<I", recv_exact(s, 4))[0]
    return {k: (v >> p) & 1 for k, p in PINS.items()}


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.7.1"
    s = socket.create_connection((host, 2542), timeout=5)
    s.sendall(b"settck:" + struct.pack("<I", 1000))
    recv_exact(s, 4)
    ok = True
    for tdi, tms in ((0, 0), (1, 0), (0, 1), (1, 1)):
        shift(s, [tms], [tdi])  # после сдвига выходы держат последнее значение
        lv = read_pins(s)
        good = lv["TDI"] == tdi and lv["TMS"] == tms
        ok &= good
        print(f"выставлено TDI={tdi} TMS={tms} → на ножках: "
              + " ".join(f"{k}={v}" for k, v in lv.items())
              + ("" if good else "   <-- не совпадает"))
    print("Выходы TDI/TMS в порядке" if ok else "Есть выход, который не держит уровень (см. выше)")


if __name__ == "__main__":
    main()
