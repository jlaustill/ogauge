# Phase 4 Design: Needle Gauge Widget with Touch-Driven Speed

## Goal

Build a touch-interactive speedometer needle gauge on the 480x480 round display using LVGL v9.2's built-in scale and arc widgets, driven by mock touch input (0-160 mph).

## Architecture

**Approach: Scale + Arc Overlay**

- `lv_scale` (ROUND_INNER mode) — circular gauge face with ticks, labels, and color-coded speed zones
- `lv_line` needle — child of scale, positioned via `lv_scale_set_line_needle_value()`
- `lv_arc` overlay — transparent/invisible, same geometry, handles all touch input natively
- `lv_label` center — digital readout showing current speed ("85 mph")
- `lv_anim` — smooth needle transition with ease-out easing

## Layer Placement

```
display_st7701.cnx  — panel driver (unchanged)
touch_cst820.cnx    — touch driver (unchanged)
lvgl_port.cnx       — LVGL glue (unchanged, remove demo label)
gauge_speed.cnx     — NEW: speedometer widget + touch input
main.cnx            — orchestrator: init + create gauge
```

`gauge_speed.cnx` owns all gauge LVGL objects. `main.cnx` calls `GaugeSpeed.create()` after LVGL init.

## Touch Flow

```
User drags on screen
  → lv_arc (invisible) captures touch natively
  → LV_EVENT_VALUE_CHANGED fires
  → Callback reads lv_arc_get_value() → new speed
  → lv_anim animates needle from old to new value (ease-out)
  → Center label updates to show "N mph"
```

## Visual Layout (480x480)

- **Scale:** ~420x420 centered, 270-degree arc (bottom-left to bottom-right)
- **Needle:** ~180px, rounded tip, white/themed color
- **Speed zones:** 0-60 green, 60-100 yellow, 100-160 red
- **Center label:** Montserrat 40pt, "85 mph"
- **Background:** dark circle, LVGL theme default

## lv_conf.h Additions

- `LV_USE_SCALE 1`
- `LV_USE_ARC 1`
- `LV_USE_LINE 1`
- `LV_FONT_MONTSERRAT_40 1`

## C-Next Considerations

- All LVGL calls via `global.` prefix
- Expand `lvgl_helpers.h` with scale/arc/line/anim prototypes (bug #945 workaround)
- New callback pattern: `void cb(lv_event_t e)` for arc value-changed events — verify C-Next generates correct signature

## Not in Scope

- Layout system (just one full-screen gauge)
- Theme system (hardcoded colors)
- Signal catalog (mock speed from touch only)
- CAN integration
