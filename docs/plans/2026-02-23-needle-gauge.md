# Phase 4: Needle Gauge Widget Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a touch-interactive speedometer needle gauge (0-160 mph) on the 480x480 round display using LVGL v9.2's scale + arc widgets, with colored speed zones.

**Architecture:** `lv_scale` (ROUND_INNER mode) provides the gauge face with ticks/labels. An invisible `lv_arc` overlays it for native touch input. On touch, the arc fires `LV_EVENT_VALUE_CHANGED`, and a callback updates the line needle position and center speed label. Color-coded sections (green/yellow/red) are set up via a C helper function (since `lv_style_t` and `lv_color_t` require pass-by-value which C-Next can't do through `global.` calls).

**Tech Stack:** LVGL v9.2 (scale, arc, line, label widgets), ESP32-S3, PlatformIO + Arduino, C-Next v0.2.9+

**Key constraints:**
- `lv_scale_mode_t` enum values are behind `#if LV_USE_SCALE` — transpiler can't see them. Use numeric constants.
- `lv_color_t` is a struct passed by value in style functions — C-Next adds `*` via ADR-006. Use C helpers.
- `lv_style_t` init requires `&` on scope variables — `global.` calls don't add `&`. Use C helpers.
- `lv_label_set_text_fmt` is variadic — C-Next may not handle `...` args. Use C helper.
- LVGL widget creation functions are behind `#if LV_USE_*` guards — add to `lvgl_helpers.h` (bug #945 workaround).

---

## Status

| Task | Status | Notes |
|------|--------|-------|
| Task 1: Enable widgets in lv_conf.h | | |
| Task 2: Expand lvgl_helpers.h | | |
| Task 3: Create gauge_speed.cnx — static gauge | | |
| Task 4: Add arc overlay + touch callback | | |
| Task 5: Add colored speed zone sections | | |
| Task 6: Update main.cnx | | |
| Task 7: Transpile, inspect, build | | |
| Task 8: Flash and verify on hardware | | |

---

### Task 1: Enable LVGL widgets in lv_conf.h

**Files:**
- Modify: `src/lv_conf.h`

**Step 1: Add widget enables and font**

Add to the WIDGET SETTINGS section (after `LV_USE_LABEL 1`):

```c
#define LV_USE_SCALE 1
#define LV_USE_ARC   1
#define LV_USE_LINE  1
```

Add to the FONT SETTINGS section (after `LV_FONT_MONTSERRAT_32 1`):

```c
#define LV_FONT_MONTSERRAT_40 1
```

**Step 2: Build to verify config compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. LVGL now compiles with scale, arc, line widgets and Montserrat 40 font.

**Step 3: Commit**

```
git add src/lv_conf.h
git commit -m "Enable LVGL scale, arc, line widgets and Montserrat 40 font"
```

---

### Task 2: Expand lvgl_helpers.h

**Files:**
- Modify: `src/lvgl_helpers.h`

**Why:** The C-Next transpiler's preprocessor can't evaluate `#if LV_USE_SCALE != 0` guards in LVGL headers (bug #945). Widget-specific functions are invisible. We re-declare them here. We also add C helpers for `lv_color_t` (value-type struct) and `lv_style_t` (needs `&` for pointer) which C-Next can't pass correctly through `global.` calls.

**Step 1: Replace entire file contents**

```c
/**
 * LVGL function declarations for C-Next transpiler.
 *
 * The transpiler's preprocessor can't evaluate LVGL's #if LV_USE_* guards,
 * so widget creation functions are invisible to C-Next (bug #945).
 * This header re-declares them so the transpiler knows the return types.
 *
 * Also provides C helpers for:
 * - lv_color_t params (value-type struct — C-Next adds * via ADR-006)
 * - lv_style_t init (needs & on scope variables — global. doesn't add &)
 * - Variadic functions (lv_label_set_text_fmt)
 */
#ifndef LVGL_HELPERS_H
#define LVGL_HELPERS_H

#include <lvgl.h>
#include <inttypes.h>

/* ─── Label widget (behind #if LV_USE_LABEL) ─── */

lv_obj_t * lv_label_create(lv_obj_t * parent);
void lv_label_set_text(lv_obj_t * obj, const char * text);

/* ─── Scale widget (behind #if LV_USE_SCALE) ─── */

lv_obj_t * lv_scale_create(lv_obj_t * parent);
void lv_scale_set_mode(lv_obj_t * obj, lv_scale_mode_t mode);
void lv_scale_set_total_tick_count(lv_obj_t * obj, uint32_t total_tick_count);
void lv_scale_set_major_tick_every(lv_obj_t * obj, uint32_t major_tick_every);
void lv_scale_set_label_show(lv_obj_t * obj, bool show_label);
void lv_scale_set_range(lv_obj_t * obj, int32_t min, int32_t max);
void lv_scale_set_angle_range(lv_obj_t * obj, uint32_t angle_range);
void lv_scale_set_rotation(lv_obj_t * obj, int32_t rotation);
void lv_scale_set_line_needle_value(lv_obj_t * obj, lv_obj_t * needle_line,
                                     int32_t needle_length, int32_t value);
lv_scale_section_t * lv_scale_add_section(lv_obj_t * obj);
void lv_scale_set_section_range(lv_obj_t * scale, lv_scale_section_t * section,
                                 int32_t min, int32_t max);
void lv_scale_set_section_style_main(lv_obj_t * scale, lv_scale_section_t * section,
                                      const lv_style_t * style);
void lv_scale_set_section_style_indicator(lv_obj_t * scale, lv_scale_section_t * section,
                                           const lv_style_t * style);
void lv_scale_set_section_style_items(lv_obj_t * scale, lv_scale_section_t * section,
                                       const lv_style_t * style);

/* ─── Arc widget (behind #if LV_USE_ARC) ─── */

lv_obj_t * lv_arc_create(lv_obj_t * parent);
void lv_arc_set_range(lv_obj_t * obj, int32_t min, int32_t max);
void lv_arc_set_value(lv_obj_t * obj, int32_t value);
void lv_arc_set_bg_angles(lv_obj_t * obj, lv_value_precise_t start, lv_value_precise_t end);
void lv_arc_set_rotation(lv_obj_t * obj, int32_t rotation);
int32_t lv_arc_get_value(const lv_obj_t * obj);

/* ─── Line widget (behind #if LV_USE_LINE) ─── */

lv_obj_t * lv_line_create(lv_obj_t * parent);

/* ─── C helpers: lv_color_t value-type workaround ─── */

/*
 * C-Next passes all non-primitive params by reference (ADR-006), but LVGL's
 * style functions take lv_color_t by value. These helpers take uint32_t hex
 * instead, avoiding the pointer mismatch.
 */

static inline void lvgl_helper_obj_style_line_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_line_color(obj, lv_color_hex(hex), sel);
}

static inline void lvgl_helper_obj_style_bg_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(hex), sel);
}

static inline void lvgl_helper_obj_style_text_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(hex), sel);
}

/* ─── C helper: variadic label format + PRId32 ─── */

static inline void lvgl_helper_label_set_speed(lv_obj_t * label, int32_t speed)
{
    lv_label_set_text_fmt(label, "%" PRId32 " mph", speed);
}

/* ─── C helper: speed zone sections ─── */

/*
 * Encapsulates lv_style_t init + lv_color_t + section API in pure C.
 * C-Next can't call these directly because:
 * - lv_style_init needs & on scope variables (global. doesn't add &)
 * - lv_style_set_arc_color takes lv_color_t by value (ADR-006 adds *)
 */

static inline void lvgl_helper_add_speed_sections(lv_obj_t * scale)
{
    static lv_style_t green_main, green_ind, green_items;
    static lv_style_t yellow_main, yellow_ind, yellow_items;
    static lv_style_t red_main, red_ind, red_items;

    /* Green zone: 0-60 mph */
    lv_style_init(&green_main);
    lv_style_set_arc_color(&green_main, lv_color_hex(0x44BB44));
    lv_style_set_arc_width(&green_main, 8);
    lv_style_init(&green_ind);
    lv_style_set_line_color(&green_ind, lv_color_hex(0x44BB44));
    lv_style_init(&green_items);
    lv_style_set_line_color(&green_items, lv_color_hex(0x44BB44));

    lv_scale_section_t * green = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, green, 0, 60);
    lv_scale_set_section_style_main(scale, green, &green_main);
    lv_scale_set_section_style_indicator(scale, green, &green_ind);
    lv_scale_set_section_style_items(scale, green, &green_items);

    /* Yellow zone: 60-100 mph */
    lv_style_init(&yellow_main);
    lv_style_set_arc_color(&yellow_main, lv_color_hex(0xDDCC22));
    lv_style_set_arc_width(&yellow_main, 8);
    lv_style_init(&yellow_ind);
    lv_style_set_line_color(&yellow_ind, lv_color_hex(0xDDCC22));
    lv_style_init(&yellow_items);
    lv_style_set_line_color(&yellow_items, lv_color_hex(0xDDCC22));

    lv_scale_section_t * yellow = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, yellow, 60, 100);
    lv_scale_set_section_style_main(scale, yellow, &yellow_main);
    lv_scale_set_section_style_indicator(scale, yellow, &yellow_ind);
    lv_scale_set_section_style_items(scale, yellow, &yellow_items);

    /* Red zone: 100-160 mph */
    lv_style_init(&red_main);
    lv_style_set_arc_color(&red_main, lv_color_hex(0xDD3333));
    lv_style_set_arc_width(&red_main, 8);
    lv_style_init(&red_ind);
    lv_style_set_line_color(&red_ind, lv_color_hex(0xDD3333));
    lv_style_init(&red_items);
    lv_style_set_line_color(&red_items, lv_color_hex(0xDD3333));

    lv_scale_section_t * red = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, red, 100, 160);
    lv_scale_set_section_style_main(scale, red, &red_main);
    lv_scale_set_section_style_indicator(scale, red, &red_ind);
    lv_scale_set_section_style_items(scale, red, &red_items);
}

#endif /* LVGL_HELPERS_H */
```

**Step 2: Build to verify**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. The helpers are all `static inline` in a header — they only compile when used.

**Step 3: Commit**

```
git add src/lvgl_helpers.h
git commit -m "Expand LVGL helpers: scale/arc/line prototypes, color and section helpers"
```

---

### Task 3: Create gauge_speed.cnx — static gauge face

**Files:**
- Create: `src/gauge_speed.cnx`

**This creates the gauge visual WITHOUT touch interaction. Needle at fixed position (80 mph) to verify rendering.**

**Step 1: Create the file**

```cnx
#include <lvgl.h>
#include "lvgl_helpers.h"

scope GaugeSpeed {
  const u32 SCALE_MODE_ROUND_INNER <- 0x08;

  lv_obj_t scale;
  lv_obj_t needle;
  lv_obj_t speed_label;

  public void create() {
    lv_obj_t scr <- global.lv_screen_active();

    // Scale: 420x420, centered, 270-degree arc
    this.scale <- global.lv_scale_create(scr);
    global.lv_obj_set_size(this.scale, 420, 420);
    global.lv_obj_center(this.scale);
    global.lv_scale_set_mode(this.scale, this.SCALE_MODE_ROUND_INNER);
    global.lv_scale_set_range(this.scale, 0, 160);
    global.lv_scale_set_angle_range(this.scale, 270);
    global.lv_scale_set_rotation(this.scale, 135);
    global.lv_scale_set_total_tick_count(this.scale, 33);
    global.lv_scale_set_major_tick_every(this.scale, 4);
    global.lv_scale_set_label_show(this.scale, true);

    // Tick styling: minor=5px, major=10px
    global.lv_obj_set_style_length(this.scale, 5, LV_PART_ITEMS);
    global.lv_obj_set_style_length(this.scale, 10, LV_PART_INDICATOR);

    // Background: dark circle
    global.lv_obj_set_style_bg_opa(this.scale, LV_OPA_COVER, LV_PART_MAIN);
    global.lvgl_helper_obj_style_bg_color_hex(this.scale, 0x1A1A2E, LV_PART_MAIN);
    global.lv_obj_set_style_radius(this.scale, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    // Line needle: child of scale
    this.needle <- global.lv_line_create(this.scale);
    global.lv_obj_set_style_line_width(this.needle, 6, LV_PART_MAIN);
    global.lv_obj_set_style_line_rounded(this.needle, true, LV_PART_MAIN);
    global.lvgl_helper_obj_style_line_color_hex(this.needle, 0xFF4444, LV_PART_MAIN);

    // Set needle to 80 mph (static demo)
    global.lv_scale_set_line_needle_value(this.scale, this.needle, -10, 80);

    // Center speed label
    this.speed_label <- global.lv_label_create(scr);
    global.lvgl_helper_label_set_speed(this.speed_label, 80);
    global.lv_obj_center(this.speed_label);
    global.lv_obj_set_style_text_font(this.speed_label, lv_font_montserrat_40, LV_PART_MAIN);
    global.lvgl_helper_obj_style_text_color_hex(this.speed_label, 0xFFFFFF, LV_PART_MAIN);
  }
}
```

**Step 2: Transpile and inspect**

Run: `cnext src/gauge_speed.cnx --include src -D LV_CONF_INCLUDE_SIMPLE`

(Or transpile via main.cnx after Task 6. If transpiling standalone, the `#include "lvgl_helpers.h"` must resolve.)

**Critical things to verify in generated `gauge_speed.cpp`:**

1. `GaugeSpeed_scale`, `GaugeSpeed_needle`, `GaugeSpeed_speed_label` are all `lv_obj_t*` (pointer, not value):
   ```c
   static lv_obj_t* GaugeSpeed_scale;
   ```

2. `lv_scale_set_mode` call passes `GaugeSpeed_SCALE_MODE_ROUND_INNER` (the numeric constant, not a pointer):
   ```c
   lv_scale_set_mode(GaugeSpeed_scale, GaugeSpeed_SCALE_MODE_ROUND_INNER);
   ```

3. `lv_scale_set_line_needle_value` passes all four args correctly:
   ```c
   lv_scale_set_line_needle_value(GaugeSpeed_scale, GaugeSpeed_needle, -10, 80);
   ```

4. `lvgl_helper_*` calls are direct C function calls (no `&` wrapping on hex literal args):
   ```c
   lvgl_helper_obj_style_bg_color_hex(GaugeSpeed_scale, 0x1A1A2E, 0);
   ```

5. `lv_font_montserrat_40` is passed without `&` (it's a global C symbol, should pass as `&lv_font_montserrat_40` since the function expects `const lv_font_t *`).

**If any of these fail:** File a C-Next bug with minimal repro. Do NOT write C++ workarounds.

**Step 3: Commit (only if transpilation looks correct)**

```
git add src/gauge_speed.cnx src/gauge_speed.cpp src/gauge_speed.hpp
git commit -m "Add GaugeSpeed scope: static needle gauge at 80 mph"
```

---

### Task 4: Add arc overlay for touch-driven speed

**Files:**
- Modify: `src/gauge_speed.cnx`

**Step 1: Add arc scope variable and event callback**

Add after `lv_obj_t speed_label;`:

```cnx
  lv_obj_t arc;
```

Add a new callback function BEFORE the `create()` function:

```cnx
  void on_arc_changed(lv_event_t e) {
    lv_obj_t target <- global.lv_event_get_target_obj(e);
    i32 speed <- global.lv_arc_get_value(target);
    global.lv_scale_set_line_needle_value(this.scale, this.needle, -10, speed);
    global.lvgl_helper_label_set_speed(this.speed_label, speed);
  }
```

**Step 2: Add arc creation in `create()`, AFTER the speed_label block**

Replace the needle static value line (`global.lv_scale_set_line_needle_value(this.scale, this.needle, -10, 80);`) and add arc setup:

```cnx
    // Invisible arc overlay for touch input
    this.arc <- global.lv_arc_create(scr);
    global.lv_obj_set_size(this.arc, 420, 420);
    global.lv_obj_center(this.arc);
    global.lv_arc_set_range(this.arc, 0, 160);
    global.lv_arc_set_value(this.arc, 0);
    global.lv_arc_set_bg_angles(this.arc, 0, 270);
    global.lv_arc_set_rotation(this.arc, 135);

    // Make arc fully invisible (touch still works)
    global.lv_obj_set_style_arc_opa(this.arc, LV_OPA_TRANSP, LV_PART_MAIN);
    global.lv_obj_set_style_arc_opa(this.arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    global.lv_obj_set_style_bg_opa(this.arc, LV_OPA_TRANSP, LV_PART_KNOB);

    // Wire touch events
    global.lv_obj_add_event_cb(this.arc, this.on_arc_changed, LV_EVENT_VALUE_CHANGED, 0);

    // Set initial needle position
    global.lv_scale_set_line_needle_value(this.scale, this.needle, -10, 0);
    global.lvgl_helper_label_set_speed(this.speed_label, 0);
```

**Step 3: Transpile and inspect**

**Critical things to verify in generated `gauge_speed.cpp`:**

1. `GaugeSpeed_on_arc_changed` signature matches `lv_event_cb_t`:
   ```c
   static void GaugeSpeed_on_arc_changed(lv_event_t* e)
   ```

2. `lv_event_get_target_obj(e)` passes `e` directly (not `&e` or `*e`):
   ```c
   lv_obj_t* target = lv_event_get_target_obj(e);
   ```

3. `lv_arc_get_value(target)` returns `int32_t` and assigns correctly:
   ```c
   int32_t speed = lv_arc_get_value(target);
   ```

4. `lv_obj_add_event_cb` passes `GaugeSpeed_on_arc_changed` as function pointer:
   ```c
   lv_obj_add_event_cb(GaugeSpeed_arc, GaugeSpeed_on_arc_changed, LV_EVENT_VALUE_CHANGED, 0);
   ```

5. Arc opacity calls use `LV_OPA_TRANSP` (0) and `LV_PART_*` constants directly.

**If any of these fail:** File a C-Next bug with minimal repro.

**Step 4: Commit (only if transpilation looks correct)**

```
git add src/gauge_speed.cnx src/gauge_speed.cpp src/gauge_speed.hpp
git commit -m "Add touch-driven arc overlay: drag to set speed"
```

---

### Task 5: Add colored speed zone sections

**Files:**
- Modify: `src/gauge_speed.cnx`

**Step 1: Add section setup call in `create()`**

Add AFTER the tick styling block (after `lv_obj_set_style_length` calls), BEFORE the needle creation:

```cnx
    // Speed zone sections: green 0-60, yellow 60-100, red 100-160
    global.lvgl_helper_add_speed_sections(this.scale);
```

**Step 2: Transpile and inspect**

Verify generated code is a simple direct call:
```c
lvgl_helper_add_speed_sections(GaugeSpeed_scale);
```

**Step 3: Build**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. The inline helper from `lvgl_helpers.h` gets compiled.

**Step 4: Commit**

```
git add src/gauge_speed.cnx src/gauge_speed.cpp src/gauge_speed.hpp
git commit -m "Add colored speed zones: green/yellow/red sections"
```

---

### Task 6: Update main.cnx

**Files:**
- Modify: `src/main.cnx`

**Step 1: Add gauge include and replace demo label**

Replace entire file:

```cnx
#include <Arduino.h>
#include <lvgl.h>
#include "display_st7701.cnx"
#include "touch_cst820.cnx"
#include "lvgl_port.cnx"
#include "gauge_speed.cnx"

void setup() {
  Serial.begin(115200);
  Serial.println("OGauge: Needle gauge demo");

  Serial.println("I2C init...");
  I2C.init();

  Serial.println("TCA9554 init...");
  TCA9554.init(0x00);

  Serial.println("Display init...");
  Display.init();

  Serial.println("Touch init...");
  Touch.init();

  Serial.println("LVGL init...");
  global.lv_init();
  LvglPort.init();

  Serial.println("Gauge init...");
  GaugeSpeed.create();

  Serial.println("Ready!");
}

void loop() {
  LvglPort.loop();
  delay(5);
}
```

**Step 2: Remove `create_demo_label` from lvgl_port.cnx**

In `src/lvgl_port.cnx`, delete the `create_demo_label` function (lines 50-55):

```cnx
  public void create_demo_label() {
    lv_obj_t scr <- global.lv_screen_active();
    lv_obj_t label <- global.lv_label_create(scr);
    global.lv_label_set_text(label, "OGauge");
    global.lv_obj_center(label);
  }
```

**Step 3: Commit**

```
git add src/main.cnx src/main.cpp src/main.hpp src/lvgl_port.cnx src/lvgl_port.cpp src/lvgl_port.hpp
git commit -m "Wire up GaugeSpeed in main, remove demo label"
```

---

### Task 7: Transpile, inspect, and build

**Step 1: Transpile all files**

Run: `pio run -e waveshare_lcd_21`

(The PlatformIO pre-build hook `cnext_build.py` transpiles all `.cnx` files automatically.)

**Step 2: Inspect generated gauge_speed.cpp**

Read `src/gauge_speed.cpp` and verify ALL critical points from Tasks 3 and 4:

1. Scope variables are `lv_obj_t*` (pointers)
2. `lv_scale_set_mode` gets numeric constant
3. `lv_scale_set_line_needle_value` passes 4 args correctly
4. Helper calls are direct (no `&` on hex args)
5. Event callback signature matches `lv_event_cb_t`
6. `lv_event_get_target_obj(e)` passes `e` directly
7. `lv_arc_get_value` returns `int32_t`
8. `lv_obj_add_event_cb` passes function pointer correctly
9. Opacity/part constants are used directly

**Step 3: Inspect main.cpp**

Verify `GaugeSpeed_create()` is called (not `LvglPort_create_demo_label`).

**Step 4: If transpilation issues found**

For each issue:
1. Create minimal repro at `/tmp/cnext-bugs/<issue-number>-<slug>/`
2. File GitHub issue on jlaustill/c-next
3. STOP — do not proceed until fixed (ZERO WORKAROUNDS)

**Step 5: If build errors**

Common issues:
- **`lv_scale_section_t` not found:** Check `#include <lvgl.h>` is in lvgl_helpers.h
- **`lv_font_montserrat_40` not found:** Verify `LV_FONT_MONTSERRAT_40 1` in lv_conf.h
- **`lv_value_precise_t` not found:** This type is in lvgl core. If the transpiler mishandles it, use `i32` instead in the arc bg_angles call.
- **Duplicate symbol errors from helpers:** Verify all helpers are `static inline`
- **Memory too large:** If RAM exceeds 100%, reduce LVGL pool from 128KB to 96KB in lv_conf.h

---

### Task 8: Flash and verify on hardware

**Step 1: Flash**

Run: `pio run -e waveshare_lcd_21 -t upload`

**Step 2: Open serial monitor**

Run: `pio device monitor`

Expected serial output:
```
OGauge: Needle gauge demo
I2C init...
TCA9554 init...
Display init...
Touch init...
LVGL init...
Gauge init...
Ready!
```

**Step 3: Verify display**

Expected: Circular speedometer gauge centered on screen with:
- 270-degree arc from bottom-left to bottom-right
- Major tick marks with labels at 0, 20, 40, 60, 80, 100, 120, 140, 160
- Minor tick marks every 5 mph
- Green arc section 0-60, yellow 60-100, red 100-160
- Red needle line pointing at 0 mph
- White "0 mph" label centered

**Step 4: Verify touch interaction**

- Touch/drag around the gauge perimeter
- Needle should follow touch position smoothly
- Center label should update: "0 mph" → "85 mph" etc.
- Touch at different positions should map correctly to 0-160 range

**Step 5: Commit**

```
git add -A
git commit -m "Phase 4: Needle gauge with touch — verified on hardware"
```

---

## Known Risk Areas

### 1. `lv_font_montserrat_40` reference in C-Next

The expression `lv_font_montserrat_40` in C-Next for `lv_obj_set_style_text_font` needs to pass as `&lv_font_montserrat_40` (the font is a global `lv_font_t` struct, function expects `const lv_font_t *`). If C-Next doesn't handle this correctly, add a helper: `static inline const lv_font_t * lvgl_helper_font_montserrat_40(void) { return &lv_font_montserrat_40; }` and call it from C-Next.

### 2. `lv_scale_mode_t` enum as `u32` constant

We use `const u32 SCALE_MODE_ROUND_INNER <- 0x08;` because the enum is behind `#if LV_USE_SCALE`. The transpiler should emit `static const uint32_t GaugeSpeed_SCALE_MODE_ROUND_INNER = 0x08;`. Passing `uint32_t` to an `lv_scale_mode_t` (enum/int) param should work with implicit conversion, but watch for warnings.

### 3. Event callback `void*` user_data param

`lv_obj_add_event_cb(arc, cb, filter, 0)` — the 4th arg `0` is `void* user_data`. In C++, `0` implicitly converts to `nullptr`. Should work, but if not, use a C helper wrapper.

### 4. Arc overlay must be ON TOP of scale

LVGL renders children in creation order. The arc MUST be created after the scale so it sits on top and receives touch events. If touch doesn't respond, verify the arc is the last object created (or call `lv_obj_move_foreground(arc)`).

### 5. Arc angle alignment with scale

Both must use: `angle_range = 270`, `rotation = 135`. The arc uses `bg_angles(0, 270)` + `rotation(135)`. The scale uses `angle_range(270)` + `rotation(135)`. If the needle position doesn't match the arc's touch angle, check these values are consistent.

---

## Troubleshooting

**If gauge shows nothing:** Check serial for LVGL errors. Verify scale is sized (420x420) and centered. Check that `lv_conf.h` has `LV_USE_SCALE 1`.

**If needle doesn't appear:** Verify `lv_line_create(scale)` — needle must be child of scale, not screen. Check `lv_scale_set_line_needle_value` is called after needle creation.

**If touch doesn't move needle:** Verify arc is created ON TOP (after scale). Check `LV_OPA_TRANSP` on all arc parts. Verify `lv_obj_add_event_cb` uses `LV_EVENT_VALUE_CHANGED`. Add serial print in callback to verify it's called.

**If LVGL crashes:** Check memory pool. With Montserrat 40 font + scale + arc + line + label, 128KB might be tight. Increase to 192KB if needed.

**If sections don't show colors:** The `lvgl_helper_add_speed_sections` helper may not match the scale's tick geometry. Verify section ranges (0-60, 60-100, 100-160) cover the full scale range (0-160).
