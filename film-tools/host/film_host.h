// Host stand-ins for the CHDK headers film.c uses, so the film pipeline can be
// compiled and run on a Mac against a simulated A570 raw frame.
#ifndef FILM_HOST_H
#define FILM_HOST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAM_SENSOR_BITS_PER_PIXEL   10
#define PROPCASE_MY_COLORS          184

typedef struct
{
    int bits_per_pixel, black_level, white_level;
    unsigned raw_rows, raw_rowpix, raw_rowlen, raw_size;
    struct { int y1, x1, y2, x2; } active_area;
    int cfa_pattern;
} host_sensor_t;

typedef struct
{
    struct { int mode_rec, mode_video, is_shutter_half_press, state_kbd_script_run; } state;
} host_info_t;

typedef struct { int width, height, disp_left, disp_width; } host_screen_t;

typedef struct
{
    int film_stock, film_grain, film_grain_size, film_vignette, film_osd, film_camera_color, film_dev_screen;
    int save_raw, dng_raw;
    long alt_mode_button;
} host_conf_t;

extern host_sensor_t camera_sensor;
extern host_info_t camera_info;
extern host_screen_t camera_screen;
extern host_conf_t conf;

extern char *get_raw_image_addr(void);
extern long get_tick_count(void);
extern void shooting_set_prop(int id, int v);
extern void gui_enum_value_change(int *value, int change, int num_items);
extern void conf_change_dng(void);
extern void gui_set_need_restore(void);
extern unsigned long get_batt_perc(void);
extern unsigned int GetFreeCardSpaceKb(void);
extern void debug_led(int state);
extern long kbd_get_autoclicked_key(void);
extern long kbd_is_key_clicked(long key);

#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 3
#define KEY_RIGHT 4
#define KEY_SET 5
#define KEY_DISPLAY 11

// Drawing: implemented in host_draw.c onto a 360x240 canvas, same semantics as CHDK
typedef int twoColors;
typedef int color;
#define MAKE_COLOR(bg, fg)      ((twoColors)(((bg) << 8) | (fg)))
#define FG_COLOR(c)             ((c) & 0xFF)
#define BG_COLOR(c)             (((c) >> 8) & 0xFF)
#define COLOR_TRANSPARENT 0
#define COLOR_BLACK 1
#define COLOR_GREY_DK 2
#define COLOR_GREY 3
#define COLOR_GREY_LT 4
#define COLOR_WHITE 5
#define COLOR_RED 6
#define TEXT_LEFT 0
#define TEXT_CENTER 1
#define TEXT_RIGHT 2
#define RECT_BORDER0 0
#define RECT_BORDER1 1
#define DRAW_FILLED 0x100
#define RECT_ROUND_CORNERS 0x200
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
extern void draw_rectangle(int x1, int y1, int x2, int y2, twoColors cl, int flags);
extern int  draw_string(int x, int y, const char *s, twoColors cl);
extern int  draw_string_justified(int x, int y, const char *s, twoColors cl, int xo, int max_width, int justification);
extern void draw_string_scaled(int x, int y, const char *s, twoColors cl, int xsize, int ysize);
extern void draw_hline(int x, int y, int len, color cl);
extern void draw_vline(int x, int y, int len, color cl);

#endif
