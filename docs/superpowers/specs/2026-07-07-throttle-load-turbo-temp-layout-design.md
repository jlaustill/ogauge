# Dashboard update: swap ODO↔RPM, add throttle/load + turbo MCU temp

- **Date:** 2026-07-07
- **Status:** Approved design (pre-implementation)
- **Repo:** ogauge

## Objective

Three dashboard changes, driven by three new CAN signals:

1. **Swap** the odometer and RPM readouts in the center column.
2. **Add** throttle % and engine load side by side in the center gap (between OIL and RPM).
3. **Add** turbo controller MCU temperature to the top-left, above BOOST.

Layout is iterate-first: coordinates below are sensible starting values, fine-tuned on
hardware after the first flash.

## New signals

Added to `SignalData` in `src/data/signal_data.cnx`, in native J1939 units (conversion at
display time, per existing convention):

| Field                | Source                          | Native unit | Display |
|----------------------|---------------------------------|-------------|---------|
| `throttle_pct`       | SPN 91 (EEC2, PGN 61443, SA 0)  | %           | %       |
| `engine_load_pct`    | SPN 92 (EEC2, PGN 61443, SA 0)  | %           | %       |
| `turbo_mcu_temp_c`   | PGN 65500 die temp (SA 1, OVGT) | °C          | °C      |

## Decoder (`src/data/j1939_decoder.cnx`)

Two new PGN handlers, dispatched with SA gating like the existing ones.

### `decode_61443` — EEC2 (SA 0 / ECM)
- SPN 91 Accelerator Pedal Position 1: byte 2, `raw × 0.4` → throttle %.
- SPN 92 Engine Percent Load At Current Speed: byte 3, `raw × 1` → engine load %.

### `decode_65500` — MCU health (SA 1 / OVGT)
- Turbo MCU die temp: bytes 0–1 (little-endian u16), `raw × 0.03125 − 273` → °C.
  Reuses the same temperature math as SPN 175 (oil temp).
- **Sentinel:** if the u16 raw is `0xFFFF` (N/A), skip the write so the timestamp stays
  stale and the display shows `----` — mirrors the odometer's `0xFFFFFFFF` guard.

### Address-collision note (recorded risk)
OVGT transmits at **SA 0x01**, the same address ogauge already labels `SA_SENSOR` (the OSSM
sensor module). This is a genuine J1939 bus-level address collision between OVGT and OSSM.
Decoding here is still unambiguous because **only OVGT emits PGN 65500**, and we gate by
PGN + SA together. A `SA_TURBO <- 1` constant is added with a comment flagging the shared
address. If OSSM later also broadcasts PGN 65500, ogauge could not distinguish the two —
that would require resolving the underlying address conflict.

## Layout — center column (`src/display/gauge_temp.cnx`)

Current order (top→bottom): EGT, OIL, *[gap]*, ODO, RPM.

- **Swap ODO↔RPM:** RPM takes the upper slot (title y≈268, val y≈303, stays white); ODO
  takes the lower slot (title y≈347, val y≈382, stays gray `ODO (mi)`, keeps the
  ×0.621371 km→mi conversion). Both already use the title-above-value pattern, so this is
  a position swap.
- **Throttle + load, side by side** in the gap (≈ `LV_ALIGN_CENTER` y −25, near the circle's
  widest band). Two readouts offset left/right (x ≈ ∓95): `THR` and `LOAD`, each a compact
  value (montserrat_32) with a small title (montserrat_14) above. Informational — no
  warning styling. The commented-out FUEL block stays parked for later.

## Layout — turbo MCU temp (`src/display/gauge_trans.cnx`)

Owned by GaugeTrans (lives with BOOST on the left edge). A **compact single-line readout**
(`TURBO 42 C`, montserrat_14) placed above BOOST (y higher than −126) and indented right
enough to clear the circle edge at that height. Small footprint — it's a health/diagnostic
value, not a primary gauge, and the top-left corner is tight against the bezel. Shows
`----` when stale.

## Scope ownership

- Center readouts (throttle, load) → **GaugeTemp**.
- Turbo temp → **GaugeTrans**.

Each scope keeps its screen region self-contained, per the codebase pattern.

## C-Next constraints honored

- No `lv_obj_t` arrays or handle params — individual scope fields + inlined `lv_obj_*`
  styling (bugs #995/#996).
- No per-frame `Serial.printf` added to any RX path.

## Out of scope / YAGNI

- No warning thresholds for throttle/load/turbo-temp (all informational for now).
- No decode of the other MCU-health fields (reset cause, boot count, uptime) — only die
  temp is displayed.
- Exact final coordinates/fonts — tuned live on hardware.
