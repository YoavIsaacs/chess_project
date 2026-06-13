/*
 * lcd_2x16.h — HD44780 2x16 LCD driver via PCF8574 I2C backpack.
 *
 * Hardware assumptions:
 *   - 2x16 HD44780-compatible LCD with PCF8574 I2C backpack
 *   - I2C address: 0x3F (7-bit); passed to HAL as 0x3F << 1
 *   - PCF8574 pin mapping: P7-P4 = DB7-DB4, P3 = backlight, P2 = EN,
 *     P1 = RW (always 0, write-only), P0 = RS
 *   - I2C1 must be initialised and passed to LCD2_Init() before any
 *     other function is called.
 *
 * Backlight note:
 *   The backlight is always on. LCD2_Backlight() exists to satisfy the
 *   driver interface but is a no-op. Do not rely on it to turn the
 *   backlight off.
 *
 * Deviation from stub:
 *   LCD2_Init() returns LCD2_Status (not void) so the caller can detect
 *   a dead I2C bus at startup.
 */

#ifndef LCD2_H
#define LCD2_H

#include "stm32f4xx_hal.h"

#define LCD2_I2C_ADDR (0x3F << 1)

typedef enum {
    LCD2_OK,
    LCD2_ERROR
} LCD2_Status;

/*
 * Initialise the LCD. Must be called once after I2C is ready.
 * Stores the handle internally — no other function takes a handle.
 * Returns LCD2_ERROR if the initial I2C write fails (bus fault,
 * wrong address, no pull-ups).
 */
LCD2_Status LCD2_Init(I2C_HandleTypeDef *hi2c);

/* Clear the display and return cursor to home (row 0, col 0). */
void LCD2_Clear(void);

/* Move the cursor. row: 0 or 1. col: 0–15. */
void LCD2_SetCursor(uint8_t row, uint8_t col);

/* Print a null-terminated string at the current cursor position. */
void LCD2_PrintString(const char *str);

/* Print a single character at the current cursor position. */
void LCD2_PrintChar(char c);

/*
 * No-op — backlight is hardwired on.
 * Present to satisfy the driver interface contract.
 */
void LCD2_Backlight(uint8_t on);

#endif /* LCD2_H */
