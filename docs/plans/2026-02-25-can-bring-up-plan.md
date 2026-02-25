# CAN Bring-up Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Get TWAI receiving CAN frames from OSSM and logging them to Serial in candump format.

**Architecture:** Single new file `src/data/twai_driver.cnx` handles TWAI init and polling. Main loop calls `TwaiDriver.poll()` each iteration, which drains the RX queue and prints frames. Serial switches from USB CDC to UART.

**Tech Stack:** ESP-IDF TWAI driver, C-Next, Arduino framework (UART Serial)

---

### Task 1: Switch Serial from USB CDC to UART

**Files:**
- Modify: `platformio.ini`

**Step 1: Update build flags**

Change:
```
-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
```
To:
```
-DARDUINO_USB_CDC_ON_BOOT=0 -DARDUINO_USB_MODE=0
```

This frees GPIO19/20 for CAN and routes Serial to the UART USB-C port (GPIO43/44).

**Step 2: Verify build compiles**

Run: `pio run -e waveshare_lcd_21`
Expected: Build succeeds. Serial.begin/println calls now use hardware UART.

**Step 3: Commit**

```bash
git add platformio.ini
git commit -m "Switch serial from USB CDC to UART for CAN pin reuse"
```

---

### Task 2: Create TWAI driver scope

**Files:**
- Create: `src/data/twai_driver.cnx`

**Step 1: Write the TWAI driver**

```cnx
#include <driver/twai.h>
#include <Arduino.h>

scope TwaiDriver {
  const i32 TX_PIN <- 19;
  const i32 RX_PIN <- 20;

  public void init() {
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
      global.Serial.println("TWAI: driver install FAILED");
      return;
    }

    err <- global.twai_start();
    if (err != 0) {
      global.Serial.println("TWAI: start FAILED");
      return;
    }

    global.Serial.println("TWAI: started at 250kbps");
  }

  public void poll() {
    twai_message_t msg;
    esp_err_t result <- global.twai_receive(msg, 0);
    while (result = ESP_OK) {
      this.print_frame(msg);
      result <- global.twai_receive(msg, 0);
    }
  }

  void print_frame(twai_message_t msg) {
    u32 id <- msg.identifier;

    // Print ID (8 hex chars for extended, 3 for standard)
    if (msg.extd = 1) {
      global.Serial.print("can0  ");
    } else {
      global.Serial.print("can0  ");
    }

    // ID in hex
    if (id < 0x10000000) { global.Serial.print("0"); }
    if (id < 0x01000000) { global.Serial.print("0"); }
    if (id < 0x00100000) { global.Serial.print("0"); }
    if (id < 0x00010000) { global.Serial.print("0"); }
    if (id < 0x00001000) { global.Serial.print("0"); }
    if (id < 0x00000100) { global.Serial.print("0"); }
    if (id < 0x00000010) { global.Serial.print("0"); }
    global.Serial.print(id, 16);

    // DLC
    global.Serial.print("  [");
    global.Serial.print(msg.data_length_code);
    global.Serial.print("]  ");

    // Data bytes
    u8 i <- 0;
    while (i < msg.data_length_code) {
      if (msg.data[i] < 0x10) {
        global.Serial.print("0");
      }
      global.Serial.print(msg.data[i], 16);
      if (i < msg.data_length_code - 1) {
        global.Serial.print(" ");
      }
      i <- i + 1;
    }
    global.Serial.println("");
  }
}
```

**Step 2: Transpile to verify C-Next compiles**

Run: `cnext src/data/twai_driver.cnx --include src -D LV_CONF_INCLUDE_SIMPLE`

Fix any transpiler issues. This is pure C-Next — struct init, bitfield access, and TWAI API calls should all work. If any pattern fails, create a minimal C helper header.

**Step 3: Commit**

```bash
git add src/data/twai_driver.cnx
git commit -m "Add TWAI driver scope for CAN RX with candump output"
```

---

### Task 3: Wire TWAI into main loop

**Files:**
- Modify: `src/main.cnx`

**Step 1: Add include and init/poll calls**

Add `#include "data/twai_driver.cnx"` to includes.

Add `TwaiDriver.init()` to `setup()` after display/touch/LVGL init.

Add `TwaiDriver.poll()` to `loop()` before `LvglPort.loop()`.

**Step 2: Transpile all files**

Run: `cnext src/ --include src -D LV_CONF_INCLUDE_SIMPLE`
Expected: All files transpile successfully.

**Step 3: Build**

Run: `pio run -e waveshare_lcd_21`
Expected: Build succeeds.

**Step 4: Commit**

```bash
git add src/main.cnx
git commit -m "Wire TwaiDriver init and poll into main loop"
```

---

### Task 4: Hardware test with OSSM

**Step 1: Flash and monitor**

Run: `pio run -e waveshare_lcd_21 -t upload && pio device monitor`

Use the **UART** USB-C port (not the USB port — those pins are now CAN).

**Step 2: Verify output**

With OSSM broadcasting on CAN bus at 250kbps, expect candump-style output:
```
TWAI: started at 250kbps
can0  18FEF100  [8]  01 02 03 04 05 06 07 08
can0  18FEEE00  [8]  ...
```

If no output: check wiring (GPIO19→TX, GPIO20→RX), bitrate match, CAN transceiver power, bus termination.

**Step 3: Commit any fixes**

```bash
git add -A
git commit -m "CAN bring-up verified with OSSM hardware"
```
