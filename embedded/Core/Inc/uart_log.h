/*
 * uart_log.h — UART logging utility for STM32 debug output.
 *
 * Output format: [LEVEL][TAG][FUNC] message
 * Example:       [INFO][LCD2][Init] Init OK
 *
 * Usage:
 *   ULOG_Init(&huart2);
 *   ULOG_Info("LCD2", "Init", "Init OK");
 *   ULOG_InfoInt("JOY", "Read", "X raw", j.x_raw);
 */

#ifndef UART_LOG_H
#define UART_LOG_H

#include "stm32f4xx_hal.h"

/*
 * Call once after HAL_UART_Init. Flushes PuTTY terminal with blank lines.
 */
void ULOG_Init(UART_HandleTypeDef *huart);

/* [INFO][tag][func] msg */
void ULOG_Info(const char *tag, const char *func, const char *msg);

/* [INFO][tag][func] msg value */
void ULOG_InfoInt(const char *tag, const char *func, const char *msg, int32_t value);

/* [INFO][tag][func] msg whole.frac */
void ULOG_InfoFloat(const char *tag, const char *func, const char *msg, float value);

/* [ERR ][tag][func] msg */
void ULOG_Error(const char *tag, const char *func, const char *msg);

/* [ERR ][tag][func] msg value */
void ULOG_ErrorInt(const char *tag, const char *func, const char *msg, int32_t value);

#endif /* UART_LOG_H */
