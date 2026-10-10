<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Status Pager

A standalone focus-status application for the AI Passport wearable:

- **Focus / Available / Away three-state display** — the whole screen band
  changes color (red / green / amber), so the current status is visible from
  across the room without touching the device.
- **Punch and leave/return check-in** — short-press OK to punch your current
  focus segment; switching to Away records leaving and returning, with the
  away duration attached.
- **Cycle statistics and event log** — the stats screen shows focus duration,
  punch count, leave count, and focus-segment count for the current cycle,
  plus the most recent events, all stored locally in NVS.

Everything runs offline on the device. No Wi-Fi, BLE, or cloud account is
required; there is no phone companion in this version. Data is exported
through the USB-Serial-JTAG console for later sync in a companion app.

## Features

| State | Band color | Meaning |
| --- | --- | --- |
| FOCUS | Red | Deep-work segment in progress |
| AWAY | Amber | Physically away; the board logs leave/return timestamps |
| AVAILABLE | Green | Open for interruptions; shows cycle focus total |

- 250 ms UI tick keeps segment durations and the cycle totals live.
- Local event log holds the latest 120 events; the UI shows the newest 12.
- On power-on the model restores the last saved state and counters from NVS.
- Console commands let you dump the model, set a wall-clock reference, and
  clear the cycle (see below).

## Hardware and build prerequisites

Targets the FoloToy AI Passport (ESP32-C3, 8 MB Flash, no PSRAM). Required
setup:

- ESP-IDF 5.5.3 activated in the environment
- `lv_font_conv` 1.5.3 on `PATH` (only needed when regenerating fonts)
- `assets/fonts/gen_fonts.sh` regenerates the embedded Chinese font subset;
  the two `app_font_32.c` / `app_font_16.c` files must be present before building

Build:

```bash
idf.py build
```

The generated firmware reuses the repository's minimal partition table.

## Flash and run

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Connect the device with a data-capable USB cable. The app boots straight into
the main status screen.

## Controls

| Action | Effect |
| --- | --- |
| UP / DOWN short press | Cycle state: FOCUS → AVAILABLE → AWAY → FOCUS |
| OK short press | Punch the current focus segment |
| OK long press | Toggle between the main screen and the stats screen |

Button callbacks stay non-blocking; every UI update happens under the BSP
LVGL lock.

## Console export

The USB-Serial-JTAG console provides three commands (one JSON line per
response):

| Command | Purpose |
| --- | --- |
| `dump` | Print the current model, cycle totals, and all buffered events as JSON |
| `settime <epoch_sec>` | Set the wall-clock reference so logs display `HH:MM` timestamps |
| `clear` | Clear the current cycle counters and event log |

`dump` is split across one line per event when the log is long; the first line
is the model summary. Example:

```text
> dump
{"model":{"state":"focus",...},"summary":{...}}
{"event":{"type":"punch","t_sec":123,...}}
```

## Source layout

| File | Purpose |
| --- | --- |
| `status_model.c/h` | Pure state machine and cycle accounting; covered by host tests |
| `status_store.c/h` | NVS persistence: model blob + CRC, 120-event ring log, async worker |
| `status_export.c/h` | USB console REPL: `dump` / `settime` / `clear` commands |
| `status_ui.c/h` | LVGL main and stats screens with the 32/16 px CJK subset fonts |
| `status_main.c` | `app_main` entry: board init, buttons, UI timer, wiring |

Run the focused host test while editing:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_status_model.c main/status_model.c -o /tmp/test_status_model && /tmp/test_status_model
```

## Extending

The model/store boundary is ESP-IDF-free on purpose: add new status transitions
and counters to `status_model.c` first, cover them with host tests, then expose
them in the UI or the console. A companion app or BLE sync can consume
`dump` output later without changing the state machine.

See `docs/development/engineering/lvgl-chinese-fonts.md` before adding any
Chinese UI text: the embedded font subset is intentionally compact.