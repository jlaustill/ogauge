# Phase 3: LVGL Bring-up Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Get LVGL v9 rendering a centered "OGauge" label on the ST7701S 480x480 RGB panel with CST820 touch input wired through LVGL's indev system.

**Architecture:** LVGL renders into partial static buffers (480x48 lines, double-buffered). A flush callback copies dirty regions to the RGB panel via `esp_lcd_panel_draw_bitmap()` (called directly as a C function from the callback — no C-Next wrapper). Touch input is polled via CST820 and fed to LVGL's input device system. LVGL's tick driven by manual `lv_tick_inc()` in Arduino `loop()`.

**Tech Stack:** LVGL v9.2, ESP32-S3, PlatformIO + Arduino framework, C-Next v0.2.7+

**Key constraints:**
- `esp_lcd_rgb_panel_get_frame_buffer()` doesn't exist in ESP-IDF 4.4 — use partial buffers + flush
- ADR-003 forbids dynamic allocation — LVGL uses static 128KB pool (`LV_MEM_CUSTOM 0`)
- Buffer pointer can't pass through C-Next wrapper (double indirection via ADR-006) — call `esp_lcd_panel_draw_bitmap` directly via `global.` from flush callback
- 4-layer separation: display driver, touch driver, LVGL glue, orchestrator

---

## Status

| Task | Status | Notes |
|------|--------|-------|
| Task 1: LVGL dependency | **DONE** | Committed `1411fef` |
| Task 2: lv_conf.h | **DONE** | Committed `68c7987` |
| Task 3: Update lv_conf.h for static pool | **DONE** | Committed `294562e` |
| Task 4: Add get_panel() to Display | **DONE** | Committed `6c79e06` |
| Task 5-7: lvgl_port + main + build | **DONE** | Committed `bf9d934` |
| Task 8: Flash and verify | **DONE** | "OGauge" label verified on hardware |

---

### Task 1: Add LVGL library dependency — DONE

**Committed:** `1411fef`

---

### Task 2: Create lv_conf.h — DONE

**Committed:** `68c7987`

---

### Task 3: Update lv_conf.h for static memory pool

**Files:**
- Modify: `src/lv_conf.h`

**Step 1: Change memory config from malloc to static pool**

Replace `LV_MEM_CUSTOM 1` section with:

```c
/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0                /* Use LVGL's built-in allocator (no stdlib malloc) */
#define LV_MEM_SIZE (128U * 1024U)     /* 128KB static pool */
```

**Step 2: Verify build still compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean build (no LVGL source changes, just config).

**Step 3: Commit**

```
git add src/lv_conf.h
git commit -m "Switch LVGL to static 128KB memory pool (ADR-003)"
```

---

### Task 4: Add get_panel() getter to Display scope

**Files:**
- Modify: `src/display_st7701.cnx`

**Why:** The flush callback needs the panel handle to call `esp_lcd_panel_draw_bitmap` directly via `global.` (avoids C-Next pass-by-reference double indirection on buffer pointers). A getter keeps the handle encapsulated in Display while making it accessible.

**Step 1: Add getter at the end of Display scope, before the closing `}`**

Add after `fill_color`:

```cnx
  public esp_lcd_panel_handle_t get_panel() {
    return this.panel_handle;
  }
```

**Step 2: Transpile and verify**

Run: `cnext src/display_st7701.cnx`
Check generated `display_st7701.cpp` for: `esp_lcd_panel_handle_t Display_get_panel(void)` that returns `Display_panel_handle`.

**Step 3: Commit**

```
git add src/display_st7701.cnx src/display_st7701.cpp src/display_st7701.h
git commit -m "Add Display.get_panel() getter for LVGL flush path"
```

---

### Task 5: Create lvgl_port.cnx

**Files:**
- Create: `src/lvgl_port.cnx`

**This is the core integration layer. Key design decisions:**
- Flush callback calls `esp_lcd_panel_draw_bitmap` directly via `global.` (not through a C-Next wrapper) because the `u8 px_map` callback param is `uint8_t*` in C, and passing it through a C-Next function would add `&` (ADR-006), creating `uint8_t**`.
- Panel handle stored in scope variable during init, accessed as `this.panel` in the callback (scope variables are static — accessible from all scope functions).
- Touch callback writes directly to `lv_indev_data_t` struct fields via pass-by-reference.
- Tick driven by manual `lv_tick_inc(elapsed)` in loop, not `lv_tick_set_cb`.

**Step 1: Create the file**

```cnx
#include "display_st7701.cnx"
#include "touch_cst820.cnx"
#include <lvgl.h>
#include <Arduino.h>
#include <esp_lcd_panel_ops.h>

scope LvglPort {
  const i32 LCD_WIDTH <- 480;
  const i32 LCD_HEIGHT <- 480;
  const u32 BUF_SIZE <- 46080;

  u8[46080] buf1;
  u8[46080] buf2;
  u32 last_tick;
  esp_lcd_panel_handle_t panel;

  void disp_flush(lv_display_t disp, const lv_area_t area, u8 px_map) {
    i32 x2_end <- area.x2 + 1;
    i32 y2_end <- area.y2 + 1;
    global.esp_lcd_panel_draw_bitmap(this.panel, area.x1, area.y1, x2_end, y2_end, px_map);
    global.lv_display_flush_ready(disp);
  }

  void touch_read(lv_indev_t indev, lv_indev_data_t data) {
    bool touched <- global.Touch.read();
    if (touched = true) {
      data.point.x <- global.Touch.get_x();
      data.point.y <- global.Touch.get_y();
      data.state <- LV_INDEV_STATE_PRESSED;
    } else {
      data.state <- LV_INDEV_STATE_RELEASED;
    }
  }

  public void init() {
    this.panel <- global.Display.get_panel();

    lv_display_t c_disp <- global.lv_display_create(this.LCD_WIDTH, this.LCD_HEIGHT);
    global.lv_display_set_buffers(c_disp, this.buf1, this.buf2, this.BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
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
}
```

**Step 2: Transpile and inspect**

Run: `cnext src/lvgl_port.cnx` (or `cnext src/main.cnx` if includes require it)

**Critical things to verify in generated `lvgl_port.cpp`:**

1. `LvglPort_disp_flush` signature matches `lv_display_flush_cb_t`:
   ```c
   static void LvglPort_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
   ```

2. `esp_lcd_panel_draw_bitmap` call passes `px_map` directly (NOT `&px_map` or `*px_map`):
   ```c
   esp_lcd_panel_draw_bitmap(LvglPort_panel, area->x1, area->y1, ...x2_end..., ...y2_end..., px_map);
   ```

3. `LvglPort_touch_read` signature matches `lv_indev_read_cb_t`:
   ```c
   static void LvglPort_touch_read(lv_indev_t* indev, lv_indev_data_t* data)
   ```

4. `data->point.x`, `data->state` writes through pointer correctly

5. `lv_display_set_buffers` call passes `buf1`/`buf2` as pointers (array decay, NOT `&buf1`):
   ```c
   lv_display_set_buffers(c_disp, LvglPort_buf1, LvglPort_buf2, ...)
   ```

6. `c_disp` and `c_indev` are `lv_display_t*` and `lv_indev_t*` (opaque pointer types, not values)

**If any of these fail:** File a C-Next bug with the specific generated output vs expected output. Do NOT write C++ workarounds.

**Step 3: Commit (only if transpilation looks correct)**

```
git add src/lvgl_port.cnx src/lvgl_port.cpp src/lvgl_port.h
git commit -m "Add LVGL port layer: display flush + touch input callbacks"
```

---

### Task 6: Update main.cnx for LVGL

**Files:**
- Modify: `src/main.cnx`

**Step 1: Replace entire file contents**

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

  // Create demo label
  lv_obj_t c_scr <- global.lv_screen_active();
  lv_obj_t c_label <- global.lv_label_create(c_scr);
  global.lv_label_set_text(c_label, "OGauge");
  global.lv_obj_center(c_label);

  Serial.println("Ready!");
}

void loop() {
  LvglPort.loop();
  delay(5);
}
```

**Step 2: Commit**

```
git add src/main.cnx src/main.cpp src/main.h
git commit -m "Wire up LVGL loop in main: init, label demo, tick"
```

---

### Task 7: Transpile and verify full C output

**Step 1: Transpile all files**

Run: `cnext src/main.cnx`

This should transpile `main.cnx` and all included files (`display_st7701.cnx`, `touch_cst820.cnx`, `lvgl_port.cnx`, etc.).

**Step 2: Inspect generated files**

Read `src/lvgl_port.cpp` and verify the 6 critical points from Task 5 Step 2.
Read `src/display_st7701.cpp` and verify `Display_get_panel` exists.
Read `src/main.cpp` and verify LVGL calls transpile correctly.

**Step 3: If transpilation issues found**

For each issue:
1. Create minimal repro at `/tmp/cnext-bugs/<issue-number>-<slug>/`
2. File GitHub issue on jlaustill/c-next
3. STOP — do not proceed until fixed (ZERO WORKAROUNDS)

**Step 4: Build**

Run: `pio run -e waveshare_lcd_21`
Expected: Clean compilation.

**Step 5: If build errors**

Common issues:
- **lv_conf.h not found:** Verify `-DLV_CONF_INCLUDE_SIMPLE` in build_flags
- **LVGL type not found:** Check `#include <lvgl.h>` in lvgl_port.cnx
- **Linker errors:** Check that generated `.h` files have correct prototypes
- **Buffer too large:** Reduce `BUF_LINES` from 48 to 24 if memory insufficient

---

### Task 8: Flash and verify on hardware

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

Expected: "OGauge" label centered on screen. Background may be dark or default LVGL theme color.

**Step 4: Verify touch (optional)**

Touch the screen. LVGL should register input events (even without visual response — the label doesn't respond to touch, but LVGL won't crash).

**Step 5: Commit**

```
git add -A
git commit -m "Phase 3: LVGL bring-up — label demo verified on hardware"
```

---

## Known Risk Areas

### 1. `void*` params in `lv_display_set_buffers`

`lv_display_set_buffers(disp, buf1, buf2, size, mode)` expects `void*` for buffers. Our `u8[46080]` arrays should decay to `uint8_t*` then implicitly convert to `void*`. But C-Next's `global.` call handling of arrays hasn't been explicitly tested with `void*` params. **Verify in Task 7.**

### 2. Nested struct writes in touch callback

`data.point.x <- Touch.get_x()` writes through a pointer to a nested struct member. `data` is `lv_indev_data_t*` (ADR-006), `.point` is `lv_point_t` (nested), `.x` is `int32_t`. Transpiler needs to generate `data->point.x = ...`. **Verify in Task 7.**

### 3. `lv_indev_data_t` has complex fields

`lv_indev_data_t` contains `void*` arrays and gesture types. We only touch `.point.x`, `.point.y`, and `.state`. Untouched fields should be irrelevant, but verify the struct passes through C-Next without errors.

### 4. LVGL `lv_point_t` member types

`lv_point_t` uses `int32_t` for x/y. `Touch.get_x()` returns `u16`. The widening from `uint16_t` to `int32_t` should be implicit in C-Next. **Verify no narrowing warnings.**

---

## Troubleshooting

**If LVGL shows nothing:** Add `Serial.println("flush")` equivalent in flush callback (note: MISRA 13.5 means extract the call). Check that `Display.get_panel()` returns a valid handle. Verify backlight is on.

**If touch doesn't work:** Add serial prints in touch callback to verify it's being called and coordinates are sane (0-479). Check `lv_indev_set_type` is set to `LV_INDEV_TYPE_POINTER`.

**If LVGL asserts/crashes:** Check serial output for LVGL error messages. Common: buffer size mismatch, null display handle, memory pool exhausted.

**If memory pool exhausted:** Increase `LV_MEM_SIZE` from 128KB to 192KB, or reduce font includes.
