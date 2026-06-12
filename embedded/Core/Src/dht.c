#include "../Inc/dht.h"

/*
 * MOCK IMPLEMENTATION
 * Real DHT11 module was destroyed during hardware debug (reverse-polarity).
 * Replacement on order. This file simulates plausible sensor data so the
 * rest of the system can continue development unblocked.
 * Replace this file with the real implementation when the new sensor arrives.
 */

static const uint8_t mock_temperature[] = { 22, 23, 24, 25, 25, 24, 23, 22, 21, 22 };
static const uint8_t mock_humidity[]    = { 50, 52, 55, 57, 60, 62, 60, 57, 54, 51 };

#define MOCK_STEPS (sizeof(mock_temperature) / sizeof(mock_temperature[0]))

static uint32_t mock_index = 0;

void DHT_Init(void)
{
    /* No hardware to initialise in mock mode */
    mock_index = 0;
}

DHT_Data DHT_Read(void)
{
    DHT_Data result = {0};
    result.temperature_c = mock_temperature[mock_index];
    result.humidity_pct  = mock_humidity[mock_index];
    result.status        = DHT_OK;

    mock_index = (mock_index + 1) % MOCK_STEPS;

    return result;
}
