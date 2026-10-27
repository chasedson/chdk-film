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
    struct { int mode_rec, mode_video, is_shutter_half_press; } state;
} host_info_t;

typedef struct { int disp_left, disp_width; } host_screen_t;

typedef struct
{
    int film_stock, film_grain, film_grain_size, film_vignette, film_osd, film_camera_color;
    int save_raw, dng_raw;
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

// Drawing is not exercised on the host
typedef int twoColors;
#define MAKE_COLOR(bg, fg)      0
#define COLOR_BLACK 0
#define COLOR_WHITE 0
#define COLOR_RED 0
#define COLOR_GREY_DK 0
#define RECT_BORDER0 0
#define RECT_BORDER1 0
#define DRAW_FILLED 0
#define RECT_ROUND_CORNERS 0
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
static inline void draw_rectangle(int a, int b, int c, int d, int e, int f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static inline int draw_string(int x, int y, const char *s, int c) { (void)x;(void)y;(void)s;(void)c; return 0; }

#endif
