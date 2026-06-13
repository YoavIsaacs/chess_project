/*
 * lcd_4x20.c — HD44780 4x20 LCD driver via PCF8574 I2C backpack.
 *
 * PCF8574 bit layout sent in every I2C byte:
 *   Bit 7 = DB7   Bit 6 = DB6   Bit 5 = DB5   Bit 4 = DB4
 *   Bit 3 = BL    Bit 2 = EN    Bit 1 = RW    Bit 0 = RS
 *
 * All register writes use 4-bit mode: high nibble first, then low nibble.
 * Each nibble is pulsed by toggling EN high then low.
 */

#include "../Inc/lcd_4x20.h"
#include "../Inc/uart_log.h"

/* HD44780 instruction set */
#define LCD4_CMD_CLEAR          0x01
#define LCD4_CMD_HOME           0x02
#define LCD4_CMD_ENTRY_MODE     0x06   /* increment, no shift */
#define LCD4_CMD_DISPLAY_ON     0x0C   /* display on, cursor off, blink off */
#define LCD4_CMD_FUNCTION_SET   0x28   /* 4-bit, 2-line, 5x8 font */
#define LCD4_CMD_CGRAM_ADDR     0x40   /* OR with (slot << 3) for CGRAM */
#define LCD4_CMD_DDRAM_ADDR     0x80   /* OR with address */

/* PCF8574 control bits */
#define LCD4_BL   0x08
#define LCD4_EN   0x04
#define LCD4_RW   0x02
#define LCD4_RS   0x01

/* HD44780 DDRAM row offsets for 4x20 */
static const uint8_t ROW_OFFSETS[4] = { 0x00, 0x40, 0x14, 0x54 };

/* Module-level state */
static I2C_HandleTypeDef *s_hi2c = NULL;
static uint8_t            s_bl   = LCD4_BL;   /* backlight on by default */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static HAL_StatusTypeDef write_byte(uint8_t byte)
{
    return HAL_I2C_Master_Transmit(s_hi2c, LCD4_I2C_ADDR, &byte, 1, HAL_MAX_DELAY);
}

static void pulse_en(uint8_t data)
{
    write_byte(data | LCD4_EN);
    HAL_Delay(1);
    write_byte(data & ~LCD4_EN);
    HAL_Delay(1);
}

static void write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = (nibble & 0xF0) | s_bl | rs;
    pulse_en(data);
}

static void send_byte(uint8_t byte, uint8_t rs)
{
    write_nibble(byte & 0xF0, rs);          /* high nibble */
    write_nibble((byte << 4) & 0xF0, rs);  /* low nibble  */
}

static void send_cmd(uint8_t cmd)
{
    send_byte(cmd, 0);
}

static void send_data(uint8_t data)
{
    send_byte(data, LCD4_RS);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

LCD4_Status LCD4_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;
    s_bl   = LCD4_BL;

    HAL_Delay(50);   /* power-on stabilisation */

    /* HD44780 init-by-instruction sequence (4-bit mode entry) */
    /* Three function-set writes in 8-bit mode to reset the controller */
    write_nibble(0x30, 0);
    HAL_Delay(5);
    write_nibble(0x30, 0);
    HAL_Delay(1);
    write_nibble(0x30, 0);
    HAL_Delay(1);

    /* Switch to 4-bit mode */
    write_nibble(0x20, 0);
    HAL_Delay(1);

    /* Now in 4-bit mode — send full commands as two nibbles */
    send_cmd(LCD4_CMD_FUNCTION_SET);   /* 4-bit, 2-line (HD44780 sees 2-line for 4x20), 5x8 */
    HAL_Delay(1);
    send_cmd(LCD4_CMD_DISPLAY_ON);
    HAL_Delay(1);
    send_cmd(LCD4_CMD_CLEAR);
    HAL_Delay(2);
    send_cmd(LCD4_CMD_ENTRY_MODE);
    HAL_Delay(1);

    /* Verify I2C is alive with a probe write */
    uint8_t probe = s_bl;
    if (HAL_I2C_Master_Transmit(s_hi2c, LCD4_I2C_ADDR, &probe, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        ULOG_Error("LCD4", "Init", "I2C probe failed");
        return LCD4_ERROR;
    }

    ULOG_Info("LCD4", "Init", "OK");
    return LCD4_OK;
}

void LCD4_Clear(void)
{
    send_cmd(LCD4_CMD_CLEAR);
    HAL_Delay(2);
}

void LCD4_SetCursor(uint8_t row, uint8_t col)
{
    if (row > 3) row = 3;
    if (col > 19) col = 19;
    send_cmd(LCD4_CMD_DDRAM_ADDR | (ROW_OFFSETS[row] + col));
}

void LCD4_PrintChar(char c)
{
    send_data((uint8_t)c);
}

void LCD4_PrintString(const char *str)
{
    while (*str)
    {
        send_data((uint8_t)*str++);
    }
}

void LCD4_Backlight(uint8_t on)
{
    s_bl = on ? LCD4_BL : 0x00;
    write_byte(s_bl);
}

void LCD4_DefineCustomChar(uint8_t slot, uint8_t *bitmap)
{
    if (slot > 7) return;
    send_cmd(LCD4_CMD_CGRAM_ADDR | (slot << 3));
    for (uint8_t i = 0; i < 8; i++)
    {
        send_data(bitmap[i]);
    }
    /* Return to DDRAM mode so subsequent PrintChar calls go to the display */
    send_cmd(LCD4_CMD_DDRAM_ADDR);
}
