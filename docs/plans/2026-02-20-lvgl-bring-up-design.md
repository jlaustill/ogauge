# Phase 3: LVGL Minimal Bring-up

## Goal

Get LVGL v9 rendering to the ST7701S RGB panel with CST820 touch input working through LVGL's input device system. Demo: an LVGL screen with a label ("OGauge") and an arc widget that responds to touch drag.

## Architecture

```
CST820 touch poll --> LVGL indev callback
                          |
                      LVGL core
                          |
                    partial buffers (PSRAM, double-buffered)
                          |
                    flush callback
                          |
                    Display.draw_bitmap()
                          |
                    esp_lcd_panel_draw_bitmap()
```

LVGL renders into two 480x48-line PSRAM buffers (static arrays, ~45KB each). On flush, the dirty region is copied to the RGB panel via `esp_lcd_panel_draw_bitmap()`. The ESP32-S3 LCD peripheral continuously DMA-scans the panel's framebuffer to the display.

**Why not direct framebuffer?** `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in ESP-IDF 4.4 / Arduino ESP32 v2. Partial buffers + flush achieve the same result using the proven `esp_lcd_panel_draw_bitmap()` path.

## 4-Layer Architecture

```
display_st7701.cnx  — owns ST7701S panel + exposes draw_bitmap()
touch_cst820.cnx    — owns CST820 touch + exposes read()/get_x()/get_y()
lvgl_port.cnx       — thin LVGL glue (display/indev setup, callbacks)
main.cnx            — orchestrator (init sequence, loop, demo content)
```

Each layer owns its hardware. Don't duplicate access across layers.

## Components

### 1. LVGL Library

Add `lvgl/lvgl@^9` to `platformio.ini` lib_deps. Create `lv_conf.h` with minimal config:
- 480x480 resolution
- RGB565 color depth (16-bit)
- Partial render mode (double-buffered)
- No filesystem, no GPU, no logging

### 2. LVGL Port (`src/lvgl_port.cnx`)

Scope `LvglPort` — thin glue layer:
- Static double buffers: `u8[46080] buf1, buf2` (480 * 48 * 2 bytes each)
- Flush callback: calls `Display.draw_bitmap()` then `lv_display_flush_ready()`
- Touch callback: polls `Touch.read()` and fills `lv_indev_data_t`
- `init()`: creates LVGL display + indev, sets buffers and callbacks
- `loop()`: drives `lv_tick_inc()` + `lv_timer_handler()`
- `create_demo()`: builds label + arc widget

### 3. Display Extension (`src/display_st7701.cnx`)

Add public `draw_bitmap()` to existing Display scope:
- Wraps `esp_lcd_panel_draw_bitmap()` with the private panel handle
- Keeps panel handle encapsulated — lvgl_port never touches it directly

### 4. Main Loop (updated `src/main.cnx`)

- `setup()`: init hardware -> `lv_init()` -> `LvglPort.init()` -> `LvglPort.create_demo()`
- `loop()`: `LvglPort.loop()` + ~5ms delay
- Replaces the Phase 2 color-cycle demo entirely

## Files Changed

| File | Action |
|------|--------|
| `platformio.ini` | Add `lib_deps = lvgl/lvgl@^9` — **DONE** |
| `src/lv_conf.h` | New — LVGL configuration — **DONE** |
| `src/lvgl_port.cnx` | New — display + touch glue — **BLOCKED on C-Next #883** |
| `src/display_st7701.cnx` | Add public `draw_bitmap()` — **BLOCKED on C-Next #883** |
| `src/main.cnx` | Replace color-cycle with LVGL loop — blocked on above |

## What Stays the Same

- I2C driver, TCA9554, ST7701S init, CST820 touch — all reused as-is
- Backlight, reset sequences unchanged
- Hardware init order unchanged (I2C -> TCA9554 -> Display -> Touch)

## Blocker: C-Next Bug #883

The transpiler generates incorrect C signatures for callback functions and opaque type usage:
- Opaque types (`lv_display_t`, `lv_indev_t`) transpile as values instead of pointers
- Primitive buffer params (`u8 px_map`) transpile as `uint8_t` instead of `uint8_t *`
- Non-opaque structs (`lv_area_t`) correctly transpile as pointers (ADR-006 works)

Minimal repro: `/tmp/cnext-bugs/883-callback-pointer-sig/`

**No workarounds.** Phase 3 resumes when #883 is fixed.

## Decisions

- **Partial buffers** over direct framebuffer: ESP-IDF 4.4 lacks `get_frame_buffer()` API
- **Static arrays** over `heap_caps_malloc`: ADR-003 forbids dynamic allocation
- **Arduino loop()** over FreeRTOS task: sufficient for bring-up, defer threading to CAN phase
- **Minimal demo** over widget development: prove the plumbing, build widgets later
- **4-layer split** over monolithic: each layer owns its hardware, no duplication
