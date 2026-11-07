/*
 * board_port.h -- the only place where the application touches the Wildfire
 * BSP.  Everything above this layer (canvas, UI, AI) is hardware independent.
 *
 * Adapting to your board only requires editing board_port.c, not the rest of
 * the App layer.
 */
#ifndef BOARD_PORT_H
#define BOARD_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RGB565 colours */
#define BP_BLACK 0x0000u
#define BP_WHITE 0xFFFFu
#define BP_GRAY  0x8410u
#define BP_DGRAY 0x4208u
#define BP_RED   0xF800u
#define BP_GREEN 0x07E0u
#define BP_BLUE  0x001Fu
#define BP_CYAN  0x07FFu
#define BP_YELLOW 0xFFE0u

#define BP_SCREEN_W 320
#define BP_SCREEN_H 240

void bp_init(void);
void bp_clear(uint16_t color);
void bp_fill(int x0, int y0, int x1, int y1, uint16_t color);
void bp_pixel(int x, int y, uint16_t color);
void bp_rect(int x0, int y0, int x1, int y1, uint16_t color);
void bp_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);

/** @return 1 while the panel is touched, with screen coordinates in x/y */
int bp_touch_read(int *x, int *y);
/** @return 1 on a fresh key press (used for "clear") */
int bp_key_pressed(void);

/** milliseconds since boot (wraps every ~49 days, unsigned diff is safe) */
uint32_t bp_millis(void);
void bp_delay_ms(uint32_t ms);

/** formatted log over UART, no-op when the UART is unavailable */
void bp_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PORT_H */
