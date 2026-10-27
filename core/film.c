#include "platform.h"
#include "camera_info.h"
#include "conf.h"
#include "raw.h"
#include "properties.h"
#include "gui.h"
#include "gui_draw.h"
#include "keyboard.h"
#include "gui_batt.h"
#include "sd_card.h"
#include "backlight.h"
#include "film.h"

/*
    Film simulation

    Runs in the raw hook after any RAW/DNG has been written. Each 2x2 Bayer
    quad is white balanced with a grey-world estimate, then:

        vignette -> saturation -> grain -> per-channel tone curve

    and un-balanced again so the camera's own JPEG pipeline develops the result.
    Tone curves are built in a perceptual (gamma 2) space so contrast, lift and
    tints behave like they would on a print. All maths is integer, Q12 (4096 = 1.0).
*/

#define Q(x)        ((short)((x)*4096))
#define ONE         4096
#define LUT_SIZE    4096        // balanced linear input range 0..4095
#define PIVOT       1740        // perceptual mid grey (~18% linear)

// My Colors propcase values (propset 2)
#define MC_OFF      0
#define MC_VIVID    1
#define MC_BW       3
#define MC_POSITIVE 5

const film_stock_t film_stocks[] =
{
    //  name               tag        description                                 my_colors    sat       contrast  lift      white      shadow tint R,G,B                highlight tint R,G,B              grain size vignette
    { "Off",              "",        "No film look, camera colour only",         -1,          Q(1.00),  Q(0.00),  Q(0.000), Q(1.000), { 0, 0, 0 },                    { 0, 0, 0 },                     0, 0, 0 },
    { "Portra 400",       "PORTRA",  "Soft contrast, warm skin, lifted blacks",  MC_OFF,      Q(0.90),  Q(0.08),  Q(0.030), Q(0.975), { Q(0.010),  Q(0.004), Q(-0.004) }, { Q(0.016),  Q(0.006), Q(-0.018) }, 3, 0, 0 },
    { "Ektar 100",        "EKTAR",   "Punchy colour, fine grain",                MC_VIVID,    Q(1.15),  Q(0.18),  Q(0.000), Q(1.000), { Q(-0.004), Q(0.000), Q(0.010) },  { Q(0.010),  Q(0.004), Q(-0.008) }, 1, 0, 0 },
    { "Gold 200",         "GOLD",    "Warm golden highlights",                   MC_OFF,      Q(1.05),  Q(0.12),  Q(0.020), Q(0.985), { Q(0.012),  Q(0.006), Q(-0.006) }, { Q(0.026),  Q(0.014), Q(-0.030) }, 3, 0, 0 },
    { "Superia 400",      "SUPERIA", "Green shadows, cool everyday colour",      MC_OFF,      Q(1.00),  Q(0.12),  Q(0.025), Q(0.980), { Q(-0.010), Q(0.012), Q(0.006) },  { Q(0.000),  Q(0.004), Q(0.006) },  4, 0, 0 },
    { "Velvia 50",        "VELVIA",  "Vivid slide film, deep contrast",          MC_POSITIVE, Q(1.30),  Q(0.28),  Q(0.000), Q(1.000), { Q(-0.004), Q(-0.006), Q(0.012) }, { Q(0.006),  Q(0.000), Q(-0.006) }, 1, 0, Q(0.10) },
    { "Ektachrome E100",  "E100",    "Clean slide film, cool blues",             MC_OFF,      Q(1.12),  Q(0.20),  Q(0.000), Q(1.000), { Q(-0.006), Q(0.000), Q(0.016) },  { Q(-0.004), Q(0.002), Q(0.012) },  1, 0, 0 },
    { "Cinestill 800T",   "800T",    "Tungsten night film, teal shadows",        MC_OFF,      Q(1.00),  Q(0.14),  Q(0.020), Q(0.985), { Q(-0.012), Q(0.010), Q(0.030) },  { Q(0.012),  Q(0.000), Q(-0.004) }, 6, 1, 0 },
    { "Tri-X 400",        "TRI-X",   "Contrasty black & white, classic grain",   MC_BW,       Q(0.00),  Q(0.25),  Q(0.010), Q(1.000), { 0, 0, 0 },                    { 0, 0, 0 },                     6, 1, 0 },
    { "HP5 Plus",         "HP5",     "Soft black & white, coarse grain",         MC_BW,       Q(0.00),  Q(0.12),  Q(0.030), Q(0.980), { 0, 0, 0 },                    { 0, 0, 0 },                     7, 2, 0 },
    { "T-Max 100",        "T-MAX",   "Smooth, fine black & white",               MC_BW,       Q(0.00),  Q(0.15),  Q(0.000), Q(1.000), { 0, 0, 0 },                    { 0, 0, 0 },                     2, 0, 0 },
    { "Expired Lomo",     "LOMO",    "Faded, cross-processed, dark corners",     MC_OFF,      Q(1.20),  Q(0.25),  Q(0.060), Q(0.950), { Q(-0.016), Q(0.010), Q(0.020) },  { Q(0.024),  Q(0.016), Q(-0.024) }, 7, 2, Q(0.45) },
};

const int film_stock_count = sizeof(film_stocks)/sizeof(film_stocks[0]);

static const char *size_names[]  = { "Auto", "Fine", "Medium", "Coarse" };

//-------------------------------------------------------------------
// Settings helpers

static const film_stock_t* current_stock()
{
    int i = conf.film_stock;
    if (i < 0 || i >= film_stock_count) i = 0;
    return &film_stocks[i];
}

static int grain_amount()
{
    if (conf.film_grain == FILM_GRAIN_STOCK) return current_stock()->grain;
    return conf.film_grain - 1;     // 1 = off -> 0
}

static int grain_size()
{
    if (conf.film_grain_size == FILM_SIZE_STOCK) return current_stock()->grain_size;
    return conf.film_grain_size - 1;
}

// Each film brings its own grain, so changing stock goes back to the stock's grain
static void set_stock(int i)
{
    if (i < 0) i = film_stock_count - 1;
    if (i >= film_stock_count) i = 0;
    conf.film_stock = i;
    conf.film_grain = FILM_GRAIN_STOCK;
    conf.film_grain_size = FILM_SIZE_STOCK;
    film_settings_changed();
}

static void set_grain(int amount)
{
    if (amount < 0) amount = 0;
    if (amount > 10) amount = 10;
    conf.film_grain = amount + 1;
}

static const char* grain_text()
{
    static char buf[16];
    int a = grain_amount();
    if (a == 0) strcpy(buf, "Off");
    else sprintf(buf, "%d", a);
    if (conf.film_grain == FILM_GRAIN_STOCK) strcat(buf, " (auto)");
    return buf;
}

static const char* raw_text()
{
    return !conf.save_raw ? "Off" : conf.dng_raw ? "DNG" : "CRW";
}

//-------------------------------------------------------------------
// Curves

static unsigned short *lut = 0;     // 3 * LUT_SIZE: R, G, B
static int lut_stock = -1;
static int lut_white = -1;

static unsigned isqrt(unsigned v)
{
    unsigned r = 0, b = 1 << 30;
    while (b > v) b >>= 2;
    while (b)
    {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static int clamp(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static int build_luts(int range)
{
    const film_stock_t *s = current_stock();

    if (lut && lut_stock == conf.film_stock && lut_white == range)
        return 1;

    if (!lut)
        lut = malloc(3 * LUT_SIZE * sizeof(unsigned short));
    if (!lut)
        return 0;

    int x, c;
    for (x = 0; x < LUT_SIZE; x++)
    {
        // linear -> perceptual
        int p = isqrt(((x << 12) / range) << 12);
        if (p > ONE) p = ONE;

        // S-curve around mid grey; derivative stays positive for contrast <= 0.4
        int t = (p * (ONE - p)) >> 12;
        int f = ((p - PIVOT) * t) >> 10;
        int v = p + ((s->contrast * f) >> 12);
        v = clamp(v, 0, ONE);

        // faded blacks / muted whites
        v = s->lift + ((v * (s->white - s->lift)) >> 12);

        int inv = ONE - v;
        int sh = (inv * inv) >> 12;
        int hi = (v * v) >> 12;

        for (c = 0; c < 3; c++)
        {
            int vc = v + ((s->shadow[c] * sh) >> 12) + ((s->highlight[c] * hi) >> 12);
            vc = clamp(vc, 0, ONE);
            // perceptual -> linear
            lut[c*LUT_SIZE + x] = (((vc * vc) >> 12) * range) >> 12;
        }
    }

    lut_stock = conf.film_stock;
    lut_white = range;
    return 1;
}

//-------------------------------------------------------------------
// Grain

static unsigned rng_state = 0x9E3779B9;

static inline unsigned xorshift()
{
    unsigned x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

// Approximate gaussian, mean 0, sd ~147
static inline int noise()
{
    unsigned r = xorshift();
    return (int)((r & 0xFF) + ((r >> 8) & 0xFF) + ((r >> 16) & 0xFF) + (r >> 24)) - 510;
}

// Grain amplitude by luminance (L >> 4), in linear units scaled so that (noise * amp) >> 12
// gives the target standard deviation. Strongest in the mid tones like real film.
static void build_grain_amp(int *amp, int amount, int range)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        int x = (i << 4) + 8;
        int p = isqrt(((x << 12) / range) << 12);
        if (p > ONE) p = ONE;
        int shape = 1434 + ((10650 * ((p * (ONE - p)) >> 12)) >> 12);   // 0.35 .. 1.0
        int dp = (amount * 184 * shape) / (10 * ONE);                   // Q12, 0.45% per step at peak
        int pd = (p * dp) >> 12;
        amp[i] = (2 * range * pd) / 147;
    }
}

//-------------------------------------------------------------------
// Raw buffer access (10 bit packed, 8 pixels per 10 bytes)

#if CAM_SENSOR_BITS_PER_PIXEL == 10

static inline void unpack8(const unsigned char *b, int *p)
{
    p[0] = (b[1] << 2) | (b[0] >> 6);
    p[1] = ((b[0] & 0x3F) << 4) | (b[3] >> 4);
    p[2] = ((b[3] & 0x0F) << 6) | (b[2] >> 2);
    p[3] = ((b[2] & 0x03) << 8) | b[5];
    p[4] = (b[4] << 2) | (b[7] >> 6);
    p[5] = ((b[7] & 0x3F) << 4) | (b[6] >> 4);
    p[6] = ((b[6] & 0x0F) << 6) | (b[9] >> 2);
    p[7] = ((b[9] & 0x03) << 8) | b[8];
}

static inline void pack8(unsigned char *b, const int *p)
{
    b[1] = p[0] >> 2;
    b[0] = (p[0] << 6) | (p[1] >> 4);
    b[3] = (p[1] << 4) | (p[2] >> 6);
    b[2] = (p[2] << 2) | (p[3] >> 8);
    b[5] = p[3];
    b[4] = p[4] >> 2;
    b[7] = (p[4] << 6) | (p[5] >> 4);
    b[6] = (p[5] << 4) | (p[6] >> 6);
    b[9] = (p[6] << 2) | (p[7] >> 8);
    b[8] = p[7];
}

#define GROUP_PIX   8
#define GROUP_BYTES 10

#endif

//-------------------------------------------------------------------
// "Developing" card shown while a shot is processed. Drawn from the capture
// task straight onto the bitmap overlay; the GUI restores the screen afterwards.

#define DEV_W   240
#define DEV_H   84
#define BAR_H   4

static int dev_x, dev_y, dev_bar_drawn;

static void dev_card_begin(int amount, int size)
{
    static char buf[40];
    dev_x = camera_screen.disp_left + (camera_screen.disp_width - DEV_W) / 2;
    dev_y = (camera_screen.height - DEV_H) / 2;
    dev_bar_drawn = 0;

    // Canon darkens the LCD while it captures; ask for the backlight so the card can be seen
    TurnOnBackLight();

    draw_rectangle(dev_x, dev_y, dev_x+DEV_W-1, dev_y+DEV_H-1, MAKE_COLOR(COLOR_BLACK, COLOR_GREY_DK), RECT_BORDER1|DRAW_FILLED|RECT_ROUND_CORNERS);
    draw_string(dev_x+12, dev_y+8, "DEVELOPING", MAKE_COLOR(COLOR_BLACK, COLOR_RED));
    if (conf.save_raw)
        draw_string_justified(dev_x+12, dev_y+8, conf.dng_raw ? "+ DNG" : "+ CRW", MAKE_COLOR(COLOR_BLACK, COLOR_GREY), 0, DEV_W-24, TEXT_RIGHT);
    draw_string(dev_x+12, dev_y+28, current_stock()->name, MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));
    if (amount > 0)
        sprintf(buf, "Grain %d, %s", amount, size+1 == 1 ? "fine" : size+1 == 2 ? "medium" : "coarse");
    else
        strcpy(buf, "No grain");
    if (current_stock()->vignette && conf.film_vignette) strcat(buf, "  Vignette");
    draw_string(dev_x+12, dev_y+46, buf, MAKE_COLOR(COLOR_BLACK, COLOR_GREY));
    draw_rectangle(dev_x+12, dev_y+68, dev_x+DEV_W-13, dev_y+68+BAR_H-1, MAKE_COLOR(COLOR_GREY_DK, COLOR_GREY_DK), RECT_BORDER0|DRAW_FILLED);
}

static void dev_card_progress(int done, int total)
{
    int w = (DEV_W - 24) * done / total;
    if (w > dev_bar_drawn)
    {
        draw_rectangle(dev_x+12+dev_bar_drawn, dev_y+68, dev_x+12+w-1, dev_y+68+BAR_H-1, MAKE_COLOR(COLOR_RED, COLOR_RED), RECT_BORDER0|DRAW_FILLED);
        dev_bar_drawn = w;
    }
}

#ifdef FILM_HOST_PREVIEW
// lets film-tools/host/ui_preview render the card without a raw buffer
void film_ui_preview_dev_card(int done, int total)
{
    dev_card_begin(grain_amount(), grain_size());
    dev_card_progress(done, total);
}
#endif

//-------------------------------------------------------------------

void film_apply(void)
{
#if CAM_SENSOR_BITS_PER_PIXEL == 10
    const film_stock_t *s = current_stock();
    if (conf.film_stock == 0)
        return;

    unsigned char *raw = (unsigned char*)get_raw_image_addr();
    int black = camera_sensor.black_level;
    int white = camera_sensor.white_level;
    int range = white - black;
    int rowlen = camera_sensor.raw_rowlen;

    if (!build_luts(range))
        return;

    // Bayer layout of a 2x2 quad: index 0=(0,0) 1=(0,1) 2=(1,0) 3=(1,1), colour 0=R 1=G 2=B
    int iR = 0, iB = 3, iG1 = 1, iG2 = 2, k;
    {
        int n = 0, g[2] = { 1, 2 };
        for (k = 0; k < 4; k++)
        {
            int col = (camera_sensor.cfa_pattern >> (k*8)) & 0xFF;
            if (col == 0) iR = k;
            else if (col == 2) iB = k;
            else if (n < 2) g[n++] = k;
        }
        iG1 = g[0]; iG2 = g[1];
    }

    // Active area, aligned so groups start on whole 8 pixel blocks and quads on even rows
    int x0 = (camera_sensor.active_area.x1 + GROUP_PIX - 1) / GROUP_PIX;
    int x1 = camera_sensor.active_area.x2 / GROUP_PIX;
    int y0 = (camera_sensor.active_area.y1 + 1) & ~1;
    int y1 = camera_sensor.active_area.y2 & ~1;
    int groups = x1 - x0;
    if (groups <= 0 || y1 <= y0)
        return;

    int x, y, q[4], a[8], b[8];

    // Grey world white balance estimate on a sparse grid, skipping clipped and very dark quads
    unsigned sr = 0, sg = 0, sb = 0;
    for (y = y0; y < y1; y += 32)
    {
        unsigned char *ra = raw + y*rowlen;
        unsigned char *rb = ra + rowlen;
        for (x = x0; x < x1; x += 4)
        {
            unpack8(ra + x*GROUP_BYTES, a);
            unpack8(rb + x*GROUP_BYTES, b);
            q[0] = a[0]; q[1] = a[1]; q[2] = b[0]; q[3] = b[1];
            int r = q[iR] - black, g = ((q[iG1] + q[iG2]) >> 1) - black, bl = q[iB] - black;
            if (r < 16 || g < 16 || bl < 16) continue;
            if (q[iR] >= white - 8 || q[iG1] >= white - 8 || q[iB] >= white - 8) continue;
            sr += r; sg += g; sb += bl;
        }
    }
    int gR = ONE, gB = ONE;
    if (sr > 1000 && sb > 1000)
    {
        gR = clamp((int)(((unsigned long long)sg << 12) / sr), ONE/2, ONE*4);
        gB = clamp((int)(((unsigned long long)sg << 12) / sb), ONE/2, ONE*4);
    }
    int igR = (ONE << 12) / gR;
    int igB = (ONE << 12) / gB;

    // Grain setup
    int amount = grain_amount();
    int gsize = grain_size();
    static int gamp[256];   // static: raw hook runs on a small task stack
    int quads = groups * 4;
    int *nrow = 0;
    if (amount > 0)
    {
        build_grain_amp(gamp, amount, range);
        if (gsize > 0)
        {
            nrow = malloc(quads * sizeof(int));
            if (nrow) memset(nrow, 0, quads * sizeof(int));
            else gsize = 0;
        }
        rng_state ^= get_tick_count() * 2654435761u;
        if (!rng_state) rng_state = 0x9E3779B9;
    }

    int show_dev = conf.film_dev_screen;
    if (show_dev)
        dev_card_begin(amount, gsize);

    // Vignette: separable falloff, per quad column and per row pair
    int vig = s->vignette;
    if (conf.film_vignette == 0) vig = 0;
    int *vx = 0;
    int qcx = quads / 2, qcy = (y1 - y0) / 4;
    if (vig)
    {
        vx = malloc(quads * sizeof(int));
        if (vx)
        {
            for (x = 0; x < quads; x++)
            {
                int d = ((x - qcx) << 12) / qcx;
                vx[x] = ONE - ((vig * ((d * d) >> 12)) >> 12);
            }
        }
        else vig = 0;
    }

    unsigned short *lR = lut, *lG = lut + LUT_SIZE, *lB = lut + 2*LUT_SIZE;
    int sat = s->saturation;

    for (y = y0; y < y1; y += 2)
    {
        if (show_dev && ((y - y0) & 63) == 0)
            dev_card_progress(y - y0, y1 - y0);

        unsigned char *ra = raw + y*rowlen + x0*GROUP_BYTES;
        unsigned char *rb = ra + rowlen;
        int vy = ONE;
        if (vig)
        {
            int d = (((y - y0) / 2 - qcy) << 12) / qcy;
            vy = ONE - ((vig * ((d * d) >> 12)) >> 12);
        }
        int nleft = 0;
        int qx = 0;

        for (x = 0; x < groups; x++, ra += GROUP_BYTES, rb += GROUP_BYTES)
        {
            unpack8(ra, a);
            unpack8(rb, b);

            for (k = 0; k < 8; k += 2, qx++)
            {
                q[0] = a[k]; q[1] = a[k+1]; q[2] = b[k]; q[3] = b[k+1];

                int R  = q[iR]  - black; if (R  < 0) R  = 0;
                int G1 = q[iG1] - black; if (G1 < 0) G1 = 0;
                int G2 = q[iG2] - black; if (G2 < 0) G2 = 0;
                int B  = q[iB]  - black; if (B  < 0) B  = 0;

                R = (R * gR) >> 12;
                B = (B * gB) >> 12;

                if (vig)
                {
                    int v = (vx[qx] * vy) >> 12;
                    R = (R * v) >> 12; G1 = (G1 * v) >> 12; G2 = (G2 * v) >> 12; B = (B * v) >> 12;
                }

                int L = (77*R + 76*G1 + 75*G2 + 28*B) >> 8;

                if (sat != ONE)
                {
                    R  = L + (((R  - L) * sat) >> 12);
                    G1 = L + (((G1 - L) * sat) >> 12);
                    G2 = L + (((G2 - L) * sat) >> 12);
                    B  = L + (((B  - L) * sat) >> 12);
                }

                if (amount > 0)
                {
                    int n = noise();
                    if (gsize == 1)
                    {
                        n = (2*n + nleft + nrow[qx]) >> 2;
                        nleft = nrow[qx] = n;
                        n = (n * 13) >> 3;
                    }
                    else if (gsize == 2)
                    {
                        n = ((n + 2*nleft + 2*nrow[qx]) * 13) >> 6;
                        nleft = nrow[qx] = n;
                        n = (n * 19) >> 3;
                    }
                    int g = (n * gamp[clamp(L >> 4, 0, 255)]) >> 12;
                    R += g; G1 += g; G2 += g; B += g;
                }

                R  = lR[clamp(R,  0, LUT_SIZE-1)];
                G1 = lG[clamp(G1, 0, LUT_SIZE-1)];
                G2 = lG[clamp(G2, 0, LUT_SIZE-1)];
                B  = lB[clamp(B,  0, LUT_SIZE-1)];

                R = (R * igR) >> 12;
                B = (B * igB) >> 12;

                q[iR]  = clamp(R  + black, 0, white);
                q[iG1] = clamp(G1 + black, 0, white);
                q[iG2] = clamp(G2 + black, 0, white);
                q[iB]  = clamp(B  + black, 0, white);

                a[k] = q[0]; a[k+1] = q[1]; b[k] = q[2]; b[k+1] = q[3];
            }

            pack8(ra, a);
            pack8(rb, b);
        }
    }

    if (nrow) free(nrow);
    if (vx) free(vx);

    if (show_dev)
    {
        dev_card_progress(1, 1);
        gui_set_need_restore();
    }
#endif
}

//-------------------------------------------------------------------
// Camera colour mode follows the stock (only when changed, so manual
// Canon menu changes are respected afterwards)

static int applied_stock = -1;

static void apply_camera_colors()
{
    if (!conf.film_camera_color || !camera_info.state.mode_rec)
        return;
    if (applied_stock == conf.film_stock)
        return;
    int mc = current_stock()->my_colors;
    if (mc >= 0)
        shooting_set_prop(PROPCASE_MY_COLORS, mc);
    applied_stock = conf.film_stock;
}

void film_settings_changed(void)
{
    lut_stock = -1;
    if (conf.film_stock == 0 && lut)
    {
        free(lut);
        lut = 0;
    }
    applied_stock = -1;
    apply_camera_colors();
}

//-------------------------------------------------------------------
// Status pill at the top of the shooting screen, replacing CHDK's separate
// battery / card space / RAW indicators:  ● PORTRA G3 DNG | 87% 1.2G

static int pill_text(int x, int y, const char *t, color fg)
{
    draw_string(x, y, t, MAKE_COLOR(COLOR_BLACK, fg));
    return x + strlen(t) * FONT_WIDTH;
}

void film_draw_osd(int is_osd_edit)
{
    apply_camera_colors();

    if (!conf.film_osd)
        return;
    if (!is_osd_edit && (!camera_info.state.mode_rec || camera_info.state.mode_video || camera_info.state.is_shutter_half_press))
        return;

    static char grain[8], batt[8], space[8];
    const char *tag = conf.film_stock ? current_stock()->tag : "NO FILM";
    int amount = grain_amount();
    grain[0] = 0;
    if (conf.film_stock && amount > 0) sprintf(grain, " G%d", amount);
    const char *raw = conf.save_raw ? (conf.dng_raw ? " DNG" : " CRW") : "";

    int bp = get_batt_perc();
    sprintf(batt, "%d%%", bp);
    unsigned mb = GetFreeCardSpaceKb() >> 10;
    if (mb >= 1024) sprintf(space, " %d.%dG", mb >> 10, ((mb & 1023) * 10) >> 10);
    else sprintf(space, " %dM", mb);

    int chars = strlen(tag) + strlen(grain) + strlen(raw) + strlen(batt) + strlen(space);
    int w = chars * FONT_WIDTH + 13 + 12 + 8;       // dot + separator gap + padding
    int h = FONT_HEIGHT;
    int x = camera_screen.disp_left + (camera_screen.disp_width - w) / 2;
    int y = 4;

    draw_rectangle(x, y, x + w, y + h + 3, MAKE_COLOR(COLOR_BLACK, COLOR_GREY_DK), RECT_BORDER1|DRAW_FILLED|RECT_ROUND_CORNERS);
    draw_rectangle(x + 5, y + h/2 - 1, x + 8, y + h/2 + 2, MAKE_COLOR(COLOR_RED, COLOR_RED), RECT_BORDER0|DRAW_FILLED);
    int tx = pill_text(x + 13, y + 2, tag, conf.film_stock ? COLOR_WHITE : COLOR_GREY);
    tx = pill_text(tx, y + 2, grain, COLOR_GREY_LT);
    tx = pill_text(tx, y + 2, raw, COLOR_GREY_LT);
    draw_vline(tx + 5, y + 4, h - 4, COLOR_GREY_DK);
    tx = pill_text(tx + 12, y + 2, batt, bp <= 20 ? COLOR_RED : COLOR_GREY);
    pill_text(tx, y + 2, space, (mb < 100) ? COLOR_RED : COLOR_GREY);
}

//-------------------------------------------------------------------
// Film picker: what the ALT button shows in record mode.
//
//   FILM                                4 / 11
//   <        Superia 400                     >
//        Green shadows, cool everyday colour
//   ------------------------------------------
//   GRAIN  [##########]  4 (auto)       Medium
//   RAW    DNG
//   <> film   ^v grain   SET size   DISP raw

#define PK_H        140
#define PK_PAD      12

int film_picker_active(void)
{
    return camera_info.state.mode_rec && !camera_info.state.mode_video && !camera_info.state.state_kbd_script_run;
}

static int raw_key_free()
{
    return conf.alt_mode_button != KEY_DISPLAY;
}

void film_picker_draw(int force_redraw)
{
    static int last_sig = -1, ticks = 0;
    int sig = (conf.film_stock << 16) | (conf.film_grain << 8) | (conf.film_grain_size << 4) | (conf.save_raw << 1) | conf.dng_raw;

    // Redraw on change, on request, and now and then in case Canon's OSD drew over us
    if (!force_redraw && sig == last_sig && (++ticks & 31))
        return;
    last_sig = sig;

    static char buf[48];
    const film_stock_t *s = current_stock();
    int x = camera_screen.disp_left + 8;
    int w = camera_screen.disp_width - 16;
    int y = camera_screen.height - FONT_HEIGHT - PK_H - 4;
    int ix = x + PK_PAD, iw = w - 2*PK_PAD;
    twoColors label = MAKE_COLOR(COLOR_BLACK, COLOR_GREY);
    twoColors value = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    twoColors accent = MAKE_COLOR(COLOR_BLACK, COLOR_RED);

    draw_rectangle(x, y, x+w-1, y+PK_H-1, MAKE_COLOR(COLOR_BLACK, COLOR_GREY_DK), RECT_BORDER1|DRAW_FILLED|RECT_ROUND_CORNERS);

    // header
    draw_string(ix, y+6, "FILM", label);
    sprintf(buf, "%d / %d", conf.film_stock, film_stock_count-1);
    draw_string_justified(ix, y+6, conf.film_stock ? buf : "OFF", label, 0, iw, TEXT_RIGHT);

    // stock name, double size, with arrows
    int nl = strlen(s->name);
    int sc = (nl * FONT_WIDTH * 2 <= iw - 2*FONT_WIDTH*2) ? 2 : 1;
    int nw = nl * FONT_WIDTH * sc;
    int ny = y + 24 + (sc == 1 ? FONT_HEIGHT/2 : 0);
    draw_string_scaled(ix, y+24+FONT_HEIGHT/2, "<", accent, 1, 1);
    draw_string_scaled(ix+iw-FONT_WIDTH, y+24+FONT_HEIGHT/2, ">", accent, 1, 1);
    draw_string_scaled(ix + (iw - nw)/2, ny, s->name, value, sc, sc);

    // description
    draw_string_justified(ix, y+60, s->desc, label, 0, iw, TEXT_CENTER);

    draw_hline(ix, y+80, iw, COLOR_GREY_DK);

    // grain: segmented meter + amount + size
    int amount = grain_amount();
    draw_string(ix, y+86, "GRAIN", label);
    int bx = ix + 7*FONT_WIDTH, i;
    for (i = 0; i < 10; i++)
    {
        color c = (i < amount) ? COLOR_RED : COLOR_GREY_DK;
        draw_rectangle(bx + i*9, y+90, bx + i*9 + 6, y+97, MAKE_COLOR(c, c), RECT_BORDER0|DRAW_FILLED);
    }
    draw_string(bx + 96, y+86, grain_text(), value);
    draw_string_justified(ix, y+86, amount ? size_names[grain_size()+1] : "", label, 0, iw, TEXT_RIGHT);

    // raw
    draw_string(ix, y+104, "RAW", label);
    draw_string(bx, y+104, raw_text(), value);

    // key hints
    sprintf(buf, "<> film  ^v grain  SET size%s", raw_key_free() ? "  DISP raw" : "");
    draw_string_justified(ix, y+122, buf, label, 0, iw, TEXT_CENTER);
}

int film_picker_kbd(void)
{
    if (!film_picker_active())
        return 0;

    switch (kbd_get_autoclicked_key())
    {
    case KEY_RIGHT: set_stock(conf.film_stock + 1); return 1;
    case KEY_LEFT:  set_stock(conf.film_stock - 1); return 1;
    case KEY_UP:    set_grain(grain_amount() + 1);  return 1;
    case KEY_DOWN:  set_grain(grain_amount() - 1);  return 1;
    }

    if (kbd_is_key_clicked(KEY_SET))
    {
        conf.film_grain_size = (grain_size() + 1) % 3 + 1;
        return 1;
    }
    if (raw_key_free() && kbd_is_key_clicked(KEY_DISPLAY))
    {
        gui_film_raw_enum(1, 0);
        return 1;
    }
    return 0;
}

//-------------------------------------------------------------------
// Menu

const char* gui_film_stock_enum(int change, __attribute__ ((unused))int arg)
{
    if (change)
        set_stock(conf.film_stock + change);
    return current_stock()->name;
}

// Auto (stock default), Off, 1..10
const char* gui_film_grain_enum(int change, __attribute__ ((unused))int arg)
{
    gui_enum_value_change(&conf.film_grain, change, 12);
    return grain_text();
}

const char* gui_film_grain_size_enum(int change, __attribute__ ((unused))int arg)
{
    static char buf[20];
    gui_enum_value_change(&conf.film_grain_size, change, 4);
    if (conf.film_grain_size == FILM_SIZE_STOCK)
    {
        sprintf(buf, "%s (auto)", size_names[grain_size()+1]);
        return buf;
    }
    return size_names[conf.film_grain_size];
}

const char* gui_film_vignette_enum(int change, __attribute__ ((unused))int arg)
{
    static const char *modes[] = { "Off", "Auto" };
    gui_enum_value_change(&conf.film_vignette, change, 2);
    if (conf.film_vignette && !current_stock()->vignette)
        return "Auto (none)";
    return modes[conf.film_vignette];
}

// RAW: Off / DNG / CRW maps onto the existing save_raw + dng_raw settings
const char* gui_film_raw_enum(int change, __attribute__ ((unused))int arg)
{
    int v = !conf.save_raw ? 0 : conf.dng_raw ? 1 : 2;
    gui_enum_value_change(&v, change, 3);
    if (change)
    {
        conf.save_raw = (v != 0);
        if (v) conf.dng_raw = (v == 1);
        conf_change_dng();
    }
    return raw_text();
}
