/*
 * host_test.c -- verify the on-device pipeline on the PC (x86 gcc).
 *
 * Compiles App/ai_mlp.c and App/ai_preprocess.c unchanged, so every check here
 * exercises exactly the code that goes into the STM32 firmware.
 *
 * Checks
 *   1. int8 inference on raw MNIST images          -> accuracy vs ground truth
 *   2. C output vs Python int8 simulation          -> agreement
 *   3. full chain (canvas -> preprocess -> infer)  -> accuracy on a simulated
 *                                                     "fat touch stroke" canvas
 *   4. empty canvas handling
 *   5. per-inference cost on the host (order of magnitude only)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ai_mlp.h"
#include "ai_preprocess.h"

#define N_PIX  (AI_IMG_SIZE * AI_IMG_SIZE)
#define CANVAS (AI_CANVAS_SIZE * AI_CANVAS_SIZE)

static uint8_t s_canvas[CANVAS];
static uint8_t s_frame[N_PIX];

static void die(const char *msg)
{
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static void load(const char *path, void *buf, size_t bytes)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open %s\n", path);
        exit(1);
    }
    if (fread(buf, 1, bytes, f) != bytes) {
        die("short read");
    }
    fclose(f);
}

/* Paint a disk of radius r onto the canvas (mimics the on-device brush). */
static void paint_disk(int cx, int cy, int r, uint8_t value)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) {
                continue;
            }
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || x >= AI_CANVAS_SIZE || y < 0 || y >= AI_CANVAS_SIZE) {
                continue;
            }
            uint8_t *p = &s_canvas[y * AI_CANVAS_SIZE + x];
            if (value > *p) {
                *p = value;
            }
        }
    }
}

/* 28x28 MNIST image -> 96x96 canvas drawn with a radius-3 brush. */
static void image_to_canvas(const uint8_t *img)
{
    memset(s_canvas, 0, sizeof(s_canvas));
    for (int y = 0; y < AI_IMG_SIZE; y++) {
        for (int x = 0; x < AI_IMG_SIZE; x++) {
            uint8_t v = img[y * AI_IMG_SIZE + x];
            if (v < 32) {
                continue;
            }
            int cx = (x * AI_CANVAS_SIZE + AI_CANVAS_SIZE / 2) / AI_IMG_SIZE;
            int cy = (y * AI_CANVAS_SIZE + AI_CANVAS_SIZE / 2) / AI_IMG_SIZE;
            paint_disk(cx, cy, 3, v);
        }
    }
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "tools/data";
    const int n = 1000;
    char path[512];

    uint8_t *images = (uint8_t *)malloc((size_t)n * N_PIX);
    uint8_t *labels = (uint8_t *)malloc((size_t)n);
    uint8_t *py_pred = (uint8_t *)malloc((size_t)n);
    if (images == NULL || labels == NULL || py_pred == NULL) {
        die("out of memory");
    }

    snprintf(path, sizeof(path), "%s/test_images.bin", dir);
    load(path, images, (size_t)n * N_PIX);
    snprintf(path, sizeof(path), "%s/test_labels.bin", dir);
    load(path, labels, (size_t)n);
    snprintf(path, sizeof(path), "%s/test_pred.bin", dir);
    load(path, py_pred, (size_t)n);

    /* ---- 1 & 2: raw MNIST ------------------------------------------- */
    int correct = 0;
    int agree = 0;
    for (int i = 0; i < n; i++) {
        float probs[AI_CLASSES];
        int pred = ai_mlp_infer(&images[(size_t)i * N_PIX], probs);
        if (pred == labels[i]) {
            correct++;
        }
        if (pred == py_pred[i]) {
            agree++;
        }
    }
    printf("[1] raw MNIST int8 accuracy     : %.2f%%  (%d/%d)\n",
           100.0 * correct / n, correct, n);
    printf("[2] C vs Python agreement       : %.2f%%  (%d/%d)\n",
           100.0 * agree / n, agree, n);

    /* ---- 3: simulated touch canvas ---------------------------------- */
    correct = 0;
    int valid = 0;
    for (int i = 0; i < n; i++) {
        image_to_canvas(&images[(size_t)i * N_PIX]);
        if (!ai_preprocess_canvas(s_canvas, s_frame)) {
            continue;
        }
        valid++;
        if (ai_mlp_infer(s_frame, NULL) == labels[i]) {
            correct++;
        }
    }
    printf("[3] canvas->preprocess->infer    : %.2f%%  (%d/%d valid)\n",
           100.0 * correct / (valid ? valid : 1), correct, valid);

    /* ---- 4: empty canvas -------------------------------------------- */
    memset(s_canvas, 0, sizeof(s_canvas));
    printf("[4] empty canvas rejected        : %s\n",
           ai_preprocess_canvas(s_canvas, s_frame) == 0 ? "yes" : "NO");

    /* ---- 5: host timing --------------------------------------------- */
    {
        float probs[AI_CLASSES];
        clock_t t0 = clock();
        volatile int sink = 0;
        for (int i = 0; i < 200; i++) {
            sink += ai_mlp_infer(&images[(size_t)(i % n) * N_PIX], probs);
        }
        clock_t t1 = clock();
        (void)sink;
        double us = 1e6 * (double)(t1 - t0) / CLOCKS_PER_SEC / 200.0;
        printf("[5] host inference time         : %.1f us/call (M3 estimate: 12-16 ms @72 MHz)\n", us);
    }

    free(images);
    free(labels);
    free(py_pred);

    int ok = (100.0 * agree / n >= 99.5);
    printf("\nresult: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
