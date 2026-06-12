/*
 * lcd_2x16.c — HD44780 2x16 LCD driver via PCF8574 I2C backpack.
 *
 * PCF8574 bit mapping (matches backpack silkscreen):
 *   P7 P6 P5 P4 = DB7 DB6 DB5 DB4   (high nibble of LCD data)
 *   P3          = backlight enable    (always 1 — see lcd_2x16.h)
 *   P2          = EN                  (pulse high then low to latch)
 *   P1          = RW                  (always 0, write-only)
 *   P0          = RS                  (0 = command, 1 = data)
 *
 * The HD44780 is driven in 4-bit mode. Every byte is sent as two
 * nibbles, high nibble first, each latched by an EN pulse.
 *
 * Timing:
 *   - EN pulse width must be ≥450 ns (HD44780 datasheet §4.4 tPW).
 *     HAL_Delay(1) is used — coarser than necessary but safe and
 *     avoids a spin-wait that would depend on clock speed.
 *   - The Clear command requires ≥1.52 ms; HAL_Delay(2) is used.
 *   - All other commands require ≥37 µs; HAL_Delay(1) covers this.
 *   - Power-on delay ≥40 ms: HAL_Delay(50) is used.
 *   - Init-by-instruction inter-step delays per HD44780 §4.4:
 *       first 0x30 repetition: ≥4.1 ms → HAL_Delay(5)
 *       second repetition:     ≥100 µs  → HAL_Delay(1)
 */

#include "../Inc/lcd_2x16.h"
#include "../Inc/uart_log.h"

static I2C_HandleTypeDef *s_hi2c     = NULL;
static uint8_t            s_backlight = 0x08u;

static HAL_StatusTypeDef lcd2_i2c_write(uint8_t data)
{
    return HAL_I2C_Master_Transmit(s_hi2c, LCD2_I2C_ADDR, &data, 1,
                                   HAL_MAX_DELAY);
}

static void lcd2_send_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t base = (nibble & 0xF0u) | s_backlight | (rs & 0x01u);
    lcd2_i2c_write(base | 0x04u);   /* EN high */
    HAL_Delay(1);
    lcd2_i2c_write(base & ~0x04u);  /* EN low  */
    HAL_Delay(1);
}

static void lcd2_send_byte(uint8_t data, uint8_t rs)
{
    lcd2_send_nibble( data & 0xF0u,       rs);
    lcd2_send_nibble((data << 4) & 0xF0u, rs);
}

LCD2_Status LCD2_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    HAL_Delay(50);

    lcd2_send_nibble(0x30u, 0);  HAL_Delay(5);
    lcd2_send_nibble(0x30u, 0);  HAL_Delay(1);
    lcd2_send_nibble(0x30u, 0);  HAL_Delay(1);
    lcd2_send_nibble(0x20u, 0);  HAL_Delay(1);

    lcd2_send_byte(0x28u, 0);  HAL_Delay(1);
    lcd2_send_byte(0x08u, 0);  HAL_Delay(1);
    lcd2_send_byte(0x01u, 0);  HAL_Delay(2);
    lcd2_send_byte(0x06u, 0);  HAL_Delay(1);
    lcd2_send_byte(0x0Cu, 0);  HAL_Delay(1);

    if (lcd2_i2c_write(s_backlight) != HAL_OK)
    {
        ULOG_Error("LCD2", "Init", "no ACK from PCF8574");
        return LCD2_ERROR;
    }

    ULOG_Info("LCD2", "Init", "OK");
    return LCD2_OK;
}

void LCD2_Clear(void)
{
    lcd2_send_byte(0x01u, 0);
    HAL_Delay(2);
}

void LCD2_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0u ? 0x00u : 0x40u) + col;
    lcd2_send_byte(0x80u | addr, 0);
    HAL_Delay(1);
}

void LCD2_PrintString(const char *str)
{
    ULOG_Info("LCD2", "PrintString", str);
    for (int i = 0; str[i] != '\0'; i++)
    {
        lcd2_send_byte((uint8_t)str[i], 1);
    }
}

void LCD2_PrintChar(char c)
{
    lcd2_send_byte((uint8_t)c, 1);
}

void LCD2_Backlight(uint8_t on)
{
    (void)on;
}
