#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "stm32f4xx_hal.h"

#define JOY_THRESH_LOW      800
#define JOY_THRESH_HIGH     3300
#define JOY_DEADZONE_LOW    1548
#define JOY_DEADZONE_HIGH   2548

typedef enum{
  JOY_NONE,
  JOY_UP,
  JOY_DOWN,
  JOY_LEFT,
  JOY_RIGHT,
  JOY_CENTRE,
} JOY_Direction;

typedef enum {
  JOY_OK,
  JOY_ERROR
} JOY_Status;

typedef struct {
  uint16_t      x_raw;
  uint16_t      y_raw;
  uint8_t       click;
  JOY_Direction direction;
  JOY_Status    status;
} JOY_Data;

void JOY_Init(ADC_HandleTypeDef *hadc, GPIO_TypeDef *clickPort, uint16_t clickPin);
JOY_Data JOY_Read(void);

#endif // !JOYSTICK_H
