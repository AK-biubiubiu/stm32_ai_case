/*
 * draw_canvas.h -- 96x96 ink canvas with a 2x on-screen preview.
 */
#ifndef DRAW_CANVAS_H
#define DRAW_CANVAS_H

#include <stdint.h>

#include "ai_preprocess.h"

#define CANVAS_SIZE     AI_CANVAS_SIZE
#define CANVAS_ZOOM     2
#define CANVAS_X        6
#define CANVAS_Y        26
#define BRUSH_RADIUS    3

#ifdef __cplusplus
extern "C" {
#endif

/** Clear the buffer and the on-screen drawing area. */
void canvas_reset(void);

/** Start a new stroke: the next sample is not joined to the previous one. */
void canvas_new_stroke(void);

/**
 * Extend the current stroke to a canvas coordinate (0..95).  Consecutive
 * samples are joined with a Bresenham line so a sparse resistive-touch
 * sample rate does not break the stroke.
 */
void canvas_stroke_to(int cx, int cy);

/** Map a screen coordinate to the canvas. @return 0 when outside the area. */
int canvas_screen_to_canvas(int sx, int sy, int *cx, int *cy);

const uint8_t *canvas_buffer(void);

/** Number of ink pixels (fast enough for a 96x96 buffer). */
int canvas_ink_pixels(void);

#ifdef __cplusplus
}
#endif

#endif /* DRAW_CANVAS_H */
