# Transmission Signals — Design Spec

**Date:** 2026-06-08
**Status:** Approved for planning
**Scope:** Decode and display Allison transmission data newly observed on the J1939 bus.

## Goal

Add five new transmission signals to OGauge, decoded from the J1939 bus and shown on
the 480×480 round display in an OEM-dash style:

| Signal | PGN | SPN | Bytes (0-idx) | Type | Decode |
|---|---|---|---|---|---|
| Transmission oil temp | 65272 TRF1 | 177 | 5-6 `[4][5]` | numeric °C | `raw * 0.03125 - 273` |
| Current range (PRND5432) | 61445 ETC2 | 163 | 7-8 `[6][7]` | 2-char ASCII | raw bytes as chars |
| Tow/Haul mode | 65098 ETC7 | 2537 | byte 3 `[2]` bits 5-6 | 2-bit status | `(byte3 >> 4) & 0x03` |
| Trans Service lamp | 65098 ETC7 | 4178 | byte 1 `[0]` bits 3-4 | 2-bit status | `(byte1 >> 2) & 0x03` |
| Trans Warning lamp | 65098 ETC7 | 5344 | byte 6 `[5]` bits 3-4 | 2-bit status | `(byte6 >> 2) & 0x03` |

Broadcast rates: ETC7 and ETC2 every 100 ms, TRF1 every 1 s — all well inside the
existing `SIGNAL_STALE_MS` (2000 ms) staleness window.

Bench-verified raw values to validate against:
- Tow/haul: `byte3 = 0x4C` → off, `0x1C` → on.
- Range (neutral): `bytes 7-8 = 0x4E 0x43` = `"N"` + `0x43`. Second byte is cal-specific
  and must be confirmed live by moving the selector through P-R-N-D-5-4-3-2.

## Architecture

No change to the data pipeline. The existing flow is reused verbatim:

```
CanBus.poll()  →  J1939Decoder.decode(pgn, data)  →  SignalStore.current  →  UI timer reads
```

We add: three new PGN decoders, two new data-model structs, and one new UI panel scope.
Threading, the CAN router, and the frame loop are untouched.

## Data Model — `src/data/signal_data.cnx`

The existing `Signal { f32 value; u32 time }` fits trans temp directly. The telltales and
range are not floats, so two new typed structs are added (chosen over overloading `f32`,
which would misrepresent the type and still couldn't hold the ASCII range):

```
struct Status {   // J1939 2-bit telltale: 0=off 1=on 2=error 3=N/A
  u8  state;
  u32 time;
}
struct Range {    // PRND current range, 2 raw ASCII bytes
  u8  c0;
  u8  c1;
  u32 time;
}
```

Added to `SignalData`:

```
Signal trans_temp_c;
Status tow_haul;
Status trans_service;
Status trans_warning;
Range  range;
```

## Decoders — `src/data/j1939_decoder.cnx`

Three new private functions, registered in the `decode()` dispatcher:

- **`decode_65272(data)`** — SPN 177 via existing `extract_u16(data, 4, 5)`, then
  `raw * 0.03125 - 273.0` (identical to the EGT/oil/ambient path). Writes `trans_temp_c`.
- **`decode_65098(data)`** — three 2-bit fields via C-Next bit indexing:
  - `data[2][4, 2]` → `tow_haul.state`
  - `data[0][2, 2]` → `trans_service.state`
  - `data[5][2, 2]` → `trans_warning.state`
  C-Next bit indexing is LSB-0, so `b[4,2]` == `(b >> 4) & 0x3` and `b[2,2]` == `(b >> 2) & 0x3`.
- **`decode_61445(data)`** — stores `data[6]` → `range.c0`, `data[7]` → `range.c1`.

All set their `.time <- global.millis()`.

## UI — new `src/display/gauge_trans.cnx`

A separate scope (keeps `gauge_temp.cnx`, already 137 lines, focused on the center engine
stack). It draws onto the same `lv_screen_active()` and runs its own 100 ms update timer.
It owns three regions:

1. **Trans temp** — one numeric label on the left edge, styled like the engine values,
   with the standard stale → `"---- F"` fallback.
2. **Telltale chips** — `( TOW )`, `( SVC )`, `( WARN )` rounded bordered labels stacked
   top-left, **always visible**, restyled by `Status.state`:
   | state | appearance |
   |---|---|
   | 0 off | gray, dimmed |
   | 1 on | full color (TOW=blue, SVC=amber, WARN=red), bright |
   | 2 error | flashing (toggle on the update timer, ~500 ms cadence) |
   | 3 N/A | gray, dimmed (same as off) |
3. **PRND arc** — 8 fixed labels `P R N D 5 4 3 2` in a gentle arc near the bottom; the
   label whose character matches the decoded range gets a box (border). First-char match
   is sufficient for v1; the cal-specific 2nd byte is a later iteration.

`main.cnx` gains one line: `GaugeTrans.create()` after `GaugeTemp.create()`.

## Decisions deferred to implementation (author writes these)

Two functions encode real behavior choices and will be written deliberately, not guessed:

- **`Status` → chip styling** — the tri-state→style mapping above (off/on/flash), with N/A
  treated as off.
- **PRND active-gear matching** — mapping the decoded ASCII char to the boxed position.
  v1: match `range.c0` against the static sequence. Iterate later for reverse/park and the
  cal-specific 2nd byte.

## Build sequence (one signal at a time)

Per agreed workflow: decode + serial-log + verify **before** any display wiring, working
through signals one at a time.

1. **Data model** — add `Status`, `Range`, and the five `SignalData` fields.
2. **Trans temp** — `decode_65272`, serial-log, verify on bus.
3. **Tow/Haul + lamps** — `decode_65098`, serial-log all three states (verify `0x4C`/`0x1C`).
4. **Range** — `decode_61445`, serial-log both bytes as hex + char (walk P-R-N-D-5-4-3-2).
5. **UI panel** — `gauge_trans.cnx`: trans temp label, then telltale chips, then PRND arc,
   wired one region at a time and confirmed on hardware.

## Verification

Each decoder is verified on the live bus via serial before its UI counterpart is built.
Serial output includes decoded trans temp, each status state value, and the two range bytes
as both hex and character. The tow/haul `0x4C`→off / `0x1C`→on cases and a full selector
sweep are the explicit acceptance checks for the trickiest signals.

## Out of scope (YAGNI)

- Requested range (SPN 162) and current numeric gear (SPN 523) — optional, not displayed.
- Other TRF1 fields (clutch pressure, oil level/pressure, filter ΔP).
- Reverse/park-specific PRND rendering and the cal-specific 2nd-byte interpretation (later).
