#!/bin/sh
# Прошить pico-xvc: перезагрузить в BOOTSEL по сети (если уже прошит), затем picotool.
set -e
cd "$(dirname "$0")"
cmake --build build
printf 'bootsel:' | timeout 2 nc -q1 192.168.7.1 2542 2>/dev/null || true
[ -e /dev/ttyACM0 ] && stty -F /dev/ttyACM0 1200 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do
    picotool info >/dev/null 2>&1 && break
    sleep 0.5
done
picotool load -x build/pico_xvc.uf2
