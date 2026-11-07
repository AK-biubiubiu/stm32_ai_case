/*
 * ai_preprocess.h -- turn the 96x96 touch canvas into a 28x28 MNIST-style image.
 *
 * Pure C99, no STM32 dependency.
 */
#ifndef AI_PREPROCESS_H
#define AI_PREPROCESS_H

#include <stdint.h>

#define AI_CANVAS_SIZE 96
#define AI_IMG_SIZE    28

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Crop the drawn ink, rescale it so the longest side is 20 px, centre it by
 * its grey-level centroid in a 28x28 frame and normalise to 0..255.
 *
 * @param canvas AI_CANVAS_SIZE*AI_CANVAS_SIZE, row-major, 0 = background
 * @param out    28*28 bytes, 0 = background, 255 = ink
 * @return       1 on success, 0 when the canvas is empty / too small
 */
int ai_preprocess_canvas(const uint8_t *canvas, uint8_t out[AI_IMG_SIZE * AI_IMG_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* AI_PREPROCESS_H */
