# Signal Catalog Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restructure src/ into two layers (data/ and display/) and implement a signal catalog with 5 J1939 signals decoded from CAN frames into a shared SignalData struct.

**Architecture:** Two layers + orchestrator. `data/` owns CAN hardware, J1939 protocol decode, and SignalData. `display/` owns screen hardware, LVGL, and widgets. `main.cnx` orchestrates init + loop. Widgets read SignalData directly by field name.

**Tech Stack:** C-Next (.cnx), ESP-IDF TWAI driver, J1939 library (jlaustill/J1939), LVGL v9.2, PlatformIO

**Design doc:** `docs/plans/2026-03-01-signal-catalog-design.md`

---

## Important Context

- C-Next transpiles from `src/main.cnx` entry point — follows `#include` chains
- Generated `.cpp` go next to source, `.hpp` go to `include/` (mirroring src structure)
- Moving files changes generated output paths — old `.cpp`/`.hpp` must be deleted
- `pio run` = transpile + compile. No unit test framework — verification is compile + hardware
- OSSM (~/code/ossm) is the J1939 test source at 250kbps on the bench
- OSSM uses 1-indexed byte positions; ogauge uses 0-indexed `msg.data[]`
- C-Next syntax: `<-` for assignment, `=` for equality, no raw pointers, MISRA enforced
- All params are pass-by-reference (ADR-006)

---

### Task 1: Create signal_data.cnx

**Files:**
- Create: `src/data/signal_data.cnx`

**Step 1: Create the Signal and SignalData structs**

```c-next
// Signal value + timestamp pair
// Written by J1939 decoder, read by display widgets
struct Signal {
  f32 value;
  u32 time;
}

// All known signal values in native J1939 units
// Unit conversion (kPa→PSI, C→F) happens at display time
struct SignalData {
  Signal ambient_temp_c;
  Signal barometric_pressure_kpa;
  Signal relative_humidity_pct;
  Signal egt_c;
  Signal fuel_pressure_kpa;
}

const u32 SIGNAL_STALE_MS <- 2000;

scope SignalStore {
  public SignalData current;
}
```

**Step 2: Commit**

```bash
git add src/data/signal_data.cnx
git commit -m "feat: add Signal/SignalData structs for signal catalog"
```

---

### Task 2: Create can_bus.cnx

Extract TWAI hardware init + frame receive from `src/data/twai_driver.cnx` into a new `can_bus.cnx`. This file owns ONLY the CAN hardware — no J1939 decode.

**Files:**
- Create: `src/data/can_bus.cnx`
- Reference: `src/data/twai_driver.cnx` (existing, will be deleted in Task 7)

**Step 1: Create can_bus.cnx with TWAI init + poll**

The poll loop receives frames and hands them to `J1939Decoder.on_frame()`. The print_frame debug function moves here too (it prints raw CAN frames, a hardware-level concern).

```c-next
#include <driver/twai.h>
#include <Arduino.h>
#include "j1939_decoder.cnx"

scope CanBus {
  const gpio_num_t TX_PIN <- GPIO_NUM_19;
  const gpio_num_t RX_PIN <- GPIO_NUM_20;

  void init() {
    twai_general_config_t g_config <- {
      mode: TWAI_MODE_NORMAL,
      tx_io: TX_PIN,
      rx_io: RX_PIN,
      clkout_io: TWAI_IO_UNUSED,
      bus_off_io: TWAI_IO_UNUSED,
      tx_queue_len: 0,
      rx_queue_len: 32,
      alerts_enabled: TWAI_ALERT_NONE,
      clkout_divider: 0,
      intr_flags: 0
    };

    twai_timing_config_t t_config <- {
      brp: 16,
      tseg_1: 15,
      tseg_2: 4,
      sjw: 3,
      triple_sampling: false
    };

    twai_filter_config_t f_config <- {
      acceptance_code: 0,
      acceptance_mask: 0xFFFFFFFF,
      single_filter: true
    };

    esp_err_t err <- global.twai_driver_install(g_config, t_config, f_config);
    if (err != 0) {
      global.Serial.println("CAN: driver install FAILED");
      return;
    }

    err <- global.twai_start();
    if (err != 0) {
      global.Serial.println("CAN: start FAILED");
      return;
    }

    global.Serial.println("CAN: started at 250kbps");
  }

  void poll() {
    twai_message_t msg;
    esp_err_t result <- global.twai_receive(msg, 0);
    while (result = ESP_OK) {
      J1939Decoder.on_frame(msg);
      result <- global.twai_receive(msg, 0);
    }
  }
}
```

Note: `print_frame` debug output is removed from the poll loop for cleanliness. If serial debug is needed, add it to the decoder.

**Step 2: Commit**

```bash
git add src/data/can_bus.cnx
git commit -m "feat: extract CAN hardware into can_bus.cnx"
```

---

### Task 3: Create j1939_decoder.cnx

This is the core business logic — PGN matching and SPN decode for all 5 signals. It writes decoded values to `SignalStore.current`.

**Files:**
- Create: `src/data/j1939_decoder.cnx`

**Step 1: Create j1939_decoder.cnx**

```c-next
#include <driver/twai.h>
#include <Arduino.h>
#include <J1939Message.cnx>
#include "signal_data.cnx"

scope J1939Decoder {

  private u16 extract_u16(u8[8] data, u8 byte_lo, u8 byte_hi) {
    u16 raw <- 0;
    raw[0, 8] <- data[byte_lo];
    raw[8, 8] <- data[byte_hi];
    return raw;
  }

  private void decode_65269(u8[8] data) {
    // SPN 108 - Barometric Pressure: byte 0, 1 byte, 0.5 kPa/bit
    u8 raw108 <- data[0];
    SignalStore.current.barometric_pressure_kpa.value <- raw108 * 0.5;
    SignalStore.current.barometric_pressure_kpa.time <- global.millis();

    // SPN 171 - Ambient Air Temperature: bytes 3-4, 2 bytes, 0.03125 C/bit, -273 offset
    u16 raw171 <- this.extract_u16(data, 3, 4);
    i32 raw171_wide <- raw171[0, 16];
    SignalStore.current.ambient_temp_c.value <- raw171_wide * 0.03125 - 273.0;
    SignalStore.current.ambient_temp_c.time <- global.millis();
  }

  private void decode_65270(u8[8] data) {
    // SPN 173 - Exhaust Gas Temperature: bytes 5-6, 2 bytes, 0.03125 C/bit, -273 offset
    u16 raw173 <- this.extract_u16(data, 5, 6);
    i32 raw173_wide <- raw173[0, 16];
    SignalStore.current.egt_c.value <- raw173_wide * 0.03125 - 273.0;
    SignalStore.current.egt_c.time <- global.millis();
  }

  private void decode_65263(u8[8] data) {
    // SPN 94 - Fuel Delivery Pressure: byte 0, 1 byte, 4.0 kPa/bit
    u8 raw94 <- data[0];
    SignalStore.current.fuel_pressure_kpa.value <- raw94 * 4.0;
    SignalStore.current.fuel_pressure_kpa.time <- global.millis();
  }

  private void decode_65164(u8[8] data) {
    // SPN 354 - Relative Humidity: byte 6, 1 byte, 0.4 %/bit
    u8 raw354 <- data[6];
    SignalStore.current.relative_humidity_pct.value <- raw354 * 0.4;
    SignalStore.current.relative_humidity_pct.time <- global.millis();
  }

  void on_frame(twai_message_t msg) {
    J1939Message j_msg;
    J1939.init(j_msg);
    J1939.setCanId(j_msg, msg.identifier);
    J1939.setData(j_msg, msg.data);

    u16 pgn <- j_msg.pgn;

    if (pgn = 65269) { this.decode_65269(msg.data); }
    if (pgn = 65270) { this.decode_65270(msg.data); }
    if (pgn = 65263) { this.decode_65263(msg.data); }
    if (pgn = 65164) { this.decode_65164(msg.data); }
  }
}
```

**Important C-Next notes for implementer:**
- `u8[8] data` — array params use `type[size] name` syntax
- `this.extract_u16()` — private helpers must be defined before use in the scope
- MISRA 13.5: no function calls in `if` conditions — the PGN comparisons are fine (just variable equality)
- `i32 raw171_wide <- raw171[0, 16]` — widen u16→i32 via bit extraction (C-Next rejects direct u16→i32 due to sign change)

**Step 2: Commit**

```bash
git add src/data/j1939_decoder.cnx
git commit -m "feat: add J1939 decoder with 5 SPN decode (171, 108, 354, 173, 94)"
```

---

### Task 4: Move display files to src/display/

Move all display-layer files from `src/` root into `src/display/`. This is a structural-only change.

**Files to move:**
- `src/i2c_driver.cnx` → `src/display/i2c_driver.cnx`
- `src/tca9554.cnx` → `src/display/tca9554.cnx`
- `src/display_st7701.cnx` → `src/display/display_st7701.cnx`
- `src/touch_cst820.cnx` → `src/display/touch_cst820.cnx`
- `src/lvgl_port.cnx` → `src/display/lvgl_port.cnx`
- `src/gauge_temp.cnx` → `src/display/gauge_temp.cnx`
- `src/needle_img.cnx` → `src/display/needle_img.cnx`
- `src/needle_img_boundary.h` → `src/display/needle_img_boundary.h`
- `src/lv_conf.h` → `src/display/lv_conf.h`

**Step 1: Create display directory and move files**

```bash
mkdir -p src/display
git mv src/i2c_driver.cnx src/display/
git mv src/tca9554.cnx src/display/
git mv src/display_st7701.cnx src/display/
git mv src/touch_cst820.cnx src/display/
git mv src/lvgl_port.cnx src/display/
git mv src/gauge_temp.cnx src/display/
git mv src/needle_img.cnx src/display/
git mv src/needle_img_boundary.h src/display/
git mv src/lv_conf.h src/display/
```

**Step 2: Move corresponding generated .cpp files**

These are committed transpiler output — move them to match:

```bash
mkdir -p src/display
git mv src/i2c_driver.cpp src/display/
git mv src/tca9554.cpp src/display/
git mv src/display_st7701.cpp src/display/
git mv src/touch_cst820.cpp src/display/
git mv src/lvgl_port.cpp src/display/
git mv src/gauge_temp.cpp src/display/
git mv src/needle_img.cpp src/display/
```

**Step 3: Commit the structural move**

```bash
git add -A
git commit -m "refactor: move display files into src/display/"
```

---

### Task 5: Update all include paths + configs

After moving files, every `#include` path between files needs updating. Also update `platformio.ini`, `cnext.config.json`, and the `lv_conf.h` symlink.

**Files to modify:**
- `src/main.cnx` — includes for display and data files
- `src/display/display_st7701.cnx` — includes tca9554, i2c_driver
- `src/display/touch_cst820.cnx` — includes i2c_driver
- `src/display/tca9554.cnx` — includes i2c_driver
- `src/display/lvgl_port.cnx` — includes display_st7701, touch_cst820
- `src/display/gauge_temp.cnx` — includes needle_img, needle_img_boundary.h
- `platformio.ini` — add `-I include/display` to build_flags
- `cnext.config.json` — add `include/display` to include array
- LVGL symlink — point to new lv_conf.h location

**Step 1: Update main.cnx includes**

Replace the includes section of `src/main.cnx`:

```c-next
#include <Arduino.h>
#include <lvgl.h>
#include "display/display_st7701.cnx"
#include "display/touch_cst820.cnx"
#include "display/lvgl_port.cnx"
#include "display/gauge_temp.cnx"
#include "data/can_bus.cnx"
```

Note: `main.cnx` no longer includes `data/twai_driver.cnx` — replaced by `data/can_bus.cnx` which chains to `j1939_decoder.cnx` → `signal_data.cnx`.

**Step 2: Update display file internal includes**

Within `src/display/`, files that include each other now use local paths (same directory):

In `display_st7701.cnx`: change `"tca9554.cnx"` → `"tca9554.cnx"` (already local, no change needed if they used quotes)
In `touch_cst820.cnx`: same pattern
In `gauge_temp.cnx`: `"needle_img.cnx"` stays local, `"needle_img_boundary.h"` stays local

Check each file — if they used relative paths from `src/` root, those need to become local paths within `display/`.

**Step 3: Update platformio.ini build_flags**

Add include path for display headers:

```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DARDUINO_USB_MODE=0
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
    -I include
    -I include/data
    -I include/display
    -I include/.pio/libdeps/waveshare_lcd_21/J1939/src
```

**Step 4: Update cnext.config.json**

Add `include/display` to the include array:

```json
{
  "cppRequired": true,
  "noCache": true,
  "basePath": "src",
  "headerOut": "include",
  "include": [
    "include/",
    "include/display",
    ".pio/libdeps/waveshare_lcd_21/J1939/src",
    ...existing SDK paths...
  ]
}
```

**Step 5: Fix LVGL lv_conf.h symlink**

LVGL expects lv_conf.h via the symlink at `.pio/libdeps/waveshare_lcd_21/lvgl/src/lv_conf.h`. Update it:

```bash
rm .pio/libdeps/waveshare_lcd_21/lvgl/src/lv_conf.h
ln -s ../../../../../../src/display/lv_conf.h .pio/libdeps/waveshare_lcd_21/lvgl/src/lv_conf.h
```

**Step 6: Delete stale generated headers**

Old `.hpp` files at `include/` root are now stale (new ones will be at `include/display/`):

```bash
rm -f include/display_st7701.hpp include/gauge_temp.hpp include/i2c_driver.hpp
rm -f include/lvgl_port.hpp include/needle_img.hpp include/tca9554.hpp include/touch_cst820.hpp
```

Also delete the old `src/data/twai_driver.cpp` and `include/data/twai_driver.hpp`:

```bash
rm -f src/data/twai_driver.cpp src/data/twai_driver.cnx include/data/twai_driver.hpp
```

**Step 7: Commit**

```bash
git add -A
git commit -m "refactor: update all include paths for two-layer architecture"
```

---

### Task 6: Update main.cnx orchestrator

Rewrite `main.cnx` to use the new two-layer structure. The loop simplifies — no per-signal wiring.

**Files:**
- Modify: `src/main.cnx`

**Step 1: Rewrite main.cnx**

```c-next
#include <Arduino.h>
#include <lvgl.h>
#include "display/display_st7701.cnx"
#include "display/touch_cst820.cnx"
#include "display/lvgl_port.cnx"
#include "display/gauge_temp.cnx"
#include "data/can_bus.cnx"

void setup() {
  Serial.begin(115200);
  Serial.println("OGauge: Signal Catalog");

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
  GaugeTemp.create();

  Serial.println("CAN init...");
  CanBus.init();

  Serial.println("Ready!");
}

void loop() {
  CanBus.poll();
  LvglPort.loop();
  delay(5);
}
```

Note: No `GaugeTemp.set_value()` call in the loop. The widget reads SignalData directly (next task).

**Step 2: Commit**

```bash
git add src/main.cnx
git commit -m "refactor: simplify main.cnx orchestrator for two-layer architecture"
```

---

### Task 7: Update gauge_temp.cnx to read from SignalData

Modify the gauge widget to read ambient temperature from SignalData directly, with freshness checking.

**Files:**
- Modify: `src/display/gauge_temp.cnx`

**Step 1: Add signal_data include and update set_value to self-update**

Add at the top of gauge_temp.cnx (after existing includes):

```c-next
#include "../data/signal_data.cnx"
```

Replace the existing `set_value(i32 temp_c)` method with an `update()` method that reads SignalData:

```c-next
void update() {
  f32 temp_f <- SignalStore.current.ambient_temp_c.value;
  u32 age <- global.millis() - SignalStore.current.ambient_temp_c.time;
  i32 temp <- temp_f;

  if (age > SIGNAL_STALE_MS) {
    global.lv_label_set_text(this.temp_label, "---- C");
    global.lv_scale_set_image_needle_value(this.scale, this.needle, -40);
  } else {
    global.lv_scale_set_image_needle_value(this.scale, this.needle, temp);
    global.lv_label_set_text_fmt(this.temp_label, "%d C", temp);
  }
}
```

**Step 2: Call update from an LVGL timer instead of main loop**

Add a timer callback and set it up in `create()`:

```c-next
private void on_update_timer(lv_timer_t t) {
  this.update();
}
```

At the end of `create()`, add:

```c-next
global.lv_timer_create(this.on_update_timer, 100, 0);
```

This updates the gauge at 10Hz from LVGL's timer system — no orchestrator wiring needed.

**Step 3: Remove the old set_value method and the touch arc overlay**

The touch arc was for development testing (manually setting temperature). Remove:
- The `arc` scope variable
- The `on_arc_changed` callback
- All arc creation code in `create()`
- The old `set_value` method

Keep the scale, needle, temp_label, and all section styling.

**Step 4: Commit**

```bash
git add src/display/gauge_temp.cnx
git commit -m "feat: gauge reads SignalData directly with freshness check"
```

---

### Task 8: Build and verify

**Step 1: Clean old generated files**

```bash
# Remove any stale .cpp/.hpp that might conflict
find src/ -name "*.cpp" -newer src/main.cnx -delete 2>/dev/null
find include/ -name "*.hpp" -newer src/main.cnx -delete 2>/dev/null
```

Actually, safer approach — just remove all generated files and let transpiler regenerate:

```bash
# Remove ALL generated .cpp files (transpiler will regenerate)
find src/ -name "*.cpp" -delete
# Remove ALL generated .hpp files
find include/ -name "*.hpp" -delete
```

**Step 2: Transpile and build**

```bash
pio run -e waveshare_lcd_21
```

Expected: Clean transpilation of all .cnx files, then successful C++ compilation.

**Step 3: Fix any transpiler or compile errors**

Common issues to watch for:
- Include path resolution failures (check cnext.config.json include array)
- Missing `-I` flags in platformio.ini for new subdirectories
- C-Next scope variable visibility (`public` vs default `private` in v0.2.16)
- u16→i32 widening rejected — use bit extraction `val[0, 16]`
- MISRA 13.5 — no function calls in `if` conditions

**Step 4: Flash and verify with OSSM**

```bash
pio run -e waveshare_lcd_21 -t upload && pio device monitor
```

With OSSM powered on and sending J1939 at 250kbps, verify:
- Gauge shows ambient temperature (should match OSSM's BME280 reading)
- Gauge shows "---- C" when OSSM is powered off (stale after 2 seconds)
- No CAN frame loss (check serial output if debug prints are enabled)

**Step 5: Commit clean build**

```bash
git add -A
git commit -m "build: clean rebuild with two-layer architecture"
```

---

## Summary of final file structure

```
src/
  main.cnx                          <- orchestrator
  data/
    signal_data.cnx                  <- Signal/SignalData structs + SignalStore
    can_bus.cnx                      <- TWAI hardware init + poll
    j1939_decoder.cnx                <- J1939 PGN/SPN decode → writes SignalStore
  display/
    i2c_driver.cnx                   <- I2C bus
    tca9554.cnx                      <- IO expander
    display_st7701.cnx               <- ST7701S panel
    touch_cst820.cnx                 <- CST820 touch
    lvgl_port.cnx                    <- LVGL glue
    gauge_temp.cnx                   <- temperature gauge (reads SignalStore directly)
    needle_img.cnx                   <- needle image data
    needle_img_boundary.h            <- ADR-061 void* boundary
    lv_conf.h                        <- LVGL config
```

## What changed from before

| Before | After |
|--------|-------|
| `twai_driver.cnx` — one file, all CAN concerns | Split into `can_bus.cnx` + `j1939_decoder.cnx` + `signal_data.cnx` |
| Flat `src/` — 11 files mixed together | Two layers: `data/` and `display/` |
| `main.cnx` wires `TwaiDriver.ambient_temp` → `GaugeTemp.set_value()` | Widget reads `SignalStore.current` directly via LVGL timer |
| 1 signal (SPN 171) | 5 signals across 4 PGNs |
| No freshness tracking | 2-second stale threshold, shows "----" |
| `i32` signal value | `f32` in native units (ESP32-S3 FPU) |
