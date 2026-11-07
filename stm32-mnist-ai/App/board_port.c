/*
 * board_port.c -- Wildfire STM32F103 "Guide" BSP glue.
 *
 * ============================================================================
 *  ADAPTATION POINT -- check these names against your BSP (bsp_*.h)
 * ============================================================================
 *  Written against the standard Wildfire/F103 BSP naming.  If your template
 *  differs, only this file needs to change:
 *
 *    LCD_Init()                                   -> LCD_Init()
 *    LCD_Clear(color)                             -> LCD_Clear()
 *    LCD_Fill(x0,y0,x1,y1,color)                  -> LCD_Fill()
 *    LCD_DrawPoint(x,y) / LCD_SetPointColor()     -> LCD_DrawPoint()
 *    LCD_DrawLine(x0,y0,x1,y1)                    -> LCD_DrawLine()
 *    LCD_DrawRectangle(x0,y0,x1,y1)               -> LCD_DrawRectangle()
 *    LCD_DispString_EN(x,y,str)                   -> LCD_DispString_EN()
 *    TOUCH_Init()                                 -> TOUCH_Init()
 *    TOUCH_Scan()                                 -> TOUCH_Scan()
 *    TOUCH_GetPoint(&x,&y)                        -> TOUCH_GetPoint()
 *    KEY_Scan(mode)                               -> KEY_Scan()
 *
 *  If your LCD driver only exposes LCD_DrawPoint() without a colour argument
 *  (older Wildfire code uses LCD_SetColors / LCD_SetPointColor), set
 *  BP_LCD_POINT_HAS_COLOR to 0 below.
 * ============================================================================
 */
#include "board_port.h"

#include <stdarg.h>
#include <stdio.h>

#include "stm32f10x.h"

#include "bsp_ili9341_lcd.h"   /* LCD_*            */
#include "bsp_touch.h"         /* TOUCH_*          */
#include "bsp_key.h"           /* KEY_Scan()       */
#include "bsp_usart.h"         /* USART_printf()   */

/* Uncomment if your template uses the older point API. */
/* #define BP_LCD_POINT_HAS_COLOR 0 */

static void time_init(void);

/* ------------------------------------------------------------------------- */
/* screen                                                                    */
/* ------------------------------------------------------------------------- */
void bp_init(void)
{
    time_init();
    LCD_Init();
    TOUCH_Init();
}

void bp_clear(uint16_t color)
{
    LCD_Clear(color);
}

void bp_fill(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BP_SCREEN_W - 1) x1 = BP_SCREEN_W - 1;
    if (y1 > BP_SCREEN_H - 1) y1 = BP_SCREEN_H - 1;
    if (x1 < x0 || y1 < y0) {
        return;
    }
    LCD_Fill((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1, color);
}

void bp_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= BP_SCREEN_W || y < 0 || y >= BP_SCREEN_H) {
        return;
    }
    LCD_SetColors(color, BP_BLACK);
    LCD_DrawPoint((uint16_t)x, (uint16_t)y);
}

void bp_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    LCD_SetColors(color, BP_BLACK);
    LCD_DrawRectangle((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
}

void bp_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    LCD_SetColors(fg, bg);
    LCD_DispString_EN((uint16_t)x, (uint16_t)y, s);
}

/* ------------------------------------------------------------------------- */
/* touch / key                                                               */
/* ------------------------------------------------------------------------- */
int bp_touch_read(int *x, int *y)
{
    uint16_t tx = 0;
    uint16_t ty = 0;

    if (TOUCH_Scan() == 0) {
        return 0;
    }
    TOUCH_GetPoint(&tx, &ty);
    if (tx >= BP_SCREEN_W || ty >= BP_SCREEN_H) {
        return 0;
    }
    *x = (int)tx;
    *y = (int)ty;
    return 1;
}

int bp_key_pressed(void)
{
    /* KEY_Scan(0) returns the key code, 0 when nothing is pressed. */
    return KEY_Scan(0) != 0;
}

/* ------------------------------------------------------------------------- */
/* time -- DWT cycle counter, no dependency on the BSP delay module          */
/* ------------------------------------------------------------------------- */
static uint32_t s_last_cyc;
static uint32_t s_ms;

static uint32_t cycles_per_ms(void)
{
    uint32_t c = SystemCoreClock / 1000u;
    return c ? c : 1u;
}

uint32_t bp_millis(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t d = now - s_last_cyc;         /* wrap safe */
    uint32_t ms = d / cycles_per_ms();

    if (ms != 0u) {
        s_ms += ms;
        s_last_cyc += ms * cycles_per_ms();
    }
    return s_ms;
}

void bp_delay_ms(uint32_t ms)
{
    uint32_t start = bp_millis();
    while ((uint32_t)(bp_millis() - start) < ms) {
        __NOP();
    }
}

static void time_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_last_cyc = DWT->CYCCNT;
    s_ms = 0;
}

/* ------------------------------------------------------------------------- */
/* logging -- polled USART1, no newlib retarget needed                       */
/* ------------------------------------------------------------------------- */
static void uart_putc(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0u) {
        ;
    }
    USART1->DR = (uint16_t)(uint8_t)c;
}

void bp_log(const char *fmt, ...)
{
    char buf[96];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    for (const char *p = buf; *p != '\0'; p++) {
        uart_putc(*p);
    }
}
