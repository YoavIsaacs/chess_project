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

/* ------------------------------------------------------------------ */
/* Module state                                                         */
/* ------------------------------------------------------------------ */

static I2C_HandleTypeDef *s_hi2c     = NULL;
static uint8_t            s_backlight = 0x08u;  /* P3 always set */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * Write one byte to the PCF8574 over I2C.
 * Logs the byte value and HAL result.
 * Returns HAL_OK or HAL_ERROR.
 */
static HAL_StatusTypeDef lcd2_i2c_write(uint8_t data)
{
    HAL_StatusTypeDef result = HAL_I2C_Master_Transmit(
        s_hi2c, LCD2_I2C_ADDR, &data, 1, HAL_MAX_DELAY);

    if (result != HAL_OK)
    {
        ULOG_InfoInt("LCD2", "I2C write FAILED, data=", data);
        ULOG_InfoInt("LCD2", "I2C error code", s_hi2c->ErrorCode);
    }

    return result;
}

/*
 * Send one 4-bit nibble to the LCD.
 * nibble: the nibble occupies bits [7:4] (high nibble position).
 * rs:     0 = command register, 1 = data register.
 *
 * EN pulse: write with EN high, delay, write with EN low, delay.
 */
static void lcd2_send_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t base = (nibble & 0xF0u) | s_backlight | (rs & 0x01u);

    ULOG_InfoInt("LCD2", "nibble EN-high byte=", base | 0x04u);
    lcd2_i2c_write(base | 0x04u);   /* EN high */
    HAL_Delay(1);

    ULOG_InfoInt("LCD2", "nibble EN-low  byte=", base & ~0x04u);
    lcd2_i2c_write(base & ~0x04u);  /* EN low  */
    HAL_Delay(1);
}

/*
 * Send a full byte to the LCD as two nibbles, high nibble first.
 * rs: 0 = command, 1 = data.
 */
static void lcd2_send_byte(uint8_t data, uint8_t rs)
{
    ULOG_InfoInt("LCD2", "send_byte data=", data);
    lcd2_send_nibble( data & 0xF0u,       rs);
    lcd2_send_nibble((data << 4) & 0xF0u, rs);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

LCD2_Status LCD2_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    ULOG_Info("LCD2", "Init start");

    HAL_Delay(50);  /* >40 ms power-on delay (HD44780 §4.4) */
    ULOG_Info("LCD2", "Power-on delay done");

    /*
     * Initialisation-by-instruction sequence (HD44780 datasheet §4.4).
     * The display may be in an unknown state at power-on; this sequence
     * forces it into a known 4-bit mode regardless of starting state.
     *
     * Steps 1–3: send 0x3 (8-bit function set) three times with the
     * required inter-command delays, then switch to 4-bit mode.
     */
    ULOG_Info("LCD2", "Init step 1: 0x30");
    lcd2_send_nibble(0x30u, 0);  HAL_Delay(5);

    ULOG_Info("LCD2", "Init step 2: 0x30");
    lcd2_send_nibble(0x30u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Init step 3: 0x30");
    lcd2_send_nibble(0x30u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Init step 4: switch to 4-bit (0x20)");
    lcd2_send_nibble(0x20u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Function set: 4-bit, 2 lines, 5x8");
    lcd2_send_byte(0x28u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Display off");
    lcd2_send_byte(0x08u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Clear display");
    lcd2_send_byte(0x01u, 0);  HAL_Delay(2);

    ULOG_Info("LCD2", "Entry mode set");
    lcd2_send_byte(0x06u, 0);  HAL_Delay(1);

    ULOG_Info("LCD2", "Display on");
    lcd2_send_byte(0x0Cu, 0);  HAL_Delay(1);

    /*
     * Probe the bus: send a no-op write to confirm the PCF8574 ACKs.
     * If I2C fails here the caller knows the display is unreachable.
     */
    ULOG_Info("LCD2", "Bus probe");
    if (lcd2_i2c_write(s_backlight) != HAL_OK)
    {
        ULOG_Error("LCD2", "Init FAILED: bus probe got no ACK");
        return LCD2_ERROR;
    }

    ULOG_Info("LCD2", "Init OK");
    return LCD2_OK;
}

void LCD2_Clear(void)
{
    ULOG_Info("LCD2", "Clear");
    lcd2_send_byte(0x01u, 0);
    HAL_Delay(2);  /* ≥1.52 ms */
}

void LCD2_SetCursor(uint8_t row, uint8_t col)
{
    /*
     * HD44780 DDRAM addresses:
     *   Row 0: 0x00–0x0F
     *   Row 1: 0x40–0x4F
     */
    uint8_t addr = (row == 0u ? 0x00u : 0x40u) + col;
    ULOG_InfoInt("LCD2", "SetCursor DDRAM addr=", addr);
    lcd2_send_byte(0x80u | addr, 0);
    HAL_Delay(1);
}

void LCD2_PrintString(const char *str)
{
    ULOG_Info("LCD2", "PrintString");
    for (int i = 0; str[i] != '\0'; i++)
    {
        lcd2_send_byte((uint8_t)str[i], 1);
    }
}

void LCD2_PrintChar(char c)
{
    ULOG_InfoInt("LCD2", "PrintChar=", (int)c);
    lcd2_send_byte((uint8_t)c, 1);
}

void LCD2_Backlight(uint8_t on)
{
    /*
     * No-op — backlight is hardwired on (s_backlight = 0x08).
     * The parameter is accepted to match the interface contract.
     * Suppress unused-parameter warning:
     */
    (void)on;
}
