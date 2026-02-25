# J1939 Decode Design — Phase 5.1

## Goal

Parse received CAN frames as J1939 messages and print decoded header fields to serial. Prove the J1939 library works end-to-end with live OSSM traffic. No SPN interpretation, no signal store, no UI changes.

## Serial Output Format

Existing candump line followed by decoded J1939 fields:

```
can0  18FEF100  [8]  01 02 03 04 05 06 07 08
  J1939  PGN:65265 (0xFEF1)  SRC:0x00  PRI:6
```

## Changes

| File | Change |
|------|--------|
| `src/data/twai_driver.cnx` | Include J1939Message.cnx, decode frames in `poll()`, new `print_j1939()` method |
| `cnext_build.py` | Add J1939 lib path to `--include` flags |

## Data Flow

```
TWAI RX queue
  ↓
TwaiDriver.poll()
  ↓ twai_message_t
print_frame()         — existing candump line
  ↓
J1939.setCanId()      — extract PGN, source, priority
J1939.setData()       — copy payload
  ↓
print_j1939()         — new: print decoded fields
```

## Build Config

Add `--include .pio/libdeps/waveshare_lcd_21/J1939/src` to cnext transpiler so it can resolve the J1939Message.cnx include.

## Not in Scope

- SPN interpretation / human-readable signal values
- Signal store / catalog
- UI changes
- Threading changes
