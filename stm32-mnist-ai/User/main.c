/*
 * main.c -- Wildfire STM32F103 "Guide": hand-written digit recognition.
 *
 * Touch -> 96x96 canvas -> crop/rescale -> 28x28 -> INT8 MLP -> LCD.
 * Inference runs on every pen-up and at most every 150 ms while drawing.
 */
#include "ai_mlp.h"
#include "ai_preprocess.h"
#include "app_ui.h"
#include "board_port.h"
#include "draw_canvas.h"

#define INFER_PERIOD_MS 150

static uint8_t s_img[AI_IMG_SIZE * AI_IMG_SIZE];
static int s_touching;
static int s_dirty;
static uint32_t s_last_infer;

static void run_inference(void)
{
    float probs[AI_CLASSES];
    uint32_t t0;
    uint32_t t1;
    int digit;

    if (!ai_preprocess_canvas(canvas_buffer(), s_img)) {
        ui_show_empty();
        return;
    }

    t0 = bp_millis();
    digit = ai_mlp_infer(s_img, probs);
    t1 = bp_millis();

    ui_show_result(digit, probs, t1 - t0);

    bp_log("digit=%d conf=%d%% cost=%dms\r\n",
           digit,
           (int)(probs[digit] * 100.0f + 0.5f),
           (int)(t1 - t0));
}

int main(void)
{
    bp_init();
    bp_clear(BP_BLACK);

    canvas_reset();
    ui_init();
    ui_status("write with the stylus");

    bp_log("\r\nMNIST INT8 demo ready\r\n");

    for (;;) {
        int sx, sy, cx, cy;

        if (bp_key_pressed()) {
            canvas_reset();
            ui_show_empty();
            ui_status("cleared");
            s_dirty = 0;
            continue;
        }

        if (bp_touch_read(&sx, &sy)) {
            if (!s_touching) {
                s_touching = 1;
                canvas_new_stroke();
            }

            if (canvas_screen_to_canvas(sx, sy, &cx, &cy)) {
                canvas_stroke_to(cx, cy);
                s_dirty = 1;
            }

            if (s_dirty && (uint32_t)(bp_millis() - s_last_infer) >= INFER_PERIOD_MS) {
                s_last_infer = bp_millis();
                run_inference();
            }
        } else if (s_touching) {
            s_touching = 0;
            if (s_dirty) {
                run_inference();
            }
        }
    }
}
