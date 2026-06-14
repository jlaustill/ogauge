# Boost Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture turbo boost (SPN 102) from PGN 65270 and display it in psi at the top-left of the round dashboard, mirroring the bottom-left trans temp readout.

**Architecture:** Boost rides in PGN 65270 — the frame already decoded for EGT (gated to `SA_SENSOR`). Add one byte-extraction to the existing `decode_65270`, store it natively in kPa in `SignalData`, and render it as a new white label pair in the `GaugeTrans` scope (which owns the left column). Convert kPa→psi at display time, per the existing storage convention.

**Tech Stack:** C-Next (`.cnx`, auto-transpiled by `cnext_build.py`), LVGL v9, PlatformIO (ESP32-S3). Spec: `docs/superpowers/specs/2026-06-14-boost-display-design.md`.

**Verification reality:** No unit-test framework. Per-task verification is `pio run -e waveshare_lcd_21` (transpile + compile success). Final behavior is verified on hardware (flash + observe). The truck must be running for live boost data (SA_SENSOR / OSSM module broadcasting PGN 65270).

---

### Task 1: Capture boost (storage + decode)

**Files:**
- Modify: `src/data/signal_data.cnx` (add field to `SignalData` struct)
- Modify: `src/data/j1939_decoder.cnx` (extend `decode_65270`)

- [ ] **Step 1: Add the storage field**

In `src/data/signal_data.cnx`, add `boost_kpa` to the `SignalData` struct, immediately after the `fuel_pressure_kpa` line (groups it with the other pressures):

```cnx
  Signal fuel_pressure_kpa;
  Signal boost_kpa;
```

- [ ] **Step 2: Decode SPN 102 in the existing PGN 65270 handler**

In `src/data/j1939_decoder.cnx`, inside `decode_65270`, add the SPN 102 extraction right after the existing EGT (SPN 173) lines, before the closing `}`. Boost is byte 1, 1 byte, 2 kPa/bit — single byte, no `extract_u16` needed:

```cnx
    // SPN 102 - Engine Intake Manifold #1 Pressure (boost): byte 1, 1 byte, 2 kPa/bit
    u8 raw102 <- data[1];
    SignalStore.current.boost_kpa.value <- raw102 * 2.0;
    SignalStore.current.boost_kpa.time <- global.millis();
```

The method is already routed (`if (pgn = 65270 && sa = SA_SENSOR) { this.decode_65270(data); }`), so no change to `decode()`.

- [ ] **Step 3: Build to verify it transpiles and compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: `[SUCCESS]`. (The pre-build hook transpiles `.cnx`→`.cpp`; a C-Next syntax error would fail here.)

- [ ] **Step 4: Commit**

```bash
git add src/data/signal_data.cnx src/data/signal_data.cpp src/data/j1939_decoder.cnx src/data/j1939_decoder.cpp include/data/signal_data.hpp
git commit -m "feat(boost): capture SPN 102 boost pressure from PGN 65270

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Display the boost readout

**Files:**
- Modify: `src/display/gauge_trans.cnx` (scope fields, `create()`, `update()`)

- [ ] **Step 1: Add the two label fields to the GaugeTrans scope**

In `src/display/gauge_trans.cnx`, add to the scope's field declarations, right after `lv_obj_t warn_chip;`:

```cnx
  lv_obj_t boost_title;
  lv_obj_t boost_val;
```

- [ ] **Step 2: Create the labels at the top-left (mirror of trans temp)**

In `create()`, after the trans temp `temp_val` styling block (the line `global.lv_obj_set_style_border_opa(this.temp_val, 0, LV_PART_MAIN);`), add the boost labels. The trans temp sits at `LEFT_MID, +81` (value) / `+38` (title); boost is the vertical mirror at `-81` / `-38`, value on top, white, no border:

```cnx
    // BOOST — top-left, vertical mirror of the bottom-left trans temp. White,
    // no warning box (boost is informational). Value on top, label below.
    this.boost_val <- global.lv_label_create(scr);
    global.lv_label_set_text(this.boost_val, "---- psi");
    global.lv_obj_align(this.boost_val, LV_ALIGN_LEFT_MID, 36, -81);
    global.lv_obj_set_style_text_font(this.boost_val, lv_font_montserrat_40, LV_PART_MAIN);
    global.lv_obj_set_style_text_color(this.boost_val, global.lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    this.boost_title <- global.lv_label_create(scr);
    global.lv_label_set_text(this.boost_title, "BOOST");
    global.lv_obj_align(this.boost_title, LV_ALIGN_LEFT_MID, 31, -38);
    global.lv_obj_set_style_text_font(this.boost_title, lv_font_montserrat_32, LV_PART_MAIN);
    global.lv_obj_set_style_text_color(this.boost_title, global.lv_color_hex(0xFFFFFF), LV_PART_MAIN);
```

- [ ] **Step 3: Update the readout each tick (staleness + kPa→psi)**

In `update()`, after the trans temp block (the line `global.lv_obj_set_style_border_opa(this.temp_val, temp_box, LV_PART_MAIN);`), add the boost update. Uses the shared `SIGNAL_STALE_MS` and the existing `now`; no `warn_level` call:

```cnx
    // BOOST — convert native kPa to psi for display; no warning styling.
    u32 boost_age <- now - SignalStore.current.boost_kpa.time;
    if (boost_age > SIGNAL_STALE_MS) {
      global.lv_label_set_text(this.boost_val, "---- psi");
    } else {
      i32 psi <- SignalStore.current.boost_kpa.value * 0.145038;
      global.lv_label_set_text_fmt(this.boost_val, "%d psi", psi);
    }
```

(`f32`→`i32` truncation on assignment is the established pattern here — see the odo `* 0.621371` line in `gauge_temp.cnx`.)

- [ ] **Step 4: Build to verify it transpiles and compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/display/gauge_trans.cnx src/display/gauge_trans.cpp include/display/gauge_trans.hpp
git commit -m "feat(boost): display boost in psi at top-left of dashboard

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Flash and verify on hardware

**Files:** none (hardware verification + optional position nudge)

- [ ] **Step 1: Flash the device**

Run: `pio run -e waveshare_lcd_21 -t upload`
Expected: `[SUCCESS]`, `Hash of data verified`, `Hard resetting`.

- [ ] **Step 2: Observe on hardware (truck running)**

Confirm with the user that the truck is running (SA_SENSOR must broadcast PGN 65270). Observe the dashboard:
- BOOST appears top-left, mirroring the bottom-left TRANS readout.
- Reads ~0 psi at idle, climbs under load (stock 5.9 Cummins peaks ~30–40 psi).
- Shows `---- psi` if the signal is stale/absent.

- [ ] **Step 3: Nudge position if needed (likely)**

Top-left is crowded near the SVC chip and the circular bezel. If BOOST overlaps the chips or clips the bezel, adjust the `LV_ALIGN_LEFT_MID` x/y offsets in `create()` (Task 2, Step 2) and re-flash. Per the round-display UI workflow, expect 1–2 small iterations. When placement looks right, commit the nudge:

```bash
git add src/display/gauge_trans.cnx src/display/gauge_trans.cpp
git commit -m "fix(boost): nudge top-left position to clear SVC chip/bezel

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

- **Spec coverage:** storage field (Task 1.1) ✓; decode SPN 102 byte 1 @ 2 kPa/bit (Task 1.2) ✓; psi display (Task 2.3) ✓; top-left mirror placement at `-81`/`-38` (Task 2.2) ✓; no warnings — no `warn_level` call (Task 2.3) ✓; verification + nudge (Task 3) ✓.
- **Type consistency:** `boost_kpa` (`Signal`, `.value` f32 / `.time` u32) used identically in decoder and display; `boost_val`/`boost_title` (`lv_obj_t`) named consistently across `create()` and `update()`.
- **Out of scope** (warnings, other 65270 SPNs, unit toggle) — correctly absent from all tasks.
