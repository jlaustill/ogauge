# Transmission Signals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decode five new Allison transmission signals from the J1939 bus (trans oil temp, PRND5432 range, and three 2-bit telltales) and display them OEM-dash style on the 480×480 round screen.

**Architecture:** Reuses the existing `CanBus.poll() → J1939Decoder.decode() → SignalStore.current → UI timer` pipeline unchanged. Adds two data-model structs, three PGN decoders, and one new UI panel scope (`gauge_trans.cnx`) that draws onto the same active screen.

**Tech Stack:** C-Next (`.cnx`, auto-transpiled by `cnext_build.py`), ESP-IDF TWAI, `jlaustill/J1939`, LVGL v9.2, PlatformIO.

**Spec:** `docs/superpowers/specs/2026-06-08-transmission-signals-design.md`

**Verification model (read once):** There is no host unit-test harness. Each task is verified by (a) a clean transpile+build via `pio run -e waveshare_lcd_21`, and (b) where noted, a hardware acceptance check — flash with `pio run -e waveshare_lcd_21 -t upload && pio device monitor` and confirm the serial line or on-screen result. Hardware steps require the user (the agent cannot flash the board); call them out and wait for confirmation. Work one signal at a time — do not start a UI task before its decoder is serial-verified.

---

## File Structure

- **Modify** `src/data/signal_data.cnx` — add `Status` + `Range` structs and five `SignalData` fields.
- **Modify** `src/data/j1939_decoder.cnx` — add `decode_65272`, `decode_65098`, `decode_61445` + dispatch entries.
- **Create** `src/display/gauge_trans.cnx` — new panel scope: trans temp label, telltale chips, PRND arc.
- **Modify** `src/main.cnx` — `#include` the new panel and call `GaugeTrans.create()`.

---

### Task 1: Data model — Status, Range, and new fields

**Files:**
- Modify: `src/data/signal_data.cnx`

- [ ] **Step 1: Add the two new structs**

After the existing `struct Signal { ... }` block in `src/data/signal_data.cnx`, add:

```
// J1939 2-bit telltale state: 0=off 1=on 2=error 3=N/A
struct Status {
  u8  state;
  u32 time;
}

// PRND current range — two raw ASCII bytes (SPN 163)
struct Range {
  u8  c0;
  u8  c1;
  u32 time;
}
```

- [ ] **Step 2: Add fields to SignalData**

In `struct SignalData`, after `Signal total_dist_km;`, add:

```
  Signal trans_temp_c;
  Status tow_haul;
  Status trans_service;
  Status trans_warning;
  Range  range;
```

- [ ] **Step 3: Transpile + build to verify the model compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds (`SUCCESS`). No new runtime behavior yet — this only proves the structs transpile and `SignalData` still builds.

- [ ] **Step 4: Commit**

```bash
git add src/data/signal_data.cnx
git commit -m "feat(data): add Status/Range structs + transmission signal fields"
```

---

### Task 2: Decode trans oil temp (PGN 65272, SPN 177)

**Files:**
- Modify: `src/data/j1939_decoder.cnx`

- [ ] **Step 1: Add the decoder function**

In `src/data/j1939_decoder.cnx`, add a new private function alongside the other `decode_*` functions (it reuses the existing `extract_u16` helper, identical math to `decode_65270`):

```
  private void decode_65272(u8[8] data) {
    // SPN 177 - Transmission Oil Temperature: bytes 5-6 ([4][5]), 0.03125 C/bit, -273 offset
    u16 raw177 <- this.extract_u16(data, 4, 5);
    i32 raw177_wide <- raw177[0, 16];
    SignalStore.current.trans_temp_c.value <- raw177_wide * 0.03125 - 273.0;
    SignalStore.current.trans_temp_c.time <- global.millis();

    i32 t <- SignalStore.current.trans_temp_c.value;
    global.Serial.printf("TRANS TEMP: %d C\n", t);
  }
```

- [ ] **Step 2: Register it in the dispatcher**

In the `decode(u16 pgn, u8[8] data)` function, add alongside the existing `if` lines:

```
    if (pgn = 65272) { this.decode_65272(data); }
```

- [ ] **Step 3: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 4: Hardware acceptance check (user)**

Run: `pio run -e waveshare_lcd_21 -t upload && pio device monitor`
Expected: with the transmission powered, serial shows recurring `TRANS TEMP: <n> C` lines (~once per second; TRF1 is a 1 s message). Value should be a plausible fluid temp (e.g. 60–110 C warm). Confirm before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/data/j1939_decoder.cnx
git commit -m "feat(decode): transmission oil temp from PGN 65272 SPN 177"
```

---

### Task 3: Decode telltales (PGN 65098 ETC7 — tow/haul, service, warning)

**Files:**
- Modify: `src/data/j1939_decoder.cnx`

- [ ] **Step 1: Add the decoder function**

C-Next bit indexing is LSB-0, so `b[4,2]` == `(b >> 4) & 0x3` and `b[2,2]` == `(b >> 2) & 0x3` — matching the spec's shift extracts. Add:

```
  private void decode_65098(u8[8] data) {
    // SPN 2537 Tow/Haul: byte 3 ([2]) bits 5-6  -> (byte3 >> 4) & 0x03
    u8 b3 <- data[2];
    u8 tow <- b3[4, 2];
    SignalStore.current.tow_haul.state <- tow;
    SignalStore.current.tow_haul.time <- global.millis();

    // SPN 4178 Trans Service: byte 1 ([0]) bits 3-4  -> (byte1 >> 2) & 0x03
    u8 b1 <- data[0];
    u8 svc <- b1[2, 2];
    SignalStore.current.trans_service.state <- svc;
    SignalStore.current.trans_service.time <- global.millis();

    // SPN 5344 Trans Warning: byte 6 ([5]) bits 3-4  -> (byte6 >> 2) & 0x03
    u8 b6 <- data[5];
    u8 warn <- b6[2, 2];
    SignalStore.current.trans_warning.state <- warn;
    SignalStore.current.trans_warning.time <- global.millis();

    global.Serial.printf("ETC7: tow=%d svc=%d warn=%d\n", tow, svc, warn);
  }
```

- [ ] **Step 2: Register it in the dispatcher**

```
    if (pgn = 65098) { this.decode_65098(data); }
```

- [ ] **Step 3: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 4: Hardware acceptance check (user)**

Run: `pio run -e waveshare_lcd_21 -t upload && pio device monitor`
Expected: serial shows recurring `ETC7: tow=<n> svc=<n> warn=<n>` (~every 100 ms). **Bench validation:** toggle tow/haul — `tow` must read `0` off and `1` on (raw byte3 `0x4C`→off `0x1C`→on). Confirm before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/data/j1939_decoder.cnx
git commit -m "feat(decode): tow/haul + service + warning telltales from PGN 65098"
```

---

### Task 4: Decode range PRND5432 (PGN 61445 ETC2, SPN 163)

**Files:**
- Modify: `src/data/j1939_decoder.cnx`

- [ ] **Step 1: Add the decoder function**

```
  private void decode_61445(u8[8] data) {
    // SPN 163 Current Range (PRND5432): bytes 7-8 ([6][7]), 2-char ASCII
    SignalStore.current.range.c0 <- data[6];
    SignalStore.current.range.c1 <- data[7];
    SignalStore.current.range.time <- global.millis();

    global.Serial.printf("RANGE: %c%c (0x%02X 0x%02X)\n",
        data[6], data[7], data[6], data[7]);
  }
```

- [ ] **Step 2: Register it in the dispatcher**

```
    if (pgn = 61445) { this.decode_61445(data); }
```

- [ ] **Step 3: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 4: Hardware acceptance check (user) — selector sweep**

Run: `pio run -e waveshare_lcd_21 -t upload && pio device monitor`
Expected: serial shows `RANGE: <c0><c1> (0x.. 0x..)`. Move the selector through **P-R-N-D-5-4-3-2** and record each line. Neutral should read first byte `0x4E` (`N`). The first char (`c0`) is the gear; note what the second byte (`c1`) does per position — this informs the later cal-specific iteration. Confirm `c0` tracks the gear before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/data/j1939_decoder.cnx
git commit -m "feat(decode): current range PRND5432 from PGN 61445 SPN 163"
```

---

### Task 5: UI panel skeleton + trans temp label

**Files:**
- Create: `src/display/gauge_trans.cnx`
- Modify: `src/main.cnx`

- [ ] **Step 1: Create the panel scope with the trans temp label**

Create `src/display/gauge_trans.cnx`. Mirrors the label/timer pattern from `gauge_temp.cnx`. Trans temp is stored in C and displayed in F (`c * 1.8 + 32`):

```
#include <Arduino.h>
#include <lvgl.h>
#include "../data/signal_data.cnx"

scope GaugeTrans {
  lv_obj_t trans_title;
  lv_obj_t trans_val;

  void update() {
    u32 now <- global.millis();

    u32 trans_age <- now - SignalStore.current.trans_temp_c.time;
    if (trans_age > SIGNAL_STALE_MS) {
      global.lv_label_set_text(this.trans_val, "---- F");
    } else {
      i32 f <- SignalStore.current.trans_temp_c.value * 1.8 + 32.0;
      global.lv_label_set_text_fmt(this.trans_val, "%d F", f);
    }
  }

  private void on_update_timer(lv_timer_t t) {
    this.update();
  }

  void create() {
    lv_obj_t scr <- global.lv_screen_active();

    // TRANS temp — left edge, amber
    this.trans_title <- global.lv_label_create(scr);
    global.lv_label_set_text(this.trans_title, "TRANS");
    global.lv_obj_align(this.trans_title, LV_ALIGN_LEFT_MID, 6, -14);
    global.lv_obj_set_style_text_font(this.trans_title, lv_font_montserrat_14, LV_PART_MAIN);
    global.lv_obj_set_style_text_color(this.trans_title, global.lv_color_hex(0xFFB300), LV_PART_MAIN);

    this.trans_val <- global.lv_label_create(scr);
    global.lv_label_set_text(this.trans_val, "---- F");
    global.lv_obj_align(this.trans_val, LV_ALIGN_LEFT_MID, 6, 10);
    global.lv_obj_set_style_text_font(this.trans_val, lv_font_montserrat_14, LV_PART_MAIN);
    global.lv_obj_set_style_text_color(this.trans_val, global.lv_color_hex(0xFFB300), LV_PART_MAIN);

    global.lv_timer_create(this.on_update_timer, 100, 0);
  }
}
```

- [ ] **Step 2: Wire it into main**

In `src/main.cnx`, add the include after the other display includes:

```
#include "display/gauge_trans.cnx"
```

And in `setup()`, after `GaugeTemp.create();`, add:

```
  GaugeTrans.create();
```

- [ ] **Step 3: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 4: Hardware acceptance check (user)**

Flash and confirm `TRANS` + a temperature in °F appears on the left edge of the screen, updating live (or `---- F` when the bus is idle). Confirm before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/display/gauge_trans.cnx src/main.cnx
git commit -m "feat(ui): transmission temp panel on left edge"
```

---

### Task 6: Telltale chips (tow/haul, service, warning)

**Files:**
- Modify: `src/display/gauge_trans.cnx`

Tri-state styling: state 0/3 → gray + dim; state 1 → full color + bright; state 2 → flash (toggle dim on a ~500 ms cadence from the update timer). Each chip is a bordered, rounded label, **always visible**.

- [ ] **Step 1: Add chip object fields**

In the `GaugeTrans` scope, after `lv_obj_t trans_val;`, add:

```
  lv_obj_t tow_chip;
  lv_obj_t svc_chip;
  lv_obj_t warn_chip;
```

- [ ] **Step 2: Add a private chip styling helper**

Add inside the scope (above `update`). `on_color` is the lit color; `flash_on` is the current flash phase used for state 2:

```
  private void style_chip(lv_obj_t chip, u8 state, u32 on_color, bool flash_on) {
    bool lit <- false;
    if (state = 1) { lit <- true; }
    if (state = 2) { lit <- flash_on; }

    if (lit) {
      global.lv_obj_set_style_text_color(chip, global.lv_color_hex(on_color), LV_PART_MAIN);
      global.lv_obj_set_style_border_color(chip, global.lv_color_hex(on_color), LV_PART_MAIN);
      global.lv_obj_set_style_text_opa(chip, 255, LV_PART_MAIN);
      global.lv_obj_set_style_border_opa(chip, 255, LV_PART_MAIN);
    } else {
      global.lv_obj_set_style_text_color(chip, global.lv_color_hex(0x666666), LV_PART_MAIN);
      global.lv_obj_set_style_border_color(chip, global.lv_color_hex(0x666666), LV_PART_MAIN);
      global.lv_obj_set_style_text_opa(chip, 100, LV_PART_MAIN);
      global.lv_obj_set_style_border_opa(chip, 100, LV_PART_MAIN);
    }
  }
```

- [ ] **Step 3: Add a private chip-creation helper**

```
  private lv_obj_t make_chip(lv_obj_t scr, i32 y_off) {
    lv_obj_t chip <- global.lv_label_create(scr);
    global.lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 6, y_off);
    global.lv_obj_set_style_text_font(chip, lv_font_montserrat_14, LV_PART_MAIN);
    global.lv_obj_set_style_border_width(chip, 2, LV_PART_MAIN);
    global.lv_obj_set_style_radius(chip, 6, LV_PART_MAIN);
    global.lv_obj_set_style_pad_all(chip, 3, LV_PART_MAIN);
    return chip;
  }
```

- [ ] **Step 4: Create the chips in `create()`**

In `create()`, before `lv_timer_create`, add:

```
    this.tow_chip <- this.make_chip(scr, 8);
    global.lv_label_set_text(this.tow_chip, "TOW");

    this.svc_chip <- this.make_chip(scr, 38);
    global.lv_label_set_text(this.svc_chip, "SVC");

    this.warn_chip <- this.make_chip(scr, 68);
    global.lv_label_set_text(this.warn_chip, "WARN");
```

- [ ] **Step 5: Style the chips each frame in `update()`**

At the end of `update()`, add (TOW=blue `0x4488FF`, SVC=amber `0xFFB300`, WARN=red `0xFF3333`). Flash phase toggles every ~500 ms:

```
    bool flash_on <- false;
    u32 phase <- now % 1000;
    if (phase < 500) { flash_on <- true; }

    this.style_chip(this.tow_chip, SignalStore.current.tow_haul.state, 0x4488FF, flash_on);
    this.style_chip(this.svc_chip, SignalStore.current.trans_service.state, 0xFFB300, flash_on);
    this.style_chip(this.warn_chip, SignalStore.current.trans_warning.state, 0xFF3333, flash_on);
```

- [ ] **Step 6: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 7: Hardware acceptance check (user)**

Flash and confirm three chips (`TOW`, `SVC`, `WARN`) appear top-left, gray/dim by default. Toggle tow/haul on the truck → `TOW` turns blue and bright. If a service/warning state of 2 occurs, that chip flashes. Confirm before proceeding.

- [ ] **Step 8: Commit**

```bash
git add src/display/gauge_trans.cnx
git commit -m "feat(ui): tri-state transmission telltale chips with flash-on-error"
```

---

### Task 7: PRND5432 arc

**Files:**
- Modify: `src/display/gauge_trans.cnx`

v1: eight fixed labels `P R N D 5 4 3 2` in a row near the bottom; box the one whose
character matches `range.c0`. Gentle arc via small per-label y offsets. Cal-specific 2nd
byte handling is a later iteration (out of scope here).

- [ ] **Step 1: Add the label array field**

In the scope fields, add:

```
  lv_obj_t prnd[8];
```

- [ ] **Step 2: Create the arc in `create()`**

Before `lv_timer_create`, add. The 8 chars, evenly spaced around center, with a slight upward bow toward the middle:

```
    u8 seq[8] <- ['P', 'R', 'N', 'D', '5', '4', '3', '2'];
    i32 y_arc[8] <- [200, 210, 216, 218, 218, 216, 210, 200];
    i32 i <- 0;
    while (i < 8) {
      lv_obj_t lbl <- global.lv_label_create(scr);
      global.lv_label_set_text_fmt(lbl, "%c", seq[i]);
      i32 x_off <- (i * 44) - 154;
      global.lv_obj_align(lbl, LV_ALIGN_TOP_MID, x_off, y_arc[i]);
      global.lv_obj_set_style_text_font(lbl, lv_font_montserrat_14, LV_PART_MAIN);
      global.lv_obj_set_style_text_color(lbl, global.lv_color_hex(0x888888), LV_PART_MAIN);
      global.lv_obj_set_style_border_width(lbl, 2, LV_PART_MAIN);
      global.lv_obj_set_style_border_opa(lbl, 0, LV_PART_MAIN);
      global.lv_obj_set_style_radius(lbl, 4, LV_PART_MAIN);
      global.lv_obj_set_style_pad_all(lbl, 2, LV_PART_MAIN);
      this.prnd[i] <- lbl;
      i +<- 1;
    }
```

- [ ] **Step 3: Highlight the active gear in `update()`**

At the end of `update()`, add. Matches `range.c0`; the matched label gets a white border + bright text, others stay gray with no border:

```
    u8 cur <- SignalStore.current.range.c0;
    u8 seq2[8] <- ['P', 'R', 'N', 'D', '5', '4', '3', '2'];
    i32 j <- 0;
    while (j < 8) {
      bool active <- false;
      if (seq2[j] = cur) { active <- true; }
      if (active) {
        global.lv_obj_set_style_text_color(this.prnd[j], global.lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        global.lv_obj_set_style_border_color(this.prnd[j], global.lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        global.lv_obj_set_style_border_opa(this.prnd[j], 255, LV_PART_MAIN);
      } else {
        global.lv_obj_set_style_text_color(this.prnd[j], global.lv_color_hex(0x888888), LV_PART_MAIN);
        global.lv_obj_set_style_border_opa(this.prnd[j], 0, LV_PART_MAIN);
      }
      j +<- 1;
    }
```

- [ ] **Step 4: Transpile + build**

Run: `pio run -e waveshare_lcd_21`
Expected: build succeeds.

- [ ] **Step 5: Hardware acceptance check (user)**

Flash and confirm `P R N D 5 4 3 2` appears arced along the bottom. Move the selector → the box tracks the active gear (matching `range.c0` from Task 4). Note any gear whose char doesn't match a sequence slot for the later iteration. Confirm.

- [ ] **Step 6: Commit**

```bash
git add src/display/gauge_trans.cnx
git commit -m "feat(ui): PRND5432 arc with active-gear box"
```

---

## Notes for the implementer

- **C-Next gotchas that apply here:** equality is `=` not `==`; assignment is `<-`; `if` conditions can't contain function calls or bare bools (extract to a `bool` first — the plan already does this); array param syntax is `u8[8] data`. In `.cnx`, use `%d`/`%c`/`%02X` directly in `printf` (no `PRId32` macros).
- **Bit indexing:** `b[4, 2]` reads 2 bits starting at bit 4 (LSB-0). Assign the byte to a `u8` first (as shown) rather than chaining `data[2][4,2]`.
- **f32 → serial/display:** store native units as `f32`, then cast to `i32` for `%d` printing/labels (matches every existing decoder/widget).
- **Iterate freely:** PRND geometry (`x_off`/`y_arc` constants), chip colors, and the cal-specific 2nd-byte handling are all expected to be tuned on hardware — this plan ships the working v1.

## Self-Review

- **Spec coverage:** trans temp (Task 2), telltales (Task 3), range (Task 4), data model (Task 1), trans temp UI (Task 5), chips UI (Task 6), PRND UI (Task 7) — all five signals decode + display covered. Deferred items (requested range SPN 162, gear SPN 523, cal 2nd byte) are explicitly out of scope per spec.
- **Type consistency:** `SignalStore.current.trans_temp_c` (Signal), `.tow_haul/.trans_service/.trans_warning` (Status, `.state`), `.range` (Range, `.c0/.c1`) — used identically in decoders (Tasks 2-4) and UI (Tasks 5-7). Helper names `style_chip`/`make_chip` consistent within Task 6.
- **Placeholders:** none — every code step contains complete C-Next.
