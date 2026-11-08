# pico-xvc — JTAG-программатор Xilinx Virtual Cable на Raspberry Pi Pico (RP2040)

Pico подключается по USB и появляется в системе как два устройства:

- **сетевая карта (CDC-NCM)**: XVC-сервер работает на самом Pico, на ПК ничего запускать не нужно;
- **COM-порт (CDC-ACM)** `/dev/ttyACM*`: мост к UART целевой платы (консоль S9).

| | |
|---|---|
| Адрес Pico | `192.168.7.1`, порт `2542` |
| Адрес ПК | выдаётся DHCP-сервером Pico (`192.168.7.2`) |
| TCK | до 10 МГц (ограничение `JTAG_MIN_PERIOD_NS`), по умолчанию 1 МГц |
| Вектор XVC | до 2048 байт (`XVC_MAX_VECTOR`) |
| Отладочный вывод | UART0: GP0 (TX), 115200 |
| UART-мост | UART1: GP4 (TX), GP5 (RX); скорость и формат задаются с ПК |
| Светодиод | горит, пока подключён клиент |

## Подключение к S9 (разъём J10, 3.3 В)

| Pico | Сигнал | S9 J10 |
|---|---|---|
| GP12 (пин 16) | TDI | TDI |
| GP13 (пин 17) | TMS | TMS |
| GP14 (пин 19) | TCK | TCK |
| GP15 (пин 20) | TDO | TDO |
| GND (пин 18) | GND | GND |

UART-консоль S9 (разъём «UART 3.3V»):

| Pico | S9 |
|---|---|
| GP4 (пин 6), TX | RX (MIO49) |
| GP5 (пин 7), RX | TX (MIO48) |
| GND (пин 8) | GND |

- Уровни совпадают (3.3 В), преобразователи не нужны. 3.3V с S9 на Pico **не подавать**:
  Pico питается от USB.
- Провода — как можно короче. При сбоях уменьши частоту TCK.
- Для отладки ARM лучше выставить перемычки загрузки в режим JTAG (JP1..JP4 = 0).

## Сборка и прошивка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # SDK по умолчанию в ~/pico-sdk
cmake --build build -j
# Pico с зажатой BOOTSEL → появится диск RPI-RP2:
cp build/pico_xvc.uf2 /run/media/$USER/RPI-RP2/
# или: picotool load -x build/pico_xvc.uf2 -f
```

Нужен `lib/lwip` в pico-sdk: `git -C ~/pico-sdk submodule update --init lib/lwip`.

### Перезагрузка в BOOTSEL без кнопки

Любой из способов (`flash.sh` пробует их по очереди):

```sh
picotool reboot -u -f --vid 0xcafe --pid 0x4a59   # интерфейс сброса, как у стандартного stdio_usb
printf 'bootsel:' | nc 192.168.7.1 2542            # по сети, через XVC-сервер
stty -F /dev/ttyACM0 1200                          # «касание» COM-порта на 1200 бод
```

`--vid/--pid` обязательны: без них picotool ищет только устройства Raspberry Pi (2e8a).
Для доступа без root — правило udev:

```
# /etc/udev/rules.d/99-pico-xvc.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4a59", MODE="0666"
SUBSYSTEM=="tty", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4a59", MODE="0666"
```

Повторная прошивка без кнопки BOOTSEL: `./flash.sh` — собирает проект, перезагружает Pico в BOOTSEL
(XVC-команда `bootsel:` или 1200 бод на `/dev/ttyACM0`) и прошивает через `picotool`.

Расширения протокола (Vivado их не использует): `dbgpins:` → u32 с уровнями всех GPIO,
`bootsel:` → перезагрузка в BOOTSEL. Скрипты проверки: `xvc_test.py` (IDCODE цепочки),
`xvc_pins.py` (выходы TDI/TMS), `xvc_loopback.py` (перемычка выход → TDO).

## Проверка

```sh
ip addr            # появится интерфейс enx..., адрес 192.168.7.2
ping 192.168.7.1
python3 xvc_test.py 192.168.7.1 2542 100
```

Ожидаемый результат с подключённой S9 (Zynq-7010): два устройства —
`0x4BA00477` (ARM DAP) и `0x?3722093` (XC7Z010).

## Использование

- **Консоль S9:** `picocom -b 115200 /dev/ttyACM0` (или `minicom -D /dev/ttyACM0`).
  Скорость меняется настройкой порта на ПК, Pico подстраивает UART сам.
  Пока терминал закрыт, хранятся последние 4 КБ вывода: при открытии порта увидишь хвост лога загрузки.
- **Vivado Hardware Manager:** Open Target → Add Xilinx Virtual Cable (XVC) →
  host `192.168.7.1`, port `2542`. Или в Tcl:
  `open_hw_target -xvc_url 192.168.7.1:2542`
- **XSCT / Vitis:** `connect -xvc-url TCP:192.168.7.1:2542`
- **Makefile проекта:** `make jtag XVC=TCP:192.168.7.1:2542`

## Как устроено

- `src/jtag.pio`: PIO-программа. За каждый такт выставляет TDI и TMS, поднимает TCK и
  читает TDO. Число бит задаётся первым словом, поэтому вектор любой длины прогоняется
  без лишних тактов.
- `src/jtag.c`: упаковка TDI/TMS в слова PIO и распаковка TDO.
- `src/xvc_server.c`: протокол XVC 1.0 поверх lwIP (raw API), один клиент.
- `src/uart_bridge.c`: мост CDC-ACM ↔ UART1, приём по прерыванию в кольцевой буфер
  (не теряет байты во время длинных JTAG-сдвигов).
- `src/main.c`: TinyUSB NCM + lwIP + DHCP-сервер (по примеру TinyUSB `net_lwip_webserver`).

USB VID:PID `cafe:4a59` — тестовый VID TinyUSB, только для личного использования.
