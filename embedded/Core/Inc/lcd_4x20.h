/*
 * lcd_4x20.h — HD44780 4x20 LCD driver via PCF8574 I2C backpack.
 *
 * Hardware assumptions:
 *   - 4x20 HD44780-compatible LCD with PCF8574 I2C backpack
 *   - I2C address: 0x27 (7-bit); passed to HAL as 0x27 << 1
 *   - PCF8574 pin mapping: P7-P4 = DB7-DB4, P3 = backlight, P2 = EN,
 *     P1 = RW (always 0, write-only), P0 = RS
 *   - I2C1 must be initialised and passed to LCD4_Init() before any
 *     other function is called.
 *
 * Backlight note:
 *   Backlight state is tracked internally and toggled via LCD4_Backlight().
 *   It is ON by default after LCD4_Init().
 *
 * Deviation from stub:
 *   LCD4_Init() returns LCD4_Status (not void) so the caller can detect
 *   a dead I2C bus at startup.
 *
 * Custom characters (spec 2.5):
 *   Slot 0 — Top-left corner     (pause box border)
 *   Slot 1 — Top-right corner    (pause box border)
 *   Slot 2 — Bottom-left corner  (pause box border)
 *   Slot 3 — Bottom-right corner (pause box border)
 *   Slot 4 — Full block          (eval bar filled)
 *   Slot 5 — Understroke         (eval bar empty)
 *   Slots 6-7 — Reserved
 */

#ifndef LCD4_H
#define LCD4_H

#include "stm32f4xx_hal.h"

#define LCD4_I2C_ADDR             (0x27 << 1)

#define LCD4_CUSTOM_CORNER_TL     0
#define LCD4_CUSTOM_CORNER_TR     1
#define LCD4_CUSTOM_CORNER_BL     2
#define LCD4_CUSTOM_CORNER_BR     3
#define LCD4_CUSTOM_BLOCK         4
#define LCD4_CUSTOM_UNDERSTROKE   5

typedef enum {
    LCD4_OK,
    LCD4_ERROR
} LCD4_Status;

/*
 * Initialise the LCD. Must be called once after I2C is ready.
 * Stores the handle internally — no other function takes a handle.
 * Returns LCD4_ERROR if the initial I2C write fails (bus fault,
 * wrong address, no pull-ups).
 */
LCD4_Status LCD4_Init(I2C_HandleTypeDef *hi2c);

/* Clear the display and return cursor to home (row 0, col 0). */
void LCD4_Clear(void);

/* Move the cursor. row: 0-3. col: 0-19. */
void LCD4_SetCursor(uint8_t row, uint8_t col);

/* Print a null-terminated string at the current cursor position. */
void LCD4_PrintString(const char *str);

/* Print a single character at the current cursor position. */
void LCD4_PrintChar(char c);

/* Turn backlight on (on != 0) or off (on == 0). */
void LCD4_Backlight(uint8_t on);

/*
 * Write a custom character bitmap into CGRAM slot (0-7).
 * bitmap must point to exactly 8 bytes, one per row (bits 4-0 used).
 * Call before LCD4_Clear() or LCD4_PrintChar() that uses the slot,
 * as CGRAM writes leave the controller in CGRAM address mode.
 */
void LCD4_DefineCustomChar(uint8_t slot, uint8_t *bitmap);

#endif /* LCD4_H */
