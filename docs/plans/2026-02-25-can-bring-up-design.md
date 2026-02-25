# CAN Bring-up Design — Phase 5

## Goal

Get TWAI receiving CAN frames and logging them to Serial in candump format. No decoding, no UI changes. Prove the hardware pipeline works.

## Hardware

- **CAN TX**: GPIO19 (repurposed from USB D-)
- **CAN RX**: GPIO20 (repurposed from USB D+)
- **Bitrate**: 250 kbps (J1939 standard)
- **Debug serial**: UART port (GPIO43/44) — USB CDC disabled
- **Test source**: OSSM (Open Source Sensor Module) broadcasting J1939

## Scope

### New files

| File | Responsibility |
|------|---------------|
| `src/data/twai_driver.cnx` | TWAI init, poll RX queue, print frames to Serial |

### Modified files

| File | Change |
|------|--------|
| `src/main.cnx` | Init TWAI, call `TwaiDriver.poll()` in loop |
| `platformio.ini` | Switch from USB CDC to UART serial |

### Not in scope

- J1939 decoding
- Signal store / catalog
- UI changes / gauge updates
- Dual-core threading
- ISR-driven reception

## Data Flow

```
TWAI hardware FIFO
  ↓
TwaiDriver.poll()  — called each loop iteration
  ↓
Serial.println()   — candump-style output
```

## Serial Output Format

```
can0  18FEF100  [8]  01 02 03 04 05 06 07 08
```

- 29-bit extended ID (J1939)
- Data length
- Hex payload bytes

## Main Loop

```
loop() {
  TwaiDriver.poll()
  LvglPort.loop()
  delay(5)
}
```

## Serial Config Change

Switch from USB CDC (GPIO19/20) to hardware UART (GPIO43/44):
- `ARDUINO_USB_CDC_ON_BOOT=0`
- `ARDUINO_USB_MODE=0`
- Use the USB-C port labeled "UART" for debug serial

## Future Iterations

1. J1939 decoder + signal store (display OSSM data on gauge)
2. ISR-driven TWAI with alert queue
3. Dual-core: Core 0 = UI, Core 1 = CAN
