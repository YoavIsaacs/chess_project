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

void LCD4_Init(I2C_HandleTypeDef *hi2c);
void LCD4_Clear(void);
void LCD4_SetCursor(uint8_t row, uint8_t col);
void LCD4_PrintString(const char *str);
void LCD4_PrintChar(char c);
void LCD4_Backlight(uint8_t on);
void LCD4_DefineCustomChar(uint8_t slot, uint8_t *bitmap);

#endif // !LCD4_H
