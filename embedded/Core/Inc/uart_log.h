#ifndef UART_LOG_H
#define UART_LOG_H

#include "stm32f4xx_hal.h"

/* Call once at startup, passing the HUART handle configured in CubeMX */
void ULOG_Init(UART_HandleTypeDef *huart);

/* Log a plain message */
void ULOG_Info(const char *tag, const char *msg);

/* Log a message with one integer value */
void ULOG_InfoInt(const char *tag, const char *msg, int32_t value);

/* Log a message with one float value (2 decimal places) */
void ULOG_InfoFloat(const char *tag, const char *msg, float value);

/* Log an error */
void ULOG_Error(const char *tag, const char *msg);

/* Log an error with one integer value */
void ULOG_ErrorInt(const char *tag, const char *msg, int32_t value);

#endif /* UART_LOG_H */
