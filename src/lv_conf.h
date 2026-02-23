#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable this config file */
#if 1

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16  /* RGB565 for ST7701S */

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0                /* Use LVGL's built-in allocator (no stdlib malloc) */
#define LV_MEM_SIZE (128U * 1024U)     /* 128KB static pool */

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD 16  /* ~60 FPS refresh target */

/*====================
   OS SETTINGS
 *====================*/
#define LV_USE_OS LV_OS_NONE  /* Single-threaded for now */

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   THEME SETTINGS
 *====================*/
#define LV_USE_THEME_DEFAULT 1

#endif /* #if 1 */

#endif /* LV_CONF_H */
