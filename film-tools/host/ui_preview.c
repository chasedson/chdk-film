// Renders the film picker, shooting badge and developing card as they would
// appear on the A570's 360x240 overlay, on top of a dimmed photo standing in
// for the live view.
//
//   ./ui_preview background.bmp outdir

#include "film_host.h"
#include "film.h"

host_sensor_t camera_sensor;
host_info_t camera_info;
host_screen_t camera_screen;
host_conf_t conf;

extern unsigned char canvas[240][360][3];

char *get_raw_image_addr(void) { return 0; }
long get_tick_count(void) { return 0; }
void shooting_set_prop(int id, int v) { (void)id; (void)v; }
void conf_change_dng(void) {}
void gui_set_need_restore(void) {}
long kbd_get_autoclicked_key(void) { return 0; }
long kbd_is_key_clicked(long key) { (void)key; return 0; }
void gui_enum_value_change(int *value, int change, int num_items)
{
    *value += change;
    if (*value < 0) *value = num_items - 1;
    if (*value >= num_items) *value = 0;
}

// Defined in film.c but static; re-declared here through a tiny shim
extern void film_ui_preview_dev_card(int done, int total);

static unsigned char *bg; static int bw, bh;

static void load_bg(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    unsigned char h[54];
    if (!f || fread(h, 1, 54, f) != 54) { fprintf(stderr, "cannot read %s\n", fn); exit(1); }
    int off = *(int*)(h+10); bw = *(int*)(h+18); bh = *(int*)(h+22); int bpp = *(short*)(h+28);
    int flip = bh > 0; if (bh < 0) bh = -bh;
    int bytes = bpp/8, stride = (bw*bytes + 3) & ~3;
    unsigned char *row = malloc(stride); bg = malloc(bw*bh*3);
    fseek(f, off, SEEK_SET);
    for (int y = 0; y < bh; y++)
    {
        fread(row, 1, stride, f);
        int yy = flip ? bh-1-y : y;
        for (int x = 0; x < bw; x++)
            for (int c = 0; c < 3; c++) bg[(yy*bw+x)*3+c] = row[x*bytes+2-c];
    }
    fclose(f); free(row);
}

static void paint_bg(void)
{
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 360; x++)
            for (int c = 0; c < 3; c++)
                canvas[y][x][c] = bg[((y*bh/240)*bw + x*bw/360)*3 + c];
}

static void save(const char *dir, const char *name)
{
    char fn[512]; snprintf(fn, sizeof fn, "%s/%s.bmp", dir, name);
    FILE *f = fopen(fn, "wb");
    int w = 720, h = 480, stride = w*3;     // 2x so it reads well on a monitor
    unsigned char hd[54] = { 'B','M' };
    *(int*)(hd+2) = 54 + stride*h; *(int*)(hd+10) = 54; *(int*)(hd+14) = 40;
    *(int*)(hd+18) = w; *(int*)(hd+22) = h; *(short*)(hd+26) = 1; *(short*)(hd+28) = 24;
    fwrite(hd, 1, 54, f);
    unsigned char *row = malloc(stride);
    for (int y = h-1; y >= 0; y--)
    {
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) row[x*3+c] = canvas[y/2][x/2][2-c];
        fwrite(row, 1, stride, f);
    }
    free(row); fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ui_preview bg.bmp outdir\n"); return 1; }
    load_bg(argv[1]);
    camera_screen.width = 360; camera_screen.height = 240;
    camera_screen.disp_left = 0; camera_screen.disp_width = 360;
    camera_info.state.mode_rec = 1;
    conf.film_osd = 1; conf.film_vignette = 1; conf.film_dev_screen = 1;
    conf.alt_mode_button = 12;      // KEY_PRINT

    conf.film_stock = 1; conf.save_raw = 1; conf.dng_raw = 1;
    paint_bg(); film_draw_osd(0); save(argv[2], "badge");

    paint_bg(); film_picker_draw(1); save(argv[2], "picker_portra");

    conf.film_stock = 6; conf.film_grain = 0; conf.save_raw = 0;
    paint_bg(); film_picker_draw(1); save(argv[2], "picker_e100");

    conf.film_stock = 9; conf.film_grain = 9; conf.film_grain_size = 3; conf.save_raw = 1; conf.dng_raw = 0;
    paint_bg(); film_picker_draw(1); save(argv[2], "picker_hp5");

    conf.film_stock = 1; conf.film_grain = 0; conf.film_grain_size = 0; conf.save_raw = 1; conf.dng_raw = 1;
    paint_bg(); film_ui_preview_dev_card(6, 10); save(argv[2], "developing");
    return 0;
}
