# Phase 3: LVGL Bring-up Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Get LVGL v9 rendering to the ST7701S 480x480 RGB panel with CST820 touch input, demoed with a label + interactive arc widget.

**Architecture:** LVGL renders into partial PSRAM buffers (480x48 lines, double-buffered). A flush callback copies dirty regions to the RGB panel via `esp_lcd_panel_draw_bitmap()`. Touch input is polled via our existing CST820 driver and fed to LVGL's input device system. All driven from Arduino `loop()`.

**Tech Stack:** LVGL v9.2, ESP32-S3, PlatformIO + Arduino framework, C-Next throughout

**Design adjustment:** The original design called for direct framebuffer mode, but `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in this ESP-IDF version (4.4/Arduino ESP32 v2). Partial buffers + flush callback achieve the same result and reuse the proven `esp_lcd_panel_draw_bitmap()` path.

**C-Next note:** C-Next passes all parameters by reference automatically (ADR-006). A scope function `void flush(lv_display_t disp, const lv_area_t area, u8 px_map)` transpiles to `void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)` — exactly matching LVGL's expected callback signatures. Callbacks use the Function-as-Type Pattern (ADR-029). Nullable C pointers from `heap_caps_malloc` use the `c_` prefix (ADR-046).

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

### Task 3: Create LVGL port layer (C-Next)

This is the core integration file. It handles LVGL display driver, touch input driver, and provides a simple API for main.cnx to call. Written in C-Next — the transpiler auto-generates the pointer-based C signatures LVGL expects.

**Files:**
- Create: `src/lvgl_port.cnx`

**Step 1: Create the LVGL port scope**

```cnx
#include "tca9554.cnx"
#include "touch_cst820.cnx"
#include <lvgl.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>

scope LvglPort {
  const i32 LCD_WIDTH <- 480;
  const i32 LCD_HEIGHT <- 480;
  const i32 BUF_LINES <- 48;

  esp_lcd_panel_handle_t panel;
  u32 last_tick;

  void disp_flush(lv_display_t disp, const lv_area_t area, u8 px_map) {
    global.esp_lcd_panel_draw_bitmap(this.panel,
      area.x1, area.y1,
      area.x2 + 1, area.y2 + 1,
      px_map);
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

  public void set_panel(esp_lcd_panel_handle_t p) {
    this.panel <- p;
  }

  public void init() {
    u32 buf_size <- this.LCD_WIDTH * this.BUF_LINES * 2;

    lv_display_t c_disp <- global.lv_display_create(this.LCD_WIDTH, this.LCD_HEIGHT);
    void c_buf1 <- global.heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    void c_buf2 <- global.heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    global.lv_display_set_buffers(c_disp, c_buf1, c_buf2, buf_size,
                                  LV_DISPLAY_RENDER_MODE_PARTIAL);
    global.lv_display_set_flush_cb(c_disp, this.disp_flush);

    lv_indev_t c_indev <- global.lv_indev_create();
    global.lv_indev_set_type(c_indev, LV_INDEV_TYPE_POINTER);
    global.lv_indev_set_read_cb(c_indev, this.touch_read);

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
    lv_obj_t c_scr <- global.lv_screen_active();
    global.lv_obj_set_style_bg_color(c_scr, global.lv_color_hex(0x1a1a2e), 0);

    lv_obj_t c_label <- global.lv_label_create(c_scr);
    global.lv_label_set_text(c_label, "OGauge");
    global.lv_obj_set_style_text_color(c_label, global.lv_color_hex(0xeaf6f6), 0);
    global.lv_obj_set_style_text_font(c_label, lv_font_montserrat_32, 0);
    global.lv_obj_align(c_label, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t c_arc <- global.lv_arc_create(c_scr);
    global.lv_obj_set_size(c_arc, 300, 300);
    global.lv_arc_set_range(c_arc, 0, 100);
    global.lv_arc_set_value(c_arc, 40);
    global.lv_obj_set_style_arc_color(c_arc, global.lv_color_hex(0x0f3460), LV_PART_MAIN);
    global.lv_obj_set_style_arc_color(c_arc, global.lv_color_hex(0x16c79a), LV_PART_INDICATOR);
    global.lv_obj_set_style_arc_width(c_arc, 20, LV_PART_MAIN);
    global.lv_obj_set_style_arc_width(c_arc, 20, LV_PART_INDICATOR);
    global.lv_obj_align(c_arc, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t c_val <- global.lv_label_create(c_scr);
    global.lv_label_set_text(c_val, "40");
    global.lv_obj_set_style_text_color(c_val, global.lv_color_hex(0x16c79a), 0);
    global.lv_obj_set_style_text_font(c_val, lv_font_montserrat_24, 0);
    global.lv_obj_align(c_val, LV_ALIGN_CENTER, 0, 30);
  }
}
```

**Note on `c_` prefix:** Variables holding nullable C library return values use `c_` prefix per ADR-046 (e.g., `c_disp`, `c_buf1`). The `lv_display_create()`, `heap_caps_malloc()`, and `lv_indev_create()` functions can return NULL.

**Note on callback registration:** `this.disp_flush` passes the scope function to `lv_display_set_flush_cb()`. C-Next transpiles `LvglPort_disp_flush` which has the correct pointer signature that LVGL expects.

**Step 2: Build to verify compilation**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean compile. Verify the transpiled `lvgl_port.cpp` has correct pointer signatures.

**Step 3: Commit**

```bash
git add src/lvgl_port.cnx src/lvgl_port.cpp src/lvgl_port.h
git commit -m "Add LVGL port layer: display flush + touch input"
```

---

### Task 4: Expose panel handle from Display scope

The LVGL flush callback needs the `esp_lcd_panel_handle_t` to call `esp_lcd_panel_draw_bitmap()`. The handle is currently private in the Display scope. We need the Display init to pass it to the LVGL port.

**Files:**
- Modify: `src/display_st7701.cnx`

**Step 1: Add set_panel call in Display.init()**

In `Display.init()`, after `this.rgb_panel_init()`, add:
```cnx
global.LvglPort.set_panel(this.panel_handle);
```

So `init()` becomes:
```cnx
public void init() {
    this.reset();
    this.spi_init();
    this.st7701_init();
    this.rgb_panel_init();
    global.LvglPort.set_panel(this.panel_handle);
    this.backlight_init();
}
```

Also add to the include block at the top:
```cnx
#include "lvgl_port.cnx"
```

**Note:** This creates an include dependency: display_st7701.cnx includes lvgl_port.cnx, which includes touch_cst820.cnx, which includes tca9554.cnx, which includes i2c_driver.cnx. This chain is fine — C-Next handles the include guard via transpiled `#ifndef` headers.

**Step 2: Build to verify C-Next handles the panel handle pass-through**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. C-Next transpiles the call to `LvglPort_set_panel(Display_panel_handle)`.

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
  LvglPort.init();
  LvglPort.create_demo();

  Serial.println("Ready!");
}

void loop() {
  LvglPort.loop();
  delay(5);
}
```

**Note:** No separate `#include "lvgl_port.cnx"` needed — it's already included transitively via `display_st7701.cnx`.

**Step 2: Build**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build. Everything links: C-Next scopes + LVGL library.

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

**If C-Next rejects LVGL types:** LVGL types like `lv_display_t`, `lv_area_t`, `lv_indev_data_t` are C structs. C-Next should handle them via the `#include <lvgl.h>` header. If the transpiler doesn't recognize a type, check whether the LVGL header is being found (verify `-DLV_CONF_INCLUDE_SIMPLE` is in build_flags).

**If callback registration fails:** The key line is `global.lv_display_set_flush_cb(c_disp, this.disp_flush)`. If C-Next can't pass a scope function as a callback argument, we may need to define the callback function at file scope (outside the scope block) or use ADR-029's Function-as-Type pattern explicitly.

**If LVGL shows nothing:** Check that `LvglPort.set_panel()` was called before `LvglPort.init()`. Add `global.Serial.println()` in the flush callback to verify it's being called.

**If touch doesn't work in LVGL:** The touch read callback calls `Touch.read()` which returns bool. Verify coordinates are in 0-479 range. Add serial prints in the touch callback.

**If build fails on lv_conf.h:** Ensure `-DLV_CONF_INCLUDE_SIMPLE` is in build_flags. The `lv_conf.h` must be in `src/` (same directory as the other source files) for PlatformIO's include path to find it.

**If LVGL buffers cause memory issues:** Reduce `BUF_LINES` from 48 to 24 (halves buffer size from ~92KB to ~46KB total).

**If `const lv_area_t area` doesn't compile in C-Next:** The `const` qualifier on a pass-by-reference parameter means the transpiler generates `const lv_area_t *area`. If C-Next doesn't support `const` on parameters, remove it — the flush callback will still work, just without the const qualifier in the generated C.
