// Film preview: simulates an A570 10-bit raw frame from a BMP, runs film_apply()
// for each stock, develops the result with a simple 2x2 binning pipeline and
// writes one BMP per stock.
//
//   cc -O2 -I. -o preview preview.c ../../core/film.c
//   ./preview input.bmp outdir [grain override 0..11]

#include <math.h>
#include <time.h>
#include "film_host.h"
#include "../../core/film.h"

host_sensor_t camera_sensor;
host_info_t camera_info;
host_screen_t camera_screen;
host_conf_t conf;

static unsigned char *rawbuf;

char *get_raw_image_addr(void) { return (char*)rawbuf; }
long get_tick_count(void) { return (long)clock(); }
void shooting_set_prop(int id, int v) { (void)id; (void)v; }
void conf_change_dng(void) {}
void gui_set_need_restore(void) {}
unsigned long get_batt_perc(void) { return 87; }
unsigned int GetFreeCardSpaceKb(void) { return 29 * 1024 * 1024 + 300 * 1024; }
void debug_led(int state) { (void)state; }
long kbd_get_autoclicked_key(void) { return 0; }
long kbd_is_key_clicked(long key) { (void)key; return 0; }
void gui_enum_value_change(int *value, int change, int num_items)
{
    *value += change;
    if (*value < 0) *value = num_items - 1;
    if (*value >= num_items) *value = 0;
}

// A570 geometry (platform/a570/platform_camera.h)
#define ROWPIX  3152
#define ROWS    2340
#define AX1 38
#define AY1 14
#define AX2 3146
#define AY2 2338
#define BLACK   32
#define WHITE   1023
// Rough sensor white balance: what the camera would multiply R and B by in daylight
#define WB_R    1.9
#define WB_B    1.5

static int img_w, img_h;
static unsigned char *img;      // RGB

static int read_bmp(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    if (!f) return 0;
    unsigned char h[54];
    if (fread(h, 1, 54, f) != 54) return 0;
    int off = *(int*)(h+10), w = *(int*)(h+18), hh = *(int*)(h+22), bpp = *(short*)(h+28);
    int flip = hh > 0; if (hh < 0) hh = -hh;
    int bytes = bpp / 8, stride = (w*bytes + 3) & ~3;
    unsigned char *row = malloc(stride);
    img = malloc(w*hh*3); img_w = w; img_h = hh;
    fseek(f, off, SEEK_SET);
    for (int y = 0; y < hh; y++)
    {
        fread(row, 1, stride, f);
        int yy = flip ? hh-1-y : y;
        for (int x = 0; x < w; x++)
        {
            img[(yy*w+x)*3+0] = row[x*bytes+2];
            img[(yy*w+x)*3+1] = row[x*bytes+1];
            img[(yy*w+x)*3+2] = row[x*bytes+0];
        }
    }
    free(row); fclose(f);
    return 1;
}

static void write_bmp(const char *fn, const unsigned char *rgb, int w, int h)
{
    FILE *f = fopen(fn, "wb");
    int stride = (w*3 + 3) & ~3;
    unsigned char hd[54] = { 'B','M' };
    *(int*)(hd+2) = 54 + stride*h; *(int*)(hd+10) = 54; *(int*)(hd+14) = 40;
    *(int*)(hd+18) = w; *(int*)(hd+22) = h; *(short*)(hd+26) = 1; *(short*)(hd+28) = 24;
    fwrite(hd, 1, 54, f);
    unsigned char *row = calloc(1, stride);
    for (int y = h-1; y >= 0; y--)
    {
        for (int x = 0; x < w; x++)
        {
            row[x*3+0] = rgb[(y*w+x)*3+2];
            row[x*3+1] = rgb[(y*w+x)*3+1];
            row[x*3+2] = rgb[(y*w+x)*3+0];
        }
        fwrite(row, 1, stride, f);
    }
    free(row); fclose(f);
}

static void set_px(int x, int y, int v)
{
    unsigned char *a = rawbuf + y*camera_sensor.raw_rowlen + (x/8)*10;
    switch (x%8) {
        case 0: a[0]=(a[0]&0x3F)|(v<<6); a[1]=v>>2;                  break;
        case 1: a[0]=(a[0]&0xC0)|(v>>4); a[3]=(a[3]&0x0F)|(v<<4); break;
        case 2: a[2]=(a[2]&0x03)|(v<<2); a[3]=(a[3]&0xF0)|(v>>6); break;
        case 3: a[2]=(a[2]&0xFC)|(v>>8); a[5]=v;                     break;
        case 4: a[4]=v>>2;               a[7]=(a[7]&0x3F)|(v<<6); break;
        case 5: a[6]=(a[6]&0x0F)|(v<<4); a[7]=(a[7]&0xC0)|(v>>4); break;
        case 6: a[6]=(a[6]&0xF0)|(v>>6); a[9]=(a[9]&0x03)|(v<<2); break;
        case 7: a[8]=v;                  a[9]=(a[9]&0xFC)|(v>>8); break;
    }
}

static int get_px(int x, int y)
{
    unsigned char *a = rawbuf + y*camera_sensor.raw_rowlen + (x/8)*10;
    switch (x%8) {
        case 0: return ((0x3fc&(a[1]<<2)) | (a[0] >> 6));
        case 1: return ((0x3f0&(a[0]<<4)) | (a[3] >> 4));
        case 2: return ((0x3c0&(a[3]<<6)) | (a[2] >> 2));
        case 3: return ((0x300&(a[2]<<8)) | (a[5]));
        case 4: return ((0x3fc&(a[4]<<2)) | (a[7] >> 6));
        case 5: return ((0x3f0&(a[7]<<4)) | (a[6] >> 4));
        case 6: return ((0x3c0&(a[6]<<6)) | (a[9] >> 2));
        case 7: return ((0x300&(a[9]<<8)) | (a[8]));
    }
    return 0;
}

static double lin(int v) { return pow(v / 255.0, 2.2); }

// Mosaic the input (scaled to the active area) into an RGGB raw frame
static void make_raw(void)
{
    memset(rawbuf, 0, camera_sensor.raw_size);
    int range = WHITE - BLACK;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < ROWPIX; x++)
        {
            int v = BLACK;
            if (x >= AX1 && x < AX2 && y >= AY1 && y < AY2)
            {
                int sx = (x - AX1) * img_w / (AX2 - AX1);
                int sy = (y - AY1) * img_h / (AY2 - AY1);
                const unsigned char *p = img + (sy*img_w + sx)*3;
                int c = (y & 1) ? ((x & 1) ? 2 : 1) : ((x & 1) ? 1 : 0);
                double l = lin(p[c]) * 0.85;          // leave a little headroom
                if (c == 0) l /= WB_R;
                if (c == 2) l /= WB_B;
                v = BLACK + (int)(l * range + 0.5);
                if (v > WHITE) v = WHITE;
            }
            set_px(x, y, v);
        }
}

// Develop: 2x2 binning, white balance, optional mono (camera B/W mode), gamma
static void develop(unsigned char *out, int ow, int oh, int mono)
{
    int range = WHITE - BLACK;
    for (int oy = 0; oy < oh; oy++)
        for (int ox = 0; ox < ow; ox++)
        {
            int x = AX1 + ((ox * (AX2 - AX1 - 2) / ow) & ~1);
            int y = AY1 + ((oy * (AY2 - AY1 - 2) / oh) & ~1);
            double r = (get_px(x, y) - BLACK) / (double)range * WB_R;
            double g = ((get_px(x+1, y) + get_px(x, y+1)) / 2.0 - BLACK) / range;
            double b = (get_px(x+1, y+1) - BLACK) / (double)range * WB_B;
            r /= 0.85; g /= 0.85; b /= 0.85;
            if (mono) r = g = b = 0.3*r + 0.59*g + 0.11*b;
            double c[3] = { r, g, b };
            for (int k = 0; k < 3; k++)
            {
                double v = c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k];
                out[(oy*ow+ox)*3+k] = (unsigned char)(pow(v, 1/2.2) * 255 + 0.5);
            }
        }
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: preview in.bmp outdir [grain]\n"); return 1; }
    if (!read_bmp(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    camera_sensor.bits_per_pixel = 10;
    camera_sensor.black_level = BLACK;
    camera_sensor.white_level = WHITE;
    camera_sensor.raw_rowpix = ROWPIX;
    camera_sensor.raw_rows = ROWS;
    camera_sensor.raw_rowlen = ROWPIX * 10 / 8;
    camera_sensor.raw_size = camera_sensor.raw_rowlen * ROWS;
    camera_sensor.active_area.x1 = AX1; camera_sensor.active_area.y1 = AY1;
    camera_sensor.active_area.x2 = AX2; camera_sensor.active_area.y2 = AY2;
    camera_sensor.cfa_pattern = 0x02010100;
    conf.film_vignette = 1;
    conf.film_grain = argc > 3 ? atoi(argv[3]) : 0;

    rawbuf = malloc(camera_sensor.raw_size);
    int ow = 1024, oh = 768;
    unsigned char *out = malloc(ow*oh*3);

    // contact sheet: 4 columns of 1/3 size frames
    int tw = ow/3, th = oh/3, cols = 4, rows = (film_stock_count + cols - 1) / cols;
    int cw = cols*tw, ch = rows*th;
    unsigned char *sheet = calloc(1, cw*ch*3);

    for (int s = 0; s < film_stock_count; s++)
    {
        conf.film_stock = s;
        film_settings_changed();
        make_raw();
        clock_t t = clock();
        film_apply();
        double ms = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
        develop(out, ow, oh, film_stocks[s].my_colors == 3);
        char fn[512];
        snprintf(fn, sizeof fn, "%s/%02d.bmp", argv[2], s);
        write_bmp(fn, out, ow, oh);
        int cx = (s % cols) * tw, cy = (s / cols) * th;
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++)
                memcpy(sheet + ((cy+y)*cw + cx+x)*3, out + ((y*3)*ow + x*3)*3, 3);
        printf("%-18s %6.1f ms\n", film_stocks[s].name, ms);
    }
    char fn[512];
    snprintf(fn, sizeof fn, "%s/contact.bmp", argv[2]);
    write_bmp(fn, sheet, cw, ch);
    return 0;
}
