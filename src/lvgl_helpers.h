/**
 * LVGL function declarations for C-Next transpiler.
 *
 * The transpiler's preprocessor can't evaluate LVGL's #if LV_USE_* guards,
 * so widget creation functions are invisible to C-Next (bug #945).
 * This header re-declares them so the transpiler knows the return types.
 *
 * Also provides C helpers for:
 * - lv_color_t params (value-type struct — C-Next adds * via ADR-006)
 * - lv_style_t init (needs & on scope variables — global. doesn't add &)
 * - Variadic functions (lv_label_set_text_fmt)
 */
#ifndef LVGL_HELPERS_H
#define LVGL_HELPERS_H

#include <lvgl.h>
#include <inttypes.h>

/* ─── Handle typedef for scope variables (bug #948 workaround) ─── */

/*
 * lv_obj_t is a forward-declared struct. C-Next scope variables of this type
 * generate as value types (static lv_obj_t x = {}) instead of pointers.
 * A pointer typedef makes the transpiler treat it as a handle type, matching
 * the esp_lcd_panel_handle_t pattern that already works.
 * TODO: Remove when #948 is fixed (transpiler handles incomplete struct scope vars).
 */
typedef lv_obj_t * lv_obj_handle_t;

/* ─── Label widget (behind #if LV_USE_LABEL) ─── */

lv_obj_t * lv_label_create(lv_obj_t * parent);
void lv_label_set_text(lv_obj_t * obj, const char * text);

/* ─── Scale widget (behind #if LV_USE_SCALE) ─── */

lv_obj_t * lv_scale_create(lv_obj_t * parent);
void lv_scale_set_mode(lv_obj_t * obj, lv_scale_mode_t mode);

/* Set scale to round-inner mode (avoids int→enum conversion in C++) */
static inline void lvgl_helper_scale_set_round_inner(lv_obj_t * scale)
{
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
}
void lv_scale_set_total_tick_count(lv_obj_t * obj, uint32_t total_tick_count);
void lv_scale_set_major_tick_every(lv_obj_t * obj, uint32_t major_tick_every);
void lv_scale_set_label_show(lv_obj_t * obj, bool show_label);
void lv_scale_set_range(lv_obj_t * obj, int32_t min, int32_t max);
void lv_scale_set_angle_range(lv_obj_t * obj, uint32_t angle_range);
void lv_scale_set_rotation(lv_obj_t * obj, int32_t rotation);
void lv_scale_set_line_needle_value(lv_obj_t * obj, lv_obj_t * needle_line,
                                     int32_t needle_length, int32_t value);
lv_scale_section_t * lv_scale_add_section(lv_obj_t * obj);
void lv_scale_set_section_range(lv_obj_t * scale, lv_scale_section_t * section,
                                 int32_t min, int32_t max);
void lv_scale_set_section_style_main(lv_obj_t * scale, lv_scale_section_t * section,
                                      const lv_style_t * style);
void lv_scale_set_section_style_indicator(lv_obj_t * scale, lv_scale_section_t * section,
                                           const lv_style_t * style);
void lv_scale_set_section_style_items(lv_obj_t * scale, lv_scale_section_t * section,
                                       const lv_style_t * style);

/* ─── Arc widget (behind #if LV_USE_ARC) ─── */

lv_obj_t * lv_arc_create(lv_obj_t * parent);
void lv_arc_set_range(lv_obj_t * obj, int32_t min, int32_t max);
void lv_arc_set_value(lv_obj_t * obj, int32_t value);
void lv_arc_set_bg_angles(lv_obj_t * obj, lv_value_precise_t start, lv_value_precise_t end);
void lv_arc_set_rotation(lv_obj_t * obj, int32_t rotation);
int32_t lv_arc_get_value(const lv_obj_t * obj);

/* ─── Line widget (behind #if LV_USE_LINE) ─── */

lv_obj_t * lv_line_create(lv_obj_t * parent);

/* ─── C helpers: lv_color_t value-type workaround ─── */

/*
 * C-Next passes all non-primitive params by reference (ADR-006), but LVGL's
 * style functions take lv_color_t by value. These helpers take uint32_t hex
 * instead, avoiding the pointer mismatch.
 */

static inline void lvgl_helper_obj_style_line_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_line_color(obj, lv_color_hex(hex), sel);
}

static inline void lvgl_helper_obj_style_bg_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(hex), sel);
}

static inline void lvgl_helper_obj_style_text_color_hex(
    lv_obj_t * obj, uint32_t hex, lv_style_selector_t sel)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(hex), sel);
}

/* ─── C helper: font reference (needs & on global, C-Next can't do &) ─── */

static inline void lvgl_helper_obj_style_text_font_40(
    lv_obj_t * obj, lv_style_selector_t sel)
{
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_40, sel);
}

/* ─── C helper: variadic label format + PRId32 ─── */

static inline void lvgl_helper_label_set_speed(lv_obj_t * label, int32_t speed)
{
    lv_label_set_text_fmt(label, "%" PRId32 " mph", speed);
}

/* ─── C helper: speed zone sections ─── */

/*
 * Encapsulates lv_style_t init + lv_color_t + section API in pure C.
 * C-Next can't call these directly because:
 * - lv_style_init needs & on scope variables (global. doesn't add &)
 * - lv_style_set_arc_color takes lv_color_t by value (ADR-006 adds *)
 */

static inline void lvgl_helper_add_speed_sections(lv_obj_t * scale)
{
    static lv_style_t green_main, green_ind, green_items;
    static lv_style_t yellow_main, yellow_ind, yellow_items;
    static lv_style_t red_main, red_ind, red_items;

    /* Green zone: 0-60 mph */
    lv_style_init(&green_main);
    lv_style_set_arc_color(&green_main, lv_color_hex(0x44BB44));
    lv_style_set_arc_width(&green_main, 8);
    lv_style_init(&green_ind);
    lv_style_set_line_color(&green_ind, lv_color_hex(0x44BB44));
    lv_style_init(&green_items);
    lv_style_set_line_color(&green_items, lv_color_hex(0x44BB44));

    lv_scale_section_t * green = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, green, 0, 60);
    lv_scale_set_section_style_main(scale, green, &green_main);
    lv_scale_set_section_style_indicator(scale, green, &green_ind);
    lv_scale_set_section_style_items(scale, green, &green_items);

    /* Yellow zone: 60-100 mph */
    lv_style_init(&yellow_main);
    lv_style_set_arc_color(&yellow_main, lv_color_hex(0xDDCC22));
    lv_style_set_arc_width(&yellow_main, 8);
    lv_style_init(&yellow_ind);
    lv_style_set_line_color(&yellow_ind, lv_color_hex(0xDDCC22));
    lv_style_init(&yellow_items);
    lv_style_set_line_color(&yellow_items, lv_color_hex(0xDDCC22));

    lv_scale_section_t * yellow = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, yellow, 60, 100);
    lv_scale_set_section_style_main(scale, yellow, &yellow_main);
    lv_scale_set_section_style_indicator(scale, yellow, &yellow_ind);
    lv_scale_set_section_style_items(scale, yellow, &yellow_items);

    /* Red zone: 100-160 mph */
    lv_style_init(&red_main);
    lv_style_set_arc_color(&red_main, lv_color_hex(0xDD3333));
    lv_style_set_arc_width(&red_main, 8);
    lv_style_init(&red_ind);
    lv_style_set_line_color(&red_ind, lv_color_hex(0xDD3333));
    lv_style_init(&red_items);
    lv_style_set_line_color(&red_items, lv_color_hex(0xDD3333));

    lv_scale_section_t * red = lv_scale_add_section(scale);
    lv_scale_set_section_range(scale, red, 100, 160);
    lv_scale_set_section_style_main(scale, red, &red_main);
    lv_scale_set_section_style_indicator(scale, red, &red_ind);
    lv_scale_set_section_style_items(scale, red, &red_items);
}

#endif /* LVGL_HELPERS_H */
