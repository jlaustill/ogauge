# Phase 3: LVGL Minimal Bring-up

## Goal

Get LVGL v9 rendering to the ST7701S RGB panel with CST820 touch input working through LVGL's input device system. Demo: an LVGL screen with a label ("OGauge") and an arc widget that responds to touch drag.

## Architecture

```
CST820 touch poll --> LVGL indev callback
                          |
                      LVGL core
                          |
RGB panel framebuffer <-- LVGL display (DIRECT mode)
     |
DMA scans to ST7701S (continuously, no CPU involvement)
```

LVGL draws directly into the RGB panel's PSRAM framebuffer. The ESP32-S3 LCD peripheral continuously DMA-scans this buffer to the display — no flush copy needed.

## Components

### 1. LVGL Library

Add `lvgl/lvgl@^9` to `platformio.ini` lib_deps. Create `lv_conf.h` with minimal config:
- 480x480 resolution
- RGB565 color depth (16-bit)
- Direct render mode
- No filesystem, no GPU, no logging

### 2. Display Driver (`src/lvgl_display.cnx`)

New scope `LvglDisplay`:
- Get the RGB panel's framebuffer via `esp_lcd_rgb_panel_get_frame_buffer()`
- Create LVGL display: `lv_display_create(480, 480)`
- Set buffers: `lv_display_set_buffers(disp, fb, NULL, size, LV_DISPLAY_RENDER_MODE_DIRECT)`
- Flush callback calls `lv_display_flush_ready()` only (buffer IS the display)
- Display needs to expose panel handle from existing Display scope

### 3. Touch Input (`src/lvgl_input.cnx`)

New scope `LvglInput`:
- Create indev: `lv_indev_create()` with type `LV_INDEV_TYPE_POINTER`
- Read callback polls `Touch.read()` and fills `lv_indev_data_t` with x, y, state

### 4. Main Loop (updated `src/main.cnx`)

- `setup()`: init hardware -> `lv_init()` -> init LVGL display -> init LVGL input -> create demo widgets
- `loop()`: call `lv_tick_inc(elapsed_ms)` + `lv_timer_handler()` + ~5ms delay
- Replace the Phase 2 color-cycle demo entirely

### 5. Demo Content

Simple screen proving touch + rendering work:
- Dark background filling the round display
- "OGauge" label centered
- An arc widget (0-100 range) that responds to touch drag

## Files Changed

| File | Action |
|------|--------|
| `platformio.ini` | Add `lib_deps = lvgl/lvgl@^9` |
| `src/lv_conf.h` | New — LVGL configuration |
| `src/lvgl_display.cnx` | New — display driver binding |
| `src/lvgl_input.cnx` | New — touch input binding |
| `src/display_st7701s.cnx` | Expose panel handle + framebuffer getter |
| `src/main.cnx` | Replace color-cycle with LVGL loop |

## What Stays the Same

- I2C driver, TCA9554, ST7701S init, CST820 touch — all reused as-is
- Backlight, reset sequences unchanged
- Hardware init order unchanged (I2C -> TCA9554 -> Display -> Touch)

## Decisions

- **Direct framebuffer** over flush-copy: simpler, no extra RAM, no copy overhead
- **Arduino loop()** over FreeRTOS task: sufficient for bring-up, defer threading to CAN phase
- **Minimal demo** over widget development: prove the plumbing, build widgets later
