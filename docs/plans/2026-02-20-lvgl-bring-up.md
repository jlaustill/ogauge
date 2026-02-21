# Phase 3: LVGL Bring-up Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Get LVGL v9 rendering to the ST7701S 480x480 RGB panel with CST820 touch input, demoed with a label + interactive arc widget.

**Architecture:** LVGL renders into partial PSRAM buffers (480x48 lines, double-buffered, static arrays). A flush callback copies dirty regions to the RGB panel via `Display.draw_bitmap()` → `esp_lcd_panel_draw_bitmap()`. Touch input is polled via our existing CST820 driver and fed to LVGL's input device system. All driven from Arduino `loop()`.

**Tech Stack:** LVGL v9.2, ESP32-S3, PlatformIO + Arduino framework, C-Next throughout

**Key constraints:**
- `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in ESP-IDF 4.4 — use partial buffers + flush
- ADR-003 forbids dynamic allocation — use static `u8[46080]` arrays, not `heap_caps_malloc`
- ADR-006 auto pass-by-reference applies to structs — primitives are pass-by-value
- 4-layer separation: display driver, touch driver, LVGL glue, orchestrator

---

## Status

| Task | Status | Notes |
|------|--------|-------|
| Task 1: LVGL dependency | **DONE** | Committed `1411fef` |
| Task 2: lv_conf.h | **DONE** | Committed `68c7987` |
| Task 3: LVGL port layer | **BLOCKED** | C-Next bug #883 — callback pointer signatures |
| Task 4: Display draw_bitmap | **BLOCKED** | Same bug — `u8 data` transpiles as value not pointer |
| Task 5: Update main.cnx | Blocked on 3+4 | |
| Task 6: Flash and verify | Blocked on 5 | |

**Blocker:** C-Next bug #883 — opaque types and primitive buffer params generate incorrect C signatures in callback contexts. Minimal repro at `/tmp/cnext-bugs/883-callback-pointer-sig/`. No workarounds — fix the transpiler first.

---

### Task 1: Add LVGL library dependency and build flags — DONE

**Committed:** `1411fef`

Added to `platformio.ini`:
```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
lib_deps =
    lvgl/lvgl@^9.2
```

---

### Task 2: Create minimal lv_conf.h — DONE

**Committed:** `68c7987`

Created `src/lv_conf.h` with: RGB565, `LV_MEM_CUSTOM 1`, 60 FPS refresh, Montserrat 14/24/32 fonts, default theme.

---

### Task 3: Create LVGL port layer (C-Next) — BLOCKED on #883

**Files:**
- Create: `src/lvgl_port.cnx`

**What we know works:** The scope structure, static buffer declarations, and function layout are valid C-Next. The blocker is specifically the transpiled output for callback function signatures and opaque type usage.

**Target code (once #883 is fixed):**

```cnx
#include "display_st7701.cnx"
#include "touch_cst820.cnx"
#include <lvgl.h>
#include <Arduino.h>

scope LvglPort {
  const i32 LCD_WIDTH <- 480;
  const i32 LCD_HEIGHT <- 480;
  const i32 BUF_LINES <- 48;
  const i32 BUF_SIZE <- 46080;

  u8[46080] buf1;
  u8[46080] buf2;
  u32 last_tick;

  void disp_flush(lv_display_t disp, const lv_area_t area, u8 px_map) {
    global.Display.draw_bitmap(area.x1, area.y1, area.x2 + 1, area.y2 + 1, px_map);
    global.lv_display_flush_ready(disp);
  }

  void touch_read(lv_indev_t indev, lv_indev_data_t data) {
    bool touched <- global.Touch.read();
    if (touched) {
      data.point.x <- global.Touch.get_x();
      data.point.y <- global.Touch.get_y();
      data.state <- LV_INDEV_STATE_PRESSED;
    } else {
      data.state <- LV_INDEV_STATE_RELEASED;
    }
  }

  public void init() {
    lv_display_t disp <- global.lv_display_create(this.LCD_WIDTH, this.LCD_HEIGHT);
    global.lv_display_set_buffers(disp, this.buf1, this.buf2, this.BUF_SIZE,
                                  LV_DISPLAY_RENDER_MODE_PARTIAL);
    global.lv_display_set_flush_cb(disp, this.disp_flush);

    lv_indev_t indev <- global.lv_indev_create();
    global.lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    global.lv_indev_set_read_cb(indev, this.touch_read);

    this.last_tick <- global.millis();
  }

  public void loop() {
    u32 now <- global.millis();
    u32 elapsed <- now - this.last_tick;
    this.last_tick <- now;
    global.lv_tick_inc(elapsed);
    global.lv_timer_handler();
  }

  public void create_demo() {
    lv_obj_t scr <- global.lv_screen_active();
    global.lv_obj_set_style_bg_color(scr, global.lv_color_hex(0x1a1a2e), 0);

    lv_obj_t label <- global.lv_label_create(scr);
    global.lv_label_set_text(label, "OGauge");
    global.lv_obj_set_style_text_color(label, global.lv_color_hex(0xeaf6f6), 0);
    global.lv_obj_set_style_text_font(label, lv_font_montserrat_32, 0);
    global.lv_obj_align(label, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t arc <- global.lv_arc_create(scr);
    global.lv_obj_set_size(arc, 300, 300);
    global.lv_arc_set_range(arc, 0, 100);
    global.lv_arc_set_value(arc, 40);
    global.lv_obj_set_style_arc_color(arc, global.lv_color_hex(0x0f3460), LV_PART_MAIN);
    global.lv_obj_set_style_arc_color(arc, global.lv_color_hex(0x16c79a), LV_PART_INDICATOR);
    global.lv_obj_set_style_arc_width(arc, 20, LV_PART_MAIN);
    global.lv_obj_set_style_arc_width(arc, 20, LV_PART_INDICATOR);
    global.lv_obj_align(arc, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t val <- global.lv_label_create(scr);
    global.lv_label_set_text(val, "40");
    global.lv_obj_set_style_text_color(val, global.lv_color_hex(0x16c79a), 0);
    global.lv_obj_set_style_text_font(val, lv_font_montserrat_24, 0);
    global.lv_obj_align(val, LV_ALIGN_CENTER, 0, 30);
  }
}
```

**Expected transpiled output for `disp_flush` (once #883 is fixed):**
```c
static void LvglPort_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    Display_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}
```

**What #883 currently generates instead:**
```c
// WRONG: lv_display_t as value (incomplete type), uint8_t as value (not pointer)
static void LvglPort_disp_flush(lv_display_t disp, const lv_area_t *area, uint8_t px_map) {
```

**Verification (once unblocked):**

Run: `pio run -e waveshare_lcd_21`
Check transpiled `lvgl_port.cpp` for correct pointer signatures.

**Commit:**
```bash
git add src/lvgl_port.cnx src/lvgl_port.cpp src/lvgl_port.h
git commit -m "Add LVGL port layer: display flush + touch input"
```

---

### Task 4: Add draw_bitmap to Display scope — BLOCKED on #883

**Files:**
- Modify: `src/display_st7701.cnx`

**Add to Display scope:**

```cnx
public void draw_bitmap(i32 x1, i32 y1, i32 x2, i32 y2, u8 data) {
  global.esp_lcd_panel_draw_bitmap(this.panel_handle, x1, y1, x2, y2, data);
}
```

**Why this is blocked:** The `u8 data` parameter transpiles to `uint8_t data` (single byte value) instead of `uint8_t *data` (buffer pointer). The `esp_lcd_panel_draw_bitmap()` function expects `const void *color_data`. Same root cause as #883 — the transpiler doesn't match primitive params to the expected C function signature.

**Verification (once unblocked):**

Run: `pio run -e waveshare_lcd_21`
Check transpiled `display_st7701.cpp` for `uint8_t *data` in `Display_draw_bitmap` signature.

**Commit:**
```bash
git add src/display_st7701.cnx src/display_st7701.cpp src/display_st7701.h
git commit -m "Add draw_bitmap to Display scope for LVGL flush path"
```

---

### Task 5: Update main.cnx for LVGL loop

Replace the Phase 2 color-cycle demo with LVGL initialization and loop.

**Files:**
- Modify: `src/main.cnx`

**Target code:**

```cnx
#include <Arduino.h>
#include <lvgl.h>
#include "display_st7701.cnx"
#include "touch_cst820.cnx"
#include "lvgl_port.cnx"

void setup() {
  Serial.begin(115200);
  Serial.println("OGauge: LVGL bring-up");

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
  LvglPort.create_demo();

  Serial.println("Ready!");
}

void loop() {
  LvglPort.loop();
  delay(5);
}
```

**Verification:**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build.

**Commit:**
```bash
git add src/main.cnx src/main.cpp src/main.h
git commit -m "Wire up LVGL loop in main: init, demo, tick"
```

---

### Task 6: Flash and verify on hardware

**Step 1: Flash**

Run: `pio run -e waveshare_lcd_21 -t upload`

**Step 2: Open serial monitor**

Run: `pio device monitor`

Expected serial output:
```
OGauge: LVGL bring-up
I2C init...
TCA9554 init...
Display init...
Touch init...
LVGL init...
Ready!
```

**Step 3: Verify display**

Expected: Dark background with "OGauge" label and a teal/green arc widget. Touch-drag on the arc should change its value.

**Step 4: Commit**

```bash
git add -A
git commit -m "Phase 3: LVGL bring-up — label + arc demo verified on hardware"
```

---

## Troubleshooting

**If C-Next rejects LVGL types:** LVGL types like `lv_display_t`, `lv_area_t`, `lv_indev_data_t` are C structs. C-Next should handle them via `#include <lvgl.h>`. If the transpiler doesn't recognize a type, check whether the LVGL header is being found (verify `-DLV_CONF_INCLUDE_SIMPLE` is in build_flags).

**If LVGL shows nothing:** Add `Serial.println()` calls in the flush callback to verify it's being called. Check that `Display.draw_bitmap()` is correctly passing the buffer pointer.

**If touch doesn't work in LVGL:** The touch read callback calls `Touch.read()` which returns bool. Verify coordinates are in 0-479 range. Add serial prints in the touch callback.

**If build fails on lv_conf.h:** Ensure `-DLV_CONF_INCLUDE_SIMPLE` is in build_flags. The `lv_conf.h` must be in `src/` for PlatformIO's include path to find it.

**If LVGL buffers cause memory issues:** Reduce `BUF_LINES` from 48 to 24 (halves buffer size from ~92KB to ~46KB total).
