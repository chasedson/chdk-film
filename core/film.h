#ifndef FILM_H
#define FILM_H

// Film simulation: per-stock tone curves, colour, grain and vignette applied
// to the raw buffer after RAW/DNG has been saved, so the camera develops the
// JPEG from the filtered data while the RAW file stays untouched.

#define FILM_GRAIN_STOCK    0       // conf.film_grain: 0 = stock default, 1 = off, 2..11 = amount 1..10
#define FILM_SIZE_STOCK     0       // conf.film_grain_size: 0 = stock default, 1..3 = fine/medium/coarse

typedef struct
{
    const char *name;       // full name shown in menu
    const char *tag;        // short name for the on-screen badge
    short my_colors;        // Canon My Colors mode to select (-1 = leave alone)
    short saturation;       // Q12, 4096 = unchanged, 0 = monochrome
    short contrast;         // Q12 S-curve strength around mid grey
    short lift;             // Q12 black lift (faded blacks)
    short white;            // Q12 output white point (muted highlights)
    short shadow[3];        // Q12 R,G,B tint added in the shadows
    short highlight[3];     // Q12 R,G,B tint added in the highlights
    short grain;            // default grain amount 0..10
    short grain_size;       // default grain size 0..2 (fine/medium/coarse)
    short vignette;         // Q12 corner falloff per axis, 0 = none
} film_stock_t;

extern const film_stock_t film_stocks[];
extern const int film_stock_count;

extern void film_apply(void);                   // raw hook: process current raw buffer
extern void film_settings_changed(void);        // invalidate cached curves, re-apply camera colours
extern void film_draw_osd(int is_osd_edit);     // shooting screen badge

// Menu enum handlers
extern const char* gui_film_stock_enum(int change, int arg);
extern const char* gui_film_grain_enum(int change, int arg);
extern const char* gui_film_grain_size_enum(int change, int arg);
extern const char* gui_film_raw_enum(int change, int arg);

#endif
