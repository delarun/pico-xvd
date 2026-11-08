#!/usr/bin/env python3
"""Проверка XVC-сервера: getinfo, settck и чтение IDCODE всех устройств в JTAG-цепочке.

    python3 xvc_test.py [host] [port] [period_ns]
    python3 xvc_test.py 192.168.7.1 2542 100

Для Zynq-7010 ожидаются два устройства:
    0x4BA00477 — ARM DAP (Cortex-A9)
    0x?3722093 — XC7Z010 (старшая цифра — ревизия кристалла)
"""
import socket
import struct
import sys

# Ключи без поля версии (старшие 4 бита)
KNOWN = {
    0x0BA00477: "ARM DAP (Zynq-7000 PS)",
    0x03722093: "XC7Z010",
    0x03727093: "XC7Z020",
    0x0373B093: "XC7Z007S",
}


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("соединение закрыто сервером")
        buf += chunk
    return buf


def bits_to_bytes(bits):
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def shift(s, tms_bits, tdi_bits):
    n = len(tms_bits)
    tms, tdi = bits_to_bytes(tms_bits), bits_to_bytes(tdi_bits)
    s.sendall(b"shift:" + struct.pack("<I", n) + tms + tdi)
    tdo = recv_exact(s, len(tms))
    return [(tdo[i // 8] >> (i % 8)) & 1 for i in range(n)]


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.7.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 2542
    period = int(sys.argv[3]) if len(sys.argv) > 3 else 100

    s = socket.create_connection((host, port), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    s.sendall(b"getinfo:")
    info = b""
    while not info.endswith(b"\n"):
        info += recv_exact(s, 1)
    print("getinfo:", info.decode().strip())

    s.sendall(b"settck:" + struct.pack("<I", period))
    actual = struct.unpack("<I", recv_exact(s, 4))[0]
    print(f"settck: запрошено {period} нс, установлено {actual} нс (~{1e3 / actual:.2f} МГц)")

    # Test-Logic-Reset → Run-Test/Idle → Select-DR → Capture-DR → Shift-DR
    nav = [1, 1, 1, 1, 1, 0, 1, 0, 0]
    shift(s, nav, [0] * len(nav))

    # После сброса в DR каждого устройства — IDCODE (32 бита) или BYPASS (1 бит, всегда 0).
    # Вдвигаем единицы: когда пойдут 0xFFFFFFFF — цепочка закончилась.
    n = 32 * 8
    tdo = shift(s, [0] * (n - 1) + [1], [1] * n)
    shift(s, [1, 0], [0, 0])  # Exit1-DR → Update-DR → Run-Test/Idle

    if not any(tdo):
        print("TDO всё время 0: линию тянет к земле. Плата выключена (питание 12 В?),")
        print("TDO перепутан с GND/другим сигналом или не тот пин на Pico (нужен GP15, пин 20).")
        sys.exit(1)
    if all(tdo):
        print("TDO всё время 1: TDO не подключён (работает подтяжка) или цепочка не отвечает.")
        sys.exit(1)

    i, dev, found = 0, 0, 0
    while i + 32 <= n:
        if tdo[i] == 0:  # BYPASS
            print(f"  устройство {dev}: BYPASS (без IDCODE)")
            i += 1
            dev += 1
            continue
        word = sum(b << k for k, b in enumerate(tdo[i:i + 32]))
        if word == 0xFFFFFFFF:
            break
        name = KNOWN.get(word & 0x0FFFFFFF, "неизвестно")
        print(f"  устройство {dev}: IDCODE 0x{word:08X}  {name}")
        i += 32
        dev += 1
        found += 1

    if found == 0:
        print("Устройств не найдено: проверь проводку, GND, питание платы и режим загрузки.")
        sys.exit(1)
    print(f"Найдено устройств: {dev} (ближайшее к TDO — первое)")


if __name__ == "__main__":
    main()
