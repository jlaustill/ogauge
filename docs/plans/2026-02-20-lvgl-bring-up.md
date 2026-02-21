# Phase 3: LVGL Bring-up Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Get LVGL v9 rendering to the ST7701S 480x480 RGB panel with CST820 touch input, demoed with a label + interactive arc widget.

**Architecture:** LVGL renders into partial PSRAM buffers (480x48 lines, double-buffered). A flush callback copies dirty regions to the RGB panel via `esp_lcd_panel_draw_bitmap()`. Touch input is polled via our existing CST820 driver and fed to LVGL's input device system. All driven from Arduino `loop()`.

**Tech Stack:** LVGL v9.2, ESP32-S3, PlatformIO + Arduino framework, C-Next (for main/display/touch), C++ (for LVGL port layer)

**Design adjustment:** The original design called for direct framebuffer mode, but `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in this ESP-IDF version (4.4/Arduino ESP32 v2). Partial buffers + flush callback achieve the same result and reuse the proven `esp_lcd_panel_draw_bitmap()` path.

**C-Next limitation:** LVGL callbacks require pointer parameters (`lv_display_t*`, `lv_area_t*`, etc.) which C-Next cannot express. The LVGL port layer is therefore written in plain C++ (`lvgl_port.cpp`), while `main.cnx` calls into it via `extern "C"` functions.

---

### Task 1: Add LVGL library dependency and build flags

**Files:**
- Modify: `platformio.ini`

**Step 1: Add LVGL to platformio.ini**

Add `lib_deps` and the LVGL build flag to the existing environment:

```ini
[env:waveshare_lcd_21]
extra_scripts = pre:cnext_build.py
platform = espressif32
framework = arduino
board = esp32-s3-devkitc-1
monitor_speed = 115200
board_build.arduino.memory_type = qio_opi
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
lib_deps =
    lvgl/lvgl@^9.2
```

**Step 2: Build to verify LVGL downloads**

Run: `pio run -e waveshare_lcd_21`
Expected: LVGL downloads, build may fail (no lv_conf.h yet) — that's fine. Verify the library appears in `.pio/libdeps/`.

**Step 3: Commit**

```bash
git add platformio.ini
git commit -m "Add LVGL v9.2 dependency"
```

---

### Task 2: Create minimal lv_conf.h

**Files:**
- Create: `src/lv_conf.h`

**Step 1: Create lv_conf.h with minimal overrides**

LVGL v9's `lv_conf_internal.h` provides defaults for any setting not defined. We only need to override what matters:

```c
#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable this config file */
#if 1

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16  /* RGB565 for ST7701S */

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 1  /* Use stdlib malloc/free instead of LVGL's internal allocator */

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD 16  /* ~60 FPS refresh target */

/*====================
   OS SETTINGS
 *====================*/
#define LV_USE_OS LV_OS_NONE  /* Single-threaded for now */

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   THEME SETTINGS
 *====================*/
#define LV_USE_THEME_DEFAULT 1

#endif /* #if 1 */

#endif /* LV_CONF_H */
```

**Step 2: Build to verify LVGL compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build (LVGL compiles with our config). May see warnings — that's OK.

**Step 3: Commit**

```bash
git add src/lv_conf.h
git commit -m "Add minimal LVGL v9 config for 480x480 RGB565"
```

---

### Task 3: Create LVGL port layer (C++)

This is the core integration file. It handles LVGL display driver, touch input, and provides a simple API for C-Next to call.

**Files:**
- Create: `src/lvgl_port.h`
- Create: `src/lvgl_port.cpp`

**Step 1: Create the header**

```c
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <esp_lcd_panel_ops.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call after Display.init() to store the panel handle for LVGL's flush callback */
void lvgl_port_set_panel(esp_lcd_panel_handle_t panel);

/* Initialize LVGL: display driver, input driver. Call after lv_init(). */
void lvgl_port_init(void);

/* Call from loop(): feeds elapsed time to LVGL and runs the timer handler */
void lvgl_port_loop(void);

/* Create the demo UI (label + arc widget) */
void lvgl_port_create_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_PORT_H */
```

**Step 2: Create the implementation**

```cpp
#include "lvgl_port.h"
#include "touch_cst820.h"
#include <lvgl.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

#define LCD_WIDTH  480
#define LCD_HEIGHT 480
#define LVGL_BUF_LINES 48  /* Render 48 lines at a time */
#define LVGL_BUF_SIZE (LCD_WIDTH * LVGL_BUF_LINES * sizeof(uint16_t))

static esp_lcd_panel_handle_t s_panel = NULL;
static uint32_t s_last_tick = 0;

/* ---- Display flush callback ---- */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        (void *)px_map);
    lv_display_flush_ready(disp);
}

/* ---- Touch input read callback ---- */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    bool touched = Touch_read();
    if (touched) {
        data->point.x = Touch_get_x();
        data->point.y = Touch_get_y();
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---- Public API ---- */

void lvgl_port_set_panel(esp_lcd_panel_handle_t panel) {
    s_panel = panel;
}

void lvgl_port_init(void) {
    /* Create display */
    lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    /* Allocate double partial buffers in PSRAM */
    void *buf1 = heap_caps_malloc(LVGL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(LVGL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, buf1, buf2, LVGL_BUF_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, disp_flush_cb);

    /* Create touch input device */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    s_last_tick = millis();
}

void lvgl_port_loop(void) {
    uint32_t now = millis();
    uint32_t elapsed = now - s_last_tick;
    s_last_tick = now;
    lv_tick_inc(elapsed);
    lv_timer_handler();
}

void lvgl_port_create_demo(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    /* Title label */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "OGauge");
    lv_obj_set_style_text_color(label, lv_color_hex(0xeaf6f6), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -60);

    /* Interactive arc */
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 300, 300);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 40);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x16c79a), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 20, LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 30);

    /* Value label under the arc */
    lv_obj_t *val_label = lv_label_create(scr);
    lv_label_set_text(val_label, "40");
    lv_obj_set_style_text_color(val_label, lv_color_hex(0x16c79a), 0);
    lv_obj_set_style_text_font(val_label, &lv_font_montserrat_24, 0);
    lv_obj_align(val_label, LV_ALIGN_CENTER, 0, 30);
}
```

**Step 3: Build to verify compilation**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean compile. The port functions aren't called yet but should link.

**Step 4: Commit**

```bash
git add src/lvgl_port.h src/lvgl_port.cpp
git commit -m "Add LVGL port layer: display flush + touch input"
```

---

### Task 4: Expose panel handle from Display scope

The LVGL flush callback needs the `esp_lcd_panel_handle_t` to call `esp_lcd_panel_draw_bitmap()`. The handle is currently private in the Display scope. We need the Display init to pass it to the LVGL port.

**Files:**
- Modify: `src/display_st7701.cnx`

**Step 1: Add lvgl_port.h include and set_panel call**

After the `#include` block at the top, add:
```
#include "lvgl_port.h"
```

In `Display.init()`, after `this.rgb_panel_init()`, add:
```
global.lvgl_port_set_panel(this.panel_handle);
```

So `init()` becomes:
```cnx
public void init() {
    this.reset();
    this.spi_init();
    this.st7701_init();
    this.rgb_panel_init();
    global.lvgl_port_set_panel(this.panel_handle);
    this.backlight_init();
}
```

**Step 2: Build to verify C-Next handles the panel handle pass-through**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. C-Next transpiles the call to `lvgl_port_set_panel(Display_panel_handle)`.

If C-Next rejects passing `esp_lcd_panel_handle_t` as a function argument, fallback: declare `extern esp_lcd_panel_handle_t Display_panel_handle;` in lvgl_port.cpp and remove the `static` keyword from the generated display_st7701.cpp by making panel_handle a public scope variable. (Try the clean approach first.)

**Step 3: Commit**

```bash
git add src/display_st7701.cnx src/display_st7701.cpp src/display_st7701.h
git commit -m "Expose panel handle to LVGL port layer"
```

---

### Task 5: Update main.cnx for LVGL loop

Replace the Phase 2 color-cycle demo with LVGL initialization and loop.

**Files:**
- Modify: `src/main.cnx`

**Step 1: Rewrite main.cnx**

```cnx
#include <Arduino.h>
#include <lvgl.h>
#include "display_st7701.cnx"
#include "touch_cst820.cnx"
#include "lvgl_port.h"

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
  lv_init();
  lvgl_port_init();
  lvgl_port_create_demo();

  Serial.println("Ready!");
}

void loop() {
  lvgl_port_loop();
  delay(5);
}
```

**Step 2: Build**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. Everything links: C-Next scopes + LVGL port + LVGL library.

**Step 3: Commit**

```bash
git add src/main.cnx src/main.cpp src/main.h
git commit -m "Wire up LVGL loop in main: init, demo, tick"
```

---

### Task 6: Flash and verify on hardware

**Step 1: Flash**

Run: `pio run -e waveshare_lcd_21 -t upload`

**Step 2: Open serial monitor**

Run: `pio device monitor` (from a terminal)

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

**Step 4: Commit all remaining generated files**

```bash
git add -A
git commit -m "Phase 3: LVGL bring-up — label + arc demo verified on hardware"
```

---

## Troubleshooting

**If LVGL shows nothing:** Check that `lvgl_port_set_panel()` was called before `lvgl_port_init()`. Add `Serial.println()` in the flush callback to verify it's being called.

**If touch doesn't work in LVGL:** The touch read callback calls `Touch_read()` which returns bool. Verify coordinates are in 0-479 range. Add serial prints in the touch callback.

**If build fails on lv_conf.h:** Ensure `-DLV_CONF_INCLUDE_SIMPLE` is in build_flags. The `lv_conf.h` must be in `src/` (same directory as the other source files) for PlatformIO's include path to find it.

**If C-Next rejects `lvgl_port_set_panel(this.panel_handle)`:** The panel handle type is an opaque pointer. Workaround: in `lvgl_port.cpp`, declare `extern esp_lcd_panel_handle_t Display_panel_handle;` and manually remove `static` from the transpiled `display_st7701.cpp` (then prevent the transpiler from overwriting it by moving the Display scope to pure C++). Simpler: just try it first — C-Next already uses this type as a scope variable.

**If LVGL buffers cause memory issues:** Reduce `LVGL_BUF_LINES` from 48 to 24 (halves buffer size from ~92KB to ~46KB total).
