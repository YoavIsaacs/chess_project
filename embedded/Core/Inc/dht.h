#ifndef DHT_H
#define DHT_H

#include "stm32f4xx_hal.h"

/* ------------------------------------------------------------------ */
/*  Enums                                                               */
/* ------------------------------------------------------------------ */

typedef enum
{
    DHT_OK               = 0,
    DHT_ERROR_NO_RESPONSE,
    DHT_ERROR_CHECKSUM
} DHT_Status;

/* ------------------------------------------------------------------ */
/*  Structs                                                             */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint8_t    temperature_c;
    uint8_t    humidity_pct;
    DHT_Status status;
} DHT_Data;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief  Initialise the DHT driver.
 *         Enables the DWT cycle counter and drives PB12 high.
 *         Must be called once before DHT_Read.
 */
void     DHT_Init(void);

/**
 * @brief  Perform a single read transaction with the DHT11.
 * @return DHT_Data struct.  Check .status before using .temperature_c
 *         and .humidity_pct.  On any error the data fields are 0.
 */
DHT_Data DHT_Read(void);

#endif /* DHT_H */
