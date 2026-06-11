#ifndef LCD2_H
#define LCD2_H

#include "stm32f4xx_hal.h"

#define LCD2_I2C_ADDR (0x3F << 1)

typedef enum {
  LCD2_OK,
  LCD2_ERROR
} LCD2_Status;

void LCD2_Init(I2C_HandleTypeDef *hi2c);
void LCD2_Clear(void);
void LCD2_SetCursor(uint8_t row, uint8_t col);
void LCD2_PrintString(const char *str);
void LCD2_PrintChar(char c);
void LCD2_Backlight(uint8_t on);

#endif // !LCD2_H
