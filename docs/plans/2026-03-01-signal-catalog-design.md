# Signal Catalog Design

## Status: Complete

## Problem

The current data flow is ad-hoc: `twai_driver.cnx` receives CAN frames, does inline J1939 decode for one signal (SPN 171 ambient temp), and exposes a bare `i32` that `main.cnx` wires directly to `GaugeTemp.set_value()`. This doesn't scale to multiple signals, has no freshness tracking, and mixes hardware, protocol, and signal concerns in one file.

Beyond the data layer, the entire `src/` directory is flat — display hardware, LVGL glue, widgets, and CAN code all sit at the root with no architectural separation.

## Architecture: Two Layers + Orchestrator

All source files belong to one of two layers, with `main.cnx` as the orchestrator:

```
src/
  main.cnx                      <- orchestrator (init, loop, wiring)
  data/                          <- everything CAN: hardware, protocol, signals
    can_bus.cnx                  <- TWAI hardware: init, receive, hand off frames
    j1939_decoder.cnx            <- J1939 protocol: PGN match, SPN decode, writes SignalData
    signal_data.cnx              <- struct definitions (Signal, SignalData) + shared instance
  display/                       <- everything UI: hardware, LVGL, widgets
    i2c_driver.cnx               <- I2C bus init
    tca9554.cnx                  <- IO expander (LCD/touch reset, buzzer)
    display_st7701.cnx           <- ST7701S panel init + draw_bitmap
    touch_cst820.cnx             <- touch controller
    lvgl_port.cnx                <- LVGL glue (display driver, indev, tick, buffers)
    gauge_temp.cnx               <- temperature gauge widget
    needle_img.cnx               <- needle image pixel data
    needle_img_boundary.h        <- ADR-061 void* boundary
    lv_conf.h                    <- LVGL build config
```

**Principle:** Hardware belongs to the layer it serves, not in a separate hardware bucket. CAN transceiver → data layer. Touch controller → display layer.

## Data Layer Design

### Signal and SignalData structs (signal_data.cnx)

A `Signal` value type pairs a decoded value with its timestamp:

```c-next
struct Signal {
  f32 value;
  u32 time;
}
```

`SignalData` holds the current state of all known signals:

```c-next
struct SignalData {
  Signal ambient_temp_c;
  Signal barometric_pressure_kpa;
  Signal relative_humidity_pct;
  Signal egt_c;
  Signal fuel_pressure_kpa;
}
```

Field names use native J1939 units. Unit conversion (kPa→PSI, C→F) happens at display time.

A single shared instance is owned by this scope and written by the decoder, read by the display layer. Freshness check: `millis() - sig.time > threshold` means stale.

### CAN bus (can_bus.cnx)

Owns TWAI hardware init and frame reception. Calls `J1939Decoder.on_frame()` for each received frame. Uses ESP-IDF TWAI driver with its internal ISR + 32-frame RX queue (sufficient for 250kbps, optimize to custom ISR later if needed). Software filtering only — hardware acceptance filter accepts all frames.

### J1939 decoder (j1939_decoder.cnx)

Receives raw CAN frames from can_bus, parses J1939 header (using J1939 lib), matches PGN, extracts SPNs, applies decode formula, writes to `SignalData`.

Decode is data-driven — each SPN has byte_offset, byte_count, resolution, and offset. One generic decode path handles all signals:

```
value = raw * resolution - offset
```

Note: OSSM (the test source) stores offset as positive (e.g., 273.0) and subtracts. The encode formula is `raw = (value + offset) / resolution`. Our decode inverts this.

### Initial signals (5)

All byte positions are 0-indexed (`msg.data[]`). OSSM source uses 1-indexed — subtract 1.
Source: `~/code/ossm/src/Data/J1939Config.cnx`

| Signal | SPN | PGN | Byte(s) | Len | Resolution | Offset | Native Units |
|--------|-----|------|---------|-----|------------|--------|------|
| Ambient Temp | 171 | 65269 | 3-4 | 2 | 0.03125 C/bit | -273 | C |
| Barometric Pressure | 108 | 65269 | 0 | 1 | 0.5 kPa/bit | 0 | kPa |
| Relative Humidity | 354 | 65164 | 6 | 1 | 0.4 %/bit | 0 | % |
| EGT | 173 | 65270 | 5-6 | 2 | 0.03125 C/bit | -273 | C |
| Fuel Pressure | 94 | 65263 | 0 | 1 | 4.0 kPa/bit | 0 | kPa |

PGNs to listen on: 65269 (1s), 65270 (500ms), 65263 (500ms), 65164 (1s)

### Value type

Decoded values are `f32`. ESP32-S3 has a single-precision FPU — no penalty.

### Frame processing flow

```
TWAI ISR (ESP-IDF internal) -> RX queue
  -> CanBus.poll() drains queue
    -> J1939Decoder.on_frame(msg)
      -> parse J1939 header (PGN, source, priority)
      -> match PGN against known set
      -> for each matching SPN: extract bytes, apply formula
      -> write to SignalData.current.xxx.value + .time
```

## Display Layer Design

### Organization

Flat `display/` folder — all 9 files together. File names are descriptive enough without sub-folders.

### Widget → SignalData access

Widgets include `signal_data.cnx` directly and read fields by name:

```c-next
#include "../data/signal_data.cnx"

// widget reads the field it cares about
f32 temp <- SignalData.current.ambient_temp_c.value;
u32 age <- global.millis() - SignalData.current.ambient_temp_c.time;
```

No indirection (enum, function pointer, orchestrator push). Each widget knows which `SignalData` field it displays. This couples widgets to field names, which is natural — a temperature gauge reads the temperature field.

### Freshness handling

Widgets check freshness themselves. A global constant defines the stale threshold:

```c-next
const u32 SIGNAL_STALE_MS <- 2000;
```

When `millis() - sig.time > SIGNAL_STALE_MS`, the widget shows a stale indicator ("----" per design doc). This keeps freshness self-contained within each widget — no orchestrator involvement.

### main.cnx orchestrator role

With widgets reading SignalData directly, the orchestrator simplifies to:

```
setup(): init data layer, init display layer
loop():  CanBus.poll(), LvglPort.loop()
```

No per-signal wiring in the loop. Widgets update themselves from SignalData during LVGL's render cycle.

## Future considerations (not this phase)

- Custom TWAI ISR (bypass ESP-IDF driver) if queue overflow ever becomes a concern
- Layout system (value_count, slots, widget assignment)
- Theme system (semantic color roles)
- Per-signal freshness thresholds (replace global constant)
- Slot-based signal assignment (widget doesn't hardcode which field it reads)

## Open Questions

None — all SPNs confirmed from OSSM source.
