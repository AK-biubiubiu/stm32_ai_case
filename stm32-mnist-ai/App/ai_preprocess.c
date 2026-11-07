/*
 * ai_preprocess.c -- canvas -> 28x28 MNIST-style tensor.
 *
 * Mirrors the classic MNIST normalisation: keep the aspect ratio, fit the
 * bounding box into 20x20, then shift the centroid to the centre of a 28x28
 * frame.  Doing the same thing on-device is what makes a model trained on
 * MNIST usable with a fat resistive-touch stroke.
 */
#include "ai_preprocess.h"

#include <string.h>

#define INK_THRESHOLD 16
#define MIN_INK_PIXELS 40
#define TARGET_SIDE 20
#define TMP_MAX 20

static uint8_t s_tmp[TMP_MAX * TMP_MAX];
static uint8_t s_frame[AI_IMG_SIZE * AI_IMG_SIZE];

static int32_t round_f2i(float v)
{
    return (int32_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
}

int ai_preprocess_canvas(const uint8_t *canvas, uint8_t out[AI_IMG_SIZE * AI_IMG_SIZE])
{
    int x, y;
    int x0 = AI_CANVAS_SIZE, y0 = AI_CANVAS_SIZE, x1 = -1, y1 = -1;
    int ink = 0;

    /* ---- 1. bounding box of the ink ---------------------------------- */
    for (y = 0; y < AI_CANVAS_SIZE; y++) {
        const uint8_t *row = canvas + y * AI_CANVAS_SIZE;
        for (x = 0; x < AI_CANVAS_SIZE; x++) {
            if (row[x] >= INK_THRESHOLD) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
                ink++;
            }
        }
    }
    if (ink < MIN_INK_PIXELS || x1 < x0 || y1 < y0) {
        return 0;
    }

    int bw = x1 - x0 + 1;
    int bh = y1 - y0 + 1;
    if (bw < 3 || bh < 3) {
        return 0;
    }

    /* ---- 2. aspect-preserving box downscale -------------------------- */
    float f = (float)TARGET_SIDE / (float)(bw > bh ? bw : bh);
    int tw = round_f2i((float)bw * f);
    int th = round_f2i((float)bh * f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw > TMP_MAX) tw = TMP_MAX;
    if (th > TMP_MAX) th = TMP_MAX;

    for (int oy = 0; oy < th; oy++) {
        int sy0 = y0 + (oy * bh) / th;
        int sy1 = y0 + ((oy + 1) * bh) / th;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > AI_CANVAS_SIZE) sy1 = AI_CANVAS_SIZE;

        for (int ox = 0; ox < tw; ox++) {
            int sx0 = x0 + (ox * bw) / tw;
            int sx1 = x0 + ((ox + 1) * bw) / tw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > AI_CANVAS_SIZE) sx1 = AI_CANVAS_SIZE;

            uint32_t sum = 0;
            int n = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint8_t *row = canvas + sy * AI_CANVAS_SIZE;
                for (int sx = sx0; sx < sx1; sx++) {
                    sum += row[sx];
                    n++;
                }
            }
            s_tmp[oy * TMP_MAX + ox] = (uint8_t)(n > 0 ? (sum + n / 2) / (uint32_t)n : 0);
        }
    }

    /* ---- 3. centroid of the downscaled image ------------------------- */
    uint32_t mass = 0;
    uint32_t mx = 0, my = 0;
    for (int oy = 0; oy < th; oy++) {
        for (int ox = 0; ox < tw; ox++) {
            uint32_t v = s_tmp[oy * TMP_MAX + ox];
            mass += v;
            mx += v * (uint32_t)ox;
            my += v * (uint32_t)oy;
        }
    }
    if (mass == 0) {
        return 0;
    }
    float cx = (float)mx / (float)mass;
    float cy = (float)my / (float)mass;

    /* ---- 4. paste into a 28x28 frame centred on the centroid ---------- */
    memset(s_frame, 0, sizeof(s_frame));
    int dx = round_f2i((float)(AI_IMG_SIZE - 1) * 0.5f - cx);
    int dy = round_f2i((float)(AI_IMG_SIZE - 1) * 0.5f - cy);

    for (int oy = 0; oy < th; oy++) {
        int ty = oy + dy;
        if (ty < 0 || ty >= AI_IMG_SIZE) {
            continue;
        }
        for (int ox = 0; ox < tw; ox++) {
            int tx = ox + dx;
            if (tx < 0 || tx >= AI_IMG_SIZE) {
                continue;
            }
            int v = s_frame[ty * AI_IMG_SIZE + tx] + s_tmp[oy * TMP_MAX + ox];
            s_frame[ty * AI_IMG_SIZE + tx] = (uint8_t)(v > 255 ? 255 : v);
        }
    }

    /* ---- 5. normalise to the full 0..255 range ----------------------- */
    int maxv = 0;
    for (int i = 0; i < AI_IMG_SIZE * AI_IMG_SIZE; i++) {
        if (s_frame[i] > maxv) {
            maxv = s_frame[i];
        }
    }
    if (maxv <= 0) {
        return 0;
    }

    for (int i = 0; i < AI_IMG_SIZE * AI_IMG_SIZE; i++) {
        out[i] = (uint8_t)((s_frame[i] * 255 + maxv / 2) / maxv);
    }

    return 1;
}
