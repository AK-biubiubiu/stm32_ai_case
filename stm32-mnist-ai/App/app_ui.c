/*
 * app_ui.c -- 320x240 landscape layout.
 *
 *   y=0..23      title bar
 *   x=6..198     canvas preview (192x192), owned by draw_canvas.c
 *   x=204..318   result panel: big 7-segment digit + 10 confidence bars
 *   y=222..239   status bar
 *
 * All text is ASCII: the Wildfire font tables are GB2312 encoded and would
 * print garbage when compiled as UTF-8 by GCC.
 */
#include "app_ui.h"

#include "board_port.h"

#define TITLE_Y      4
#define PANEL_X      204
#define PANEL_W      (BP_SCREEN_W - PANEL_X)

#define BIG_X        205
#define BIG_Y        46
#define BIG_W        34
#define BIG_H        52
#define SEG_T        7

#define BAR_LABEL_X  204
#define BAR_X        214
#define BAR_TOP      112
#define BAR_ROW      10
#define BAR_H        6
#define BAR_MAX_W    (BP_SCREEN_W - 4 - BAR_X)

/* seven-segment bit map: bit0=a(segment top) ... bit6=g(middle) */
static const uint8_t S_SEG[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void seg(int x, int y, int w, int h, int on, uint16_t color)
{
    if (on) {
        bp_fill(x, y, x + w - 1, y + h - 1, color);
    }
}

static void draw_big_digit(int d, uint16_t color)
{
    const int x = BIG_X;
    const int y = BIG_Y;
    const int w = BIG_W;
    const int h = BIG_H;
    const int t = SEG_T;
    const int hm = h / 2;
    uint8_t m;

    if (d < 0 || d > 9) {
        return;
    }
    m = S_SEG[d];

    seg(x + t, y, w - 2 * t, t, (m & 0x01) != 0, color);                     /* a */
    seg(x + w - t, y + t, t, hm - t, (m & 0x02) != 0, color);                /* b */
    seg(x + w - t, y + hm, t, hm - t, (m & 0x04) != 0, color);               /* c */
    seg(x + t, y + h - t, w - 2 * t, t, (m & 0x08) != 0, color);             /* d */
    seg(x, y + hm, t, hm - t, (m & 0x10) != 0, color);                       /* e */
    seg(x, y + t, t, hm - t, (m & 0x20) != 0, color);                        /* f */
    seg(x + t, y + hm - t / 2, w - 2 * t, t, (m & 0x40) != 0, color);        /* g */
}

static int u32_to_str(uint32_t v, char *buf)
{
    char tmp[12];
    int n = 0;
    int i;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
    return n;
}

void ui_init(void)
{
    char one[2] = {0, 0};

    bp_fill(0, 0, BP_SCREEN_W - 1, 23, BP_DGRAY);
    bp_text(6, TITLE_Y, "Draw a digit 0-9", BP_WHITE, BP_DGRAY);
    bp_text(222, TITLE_Y, "KEY0:Clear", BP_WHITE, BP_DGRAY);

    /* canvas frame */
    bp_rect(4, 24, 200, 220, BP_GRAY);

    /* panel divider */
    bp_fill(201, 24, 201, 220, BP_DGRAY);

    bp_text(PANEL_X, 30, "RESULT", BP_CYAN, BP_BLACK);

    /* confidence bar labels 0..9 */
    for (int i = 0; i < AI_CLASSES; i++) {
        one[0] = (char)('0' + i);
        bp_text(BAR_LABEL_X, BAR_TOP + i * BAR_ROW, one, BP_GRAY, BP_BLACK);
    }

    ui_show_empty();
    ui_status("ready");
}

void ui_show_empty(void)
{
    bp_fill(BIG_X, BIG_Y, BIG_X + BIG_W, BIG_Y + BIG_H, BP_BLACK);
    bp_text(BIG_X + 2, BIG_Y + 8, "--", BP_GRAY, BP_BLACK);

    for (int i = 0; i < AI_CLASSES; i++) {
        bp_fill(BAR_X, BAR_TOP + i * BAR_ROW, BP_SCREEN_W - 4, BAR_TOP + i * BAR_ROW + BAR_H - 1,
                BP_BLACK);
    }
}

void ui_show_result(int digit, const float probs[AI_CLASSES], uint32_t cost_ms)
{
    char buf[32];
    int n = 0;

    bp_fill(BIG_X, BIG_Y, BIG_X + BIG_W, BIG_Y + BIG_H, BP_BLACK);
    draw_big_digit(digit, BP_YELLOW);

    for (int i = 0; i < AI_CLASSES; i++) {
        int y = BAR_TOP + i * BAR_ROW;
        int w = (int)(probs[i] * (float)BAR_MAX_W + 0.5f);

        if (w < 0) {
            w = 0;
        }
        if (w > BAR_MAX_W) {
            w = BAR_MAX_W;
        }
        bp_fill(BAR_X, y, BP_SCREEN_W - 4, y + BAR_H - 1, BP_DGRAY);
        if (w > 0) {
            bp_fill(BAR_X, y, BAR_X + w - 1, y + BAR_H - 1,
                    i == digit ? BP_YELLOW : BP_CYAN);
        }
    }

    /* "conf 98%  12ms" */
    buf[n++] = 'c';
    buf[n++] = 'o';
    buf[n++] = 'n';
    buf[n++] = 'f';
    buf[n++] = ' ';
    n += u32_to_str((uint32_t)(probs[digit] * 100.0f + 0.5f), &buf[n]);
    buf[n++] = '%';
    buf[n++] = ' ';
    buf[n++] = ' ';
    n += u32_to_str(cost_ms, &buf[n]);
    buf[n++] = 'm';
    buf[n++] = 's';
    buf[n] = '\0';
    ui_status(buf);
}

void ui_status(const char *text)
{
    int x = 6;

    bp_fill(0, 222, BP_SCREEN_W - 1, BP_SCREEN_H - 1, BP_BLACK);
    /* erase the old string by writing spaces over the same area first */
    for (int i = 0; i < 30; i++) {
        char sp[2] = {' ', '\0'};
        bp_text(x + i * 8, 224, sp, BP_BLACK, BP_BLACK);
    }
    bp_text(x, 224, text, BP_WHITE, BP_BLACK);
}
