// Host versions of the CHDK drawing calls film.c uses, rendering onto a
// 360x240 canvas with CHDK's own 8x16 font so screen layouts can be checked
// without the camera. Semantics follow core/gui_draw_shapes.c and gui_draw_text.c.

#include "film_host.h"
#include "font_8x16_uni.h"

#define W 360
#define H 240

unsigned char canvas[H][W][3];

// Approximation of the Canon palette entries CHDK maps these colours to
static const unsigned char pal[][3] = {
    {   0,   0,   0 },  // transparent (unused)
    {  14,  14,  16 },  // black
    {  58,  58,  64 },  // grey dark
    { 128, 128, 136 },  // grey
    { 200, 200, 206 },  // grey light
    { 240, 240, 240 },  // white
    { 222,  44,  40 },  // red
};

static void px(int x, int y, color c)
{
    if (c == COLOR_TRANSPARENT || x < 0 || y < 0 || x >= W || y >= H) return;
    memcpy(canvas[y][x], pal[c], 3);
}

void draw_hline(int x, int y, int len, color cl) { for (int i = 0; i < len; i++) px(x+i, y, cl); }
void draw_vline(int x, int y, int len, color cl) { for (int i = 0; i < len; i++) px(x, y+i, cl); }

void draw_rectangle(int x1, int y1, int x2, int y2, twoColors cl, int flags)
{
    int round = (flags & RECT_ROUND_CORNERS) ? 1 : 0;
    for (int i = 0; i < (flags & 0xF); i++)
    {
        draw_vline(x1, y1 + round*2, y2 - y1 - round*4 + 1, FG_COLOR(cl));
        draw_vline(x2, y1 + round*2, y2 - y1 - round*4 + 1, FG_COLOR(cl));
        draw_hline(x1 + 1 + round, y1, x2 - x1 - round*2 - 1, FG_COLOR(cl));
        draw_hline(x1 + 1 + round, y2, x2 - x1 - round*2 - 1, FG_COLOR(cl));
        x1++; x2--; y1++; y2--;
        round = 0;
    }
    if (flags & DRAW_FILLED)
        for (int y = y1; y <= y2; y++) draw_hline(x1, y, x2 - x1 + 1, BG_COLOR(cl));
}

static const unsigned char *glyph(int ch)
{
    for (unsigned i = 0; i < sizeof(orig_font_data)/sizeof(orig_font_data[0]); i++)
        if (orig_font_data[i].charcode == ch) return orig_font_data[i].data;
    return orig_font_data[0].data;
}

static void draw_char_scaled(int x, int y, char ch, twoColors cl, int sx, int sy)
{
    const unsigned char *g = glyph((unsigned char)ch);
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 8; c++)
        {
            color col = (g[r] & (0x80 >> c)) ? FG_COLOR(cl) : BG_COLOR(cl);
            for (int j = 0; j < sy; j++)
                for (int i = 0; i < sx; i++)
                    px(x + c*sx + i, y + r*sy + j, col);
        }
}

void draw_string_scaled(int x, int y, const char *s, twoColors cl, int xsize, int ysize)
{
    for (; *s && *s != '\n'; s++, x += FONT_WIDTH*xsize) draw_char_scaled(x, y, *s, cl, xsize, ysize);
}

int draw_string(int x, int y, const char *s, twoColors cl)
{
    draw_string_scaled(x, y, s, cl, 1, 1);
    return x + strlen(s)*FONT_WIDTH;
}

int draw_string_justified(int x, int y, const char *s, twoColors cl, int xo, int max_width, int justification)
{
    int l = strlen(s) * FONT_WIDTH;
    if (l > max_width) l = max_width;
    if (justification == TEXT_RIGHT) xo = max_width - l;
    if (justification == TEXT_CENTER) xo = (max_width - l) >> 1;
    int n = (max_width - xo) / FONT_WIDTH;
    for (int i = 0; s[i] && i < n; i++) draw_char_scaled(x + xo + i*FONT_WIDTH, y, s[i], cl, 1, 1);
    return x + xo + l;
}
