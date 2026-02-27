/**
 * ADR-061 C boundary: lv_image_set_src takes void* — C-Next has no void*.
 * Data lives in needle_img.cnx; this wrapper isolates the unsafe void* cast.
 */
#ifndef NEEDLE_IMG_H_GUARD
#define NEEDLE_IMG_H_GUARD

#include <lvgl.h>
#include "needle_img.hpp"

static inline lv_obj_t * lvgl_helper_create_needle_img(lv_obj_t * parent)
{
    lv_obj_t * img = lv_image_create(parent);
    lv_image_set_src(img, &NeedleImg_dsc);
    lv_image_set_pivot(img, NeedleImg_PIVOT_X, NeedleImg_PIVOT_Y);
    lv_obj_align(img, LV_ALIGN_CENTER,
                 NeedleImg_W / 2 - NeedleImg_PIVOT_X,
                 NeedleImg_H / 2 - NeedleImg_PIVOT_Y);
    return img;
}

#endif /* NEEDLE_IMG_H_GUARD */
