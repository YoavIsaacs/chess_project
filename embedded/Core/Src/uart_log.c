#include "uart_log.h"
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef *s_huart = NULL;

void ULOG_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    const char *clear = "\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n";
    HAL_UART_Transmit(huart, (uint8_t *)clear, strlen(clear), 100);
}

/* Internal send — blocks until done */
static void ulog_send(const char *buf, uint16_t len)
{
    if (s_huart == NULL) return;
    HAL_UART_Transmit(s_huart, (uint8_t *)buf, len, 100);
}

void ULOG_Info(const char *tag, const char *msg)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[INFO][%s] %s\r\n", tag, msg);
    if (len > 0) ulog_send(buf, (uint16_t)len);
}

void ULOG_InfoInt(const char *tag, const char *msg, int32_t value)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[INFO][%s] %s %ld\r\n", tag, msg, (long)value);
    if (len > 0) ulog_send(buf, (uint16_t)len);
}

void ULOG_InfoFloat(const char *tag, const char *msg, float value)
{
    /* Avoid printf float — split into integer and fractional parts */
    int32_t whole = (int32_t)value;
    int32_t frac  = (int32_t)((value - (float)whole) * 100.0f);
    if (frac < 0) frac = -frac;

    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[INFO][%s] %s %ld.%02ld\r\n",
                       tag, msg, (long)whole, (long)frac);
    if (len > 0) ulog_send(buf, (uint16_t)len);
}

void ULOG_Error(const char *tag, const char *msg)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[ERR ][%s] %s\r\n", tag, msg);
    if (len > 0) ulog_send(buf, (uint16_t)len);
}

void ULOG_ErrorInt(const char *tag, const char *msg, int32_t value)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "[ERR ][%s] %s %ld\r\n", tag, msg, (long)value);
    if (len > 0) ulog_send(buf, (uint16_t)len);
}
