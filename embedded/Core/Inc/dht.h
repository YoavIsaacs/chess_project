#ifndef DHT_H
#define DHT_H

#include "stm32f4xx_hal.h"

typedef enum {
  DHT_OK,
  DHT_ERROR_TIMEOUT,
  DHT_ERROR_CHECKSUM
} DHT_Status;

typedef struct {
  float temperature_c;
  float humidity_pct;
  DHT_Status status;
} DHT_Data;

void DHT_Init(GPIO_TypeDef *port, uint16_t pin);
DHT_Data DHT_Read(void);

#endif // !DHT_H
