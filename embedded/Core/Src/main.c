/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uart_log.h"
#include "dht.h"
#include "joystick.h"
#include "lcd_2x16.h"
#include "lcd_4x20.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
    PHASE_MENU           = 0,
    PHASE_RUNNING        = 1,
    PHASE_PAUSED         = 2,
    PHASE_RESULT_SELECT  = 3,
    PHASE_RESULT_CONFIRM = 4,
    PHASE_GAME_OVER      = 5
} App_Phase;

typedef enum
{
    PLAYER_WHITE = 0,
    PLAYER_BLACK = 1
} ActivePlayer;

typedef enum
{
    RESULT_WHITE_WINS    = 0,
    RESULT_BLACK_WINS    = 1,
    RESULT_DRAW          = 2,
    RESULT_WHITE_TIMEOUT = 3,
    RESULT_BLACK_TIMEOUT = 4
} GameResult;

typedef enum
{
    LCD2_MODE_A = 0,
    LCD2_MODE_B = 1,
    LCD2_MODE_C = 2
} LCD2_Mode;

typedef enum
{
    UART_PKT_NONE = 0,
    UART_PKT_GAME_START,
    UART_PKT_MOVE,
    UART_PKT_GAME_END
} UART_PacketType;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define BTN_DEBOUNCE_MS       50U
#define DHT_POLL_INTERVAL_MS  5000U
#define MIC_POLL_INTERVAL_MS  1000U

#define EVAL_BAR_LEN          12U
#define EVAL_BAR_HALF         6U
#define EVAL_CP_PER_CELL      100

#define UART_ACK_TIMEOUT_MS   200U
#define UART_MAX_RETRIES      3U
#define UART_TX_BUF_LEN       110U

/* Two-slot RX queue: holds one line while UART_Service is dispatching the
 * previous one.  Sized to absorb the back-to-back ACK + EVAL burst that
 * the Pi sends immediately after a MOVE packet. */
#define UART_RX_SLOTS         2U
#define UART_RX_LINE_LEN      64U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

static App_Phase s_phase = PHASE_MENU;

static volatile uint8_t  s_tick_flag    = 0;
static          uint32_t s_white_ms     = 0;
static          uint32_t s_black_ms     = 0;
static          uint32_t s_increment_ms = 0;
static          uint32_t s_last_tick_ms = 0;
static          ActivePlayer s_active   = PLAYER_WHITE;

static uint8_t s_eval_visible = 0;

static LCD2_Mode s_lcd2_mode = LCD2_MODE_A;

static uint32_t s_last_white_disp_s      = UINT32_MAX;
static uint32_t s_last_black_disp_s      = UINT32_MAX;
static uint32_t s_last_white_disp_s_lcd2 = UINT32_MAX;
static uint32_t s_last_black_disp_s_lcd2 = UINT32_MAX;

static uint32_t s_btn_white_last_ms = 0;
static uint32_t s_btn_black_last_ms = 0;
static uint8_t  s_btn_white_prev    = 1;
static uint8_t  s_btn_black_prev    = 1;

static uint32_t s_btn3_last_ms = 0;
static uint8_t  s_btn3_prev    = 1;

static uint32_t s_last_dht_ms = 0;
static int8_t   s_last_temp   = -128;
static int8_t   s_last_hum    = -128;

static int8_t   s_lcd2b_disp_temp = -128;
static int8_t   s_lcd2b_disp_hum  = -128;

static uint32_t s_last_mic_ms  = 0;
static uint8_t  s_last_bar_len = 0xFF;

static uint8_t  s_lcd2b_needs_redraw = 0;

static uint8_t  s_timeout_led_phase = 0;

static char s_white_first[MENU_NAME_MAX_LEN + 1U];
static char s_black_first[MENU_NAME_MAX_LEN + 1U];

static uint8_t    s_pause_cursor      = 0;
static uint8_t    s_result_cursor     = 0;
static GameResult s_result            = RESULT_WHITE_WINS;
static uint8_t    s_confirm_cursor    = 0;
static uint8_t    s_is_timeout_result = 0;
static uint32_t   s_game_start_ms     = 0;
static uint32_t   s_joy_click_last_ms = 0;
static uint8_t    s_joy_click_prev    = 1;
#define JOY_CLICK_DEBOUNCE_MS   80U

static uint8_t  s_game_started = 0;
static uint8_t  s_move_number  = 0;

static uint8_t  s_lcd4_row0_dirty = 1;
static uint8_t  s_lcd4_row1_dirty = 1;
static uint8_t  s_lcd4_row2_dirty = 1;

/* =========================================================================
 * Live eval state — written by UART_HandleLine on EVAL receipt.
 * LCD refresh functions read exclusively from these; no mock data remains.
 * Swapping the test Pi script for real Stockfish (Step 16) needs no STM32
 * changes — only the EVAL packet contents differ.
 * ========================================================================= */
static char    s_live_move[8]    = {0};
static int16_t s_live_eval_cp    = 0;
static char    s_live_quality[3] = "  ";
static uint8_t s_live_is_blunder = 0;
static uint8_t s_live_eval_valid = 0;

/* LCD2C display sentinels */
static int16_t  s_last_lcd2c_eval_cp     = INT16_MAX;
static uint8_t  s_last_lcd2c_eval_valid  = UINT8_MAX;
static uint8_t  s_last_lcd2c_move_number = UINT8_MAX;

/* =========================================================================
 * UART TX state machine
 * ========================================================================= */
static char            s_uart_tx_buf[UART_TX_BUF_LEN];
static uint8_t         s_uart_tx_len       = 0;
static UART_PacketType s_uart_pending_type = UART_PKT_NONE;
static uint8_t         s_uart_ack_pending  = 0;
static uint32_t        s_uart_ack_sent_ms  = 0;
static uint8_t         s_uart_retry_count  = 0;

/* =========================================================================
 * UART RX two-slot line queue
 *
 * The ISR fills slots[write_idx], advancing write_idx on each completed
 * line ('\n').  UART_Service drains slots[read_idx], advancing read_idx
 * after dispatch.  With two slots the back-to-back ACK + EVAL burst that
 * follows a MOVE packet is captured correctly even when the main loop is
 * blocked on I2C LCD writes between the send and the next service call.
 *
 * All fields are volatile: written by USART1_RxISR (ISR context) and read
 * by UART_Service (main loop context).
 * ========================================================================= */
static volatile char    s_uart_rx_data [UART_RX_SLOTS][UART_RX_LINE_LEN];
static volatile uint8_t s_uart_rx_ready[UART_RX_SLOTS];   /* per-slot flag */
static volatile uint8_t s_uart_rx_write_idx = 0;          /* ISR writes    */
static volatile uint8_t s_uart_rx_read_idx  = 0;          /* service reads */
static volatile uint8_t s_uart_rx_acc_idx   = 0;          /* byte position */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

void USART1_RxISR(void);

static void GAME_Start(const MENU_Settings *cfg);

static void CLOCK_FormatTime(uint32_t ms, char *buf, uint8_t buf_len);
static void CLOCK_DrawStaticRows(void);
static void CLOCK_RefreshDisplay(void);
static void CLOCK_HandleButtons(void);
static void CLOCK_HandleTick(void);

static void LCD4_BuildEvalBar(int16_t eval_cp, char *bar_out);
static void LCD4_RefreshMoveRow(void);
static void LCD4_RefreshEvalRow(void);
static void LCD4_RefreshLabelRow(void);

static void LCD2A_DrawStatic(void);
static void LCD2A_Refresh(void);
static void LCD2B_DrawStatic(void);
static void LCD2B_Refresh(void);
static void LCD2C_DrawStatic(void);
static void LCD2C_Refresh(void);
static void LCD2_EnterMode(LCD2_Mode mode);
static void LCD2_HandleButton3(void);

static void LED_SetState(void);
static void LED_SetPaused(void);
static void LED_SetGameOver(void);

static void PAUSE_Enter(void);
static void PAUSE_Resume(void);
static void PAUSE_DrawBox(void);
static void PAUSE_HandleInput(void);

static void RESULT_SELECT_Draw(void);
static void RESULT_SELECT_HandleInput(void);

static void RESULT_CONFIRM_Draw(void);
static void RESULT_CONFIRM_HandleInput(void);

static void TIMEOUT_Enter(void);
static void GAME_Over(void);

static void LCD2_DrawPauseOverlay(void);
static void LCD2_ExitPause(void);

static uint8_t MIC_ReadBarLevel(void);
static void    DHT_BackgroundPoll(void);

static const char *UART_PacketTypeName(UART_PacketType type);
static const char *UART_ResultToken(GameResult result);
static void        UART_SendPacket(const char *packet, UART_PacketType type);
static void        UART_SendGameStart(const MENU_Settings *cfg);
static void        UART_SendMove(char player);
static void        UART_SendGameEnd(GameResult result);
static void        UART_HandleLine(const char *line);
static void        UART_Service(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t bmp_tl[8]         = { 0b11111, 0b10000, 0b10000, 0b10000,
                                      0b10000, 0b10000, 0b10000, 0b00000 };
static uint8_t bmp_tr[8]         = { 0b11111, 0b00001, 0b00001, 0b00001,
                                      0b00001, 0b00001, 0b00001, 0b00000 };
static uint8_t bmp_bl[8]         = { 0b00000, 0b10000, 0b10000, 0b10000,
                                      0b10000, 0b10000, 0b10000, 0b11111 };
static uint8_t bmp_br[8]         = { 0b00000, 0b00001, 0b00001, 0b00001,
                                      0b00001, 0b00001, 0b00001, 0b11111 };
static uint8_t bmp_full_block[8] = { 0b11111, 0b11111, 0b11111, 0b11111,
                                      0b11111, 0b11111, 0b11111, 0b11111 };
static uint8_t bmp_understroke[8]= { 0b00000, 0b00000, 0b00000, 0b00000,
                                      0b00000, 0b00000, 0b00000, 0b11111 };

/* =========================================================================
 * USART1 RX ISR — two-slot line queue accumulator
 *
 * Called from USART1_IRQHandler in stm32f4xx_it.c on every received byte.
 * Reads SR then DR directly (RM0390 §30.3.5 ORE clear sequence).
 * ISR work is minimal: accumulate into the current write slot, set its
 * ready flag and advance write_idx on '\n', nothing else.
 *
 * If both slots are already ready (main loop has fallen two lines behind),
 * the incoming byte is dropped — protocol recovery falls to the ACK/retry
 * timeout at the application level.
 * ========================================================================= */
void USART1_RxISR(void)
{
    uint32_t sr = huart1.Instance->SR;

    if (sr & USART_SR_ORE)
    {
        /* Clear ORE: read SR (done above) then read DR. Reset accumulator. */
        (void)huart1.Instance->DR;
        s_uart_rx_acc_idx = 0;
        return;
    }

    if (!(sr & USART_SR_RXNE))
        return;

    char byte = (char)(huart1.Instance->DR & 0xFF);

    /* If the current write slot is still full, both slots are occupied —
     * drop the byte rather than overwriting unread data. */
    if (s_uart_rx_ready[s_uart_rx_write_idx])
        return;

    if (byte == '\n')
    {
        /* Complete the line: null-terminate, mark ready, advance write slot */
        uint8_t wi = s_uart_rx_write_idx;
        if (s_uart_rx_acc_idx < UART_RX_LINE_LEN)
            s_uart_rx_data[wi][s_uart_rx_acc_idx] = '\0';
        s_uart_rx_ready[wi] = 1;
        s_uart_rx_write_idx = (uint8_t)((wi + 1U) % UART_RX_SLOTS);
        s_uart_rx_acc_idx   = 0;
    }
    else if (s_uart_rx_acc_idx < (uint8_t)(UART_RX_LINE_LEN - 1U))
    {
        s_uart_rx_data[s_uart_rx_write_idx][s_uart_rx_acc_idx++] = byte;
    }
    else
    {
        /* Line too long — drop partial content and resync on next '\n' */
        s_uart_rx_acc_idx = 0;
    }
}

/* =========================================================================
 * Game / Clock
 * ========================================================================= */

static void GAME_Start(const MENU_Settings *cfg)
{
    s_white_ms     = cfg->time_per_side_ms;
    s_black_ms     = cfg->time_per_side_ms;
    s_increment_ms = cfg->increment_ms;
    s_eval_visible = cfg->eval_visible;
    s_active       = PLAYER_WHITE;

    strncpy(s_white_first, cfg->white_first, MENU_NAME_MAX_LEN);
    s_white_first[MENU_NAME_MAX_LEN] = '\0';
    strncpy(s_black_first, cfg->black_first, MENU_NAME_MAX_LEN);
    s_black_first[MENU_NAME_MAX_LEN] = '\0';

    s_pause_cursor      = 0;
    s_result_cursor     = 0;
    s_result            = RESULT_WHITE_WINS;
    s_confirm_cursor    = 0;
    s_is_timeout_result = 0;
    s_joy_click_last_ms = HAL_GetTick();
    s_game_start_ms     = HAL_GetTick();
    {
        JOY_Data joy = JOY_Read();
        s_joy_click_prev = joy.click;
    }

    s_uart_ack_pending  = 0;
    s_uart_pending_type = UART_PKT_NONE;
    s_uart_retry_count  = 0;

    s_last_white_disp_s      = UINT32_MAX;
    s_last_black_disp_s      = UINT32_MAX;
    s_last_white_disp_s_lcd2 = UINT32_MAX;
    s_last_black_disp_s_lcd2 = UINT32_MAX;
    s_last_bar_len            = 0xFF;
    s_last_temp               = -128;
    s_last_hum                = -128;
    s_lcd2b_disp_temp         = -128;
    s_lcd2b_disp_hum          = -128;
    s_timeout_led_phase       = 0;

    /* Reset live eval state */
    memset(s_live_move, 0, sizeof(s_live_move));
    s_live_eval_cp    = 0;
    s_live_quality[0] = ' ';
    s_live_quality[1] = ' ';
    s_live_quality[2] = '\0';
    s_live_is_blunder = 0;
    s_live_eval_valid = 0;

    /* Reset LCD2C sentinels */
    s_last_lcd2c_eval_cp     = INT16_MAX;
    s_last_lcd2c_eval_valid  = UINT8_MAX;
    s_last_lcd2c_move_number = UINT8_MAX;

    s_game_started = 0;
    s_move_number  = 0;

    s_lcd4_row0_dirty = 1;
    s_lcd4_row1_dirty = 1;
    s_lcd4_row2_dirty = 1;

    UART_SendGameStart(cfg);

    LCD4_Clear();
    CLOCK_DrawStaticRows();
    LCD4_RefreshMoveRow();
    LCD4_RefreshEvalRow();
    LCD4_RefreshLabelRow();
    CLOCK_RefreshDisplay();

    LCD2_EnterMode(LCD2_MODE_A);

    LED_SetState();

    s_phase = PHASE_RUNNING;

    char buf[64];
    snprintf(buf, sizeof(buf), "time=%lums inc=%lums eval=%d",
             cfg->time_per_side_ms, cfg->increment_ms,
             (int)cfg->eval_visible);
    ULOG_Info("MAIN", "GameStart", buf);
}

static void CLOCK_FormatTime(uint32_t ms, char *buf, uint8_t buf_len)
{
    uint32_t total_s = ms / 1000UL;
    uint32_t minutes = total_s / 60UL;
    uint32_t seconds = total_s % 60UL;
    snprintf(buf, buf_len, "%02lu:%02lu", minutes, seconds);
}

static void LCD4_BuildEvalBar(int16_t eval_cp, char *bar_out)
{
    char full  = (char)LCD4_CUSTOM_BLOCK;
    char under = (char)LCD4_CUSTOM_UNDERSTROKE;
    uint8_t i;

    for (i = 0; i < EVAL_BAR_LEN; i++)
        bar_out[i] = under;

    if (eval_cp > 0)
    {
        int16_t cells = (eval_cp + EVAL_CP_PER_CELL - 1) / EVAL_CP_PER_CELL;
        if (cells > (int16_t)(EVAL_BAR_HALF - 1U))
            cells = (int16_t)(EVAL_BAR_HALF - 1U);
        for (i = 0; i < (uint8_t)cells; i++)
            bar_out[EVAL_BAR_HALF + i] = full;
    }
    else if (eval_cp < 0)
    {
        int16_t cp_abs = (int16_t)(-eval_cp);
        int16_t cells  = (cp_abs + EVAL_CP_PER_CELL - 1) / EVAL_CP_PER_CELL;
        if (cells > (int16_t)(EVAL_BAR_HALF - 1U))
            cells = (int16_t)(EVAL_BAR_HALF - 1U);
        for (i = 0; i < (uint8_t)cells; i++)
            bar_out[(EVAL_BAR_HALF - 1U) - i] = full;
    }
}

/**
 * @brief  Refresh LCD4 row 0 (move notation + quality token).
 *
 *         Two-refresh model:
 *           Button press  → s_live_eval_valid cleared → blank row (1st refresh).
 *           EVAL arrives  → s_live_eval_valid set     → live data (2nd refresh).
 */
static void LCD4_RefreshMoveRow(void)
{
    if (!s_lcd4_row0_dirty)
        return;
    s_lcd4_row0_dirty = 0;

    char row[21];

    if (!s_live_eval_valid)
    {
        memset(row, ' ', 20);
        row[20] = '\0';
    }
    else if (s_eval_visible)
    {
        snprintf(row, sizeof(row), "%-18s%2s", s_live_move, s_live_quality);
    }
    else
    {
        snprintf(row, sizeof(row), "%-20s", s_live_move);
    }

    LCD4_SetCursor(0, 0);
    LCD4_PrintString(row);
    ULOG_Info("LCD4", "MoveRow", s_live_eval_valid ? s_live_move : "");
}

/**
 * @brief  Refresh LCD4 row 1 (eval bar + numerical eval).
 *         Blank until s_live_eval_valid is set by UART_HandleLine.
 */
static void LCD4_RefreshEvalRow(void)
{
    if (!s_lcd4_row1_dirty)
        return;
    s_lcd4_row1_dirty = 0;

    char row[21];

    if (!s_eval_visible || !s_live_eval_valid)
    {
        memset(row, ' ', 20);
        row[20] = '\0';
    }
    else
    {
        char bar[EVAL_BAR_LEN];
        LCD4_BuildEvalBar(s_live_eval_cp, bar);

        int16_t abs_cp = (s_live_eval_cp < 0) ? (int16_t)(-s_live_eval_cp) : s_live_eval_cp;
        int16_t whole  = abs_cp / 100;
        int16_t tenth  = (abs_cp % 100) / 10;
        char    sign   = (s_live_eval_cp >= 0) ? '+' : '-';
        char    eval_str[7];
        snprintf(eval_str, sizeof(eval_str), "%c%d.%d", sign, (int)whole, (int)tenth);

        row[0] = '[';
        uint8_t i;
        for (i = 0; i < EVAL_BAR_LEN; i++)
            row[1U + i] = bar[i];
        row[13] = ']';
        snprintf(&row[14], 7, "%6s", eval_str);
        row[20] = '\0';
    }

    LCD4_SetCursor(1, 0);
    LCD4_PrintString(row);
    ULOG_Info("LCD4", "EvalRow", row);
}

static void LCD4_RefreshLabelRow(void)
{
    if (!s_lcd4_row2_dirty)
        return;
    s_lcd4_row2_dirty = 0;

    char row[21];

    if (!s_game_started)
    {
        snprintf(row, sizeof(row), "%-20s", "Black          White");
    }
    else
    {
        uint8_t disp_move = (s_move_number > 99) ? 99 : s_move_number;
        char centre[11];
        snprintf(centre, sizeof(centre), "   #%02u    ", (unsigned)disp_move);
        snprintf(row, sizeof(row), "Black%sWhite", centre);
    }

    LCD4_SetCursor(2, 0);
    LCD4_PrintString(row);
    ULOG_Info("LCD4", "LabelRow", row);
}

static void CLOCK_DrawStaticRows(void)
{
    (void)0;
}

static void CLOCK_RefreshDisplay(void)
{
    char buf[6];
    uint32_t white_s = s_white_ms / 1000UL;
    uint32_t black_s = s_black_ms / 1000UL;

    if (black_s != s_last_black_disp_s)
    {
        CLOCK_FormatTime(s_black_ms, buf, sizeof(buf));
        LCD4_SetCursor(3, 0);
        LCD4_PrintString(buf);
        s_last_black_disp_s = black_s;
    }

    if (white_s != s_last_white_disp_s)
    {
        CLOCK_FormatTime(s_white_ms, buf, sizeof(buf));
        LCD4_SetCursor(3, 15);
        LCD4_PrintString(buf);
        s_last_white_disp_s = white_s;
    }
}

/**
 * @brief  Poll clock buttons.
 *
 *         On a valid press:
 *           - Switch active player and apply increment.
 *           - Invalidate live eval (s_live_eval_valid = 0): rows 0 and 1
 *             immediately blank on the first refresh.
 *           - Mark all three LCD4 rows dirty.
 *           - Send MOVE packet; Pi replies ACK then EVAL.  UART_HandleLine
 *             sets s_live_eval_valid = 1 and re-dirtys rows 0 and 1 for
 *             the second refresh.
 */
static void CLOCK_HandleButtons(void)
{
    uint32_t now = HAL_GetTick();

    uint8_t btn_white = HAL_GPIO_ReadPin(WHITE_CLOCK_GPIO_Port, WHITE_CLOCK_Pin);
    uint8_t btn_black = HAL_GPIO_ReadPin(BLACK_CLOCK_GPIO_Port, BLACK_CLOCK_Pin);

    if (s_btn_white_prev == 1 && btn_white == 0 &&
        (now - s_btn_white_last_ms) >= BTN_DEBOUNCE_MS)
    {
        s_btn_white_last_ms = now;

        if (s_active == PLAYER_WHITE)
        {
            s_white_ms += s_increment_ms;
            s_active    = PLAYER_BLACK;
            ULOG_Info("CLOCK", "Btn", "White pressed -> Black active");

            s_game_started    = 1;
            s_move_number++;
            s_live_eval_valid = 0;
            s_lcd4_row0_dirty = 1;
            s_lcd4_row1_dirty = 1;
            s_lcd4_row2_dirty = 1;

            LED_SetState();
            UART_SendMove('W');
        }
    }
    s_btn_white_prev = btn_white;

    if (s_btn_black_prev == 1 && btn_black == 0 &&
        (now - s_btn_black_last_ms) >= BTN_DEBOUNCE_MS)
    {
        s_btn_black_last_ms = now;

        if (s_active == PLAYER_BLACK)
        {
            s_black_ms += s_increment_ms;
            s_active    = PLAYER_WHITE;
            ULOG_Info("CLOCK", "Btn", "Black pressed -> White active");

            s_game_started    = 1;
            s_live_eval_valid = 0;
            s_lcd4_row0_dirty = 1;
            s_lcd4_row1_dirty = 1;
            s_lcd4_row2_dirty = 1;

            LED_SetState();
            UART_SendMove('B');
        }
    }
    s_btn_black_prev = btn_black;

    {
        JOY_Data joy     = JOY_Read();
        uint32_t now_joy = HAL_GetTick();
        if (s_joy_click_prev == 1 && joy.click == 0 &&
            (now_joy - s_joy_click_last_ms) >= JOY_CLICK_DEBOUNCE_MS &&
            (now_joy - s_game_start_ms)      >= 2000U)
        {
            s_joy_click_last_ms = now_joy;
            PAUSE_Enter();
        }
        s_joy_click_prev = joy.click;
    }
}

static void CLOCK_HandleTick(void)
{
    s_tick_flag = 0;

    if (s_active == PLAYER_WHITE)
    {
        if (s_white_ms >= 1000UL) s_white_ms -= 1000UL;
        else                      s_white_ms  = 0;
    }
    else
    {
        if (s_black_ms >= 1000UL) s_black_ms -= 1000UL;
        else                      s_black_ms  = 0;
    }

    s_timeout_led_phase ^= 1;
    LED_SetState();

    if (s_active == PLAYER_WHITE && s_white_ms == 0)
        TIMEOUT_Enter();
    else if (s_active == PLAYER_BLACK && s_black_ms == 0)
        TIMEOUT_Enter();
}

static void LED_SetState(void)
{
    uint8_t white_timeout = (s_white_ms == 0) ? 1 : 0;
    uint8_t black_timeout = (s_black_ms == 0) ? 1 : 0;

    HAL_GPIO_WritePin(GPIOB, WHITE_TIMEOUT_Pin,
        (white_timeout && s_timeout_led_phase) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, BLACK_TIMEOUT_Pin,
        (black_timeout && s_timeout_led_phase) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, WHITE_TURN_Pin,
        (s_active == PLAYER_WHITE && !white_timeout) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin,
        (s_active == PLAYER_BLACK && !black_timeout) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    ULOG_Info("LED", "Set", (s_active == PLAYER_WHITE) ? "White turn" : "Black turn");
}

static void LED_SetPaused(void)
{
    GPIO_PinState state = s_timeout_led_phase ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(GPIOB, WHITE_TURN_Pin,    state);
    HAL_GPIO_WritePin(GPIOB, WHITE_TIMEOUT_Pin, state);
    HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin,    state);
    HAL_GPIO_WritePin(GPIOB, BLACK_TIMEOUT_Pin, state);
}

static void LED_SetGameOver(void)
{
    GPIO_PinState on  = s_timeout_led_phase ? GPIO_PIN_SET : GPIO_PIN_RESET;
    GPIO_PinState off = GPIO_PIN_RESET;

    switch (s_result)
    {
        case RESULT_WHITE_WINS:
        case RESULT_BLACK_TIMEOUT:
            HAL_GPIO_WritePin(GPIOB, WHITE_TURN_Pin,    on);
            HAL_GPIO_WritePin(GPIOB, WHITE_TIMEOUT_Pin, on);
            HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin,    off);
            HAL_GPIO_WritePin(GPIOB, BLACK_TIMEOUT_Pin, off);
            break;
        case RESULT_BLACK_WINS:
        case RESULT_WHITE_TIMEOUT:
            HAL_GPIO_WritePin(GPIOB, WHITE_TURN_Pin,    off);
            HAL_GPIO_WritePin(GPIOB, WHITE_TIMEOUT_Pin, off);
            HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin,    on);
            HAL_GPIO_WritePin(GPIOB, BLACK_TIMEOUT_Pin, on);
            break;
        case RESULT_DRAW:
        default:
            HAL_GPIO_WritePin(GPIOB, WHITE_TURN_Pin,    on);
            HAL_GPIO_WritePin(GPIOB, WHITE_TIMEOUT_Pin, off);
            HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin,    on);
            HAL_GPIO_WritePin(GPIOB, BLACK_TIMEOUT_Pin, off);
            break;
    }
}

static void LCD2A_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("Black      White");
    ULOG_Info("LCD2", "LCD2A_DrawStatic", "Mode A static rows drawn");
}

static void LCD2A_Refresh(void)
{
    char buf[6];
    uint32_t white_s = s_white_ms / 1000UL;
    uint32_t black_s = s_black_ms / 1000UL;

    if (black_s != s_last_black_disp_s_lcd2)
    {
        CLOCK_FormatTime(s_black_ms, buf, sizeof(buf));
        LCD2_SetCursor(1, 0);
        LCD2_PrintString(buf);
        s_last_black_disp_s_lcd2 = black_s;
    }

    if (white_s != s_last_white_disp_s_lcd2)
    {
        CLOCK_FormatTime(s_white_ms, buf, sizeof(buf));
        LCD2_SetCursor(1, 11);
        LCD2_PrintString(buf);
        s_last_white_disp_s_lcd2 = white_s;
    }
}

static void LCD2B_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("Noise:          ");
    ULOG_Info("LCD2", "LCD2B_DrawStatic", "Mode B static rows drawn");
}

static void LCD2B_Refresh(void)
{
    uint32_t now = HAL_GetTick();

    if (s_lcd2b_needs_redraw ||
        s_last_temp != s_lcd2b_disp_temp ||
        s_last_hum  != s_lcd2b_disp_hum)
    {
        if (s_last_temp != -128 && s_last_hum != -128)
        {
            char buf[17];
            snprintf(buf, sizeof(buf), "T:%2dC  H:%2d%%    ", s_last_temp, s_last_hum);
            LCD2_SetCursor(0, 0);
            LCD2_PrintString(buf);
            ULOG_Info("LCD2", "LCD2B_Refresh", "DHT display updated");
        }
        s_lcd2b_disp_temp    = s_last_temp;
        s_lcd2b_disp_hum     = s_last_hum;
        s_lcd2b_needs_redraw = 0;
    }

    if ((now - s_last_mic_ms) >= MIC_POLL_INTERVAL_MS)
    {
        s_last_mic_ms = now;
        uint8_t bar_len = MIC_ReadBarLevel();

        if (bar_len != s_last_bar_len)
        {
            char bar[10];
            uint8_t i;
            for (i = 0;  i < bar_len; i++) bar[i] = (char)0xFF;
            for (      ; i < 9;       i++) bar[i] = ' ';
            bar[9] = '\0';

            LCD2_SetCursor(1, 0);
            LCD2_PrintString("Noise: ");
            LCD2_PrintString(bar);
            s_last_bar_len = bar_len;

            char log_buf[24];
            snprintf(log_buf, sizeof(log_buf), "bar=%u", bar_len);
            ULOG_Info("MIC", "Read", log_buf);
        }
    }
}

static void LCD2C_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("Move:---- Ev----");
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("                ");
    ULOG_Info("LCD2", "LCD2C_DrawStatic", "Mode C static rows drawn");
}

/**
 * @brief  Refresh LCD2 Mode C from live Pi data.
 *
 *         Gated by three sentinels: eval_valid, eval_cp, move_number.
 *         Handles all transitions correctly:
 *           - Button press: move_number changes → redraw with "----" eval.
 *           - EVAL arrives: eval_valid changes → redraw with live eval.
 *           - Same eval_cp on consecutive moves: move_number still differs.
 */
static void LCD2C_Refresh(void)
{
    if (!s_game_started)
        return;

    if (s_live_eval_valid  == s_last_lcd2c_eval_valid  &&
        s_live_eval_cp     == s_last_lcd2c_eval_cp     &&
        s_move_number      == s_last_lcd2c_move_number)
        return;

    s_last_lcd2c_eval_valid  = s_live_eval_valid;
    s_last_lcd2c_eval_cp     = s_live_eval_cp;
    s_last_lcd2c_move_number = s_move_number;

    char row0[17];
    uint8_t disp_move = (s_move_number > 99u) ? 99u : s_move_number;

    if (!s_live_eval_valid)
    {
        snprintf(row0, sizeof(row0), "Move:#%02u  ------", (unsigned)disp_move);
    }
    else if (s_eval_visible)
    {
        int16_t abs_cp = (s_live_eval_cp < 0) ? (int16_t)(-s_live_eval_cp) : s_live_eval_cp;
        int16_t whole  = abs_cp / 100;
        int16_t tenth  = (abs_cp % 100) / 10;
        char    sign   = (s_live_eval_cp >= 0) ? '+' : '-';
        char    eval_str[6];
        snprintf(eval_str, sizeof(eval_str), "%c%d.%d", sign, (int)whole, (int)tenth);
        snprintf(row0, sizeof(row0), "Move:#%02u Ev%-5s", (unsigned)disp_move, eval_str);
    }
    else
    {
        snprintf(row0, sizeof(row0), "Move: #%02u       ", (unsigned)disp_move);
    }

    LCD2_SetCursor(0, 0);
    LCD2_PrintString(row0);
    ULOG_Info("LCD2", "LCD2C_Refresh", row0);
}

static void LCD2_DrawPauseOverlay(void)
{
    if (s_lcd2_mode == LCD2_MODE_B)
        return;

    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("                ");
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("    PAUSED      ");
    ULOG_Info("LCD2", "DrawPauseOverlay", "Pause overlay drawn");
}

static void LCD2_ExitPause(void)
{
    LCD2_EnterMode(s_lcd2_mode);
    ULOG_Info("LCD2", "ExitPause", "Restored LCD2 mode content");
}

static void LCD2_EnterMode(LCD2_Mode mode)
{
    s_lcd2_mode = mode;

    switch (mode)
    {
        case LCD2_MODE_A:
            s_last_white_disp_s_lcd2 = UINT32_MAX;
            s_last_black_disp_s_lcd2 = UINT32_MAX;
            LCD2A_DrawStatic();
            LCD2A_Refresh();
            ULOG_Info("LCD2", "EnterMode", "A");
            break;
        case LCD2_MODE_B:
            s_last_bar_len       = 0xFF;
            s_lcd2b_needs_redraw = 1;
            LCD2B_DrawStatic();
            LCD2B_Refresh();
            ULOG_Info("LCD2", "EnterMode", "B");
            break;
        case LCD2_MODE_C:
            s_last_lcd2c_eval_cp     = INT16_MAX;
            s_last_lcd2c_eval_valid  = UINT8_MAX;
            s_last_lcd2c_move_number = UINT8_MAX;
            LCD2C_DrawStatic();
            LCD2C_Refresh();
            ULOG_Info("LCD2", "EnterMode", "C");
            break;
        default:
            break;
    }
}

static void LCD2_HandleButton3(void)
{
    uint32_t now  = HAL_GetTick();
    uint8_t  btn3 = HAL_GPIO_ReadPin(LCD_MODE_SWITCH_GPIO_Port, LCD_MODE_SWITCH_Pin);

    if (s_btn3_prev == 1 && btn3 == 0 &&
        (now - s_btn3_last_ms) >= BTN_DEBOUNCE_MS)
    {
        s_btn3_last_ms = now;

        LCD2_Mode next;
        switch (s_lcd2_mode)
        {
            case LCD2_MODE_A: next = LCD2_MODE_B; break;
            case LCD2_MODE_B: next = LCD2_MODE_C; break;
            case LCD2_MODE_C: next = LCD2_MODE_A; break;
            default:          next = LCD2_MODE_A; break;
        }

        LCD2_EnterMode(next);

        if (s_phase == PHASE_PAUSED)
            LCD2_DrawPauseOverlay();
    }

    s_btn3_prev = btn3;
}

/* =========================================================================
 * Pause / Result / Timeout / Game Over
 * ========================================================================= */

static void PAUSE_DrawBox(void)
{
    uint8_t i;

    LCD4_SetCursor(0, 0);
    LCD4_PrintChar((char)LCD4_CUSTOM_CORNER_TL);
    for (i = 0; i < 18; i++) LCD4_PrintChar('-');
    LCD4_PrintChar((char)LCD4_CUSTOM_CORNER_TR);

    LCD4_SetCursor(1, 0);
    LCD4_PrintChar('|');
    for (i = 0; i < 18; i++) LCD4_PrintChar(' ');
    LCD4_PrintChar('|');

    LCD4_SetCursor(2, 0);
    LCD4_PrintChar('|');
    if (s_pause_cursor == 0)
        LCD4_PrintString("  >RESUME      END");
    else
        LCD4_PrintString("   RESUME     >END");
    LCD4_PrintChar('|');

    LCD4_SetCursor(3, 0);
    LCD4_PrintChar((char)LCD4_CUSTOM_CORNER_BL);
    for (i = 0; i < 18; i++) LCD4_PrintChar('-');
    LCD4_PrintChar((char)LCD4_CUSTOM_CORNER_BR);

    ULOG_Info("LCD4", "PauseBox", (s_pause_cursor == 0) ? "cursor=RESUME" : "cursor=END");
}

static void PAUSE_Enter(void)
{
    s_pause_cursor      = 0;
    s_joy_click_last_ms = HAL_GetTick();
    s_joy_click_prev    = 1;
    s_timeout_led_phase = 1;
    LED_SetPaused();
    LCD4_Clear();
    PAUSE_DrawBox();
    LCD2_DrawPauseOverlay();
    s_phase = PHASE_PAUSED;
    ULOG_Info("MAIN", "Pause", "Entered");
}

static void PAUSE_Resume(void)
{
    LCD4_Clear();
    s_lcd4_row0_dirty = 1;
    s_lcd4_row1_dirty = 1;
    s_lcd4_row2_dirty = 1;
    s_last_white_disp_s = UINT32_MAX;
    s_last_black_disp_s = UINT32_MAX;
    LCD4_RefreshMoveRow();
    LCD4_RefreshEvalRow();
    LCD4_RefreshLabelRow();
    CLOCK_RefreshDisplay();
    LCD2_ExitPause();
    s_timeout_led_phase = 0;
    LED_SetState();
    s_phase = PHASE_RUNNING;
    ULOG_Info("MAIN", "Pause", "Resumed");
}

static void PAUSE_HandleInput(void)
{
    JOY_Data joy = JOY_Read();
    uint32_t now = HAL_GetTick();

    static JOY_Direction s_prev_dir_pause = JOY_CENTRE;

    if (joy.direction != s_prev_dir_pause)
    {
        s_prev_dir_pause = joy.direction;

        if (joy.direction == JOY_LEFT && s_pause_cursor != 0)
        {
            s_pause_cursor = 0;
            PAUSE_DrawBox();
        }
        else if (joy.direction == JOY_RIGHT && s_pause_cursor != 1)
        {
            s_pause_cursor = 1;
            PAUSE_DrawBox();
        }
    }

    if (s_joy_click_prev == 1 && joy.click == 0 &&
        (now - s_joy_click_last_ms) >= JOY_CLICK_DEBOUNCE_MS)
    {
        s_joy_click_last_ms = now;

        if (s_pause_cursor == 0)
        {
            PAUSE_Resume();
        }
        else
        {
            s_is_timeout_result = 0;
            s_result_cursor     = 0;
            s_joy_click_last_ms = HAL_GetTick();
            s_joy_click_prev    = 1;
            RESULT_SELECT_Draw();
            s_phase = PHASE_RESULT_SELECT;
            ULOG_Info("MAIN", "Pause", "End selected -> ResultSelect");
        }
    }

    s_joy_click_prev = joy.click;
    LCD2_HandleButton3();
}

static void RESULT_SELECT_Draw(void)
{
    LCD4_Clear();
    LCD4_SetCursor(0, 0);
    LCD4_PrintString("SELECT RESULT       ");

    const char *options[3] = { "White wins", "Black wins", "Draw" };
    uint8_t i;
    for (i = 0; i < 3; i++)
    {
        char row[21];
        snprintf(row, sizeof(row), "%s%-18s",
                 (s_result_cursor == i) ? ">" : " ",
                 options[i]);
        LCD4_SetCursor((uint8_t)(1 + i), 0);
        LCD4_PrintString(row);
    }

    ULOG_Info("LCD4", "ResultSelect", options[s_result_cursor]);
}

static void RESULT_SELECT_HandleInput(void)
{
    JOY_Data joy = JOY_Read();
    uint32_t now = HAL_GetTick();
    uint8_t  changed = 0;

    static JOY_Direction s_prev_dir_sel = JOY_CENTRE;

    if (joy.direction != s_prev_dir_sel)
    {
        s_prev_dir_sel = joy.direction;

        if (joy.direction == JOY_UP && s_result_cursor > 0)
        {
            s_result_cursor--;
            changed = 1;
        }
        else if (joy.direction == JOY_DOWN && s_result_cursor < 2)
        {
            s_result_cursor++;
            changed = 1;
        }
    }

    if (changed)
        RESULT_SELECT_Draw();

    if (s_joy_click_prev == 1 && joy.click == 0 &&
        (now - s_joy_click_last_ms) >= JOY_CLICK_DEBOUNCE_MS)
    {
        s_joy_click_last_ms = now;
        s_prev_dir_sel = JOY_CENTRE;
        s_result = (GameResult)s_result_cursor;
        s_confirm_cursor = 0;
        RESULT_CONFIRM_Draw();
        s_phase = PHASE_RESULT_CONFIRM;
        ULOG_Info("MAIN", "ResultSelect", "Confirmed -> ResultConfirm");
    }

    s_joy_click_prev = joy.click;
}

static void RESULT_CONFIRM_Draw(void)
{
    LCD4_Clear();

    LCD4_SetCursor(0, 0);
    if (s_is_timeout_result)
        LCD4_PrintString("TIMEOUT             ");
    else
        LCD4_PrintString("CONFIRM RESULT      ");

    {
        const char *result_str;
        switch (s_result)
        {
            case RESULT_WHITE_WINS:    result_str = "White wins          "; break;
            case RESULT_BLACK_WINS:    result_str = "Black wins          "; break;
            case RESULT_DRAW:          result_str = "Draw                "; break;
            case RESULT_WHITE_TIMEOUT: result_str = "White wins on time  "; break;
            case RESULT_BLACK_TIMEOUT: result_str = "Black wins on time  "; break;
            default:                   result_str = "                    "; break;
        }
        LCD4_SetCursor(1, 0);
        LCD4_PrintString(result_str);
    }

    {
        char row[21];
        snprintf(row, sizeof(row), "%-8s vs %-8s", s_white_first, s_black_first);
        LCD4_SetCursor(2, 0);
        LCD4_PrintString(row);
    }

    {
        char row[21];
        if (s_confirm_cursor == 0)
            snprintf(row, sizeof(row), "OK?  >Yes      No   ");
        else
            snprintf(row, sizeof(row), "OK?   Yes     >No   ");
        LCD4_SetCursor(3, 0);
        LCD4_PrintString(row);
    }

    ULOG_Info("LCD4", "ResultConfirm",
              (s_confirm_cursor == 0) ? "cursor=Yes" : "cursor=No");
}

static void RESULT_CONFIRM_HandleInput(void)
{
    JOY_Data joy = JOY_Read();
    uint32_t now = HAL_GetTick();
    uint8_t  changed = 0;

    static JOY_Direction s_prev_dir_conf = JOY_CENTRE;

    if (joy.direction != s_prev_dir_conf)
    {
        s_prev_dir_conf = joy.direction;

        if (joy.direction == JOY_LEFT && s_confirm_cursor != 0)
        {
            s_confirm_cursor = 0;
            changed = 1;
        }
        else if (joy.direction == JOY_RIGHT && s_confirm_cursor != 1)
        {
            s_confirm_cursor = 1;
            changed = 1;
        }
    }

    if (changed)
        RESULT_CONFIRM_Draw();

    if (s_joy_click_prev == 1 && joy.click == 0 &&
        (now - s_joy_click_last_ms) >= JOY_CLICK_DEBOUNCE_MS)
    {
        s_joy_click_last_ms = now;
        s_prev_dir_conf = JOY_CENTRE;

        if (s_confirm_cursor == 0)
        {
            GAME_Over();
        }
        else
        {
            if (s_is_timeout_result)
            {
                LCD4_Clear();
                PAUSE_DrawBox();
                LCD2_DrawPauseOverlay();
                s_phase = PHASE_PAUSED;
                ULOG_Info("MAIN", "ResultConfirm", "No (timeout) -> Paused");
            }
            else
            {
                s_result_cursor = (uint8_t)s_result;
                RESULT_SELECT_Draw();
                s_phase = PHASE_RESULT_SELECT;
                ULOG_Info("MAIN", "ResultConfirm", "No (manual) -> ResultSelect");
            }
        }
    }

    s_joy_click_prev = joy.click;
}

static void TIMEOUT_Enter(void)
{
    if (s_active == PLAYER_WHITE)
    {
        s_result = RESULT_BLACK_TIMEOUT;
        ULOG_Info("MAIN", "Timeout", "White");
    }
    else
    {
        s_result = RESULT_WHITE_TIMEOUT;
        ULOG_Info("MAIN", "Timeout", "Black");
    }

    s_is_timeout_result = 1;
    s_confirm_cursor    = 0;
    s_joy_click_last_ms = HAL_GetTick();
    s_joy_click_prev    = 1;
    RESULT_CONFIRM_Draw();
    s_phase = PHASE_RESULT_CONFIRM;
}

static void GAME_Over(void)
{
    UART_SendGameEnd(s_result);

    HAL_GPIO_WritePin(GPIOB,
        WHITE_TURN_Pin | WHITE_TIMEOUT_Pin |
        BLACK_TURN_Pin | BLACK_TIMEOUT_Pin,
        GPIO_PIN_RESET);

    s_timeout_led_phase = 1;

    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("   GAME OVER    ");

    {
        const char *result_str;
        switch (s_result)
        {
            case RESULT_WHITE_WINS:
            case RESULT_WHITE_TIMEOUT: result_str = " White wins     "; break;
            case RESULT_BLACK_WINS:
            case RESULT_BLACK_TIMEOUT: result_str = " Black wins     "; break;
            case RESULT_DRAW:          result_str = "     Draw       "; break;
            default:                   result_str = "                "; break;
        }
        LCD2_SetCursor(1, 0);
        LCD2_PrintString(result_str);
    }

    LCD4_Clear();
    LCD4_SetCursor(0, 0);
    LCD4_PrintString("    GAME OVER       ");
    LCD4_SetCursor(1, 0);
    {
        const char *result_str;
        switch (s_result)
        {
            case RESULT_WHITE_WINS:
            case RESULT_WHITE_TIMEOUT: result_str = "    White wins      "; break;
            case RESULT_BLACK_WINS:
            case RESULT_BLACK_TIMEOUT: result_str = "    Black wins      "; break;
            case RESULT_DRAW:          result_str = "       Draw         "; break;
            default:                   result_str = "                    "; break;
        }
        LCD4_PrintString(result_str);
    }
    LCD4_SetCursor(2, 0);
    LCD4_PrintString("                    ");
    LCD4_SetCursor(3, 0);
    LCD4_PrintString("                    ");

    s_phase = PHASE_GAME_OVER;
    ULOG_Info("MAIN", "GameOver", "Terminal state entered");
}

/* =========================================================================
 * Sensor helpers
 * ========================================================================= */

static uint8_t MIC_ReadBarLevel(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = ADC_CHANNEL_15;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t mic_val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return (uint8_t)((mic_val * 9UL) / 4095UL);
}

static void DHT_BackgroundPoll(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - s_last_dht_ms) >= DHT_POLL_INTERVAL_MS)
    {
        s_last_dht_ms = now;
        DHT_Data d = DHT_Read();

        if (d.status == DHT_OK)
        {
            s_last_temp = (int8_t)d.temperature_c;
            s_last_hum  = (int8_t)d.humidity_pct;
        }
        else
        {
            ULOG_Error("DHT", "BackgroundPoll", "read failed");
        }
    }
}

/* =========================================================================
 * UART to Pi — TX builders + ACK/retry + RX dispatch
 * ========================================================================= */

static const char *UART_PacketTypeName(UART_PacketType type)
{
    switch (type)
    {
        case UART_PKT_GAME_START: return "GAME_START";
        case UART_PKT_MOVE:       return "MOVE";
        case UART_PKT_GAME_END:   return "GAME_END";
        default:                  return "NONE";
    }
}

static const char *UART_ResultToken(GameResult result)
{
    switch (result)
    {
        case RESULT_WHITE_WINS:    return "1-0";
        case RESULT_BLACK_WINS:    return "0-1";
        case RESULT_DRAW:          return "1/2";
        case RESULT_WHITE_TIMEOUT: return "1-0T";
        case RESULT_BLACK_TIMEOUT: return "0-1T";
        default:                   return "?";
    }
}

static void UART_SendPacket(const char *packet, UART_PacketType type)
{
    size_t len = strlen(packet);
    if (len >= UART_TX_BUF_LEN)
        len = UART_TX_BUF_LEN - 1U;

    if (s_uart_ack_pending)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "overwriting still-pending ACK for %s",
                 UART_PacketTypeName(s_uart_pending_type));
        ULOG_Error("UART", "Send", buf);
    }

    memcpy(s_uart_tx_buf, packet, len);
    s_uart_tx_buf[len] = '\0';
    s_uart_tx_len      = (uint8_t)len;

    HAL_UART_Transmit(&huart1, (uint8_t *)s_uart_tx_buf, s_uart_tx_len, 100);

    s_uart_pending_type = type;
    s_uart_ack_pending  = 1;
    s_uart_ack_sent_ms  = HAL_GetTick();
    s_uart_retry_count  = 0;

    ULOG_Info("UART", "Send", UART_PacketTypeName(type));
}

static void UART_SendGameStart(const MENU_Settings *cfg)
{
    char packet[UART_TX_BUF_LEN];
    snprintf(packet, sizeof(packet), "GAME_START,%s,%s,%s,%s,%lu,%lu,%d\n",
             cfg->white_first, cfg->white_last,
             cfg->black_first, cfg->black_last,
             cfg->time_per_side_ms, cfg->increment_ms,
             (int)cfg->eval_visible);
    UART_SendPacket(packet, UART_PKT_GAME_START);
}

static void UART_SendMove(char player)
{
    uint8_t mic_bar = MIC_ReadBarLevel();
    char packet[64];
    snprintf(packet, sizeof(packet), "MOVE,%u,%c,%lu,%lu,%u,%d,%d\n",
             (unsigned)s_move_number, player,
             s_white_ms, s_black_ms,
             (unsigned)mic_bar,
             (int)s_last_temp, (int)s_last_hum);
    UART_SendPacket(packet, UART_PKT_MOVE);
}

static void UART_SendGameEnd(GameResult result)
{
    char packet[24];
    snprintf(packet, sizeof(packet), "GAME_END,%s\n", UART_ResultToken(result));
    UART_SendPacket(packet, UART_PKT_GAME_END);
}

/**
 * @brief  Parse one complete received line and act on it.
 *
 *         ACK  — clears the pending retry state.
 *         EVAL — parses all four fields and writes to the live state
 *                variables, then marks LCD4 rows 0 and 1 dirty for the
 *                second refresh.
 *
 *         EVAL wire format: EVAL,<move>,<+/-eval_cp>,<quality>,<is_blunder>\n
 *         e.g. "EVAL,Nf3,+35, !,0"
 *
 *         Quality is exactly 2 chars and may contain spaces (" !", "  ").
 *         Fields are split by manual comma search rather than sscanf %s to
 *         preserve embedded spaces in the quality token.
 */
static void UART_HandleLine(const char *line)
{
    if (strncmp(line, "ACK,", 4) == 0)
    {
        const char *type_str = &line[4];
        UART_PacketType acked = UART_PKT_NONE;

        if      (strcmp(type_str, "GAME_START") == 0) acked = UART_PKT_GAME_START;
        else if (strcmp(type_str, "MOVE")       == 0) acked = UART_PKT_MOVE;
        else if (strcmp(type_str, "GAME_END")   == 0) acked = UART_PKT_GAME_END;

        if (s_uart_ack_pending && acked != UART_PKT_NONE && acked == s_uart_pending_type)
        {
            s_uart_ack_pending = 0;
            ULOG_Info("UART", "Ack", type_str);
        }
        else
        {
            ULOG_Error("UART", "Ack", "unexpected or stale ACK");
        }
    }
    else if (strncmp(line, "EVAL,", 5) == 0)
    {
        /* Field 1: move notation */
        const char *p  = line + 5;
        const char *c1 = strchr(p, ',');
        if (!c1) { ULOG_Error("UART", "Eval", "parse err: move"); return; }

        size_t move_len = (size_t)(c1 - p);
        if (move_len == 0 || move_len >= sizeof(s_live_move))
        { ULOG_Error("UART", "Eval", "bad move len"); return; }

        char move_buf[8] = {0};
        memcpy(move_buf, p, move_len);
        move_buf[move_len] = '\0';

        /* Field 2: centipawn eval (signed integer with leading +/-) */
        p = c1 + 1;
        const char *c2 = strchr(p, ',');
        if (!c2) { ULOG_Error("UART", "Eval", "parse err: eval_cp"); return; }

        char eval_buf[16] = {0};
        size_t eval_len = (size_t)(c2 - p);
        if (eval_len == 0 || eval_len >= sizeof(eval_buf))
        { ULOG_Error("UART", "Eval", "bad eval len"); return; }
        memcpy(eval_buf, p, eval_len);
        eval_buf[eval_len] = '\0';

        int eval_cp_int = 0;
        sscanf(eval_buf, "%d", &eval_cp_int);

        /* Field 3: quality token (exactly 2 chars, then comma).
         * Direct index read — preserves embedded spaces (" !", "  "). */
        p = c2 + 1;
        if (p[0] == '\0' || p[1] == '\0' || p[2] != ',')
        { ULOG_Error("UART", "Eval", "bad quality"); return; }

        char quality_buf[3];
        quality_buf[0] = p[0];
        quality_buf[1] = p[1];
        quality_buf[2] = '\0';

        /* Field 4: is_blunder ('0' or '1') */
        p = p + 3;
        uint8_t blunder = (p[0] == '1') ? 1u : 0u;

        /* Commit to live state and trigger second LCD refresh */
        memcpy(s_live_move, move_buf, sizeof(s_live_move));
        s_live_eval_cp    = (int16_t)eval_cp_int;
        s_live_quality[0] = quality_buf[0];
        s_live_quality[1] = quality_buf[1];
        s_live_quality[2] = '\0';
        s_live_is_blunder = blunder;
        s_live_eval_valid = 1;

        s_lcd4_row0_dirty = 1;
        s_lcd4_row1_dirty = 1;

        ULOG_Info("UART", "Eval", move_buf);
    }
    else
    {
        ULOG_Error("UART", "RxLine", "unrecognised line");
    }
}

/**
 * @brief  Service the UART link each main loop iteration.
 *
 *         Drains all ready slots from the two-slot RX queue before handling
 *         the TX retry logic.  Draining in a loop (rather than one slot per
 *         call) means a back-to-back ACK + EVAL burst is fully processed in
 *         a single UART_Service call, keeping the LCD refresh latency as low
 *         as possible.
 */
static void UART_Service(void)
{
    /* --- RX: drain all ready slots --------------------------------------- */
    while (s_uart_rx_ready[s_uart_rx_read_idx])
    {
        char line[UART_RX_LINE_LEN];
        uint8_t ri = s_uart_rx_read_idx;

        /* Copy before clearing ready so the ISR cannot start refilling this
         * slot before the memcpy completes. */
        memcpy(line, (const char *)s_uart_rx_data[ri], UART_RX_LINE_LEN);
        s_uart_rx_ready[ri]  = 0;
        s_uart_rx_read_idx   = (uint8_t)((ri + 1U) % UART_RX_SLOTS);

        UART_HandleLine(line);
    }

    /* --- TX: retry any unACKed packet on timeout ------------------------- */
    if (s_uart_ack_pending)
    {
        uint32_t now = HAL_GetTick();

        if ((now - s_uart_ack_sent_ms) >= UART_ACK_TIMEOUT_MS)
        {
            if (s_uart_retry_count < UART_MAX_RETRIES)
            {
                s_uart_retry_count++;
                HAL_UART_Transmit(&huart1, (uint8_t *)s_uart_tx_buf, s_uart_tx_len, 100);
                s_uart_ack_sent_ms = now;

                char buf[48];
                snprintf(buf, sizeof(buf), "retry %u/%u for %s",
                         (unsigned)s_uart_retry_count, (unsigned)UART_MAX_RETRIES,
                         UART_PacketTypeName(s_uart_pending_type));
                ULOG_Error("UART", "Retry", buf);
            }
            else
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "gave up on %s after %u retries",
                         UART_PacketTypeName(s_uart_pending_type),
                         (unsigned)UART_MAX_RETRIES);
                ULOG_Error("UART", "AckTimeout", buf);
                s_uart_ack_pending = 0;
            }
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

    ULOG_Init(&huart2);
    ULOG_Info("MAIN", "main", "Boot OK");

    DHT_Init();
    ULOG_Info("MAIN", "main", "DHT init OK");

    if (LCD4_Init(&hi2c1) != LCD4_OK)
    {
        ULOG_Error("MAIN", "main", "LCD4 init failed");
        Error_Handler();
    }
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TL,  bmp_tl);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TR,  bmp_tr);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BL,  bmp_bl);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BR,  bmp_br);
    LCD4_DefineCustomChar(LCD4_CUSTOM_BLOCK,       bmp_full_block);
    LCD4_DefineCustomChar(LCD4_CUSTOM_UNDERSTROKE, bmp_understroke);
    ULOG_Info("MAIN", "main", "LCD4 init OK");

    if (LCD2_Init(&hi2c1) != LCD2_OK)
    {
        ULOG_Error("MAIN", "main", "LCD2 init failed");
        Error_Handler();
    }
    LCD2_Clear();
    ULOG_Info("MAIN", "main", "LCD2 init OK");

    JOY_Init(&hadc1, JOYSTICK_CLICK_GPIO_Port, JOYSTICK_CLICK_Pin);
    ULOG_Info("MAIN", "main", "Joystick init OK");

    HAL_TIM_Base_Start_IT(&htim2);
    ULOG_Info("MAIN", "main", "TIM2 started");

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    ULOG_Info("MAIN", "main", "USART1 (Pi link) ready");

    MENU_Init();
    ULOG_Info("MAIN", "main", "Menu init OK -- entering loop");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
        UART_Service();
        DHT_BackgroundPoll();

        switch (s_phase)
        {
        case PHASE_MENU:
            if (s_tick_flag)
                s_tick_flag = 0;
            if (MENU_Update())
                GAME_Start(MENU_GetSettings());
            break;

        case PHASE_RUNNING:
            CLOCK_HandleButtons();
            LCD2_HandleButton3();

            if (s_phase != PHASE_RUNNING)
                break;

            if (s_tick_flag)
                CLOCK_HandleTick();

            if (s_phase != PHASE_RUNNING)
                break;

            LCD4_RefreshMoveRow();
            LCD4_RefreshEvalRow();
            LCD4_RefreshLabelRow();
            CLOCK_RefreshDisplay();

            switch (s_lcd2_mode)
            {
                case LCD2_MODE_A: LCD2A_Refresh(); break;
                case LCD2_MODE_B: LCD2B_Refresh(); break;
                case LCD2_MODE_C: LCD2C_Refresh(); break;
                default: break;
            }
            break;

        case PHASE_PAUSED:
            if (s_tick_flag)
            {
                s_tick_flag = 0;
                s_timeout_led_phase ^= 1;
                LED_SetPaused();
            }
            PAUSE_HandleInput();
            if (s_lcd2_mode == LCD2_MODE_B)
                LCD2B_Refresh();
            break;

        case PHASE_RESULT_SELECT:
            if (s_tick_flag)
                s_tick_flag = 0;
            RESULT_SELECT_HandleInput();
            break;

        case PHASE_RESULT_CONFIRM:
            if (s_tick_flag)
                s_tick_flag = 0;
            RESULT_CONFIRM_HandleInput();
            break;

        case PHASE_GAME_OVER:
            if (s_tick_flag)
            {
                s_tick_flag = 0;
                s_timeout_led_phase ^= 1;
                LED_SetGameOver();
            }
            break;

        default:
            break;
        }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 50;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    Error_Handler();
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
    Error_Handler();

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    Error_Handler();
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    Error_Handler();
}

static void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 49999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    Error_Handler();

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
    Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
    Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
    Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
    Error_Handler();
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB,
      BLACK_TURN_Pin|BLACK_TIMEOUT_Pin|WHITE_TIMEOUT_Pin|WHITE_TURN_Pin,
      GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = JOYSTICK_CLICK_Pin|WHITE_CLOCK_Pin|BLACK_CLOCK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LCD_MODE_SWITCH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(LCD_MODE_SWITCH_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BLACK_TURN_Pin|BLACK_TIMEOUT_Pin|WHITE_TIMEOUT_Pin
                        |WHITE_TURN_Pin|DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        s_tick_flag    = 1;
        s_last_tick_ms = HAL_GetTick();
    }
}

/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
