/*
 * app_ui.h -- screen chrome and result rendering.
 */
#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

#include "model_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Draw the static layout (call once after canvas_reset()). */
void ui_init(void);

/** Show a result: big digit, 10 confidence bars, confidence + timing. */
void ui_show_result(int digit, const float probs[AI_CLASSES], uint32_t cost_ms);

/** Clear the result area ("--"). */
void ui_show_empty(void);

/** Bottom bar text, ASCII only. */
void ui_status(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
