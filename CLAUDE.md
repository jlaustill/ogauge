# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OGauge is an ESP32-S3 CAN-only digital gauge for vehicles with round touch displays. Currently in **design phase** - see `design.md` for full specification.

**Target Hardware:** Waveshare ESP32-S3 round touch display + custom CAN daughter board

## Architecture

```
TWAI Driver (ESP32 native CAN)
       ↓
CAN Frame Router
       ↓
J1939 Decoder    OBD-II ISO-TP
       ↘          ↙
     Signal Catalog (unified)
           ↓
    Application Model
           ↓
        UI (LVGL)
```

**Hard rule:** UI never blocks CAN RX or OBD polling. CAN/comms and UI run on separate threads.

## Hard Constraints

- **CAN-only** - no analog inputs, ever
- **Single CAN interface** - one TWAI controller, one bitrate at a time
- **Read-only** - no control actions sent via CAN
- **Opinionated layouts** - value_count (1/2/4/6/8/12) + layout_id, no free-form editing
- **Open signal definitions** - J1939 SPNs/PGNs and OBD-II PIDs from open repos only, no proprietary sources

## Terminology

| Term | Definition |
|------|------------|
| **value_count** | Number of slots displayed (1, 2, 4, 6, 8, 12) |
| **layout** | Predefined arrangement for a value_count; defines slot regions and which widget goes in each. Static, defined in code. |
| **widget** | Reusable visual component (needle, combo, digital). Auto-scales to fit its slot region. |
| **slot** | A region in a layout where one value is displayed |
| **slot config** | User's configuration for a slot: signal_id, units, poll_rate |

## Key Concepts

**Signal Catalog:** Unified abstraction for both J1939 and OBD-II signals. Each signal has `id`, `label`, `units_default`, `freshness_ms`, `source` (J1939|OBD2), plus protocol-specific mappings.

**Theme System:** Global Material-ish semantic roles (`primary`, `background`, `warning`, etc.). Widgets request roles, not RGB values.

**Layout & Widget System:** Layouts define geometry and assign widget types to slots. Widgets auto-scale to fit their allocated region. Users configure slot configs only (which signal, units, poll rate).

## Performance Targets

- CAN RX loss: 0 frames
- UI: ≥60 FPS
- Boot to display: <200ms
- Data latency: <50ms

## MVP Target

- **Screen**: 2.1" Waveshare ESP32-S3 round touch display
- **Bitrates**: 125k, 250k, 500k
- **Themes**: 2 (blue-ish, red-ish)

## Build Commands (ESP-IDF)

When implementation begins:
```bash
idf.py build              # Build firmware
idf.py flash              # Flash to device
idf.py monitor            # Serial monitor
idf.py build flash monitor  # All-in-one
```

## Screen Sizes

Must support: 1.75", 1.85", 2.1", 2.8", 4" (all round displays)
