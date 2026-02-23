# Phase 3: LVGL Minimal Bring-up

## Goal

Get LVGL v9 rendering to the ST7701S RGB panel with CST820 touch input working through LVGL's input device system. Demo: an LVGL screen with a centered "OGauge" label. Proves the entire flush pipeline works.

## Architecture

```
CST820 touch poll --> LVGL indev callback
                          |
                      LVGL core
                          |
                    partial buffers (static, double-buffered)
                          |
                    flush callback
                          |
                    Display.draw_bitmap()
                          |
                    esp_lcd_panel_draw_bitmap()
```

LVGL renders into two 480x48-line static buffers (~45KB each). On flush, the dirty region is copied to the RGB panel via `esp_lcd_panel_draw_bitmap()`.

**Why not direct framebuffer?** `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in ESP-IDF 4.4 / Arduino ESP32 v2. Partial buffers + flush achieve the same result.

## 4-Layer Architecture

```
display_st7701.cnx  — owns ST7701S panel + exposes draw_bitmap()
touch_cst820.cnx    — owns CST820 touch + exposes read()/get_x()/get_y()
lvgl_port.cnx       — thin LVGL glue (display/indev setup, callbacks)
main.cnx            — orchestrator (init sequence, loop, demo content)
```

Each layer owns its hardware. Don't duplicate access across layers.

## LVGL Type Analysis

| LVGL Type | Kind | C-Next Callback Behavior |
|-----------|------|--------------------------|
| `lv_display_t` | Opaque (`typedef struct _lv_display_t`) | → `lv_display_t*` (callback-aware) |
| `lv_indev_t` | Opaque (`typedef struct _lv_indev_t`) | → `lv_indev_t*` (callback-aware) |
| `lv_area_t` | Concrete struct (`{x1,y1,x2,y2}`) | → `lv_area_t*` (ADR-006) |
| `lv_indev_data_t` | Concrete struct (state, point, etc.) | → `lv_indev_data_t*` (ADR-006) |
| `lv_obj_t` | Opaque | `c_` prefix on create returns |

`lv_display_create()` and `lv_indev_create()` return pointers that can be NULL → `c_` prefix required.

## Memory

LVGL uses its built-in allocator on a static pool (no stdlib malloc):
```c
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)   // 128KB
```

## Buffers

```
480 px × 48 lines × 2 bytes (RGB565) = 46,080 bytes per buffer
Two buffers → 92,160 bytes total (static u8 arrays)
Render mode: LV_DISPLAY_RENDER_MODE_PARTIAL
```

## Components

### 1. LVGL Library + Config (DONE)

`platformio.ini` has `lvgl/lvgl@^9.2`. `lv_conf.h` needs update for static pool.

### 2. LVGL Port (`src/lvgl_port.cnx`)

Scope `LvglPort` — thin glue layer:
- Static double buffers: `u8[46080] buf1, buf2`
- Tick callback: returns `millis()` for LVGL timekeeping
- Flush callback: calls `Display.draw_bitmap()` then `lv_display_flush_ready()`
- Touch callback: polls `Touch.read()` and fills `lv_indev_data_t`
- `init()`: creates LVGL display + indev, sets buffers and callbacks
- `loop()`: drives `lv_timer_handler()`

### 3. Display Extension (`src/display_st7701.cnx`)

Add public `draw_bitmap()` to existing Display scope:
- Wraps `esp_lcd_panel_draw_bitmap()` with the private panel handle
- Keeps panel handle encapsulated

### 4. Main Loop (updated `src/main.cnx`)

- `setup()`: init hardware → `lv_init()` → `LvglPort.init()` → create label
- `loop()`: `LvglPort.loop()` + ~5ms delay
- Replaces the Phase 2 color-cycle demo

## Files Changed

| File | Action |
|------|--------|
| `platformio.ini` | Add `lib_deps = lvgl/lvgl@^9` — **DONE** |
| `src/lv_conf.h` | Update: static pool instead of malloc |
| `src/lvgl_port.cnx` | New — display + touch glue |
| `src/display_st7701.cnx` | Add public `draw_bitmap()` |
| `src/main.cnx` | Replace color-cycle with LVGL loop |

## Risk Areas

Two C-Next unknowns that may require bug reports:
1. **`void*` params** — `lv_display_set_buffers` takes `void*`. Passing `u8[]` arrays through ADR-006 might add unwanted `&` instead of pointer decay.
2. **Nested struct writes** — `data.point.x <- Touch.get_x()` needs to write through a pointer to a nested struct member.

Per ZERO WORKAROUNDS policy: if either fails, file bugs and block.

## Success Criteria

Dark screen with "OGauge" label centered, rendered by LVGL. Touch input registered (no visual response needed — just proving the pipeline).

## Decisions

- **Static LVGL pool** over malloc: ADR-003 forbids dynamic allocation
- **Partial buffers** over direct framebuffer: ESP-IDF 4.4 lacks `get_frame_buffer()` API
- **Static arrays** over `heap_caps_malloc`: consistent with static pool decision
- **Arduino loop()** over FreeRTOS task: sufficient for bring-up, defer threading to CAN phase
- **Label only** over label+arc: minimal scope, maximum signal — prove plumbing works
- **4-layer split** over monolithic: each layer owns its hardware, no duplication
