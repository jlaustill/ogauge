# Allison-Style Gear Readout — Design Spec

**Date:** 2026-06-10
**Status:** Approved for planning
**Supersedes:** the PRND5432 arc (Task 7) of `2026-06-08-transmission-signals-design.md`. The
other transmission signals in that spec (trans temp, telltales) are unaffected.

## Goal

Replace the planned static PRND arc with an Allison shifter–style three-character readout
showing **lever position, demanded gear, and actual gear** side by side, e.g. `D 4 3`,
`P 0 0`, `R R R`, `N 0 0`.

### Why the redesign

The original spec decoded PGN 61445 bytes `[6][7]` (SPN 163, *Current Range*). A live
selector sweep (P-R-N-D-5-4-3-2) showed that field only ever reporting `P`, `N`, and `1` —
in Drive it reports the *achieved gear number* (`1`), not the lever letter `D`. That is the
wrong field for a lever display, and on its own it throws away the demanded/actual gear
information that makes the real Allison readout useful.

## Data Source

All three characters come from the single ETC2 frame already decoded — PGN **61445**,
source address **3 (TCM, `SA_TCM`)**, broadcast every 100 ms (well inside the 2000 ms
`SIGNAL_STALE_MS` window).

| Char | J1939 field (SPN) | Byte (0-idx) | Raw → meaning |
|------|-------------------|--------------|---------------|
| lever | Transmission Requested Range (162) | `[4]` | ASCII char as-is (operator's lever position) |
| demanded | Transmission Selected Gear (524) | `[0]` | `raw − 125` (gear the TCM is commanding) |
| actual | Transmission Current Gear (523) | `[3]` | `raw − 125` (gear currently engaged) |

`D 4 3` = lever in Drive, TCM commanding 4th, currently engaged in 3rd. `P 0 0` = Park,
no gear demanded or engaged.

**Byte-offset caveat (verified at build step 2):** SPN 162 *Requested Range* is a two-byte
ASCII field (bytes 5-6 → `[4][5]`). Per J1939 a single display char may live in the second
byte with the first being space/control — but this truck's *Current Range* capture put the
gear char in the **first** byte with a constant `0x43 ('C')` second byte. We therefore decode
`[4]` as the lever char and confirm it live during the sweep; if the meaningful char turns out
to be `[5]`, switch the offset (one-line change). The constant 2nd byte is ignored.

## Character Rendering

A single display helper, `gear_char(raw)`, owns all interpretation so the rules live in one
place (not split across decoder and UI):

```
gear_char(u8 raw):                 // raw = J1939 gear byte, offset −125
  if raw = 0xFF or raw = 0xFE: '-' // not available / error
  i32 g <- raw − 125
  if g = 0:  '0'                   // neutral / park / no gear
  if g < 0:  'R'                   // reverse (any reverse gear)
  else:      digit '0'+g, clamped to '9'
```

Lever char: use the raw byte if printable (`>= 0x20`), else `'-'`.

| Situation | lever | demanded | actual | shows |
|---|---|---|---|---|
| Park | `P` | 0 | 0 | `P 0 0` |
| Drive, 4th cmd / 3rd actual | `D` | 4 | 3 | `D 4 3` |
| Reverse | `R` | <0 | <0 | `R R R` |
| Neutral | `N` | 0 | 0 | `N 0 0` |
| Field N/A | `D` | 0xFF | 0xFF | `D - -` |

## Components

No change to the data pipeline (`CanBus.poll → J1939Decoder.decode → SignalStore → UI timer`).

**Data model — `src/data/signal_data.cnx`**
Replace the `Range` struct (added during the earlier PRND attempt) with:

```
// ETC2 gear state — three raw bytes from PGN 61445 (SA 3 / TCM).
// Display renders each via gear_char(); kept raw here so all char logic is in one place.
struct GearState {
  u8  lever;      // Requested Range char  [4]
  u8  demanded;   // Selected Gear raw     [0]  (offset −125)
  u8  actual;     // Current Gear raw      [3]  (offset −125)
  u32 time;
}
```

In `SignalData`, replace `Range range;` with `GearState gear;`.

**Decoder — `src/data/j1939_decoder.cnx`**
Rewrite `decode_61445` to copy the three bytes + timestamp; SA_TCM dispatch entry unchanged.
Change-triggered serial log (not per-frame — see the UI-starvation lesson: a per-frame printf
in the RX drain loop freezes the UI) printing all three as char + hex for the verification sweep:

```
private void decode_61445(u8[8] data) {
  u8 lv <- data[4];
  u8 dm <- data[0];
  u8 ac <- data[3];
  bool changed <- false;
  if (lv != SignalStore.current.gear.lever)    { changed <- true; }
  if (dm != SignalStore.current.gear.demanded) { changed <- true; }
  if (ac != SignalStore.current.gear.actual)   { changed <- true; }
  SignalStore.current.gear.lever    <- lv;
  SignalStore.current.gear.demanded <- dm;
  SignalStore.current.gear.actual   <- ac;
  SignalStore.current.gear.time     <- global.millis();
  if (changed) {
    global.Serial.printf("GEAR: lever=%c(0x%02X) dmd=0x%02X cur=0x%02X\n", lv, lv, dm, ac);
  }
}
```

**Display — `src/display/gauge_trans.cnx` (new scope)**
- One large label, bottom-center, **montserrat_40** (larger than the engine values' 32;
  already enabled in `lv_conf.h`, no config change needed), styled like the engine values.
- Private `gear_char(u8 raw) -> u8` helper implementing the table above; a sibling
  `lever_char(u8 raw) -> u8` for the printable-or-`'-'` rule.
- 100 ms update timer (own `lv_timer_create`, same pattern as `GaugeTemp`): reads
  `SignalStore.current.gear`, and if `now − time > SIGNAL_STALE_MS` renders `"-  -  -"`,
  else `lv_label_set_text_fmt(lbl, "%c  %c  %c", lever, dmd, act)`.
- `main.cnx` gains one line: `GaugeTrans.create()` after `GaugeTemp.create()`.

## Build Sequence (verify on bus before UI)

1. **Data model** — swap `Range`→`GearState`, build to confirm it transpiles.
2. **Decoder** — rewrite `decode_61445`, flash, **re-sweep P-R-N-D-5-4-3-2**. Acceptance:
   lever tracks the lever cleanly (shows `D`, not `1`); demanded/actual gear bytes move as
   expected. Confirm byte `[4]` is the right lever offset before building UI.
3. **Display** — `gauge_trans.cnx` + `main.cnx` wiring, flash, confirm `D 4 3` at the bottom
   tracks the shifter.

## Verification

The decoder is validated on the live bus via the change-triggered serial log before any UI is
built. The explicit acceptance check is the selector sweep showing the lever char matching the
physical lever (the failure that motivated this redesign), plus demanded/actual responding
during shifts.

## Out of Scope (YAGNI)

- Column labels (LVR/DMD/ACT) — bare three chars per the chosen layout.
- Fonts larger than montserrat_40 (would need enabling in `lv_conf.h`).
- Multi-char ranges (D1/D2/R2…) and the cal-specific 2nd byte of the range field.
- Reverse-gear magnitude (all reverse renders `R`).
- Actual Gear Ratio (SPN 526) and any other ETC2 field.
- Trans temp and telltales — covered by the prior spec, not revisited here.
