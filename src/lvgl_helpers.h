/**
 * LVGL function declarations for C-Next transpiler.
 *
 * The transpiler's preprocessor can't evaluate LVGL's #if LV_USE_* guards,
 * so widget creation functions like lv_label_create() are invisible.
 * This header re-declares them so the transpiler knows the return types.
 *
 * TODO: Remove when c-next preprocessor handles #if MACRO != 0 in C headers.
 */
#ifndef LVGL_HELPERS_H
#define LVGL_HELPERS_H

#include <lvgl.h>

/* Label widget (behind #if LV_USE_LABEL) */
lv_obj_t * lv_label_create(lv_obj_t * parent);
void lv_label_set_text(lv_obj_t * obj, const char * text);

#endif
