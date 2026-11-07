/*
 * draw_canvas.c -- ink canvas + LCD preview.
 *
 * The canvas is the only image buffer in the whole project (9216 bytes).
 * There is no framebuffer: every brush dab is written straight to the LCD.
 */
#include "draw_canvas.h"

#include <string.h>

#include "board_port.h"

static uint8_t s_canvas[CANVAS_SIZE * CANVAS_SIZE];
static int s_have_last;
static int s_last_x;
static int s_last_y;

/* ------------------------------------------------------------------------- */
/* preview                                                                   */
/* ------------------------------------------------------------------------- */
static void screen_dab(int cx, int cy)
{
    int sx = CANVAS_X + cx * CANVAS_ZOOM;
    int sy = CANVAS_Y + cy * CANVAS_ZOOM;

    bp_fill(sx, sy, sx + CANVAS_ZOOM - 1, sy + CANVAS_ZOOM - 1, BP_WHITE);
}

/* Solid circular brush, clipped to the canvas. */
static void canvas_dab(int cx, int cy)
{
    const int r = BRUSH_RADIUS;

    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= CANVAS_SIZE) {
            continue;
        }
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= CANVAS_SIZE) {
                continue;
            }
            if (dx * dx + dy * dy > r * r) {
                continue;
            }
            uint8_t *p = &s_canvas[y * CANVAS_SIZE + x];
            if (*p != 255u) {
                *p = 255u;
                screen_dab(x, y);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* public                                                                    */
/* ------------------------------------------------------------------------- */
void canvas_reset(void)
{
    memset(s_canvas, 0, sizeof(s_canvas));
    s_have_last = 0;
    bp_fill(CANVAS_X, CANVAS_Y,
            CANVAS_X + CANVAS_SIZE * CANVAS_ZOOM - 1,
            CANVAS_Y + CANVAS_SIZE * CANVAS_ZOOM - 1, BP_BLACK);
}

void canvas_new_stroke(void)
{
    s_have_last = 0;
}

void canvas_stroke_to(int cx, int cy)
{
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= CANVAS_SIZE) cx = CANVAS_SIZE - 1;
    if (cy >= CANVAS_SIZE) cy = CANVAS_SIZE - 1;

    if (!s_have_last) {
        canvas_dab(cx, cy);
        s_last_x = cx;
        s_last_y = cy;
        s_have_last = 1;
        return;
    }

    /* Bresenham between the previous and the current sample. */
    int x0 = s_last_x;
    int y0 = s_last_y;
    int dx = cx > x0 ? cx - x0 : x0 - cx;
    int dy = cy > y0 ? cy - y0 : y0 - cy;
    int sx = x0 < cx ? 1 : -1;
    int sy = y0 < cy ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        canvas_dab(x0, y0);
        if (x0 == cx && y0 == cy) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    s_last_x = cx;
    s_last_y = cy;
}

int canvas_screen_to_canvas(int sx, int sy, int *cx, int *cy)
{
    int w = CANVAS_SIZE * CANVAS_ZOOM;
    int h = CANVAS_SIZE * CANVAS_ZOOM;

    if (sx < CANVAS_X || sx >= CANVAS_X + w || sy < CANVAS_Y || sy >= CANVAS_Y + h) {
        return 0;
    }
    *cx = (sx - CANVAS_X) / CANVAS_ZOOM;
    *cy = (sy - CANVAS_Y) / CANVAS_ZOOM;
    return 1;
}

const uint8_t *canvas_buffer(void)
{
    return s_canvas;
}

int canvas_ink_pixels(void)
{
    int n = 0;
    for (int i = 0; i < CANVAS_SIZE * CANVAS_SIZE; i++) {
        if (s_canvas[i] != 0u) {
            n++;
        }
    }
    return n;
}
