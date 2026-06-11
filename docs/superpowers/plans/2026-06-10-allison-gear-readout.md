# Allison-Style Gear Readout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the planned PRND arc with an Allison shifter–style three-character readout — lever position, demanded gear, actual gear (e.g. `D 4 3`, `P 0 0`, `R R R`) — decoded from PGN 61445 (ETC2, SA 3/TCM) and shown large at the bottom of the round display.

**Architecture:** Reuse the existing data pipeline verbatim (`CanBus.poll → J1939Decoder.decode → SignalStore → UI timer`). The decoder copies three raw bytes from the ETC2 frame into a new `GearState` struct; a new `GaugeTrans` UI scope owns all char rendering (the reverse/neutral/N-A rules) in one place and draws a single big label.

**Tech Stack:** C-Next (`.cnx`, auto-transpiled), LVGL v9, ESP32-S3 / PlatformIO. No automated test harness — verification is build success plus on-hardware acceptance (live selector sweep + screen check), matching the existing transmission plan.

**Spec:** `docs/superpowers/specs/2026-06-10-allison-gear-readout-design.md`

**Starting state:** `src/data/signal_data.cnx` currently has a `Range { u8 c0; u8 c1; u32 time }` struct and a `Range range;` field (from the abandoned PRND attempt). `src/data/j1939_decoder.cnx` has a `decode_61445` with temporary per-frame diagnostic logging and a `pgn = 61445 && sa = SA_TCM` dispatch entry. `src/display/gauge_trans.cnx` does not exist yet.

---

### Task 1: Data model + decoder — `GearState` and `decode_61445`

These change together (the struct and its only writer), committed as one green build so no
intermediate commit is broken.

**Files:**
- Modify: `src/data/signal_data.cnx`
- Modify: `src/data/j1939_decoder.cnx`

- [ ] **Step 1: Replace the `Range` struct with `GearState`**

In `src/data/signal_data.cnx`, replace the entire `Range` struct block:

```
// PRND current range — two raw ASCII bytes (SPN 163, PGN 61445 ETC2 from TCM)
struct Range {
  u8  c0;
  u8  c1;
  u32 time;
}
```

with:

```
// ETC2 gear state — three raw bytes from PGN 61445 (SA 3 / TCM).
// Display renders each via GaugeTrans.gear_char/lever_char; kept raw here so
// all character logic lives in one place (the UI), not split into the decoder.
struct GearState {
  u8  lever;      // Requested Range char  [4]  (ASCII)
  u8  demanded;   // Selected Gear raw     [0]  (offset -125)
  u8  actual;     // Current Gear raw      [3]  (offset -125)
  u32 time;
}
```

- [ ] **Step 2: Swap the field in `SignalData`**

In `struct SignalData`, replace `Range  range;` with:

```
  GearState gear;
```

- [ ] **Step 3: Replace the `decode_61445` function**

In `src/data/j1939_decoder.cnx`, replace the entire current `decode_61445` (the one with the `RANGE:` per-frame diagnostic and `range.c0`/`range.c1` writes) with:

```
  private void decode_61445(u8[8] data) {
    // PGN 61445 ETC2: lever = Requested Range char [4] (SPN 162),
    // demanded = Selected Gear [0] (SPN 524), actual = Current Gear [3] (SPN 523).
    // Gears are raw bytes (offset -125); the UI converts to characters.
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
    // Change-triggered only — a per-frame printf in the RX drain loop starves
    // the UI (see ui-starvation lesson). This prints once per lever/gear change.
    if (changed) {
      global.Serial.printf("GEAR: lever=%c(0x%02X) dmd=0x%02X cur=0x%02X\n", lv, lv, dm, ac);
    }
  }
```

The dispatch entry `if (pgn = 61445 && sa = SA_TCM) { this.decode_61445(data); }` is already present — leave it.

- [ ] **Step 4: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: `SUCCESS` (struct + decoder change together — no more `.range` references).

- [ ] **Step 5: Flash**

Run: `pio run -e waveshare_lcd_21 -t upload`
Expected: `Hash of data verified.` / `SUCCESS`.

- [ ] **Step 6: Hardware acceptance check (user) — selector sweep**

With the truck running, capture serial while sweeping the selector:

```bash
stty -F /dev/ttyACM0 115200 raw -echo
timeout 45 cat /dev/ttyACM0 | grep -a --line-buffered "GEAR:" > /tmp/gear_sweep.log
```

Sweep **P → R → N → D → 5 → 4 → 3 → 2**, pausing ~2 s per position, then:

```bash
uniq -c /tmp/gear_sweep.log
```

Acceptance criteria:
- `lever` tracks the physical lever — shows `D` in Drive (NOT `1`), `R` in Reverse, `P`/`N`, etc. **This is the check that proves the redesign fixed the wrong-field problem.**
- `dmd`/`cur` bytes move during shifts (e.g. in Drive, `cur` settles while `dmd` may lead during a shift). Record the hex values so Task 3's rendering can be sanity-checked (e.g. neutral/park ≈ `0x7D` = 125 → gear 0).

If `lever` still does not track (e.g. shows control/space), the meaningful char is byte `[5]` not `[4]` — change `u8 lv <- data[4];` to `u8 lv <- data[5];` and re-flash before proceeding.

- [ ] **Step 7: Commit**

```bash
git add src/data/signal_data.cnx src/data/j1939_decoder.cnx
git commit -m "feat(trans): ETC2 gear state — lever/demanded/actual from PGN 61445

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Display — `GaugeTrans` gear readout

**Files:**
- Create: `src/display/gauge_trans.cnx`
- Modify: `src/main.cnx`

- [ ] **Step 1: Create the scope with char helpers**

Create `src/display/gauge_trans.cnx`:

```
#include <Arduino.h>
#include <lvgl.h>
#include "../data/signal_data.cnx"

scope GaugeTrans {
  lv_obj_t gear_val;

  // Convert a raw J1939 gear byte (offset -125) to a single display character.
  // 0xFF/0xFE = not available/error -> '-'; gear 0 -> '0'; gear < 0 -> 'R';
  // gear 1..9 -> that digit (clamped at '9').
  private u8 gear_char(u8 raw) {
    u8 result <- '-';
    bool na <- false;
    if (raw = 0xFF) { na <- true; }
    if (raw = 0xFE) { na <- true; }
    if (na) {
      result <- '-';
    } else {
      i32 g <- raw[0, 8];
      g <- g - 125;
      if (g = 0) { result <- '0'; }
      if (g < 0) { result <- 'R'; }
      if (g > 0) {
        i32 d <- g;
        if (d > 9) { d <- 9; }
        i32 cv <- 48 + d;
        result <- cv[0, 8];
      }
    }
    return result;
  }

  // Lever char is ASCII already; render printable bytes as-is, space/control -> '-'.
  private u8 lever_char(u8 raw) {
    u8 result <- raw;
    if (raw <= 0x20) { result <- '-'; }
    return result;
  }

  void update() {
    u32 now <- global.millis();
    u32 age <- now - SignalStore.current.gear.time;
    if (age > SIGNAL_STALE_MS) {
      global.lv_label_set_text(this.gear_val, "-  -  -");
    } else {
      u8 lv <- this.lever_char(SignalStore.current.gear.lever);
      u8 dm <- this.gear_char(SignalStore.current.gear.demanded);
      u8 ac <- this.gear_char(SignalStore.current.gear.actual);
      global.lv_label_set_text_fmt(this.gear_val, "%c  %c  %c", lv, dm, ac);
    }
  }

  private void on_update_timer(lv_timer_t t) {
    this.update();
  }

  void create() {
    lv_obj_t scr <- global.lv_screen_active();

    this.gear_val <- global.lv_label_create(scr);
    global.lv_label_set_text(this.gear_val, "-  -  -");
    global.lv_obj_align(this.gear_val, LV_ALIGN_CENTER, 0, 180);
    global.lv_obj_set_style_text_font(this.gear_val, lv_font_montserrat_40, LV_PART_MAIN);
    global.lv_obj_set_style_text_color(this.gear_val, global.lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    global.lv_timer_create(this.on_update_timer, 100, 0);
  }
}
```

- [ ] **Step 2: Include and create it in `main.cnx`**

In `src/main.cnx`, add the include after the existing `#include "display/gauge_temp.cnx"` line:

```
#include "display/gauge_trans.cnx"
```

In `setup()`, add directly after `GaugeTemp.create();`:

```
  Serial.println("Gear readout init...");
  GaugeTrans.create();
```

- [ ] **Step 3: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: `SUCCESS`.

- [ ] **Step 4: Flash**

Run: `pio run -e waveshare_lcd_21 -t upload`
Expected: `Hash of data verified.` / `SUCCESS`.

- [ ] **Step 5: Hardware acceptance check (user)**

With the truck running, confirm a large three-character readout appears at the bottom-center of the screen, bigger than the engine values. Sweep the selector and confirm:
- Park → `P 0 0`, Neutral → `N 0 0`, Reverse → `R R R`.
- Drive → `D` followed by the demanded/actual gear digits, which change during shifts.
- Engine key off (or bus quiet > 2 s) → `-  -  -`.

Tune `lv_obj_align` y-offset (currently `180`) if the readout clips the bottom of the round display.

- [ ] **Step 6: Commit**

```bash
git add src/display/gauge_trans.cnx src/main.cnx
git commit -m "feat(ui): Allison-style gear readout (lever/demanded/actual)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for the implementer

- **C-Next gotchas:** equality is `=` not `==`; assignment is `<-`; inequality is `!=`; `if` conditions can't hold function calls or bare non-bool vars (the plan extracts to `bool`/locals); array param syntax is `u8[8] data`. Char literals (`'D'`, `'-'`, `'0'`) are valid `u8`.
- **Widening before signed math:** `i32 g <- raw[0, 8];` widens a `u8` to `i32` via bit extraction (a direct `u8 - 125` would clamp/underflow and lose reverse detection). Narrowing back uses `cv[0, 8]`.
- **No per-frame serial in the RX drain loop** — it starves the UI (documented lesson). The decoder logs change-triggered only.
- **Geometry/font are tunable on hardware** — the `180` y-offset and `montserrat_40` (already enabled in `src/display/lv_conf.h`) are starting points.
- **Build/flash/monitor:** `pio run -e waveshare_lcd_21`, `... -t upload`, monitor at 115200 on `/dev/ttyACM0`.

## Self-Review

- **Spec coverage:** data source + byte map → Task 2; `GearState` model → Task 1; char rendering rules (R/`0`/`-`, lever printable) → Task 3 `gear_char`/`lever_char`; bottom-center montserrat_40 label + stale `-  -  -` → Task 3; `main.cnx` one-line wiring → Task 3; build-order verify-on-bus-then-UI → Task ordering with sweep in Task 2, screen check in Task 3. The byte-offset caveat (`[4]` vs `[5]`) is handled in Task 2 Step 4.
- **Type consistency:** `GearState { lever, demanded, actual, time }` defined in Task 1, written in Task 2 (`SignalStore.current.gear.*`), read in Task 3 (same paths). Helpers return `u8`; `lv_label_set_text_fmt` consumes them as `%c`. Field names identical across all three tasks.
- **Placeholders:** none — every code step is complete C-Next.
