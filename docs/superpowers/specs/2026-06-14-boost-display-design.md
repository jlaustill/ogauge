# Boost Display — Design Spec

**Date:** 2026-06-14
**Status:** Approved
**Scope:** Capture and display turbocharger boost pressure (SPN 102) on the round dashboard.

## Goal

Show turbo boost pressure on the dashboard. The signal is already arriving on the
bus — boost (SPN 102) rides in **PGN 65270**, the same frame already decoded for
EGT (SPN 173). We only need to extract one more byte and add one more readout.

## Signal Source (authoritative, J1939-71)

| Property | Value |
|----------|-------|
| Name | Engine Intake Manifold #1 Pressure (SPN 102) |
| PGN | 65270 (already decoded for EGT, gated to `SA_SENSOR`) |
| Byte (0-indexed) | 1 (spec position 2), 1 byte |
| Resolution | 2 kPa/bit |
| Offset | 0 |
| Range | 0–500 kPa (≈ 0–72.5 psi) |
| Type | gauge pressure (Measured) |

## Decisions

- **Units:** display in **psi** (US diesel convention). Stored natively in kPa;
  converted at display time (matches the existing `SignalData` convention:
  "Unit conversion (kPa→PSI, C→F) happens at display time").
- **Placement:** top-left, as the **vertical mirror** of the bottom-left trans
  temp readout, across the middle horizontal axis. Lives in the `GaugeTrans`
  scope (which owns the left column).
- **Warnings:** none. Plain white readout — boost is informational; overboost is
  rare on a stock truck. No `warn_level`/border styling.

## Changes

### 1. Signal storage — `src/data/signal_data.cnx`

Add one field to `SignalData`:

```cnx
Signal boost_kpa;
```

Stored in native kPa, alongside `barometric_pressure_kpa` / `fuel_pressure_kpa`.

### 2. Decode — `src/data/j1939_decoder.cnx`

Inside the existing `decode_65270` (already runs on PGN 65270 / `SA_SENSOR`), add
the SPN 102 extraction next to the EGT decode. Single byte — mirrors the
barometric/fuel single-byte decodes, no `extract_u16`, no sign concerns:

```cnx
// SPN 102 - Engine Intake Manifold #1 Pressure (boost): byte 1, 1 byte, 2 kPa/bit
u8 raw102 <- data[1];
SignalStore.current.boost_kpa.value <- raw102 * 2.0;
SignalStore.current.boost_kpa.time <- global.millis();
```

### 3. Display — `src/display/gauge_trans.cnx`

Add two label fields to the `GaugeTrans` scope:

```cnx
lv_obj_t boost_title;
lv_obj_t boost_val;
```

**create():** place at the vertical mirror of the trans temp (which is at
`LEFT_MID, 36, +81` value / `LEFT_MID, 31, +38` title). Value on top, label below:

- value `boost_val` → `LV_ALIGN_LEFT_MID, 36, -81`, font montserrat_40, white
- title `boost_title` "BOOST" → `LV_ALIGN_LEFT_MID, 31, -38`, font montserrat_32, white

No border/radius/pad styling (no warning box).

**update():** staleness check using the shared `SIGNAL_STALE_MS`, then convert
kPa→psi for display. Reuses the existing 100ms `GaugeTrans` update timer:

```cnx
u32 boost_age <- now - SignalStore.current.boost_kpa.time;
if (boost_age > SIGNAL_STALE_MS) {
  global.lv_label_set_text(this.boost_val, "---- psi");
} else {
  i32 psi <- SignalStore.current.boost_kpa.value * 0.145038;
  global.lv_label_set_text_fmt(this.boost_val, "%d psi", psi);
}
```

## Verification

- Build + flash with the truck running.
- Boost should read ~0 psi at idle and climb under load (stock 5.9 Cummins peaks
  ~30–40 psi).
- Expect a small on-hardware x/y nudge for top-left crowding near the SVC chip
  and the circular bezel (per the round-display UI workflow).

## Out of Scope

- Warning / overboost thresholds.
- Other PGN 65270 SPNs (intake manifold temp, air-filter differential pressure).
- A psi/kPa/bar unit toggle.
