# Design Document (v1)

## ESP32-S3 CAN-Only Digital Gauge

### 1. Purpose

Build a **CAN-only**, OEM-feeling digital gauge using a **round ESP32-S3 touch display**. The gauge displays live vehicle data with **opinionated layouts**, supports **J1939 and OBD-II concurrently**, and avoids configuration dead-ends from day one.

Also: the project is a **reference consumer** for open, community-maintained signal definitions. J1939 SPNs/PGNs and OBD-II PIDs should come from open repos, not paywalled/proprietary sources.

---

### 2. Terminology

| Term | Definition |
|------|------------|
| **value_count** | Number of slots displayed (1, 2, 4, 6, 8, 12) |
| **layout** | Predefined arrangement for a specific value_count. Defines slot regions and which widget type goes in each. Static, defined in code. |
| **widget** | Reusable visual component (needle gauge, progress bar, combo, digital). Auto-scales to fit its allocated slot region. |
| **slot** | A region in a layout where one value is displayed using one widget |
| **slot config** | User's configuration for a slot: signal_id, units, poll_rate |

---

### 3. Hard Constraints (Non-Negotiables)

* **CAN-only**. No analog inputs now or ever.
* **Single CAN interface** (one TWAI controller, one transceiver).
* **One bitrate active at a time** (user-selectable). Multi-network users should use an external gateway (e.g., OCCN).
* **Read-only display device** (no control actions).
* **Opinionated layouts** (no free-form UI editing). Users configure slot configs only.
* **No CAN termination**. 120Ω must be provided externally.
* **Multiple Screen Sizes** it needs to support all of the available screen sizes, 1.75, 1.85, 2.1, 2.8, 4, and all future round sizes with proper scaling

---

### 4. Primary Goals

* Smooth UI
* Native CAN via TWAI (no SPI CAN)
* **J1939** + **OBD-II** together intelligently
* Simple, discoverable, future-proof config
* Robust handling of CAN loss/stale data + DM1 warnings

**Non-Goals**

* CAN-FD
* Multiple CAN interfaces simultaneously
* Analog sensors

---

### 5. Hardware Architecture

#### 5.1 Main board

* Waveshare ESP32-S3 round touch display
* Dual-core, PSRAM, TWAI, USB

Responsibilities:

* LVGL UI
* Signal catalog + acquisition policy
* J1939 decoding
* OBD-II polling (ISO-TP)
* Touch + button input
* Config storage

#### 5.2 Daughter boards

* 9–36 V input
* CAN transceiver (3.3 V logic)
* Reverse polarity protection
* TVS on power + CAN
* No CAN termination (external only)
* Deutsch DT connector 6 pin: CAN-H, CAN-L, CAN shield, ground, constant power, accessory power
* Reference designs for each circuit documented so PCBs can be quickly designed for different screen sizes
* Daughter board design is unique per screen size; define MVP standard only

#### 5.3 Power Management

* **OEM-style operation**: gauge turns on with accessory power (key-on), off with key-off
* **Turn-off delay**: configurable (minutes + seconds, Android timer-style picker)
* **Low-power off state**: minimize consumption when accessory is off; no wake-on-CAN

#### 5.4 Screen Brightness

Configurable source:
* **Manual**: user sets fixed level
* **Auto**: use light sensor if screen has one
* **J1939**: respond to lighting command PGN
* **OBD-II**: use dimming PID if one exists

#### 5.5 MVP Target Hardware

* **Screen size**: 2.1" Waveshare ESP32-S3 round touch display
* Other sizes (1.75", 1.85", 2.8", 4") follow after MVP

---

### 6. Software Architecture (Layered)

```
TWAI Driver
  ↓
CAN Frame Router
  ↓
J1939 Decoder        OBD-II ISO-TP
        \            /
         \          /
          Signal Catalog
                 ↓
            Application Model
                 ↓
               UI (LVGL)
```

Hard rule: **UI never blocks CAN RX or OBD polling**.

#### 6.1 Threading Model

* **Core 0**: UI (LVGL) only
* **Core 1**: CAN RX, frame routing, protocol decoding, OBD-II polling
* **Timer-driven**: all periodic work runs from timers, not busy loops
* **Mutual watchdog**: each core monitors the other; reboot on failure

#### 6.2 Signal Acquisition

* Only poll/listen for signals **configured in active slots**
* Slots can mix OBD-II PIDs and J1939 SPNs freely
* **J1939 PGNs**: either broadcast (listen) or on-request per PGN definition
* **OBD-II PIDs**: always polled; poll rate configurable per slot config
* No protocol auto-detection; if no matching traffic, widget shows empty state

---

### 7. Signal Catalog & Acquisition

#### Foundational principle: open signal definitions

Signal definitions must come from open, community repos for:

* J1939 PGNs/SPNs
* OBD-II modes/PIDs

No embedding proprietary tables. Specific repos TBD.

#### 7.1 Signals

Unified representation with optional acquisition methods:

* `id`, `label`, `units_default`
* `freshness_ms`
* `source` (`J1939` | `OBD2`)
* optional `j1939` mapping: `{ spn, pgn?, source_address? }`
* optional `obd2` mapping: `{ mode, pid, scaling }`

#### 7.2 Stale Signal Handling

When a signal exceeds its `freshness_ms` without update:
* Display empty state: "----" or blank (user-configurable option)
* No automatic retry logic; polling continues at configured rate
* Keep it simple: no traffic = empty state

---

### 8. Theme System (Global)

Layouts do not own colors. Theme is global setup.

Material-ish semantic roles:

* `primary`, `secondary`
* `background`, `surface`
* `on_primary`, `on_secondary`, `on_background`, `on_surface`
* `warning`, `error`
* `success` (optional)
* `font`

Widgets/layouts request **roles**, not RGB.

#### 8.1 MVP Themes

* **Theme 1**: Blue-ish
* **Theme 2**: Red-ish

Not user-customizable for MVP. Custom theme creation is a future feature.

---

### 9. Layout & Widget System

#### 9.1 Layouts

A layout defines:

* `value_count` (1 / 2 / 4 / 6 / 8 / 12)
* `layout_id`
* Slot definitions: region geometry + widget type for each slot
* **Check engine light position** (tappable to view fault codes)
* Semantic color role usage
* Typography sizing

Layouts are static, defined in code. Users select a layout but cannot modify its structure.

#### 9.2 Widgets

Widgets are reusable visual components that render a value. Each widget:

* Auto-scales to fit its allocated slot region (full circle, half, quarter, etc.)
* Requests theme roles for colors (not hardcoded RGB)
* Handles its own animation/smoothing

**MVP Widget Types:**

* **Needle gauge**: classic analog-style arc gauge
* **Combo gauge**: needle + digital readout
* **Digital only**: numeric display

Additional widget types in future (bar, sparkline, etc.).

#### 9.3 Slot Config

User-configurable per slot:

* `signal_id` (which PID or SPN)
* `units` (e.g., MPH vs KPH for same signal)
* `poll_rate_ms` (for OBD-II; J1939 uses PGN-defined rate)
* Optional: label override, smoothing

**OBD-II poll rate defaults**: Use J1939 broadcast rates as sane defaults for equivalent OBD-II PIDs (e.g., RPM broadcasts at 100ms in J1939, so default OBD-II RPM poll to 100ms).

Empty slots show placeholder state (not hidden).

---

### 10. Setup UX

* Long press enters setup (or hardware button if screen has one)
* **First boot**: if NVS empty, open network setup automatically
* Setup home menu:
  * Theme selector
  * Value count selector
  * Layout selector
  * Network setup (bitrate, brightness source, turn-off delay)
  * Factory reset

* Changing value count auto-opens layout carousel
* Layout carousel is swipeable previews
* Tap a slot to configure its slot config (signal, units, poll rate)

#### 10.1 Network Setup

* **Bitrate**: 125 kbps, 250 kbps, 500 kbps
* **Brightness source**: Manual / Auto / J1939 / OBD-II
* **Turn-off delay**: minutes + seconds picker

#### 10.2 Slot Config Flow

1. User taps slot → config picker opens
2. Toggle: **OBD-II** or **J1939**
3. Scrollable list of PIDs (OBD-II) or SPNs (J1939)
4. Select signal → configure units (e.g., MPH/KPH)
5. For OBD-II: configure poll rate

Iterate on UX if lists become unwieldy (search, categories, favorites).

#### 10.3 Stale Display Options

User-configurable per-device (not per-slot):
* Show "----"
* Show blank

---

### 11. Warning & Fault Handling

#### 11.1 MVP: J1939 DM1

* Listen for J1939 DM1 active fault messages
* Severity-based cues (amber/red)
* Each layout defines a **check engine light position**
* CEL is tappable → shows list of active fault codes

#### 11.2 Future: OBD-II DTCs

* OBD-II Mode $03 (stored DTCs), Mode $07 (pending)
* MIL status display
* Architecture must not preclude this; keep DTC storage generic

---

### 12. Configuration Storage

* Storage mechanism TBD (NVS, SPIFFS, or LittleFS—need ESP32-S3 evaluation)
* Versioned config format for future migration
* Persisted settings:
  * `theme_id`
  * `value_count`, `layout_id`
  * `slot_configs[]` (signal_id, units, poll_rate per slot)
  * `stale_display_mode` ("----" or blank)
  * `network_config` (bitrate, brightness_source, turn-off delay)
* Stable IDs; adding layouts won't break old configs

#### 12.1 Factory Reset

* Available in setup menu
* Defaults: theme 1, layout 1, no signals configured, sane network defaults

---

### 13. Performance Targets

* CAN RX loss: 0 frames normal load
* perceived UI: ≥60 FPS
* boot to display: <.2 s
* data latency: <50 ms

---

### 14. Definition of Done (MVP)

* Shows live CAN data on real vehicle
* Smooth UI
* J1939 + OBD coexist intelligently
* config persists across power cycles

---

### 15. Out of Scope (Future)

* OTA updates
* Wi-Fi config portal
* Data logging
* Multi-page dashboards
* CAN-FD
* Custom user-defined themes
* Signal search/filtering in picker
* OBD-II DTC display (post-MVP)

---
