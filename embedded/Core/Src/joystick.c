#include "../Inc/joystick.h"
#include "../Inc/uart_log.h"

static ADC_HandleTypeDef *s_hadc;
static GPIO_TypeDef      *s_clickPort;
static uint16_t           s_clickPin;

void JOY_Init(ADC_HandleTypeDef *hadc, GPIO_TypeDef *clickPort, uint16_t clickPin)
{
    s_hadc      = hadc;
    s_clickPort = clickPort;
    s_clickPin  = clickPin;
    ULOG_Info("JOY", "Init", "done (polling mode)");
}

JOY_Data JOY_Read(void)
{
    JOY_Data result = {0};
    result.status = JOY_ERROR;

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;

    /* --- Read X (PA0, Channel 0) --- */
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank    = 1;
    if (HAL_ADC_ConfigChannel(s_hadc, &sConfig) != HAL_OK) return result;
    if (HAL_ADC_Start(s_hadc)                   != HAL_OK) return result;
    if (HAL_ADC_PollForConversion(s_hadc, 10)   != HAL_OK) { HAL_ADC_Stop(s_hadc); return result; }
    result.x_raw = (uint16_t)HAL_ADC_GetValue(s_hadc);
    HAL_ADC_Stop(s_hadc);

    /* --- Read Y (PA1, Channel 1) --- */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank    = 1;
    if (HAL_ADC_ConfigChannel(s_hadc, &sConfig) != HAL_OK) return result;
    if (HAL_ADC_Start(s_hadc)                   != HAL_OK) return result;
    if (HAL_ADC_PollForConversion(s_hadc, 10)   != HAL_OK) { HAL_ADC_Stop(s_hadc); return result; }
    result.y_raw = (uint16_t)HAL_ADC_GetValue(s_hadc);
    HAL_ADC_Stop(s_hadc);

    /* --- Click (PA4, GPIO) --- */
    result.click = (HAL_GPIO_ReadPin(s_clickPort, s_clickPin) == GPIO_PIN_RESET) ? 1 : 0;

    /* --- Direction --- */
    if      (result.x_raw < JOY_THRESH_LOW)  result.direction = JOY_LEFT;
    else if (result.x_raw > JOY_THRESH_HIGH) result.direction = JOY_RIGHT;
    else if (result.y_raw < JOY_THRESH_LOW)  result.direction = JOY_UP;
    else if (result.y_raw > JOY_THRESH_HIGH) result.direction = JOY_DOWN;
    else if (result.x_raw >= JOY_DEADZONE_LOW && result.x_raw <= JOY_DEADZONE_HIGH &&
             result.y_raw >= JOY_DEADZONE_LOW && result.y_raw <= JOY_DEADZONE_HIGH)
        result.direction = JOY_CENTRE;
    else
        result.direction = JOY_NONE;

    result.status = JOY_OK;
    return result;
}
