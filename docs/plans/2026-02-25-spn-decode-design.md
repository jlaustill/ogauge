# SPN Decoder Design — Phase 5.2

## Goal

Decode specific J1939 SPN values from CAN frame payloads and print human-readable results to serial. Data-driven config table approach mirroring OSSM's encoder pattern.

## New Files

| File | Responsibility |
|------|---------------|
| `src/data/j1939_decode.cnx` | TSpnConfig struct, SPN config table, generic decode(), printSpnValues() |

## Modified Files

| File | Change |
|------|--------|
| `src/data/twai_driver.cnx` | Call J1939Decode.printSpnValues() after J1939 header print |

## Data Structures

```
struct TSpnConfig {
    u16 spn;         // SPN number (e.g. 171)
    u16 pgn;         // parent PGN (e.g. 65269)
    u8  bytePos;     // start byte in payload (0-indexed)
    u8  byteSize;    // 1 or 2 bytes
    f32 resolution;  // multiplier (e.g. 0.03125)
    f32 offset;      // additive offset (e.g. -273.0)
}
```

## Config Table (5 active SPNs from OSSM)

```
SPN  94:  PGN 65263, byte 0, 1B, res=4.0,     offset=0      "Fuel Pres"
SPN 108:  PGN 65269, byte 0, 1B, res=0.5,     offset=0      "Baro Pres"
SPN 171:  PGN 65269, byte 3, 2B, res=0.03125, offset=-273   "Ambient Temp"
SPN 172:  PGN 65269, byte 5, 1B, res=1.0,     offset=-40    "Air Inlet Temp"
SPN 173:  PGN 65270, byte 5, 2B, res=0.03125, offset=-273   "EGT"
```

## Decode Formula

`physical_value = (raw_bytes * resolution) + offset`

For 2-byte SPNs: raw = `data[bytePos] | (data[bytePos+1] << 8)` (little-endian)

## Serial Output

After existing candump + J1939 header lines, print decoded SPNs for the current PGN:

```
can0  18FEF500  [8]  C8 FF FF 88 22 50 FF FF
  J1939  PGN:65269 (0xFEF5)  SRC:0x00  PRI:6
  SPN 108: 100.0 kPa  SPN 171: 25.3 °C  SPN 172: 40.0 °C
```

Only SPNs belonging to the received PGN are printed. Frames with no configured SPNs show no extra line.

## Data Flow

```
TwaiDriver.poll()
  ↓ twai_message_t
print_frame()                          — candump line
J1939.setCanId()                       — decode header
print_j1939()                          — PGN/SRC/PRI line
  ↓
J1939Decode.printSpnValues(pgn, data)  — NEW: walk config, decode, print
```

## Not in Scope

- Signal store / value caching
- UI changes
- Freshness / stale data detection
- Unit conversion (kPa to PSI, °C to °F)
