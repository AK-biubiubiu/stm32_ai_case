/*
 * ai_mlp.h -- INT8 quantised MLP for the STM32F103 hand-written digit demo.
 *
 * Pure C99, no STM32 dependency: the same file is compiled on the host for
 * verification (see tools/host_test.c).
 */
#ifndef AI_MLP_H
#define AI_MLP_H

#include <stdint.h>

#include "model_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run one forward pass.
 *
 * @param x     784 bytes, row-major 28x28 grayscale, 0 = background, 255 = ink
 * @param probs optional output, AI_CLASSES softmax probabilities (may be NULL)
 * @return      predicted class (0..9)
 */
int ai_mlp_infer(const uint8_t x[AI_INPUT_SIZE], float probs[AI_CLASSES]);

/**
 * Same as ai_mlp_infer() but also exposes the raw logits (useful for tests).
 */
int ai_mlp_infer_logits(const uint8_t x[AI_INPUT_SIZE], float logits[AI_CLASSES],
                        float probs[AI_CLASSES]);

#ifdef __cplusplus
}
#endif

#endif /* AI_MLP_H */
