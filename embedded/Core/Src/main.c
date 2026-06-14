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
    PHASE_MENU    = 0,   /* Pre-game menu (stages 1-6)                        */
    PHASE_RUNNING = 1    /* Game active — timers counting, all sensors active */
} App_Phase;

typedef enum
{
    PLAYER_WHITE = 0,
    PLAYER_BLACK = 1
} ActivePlayer;

typedef enum
{
    LCD2_MODE_A = 0,   /* Clock display        */
    LCD2_MODE_B = 1,   /* Environmental        */
    LCD2_MODE_C = 2    /* Opponent view (TODO) */
} LCD2_Mode;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define BTN_DEBOUNCE_MS       50U       /* Ignore button edges within this window */
#define DHT_POLL_INTERVAL_MS  5000U     /* Sample DHT every 5 seconds             */
#define MIC_POLL_INTERVAL_MS  1000U     /* Sample microphone every 1 second       */

/* Eval bar geometry */
#define EVAL_BAR_LEN          12U       /* 12 inner positions between [ ] brackets */
#define EVAL_BAR_HALF         6U        /* positions 0-5 black, 6-11 white         */

/* Centipawn thresholds — 5 usable cells per side span +-5.0 pawns.
 * A cell fills as soon as eval crosses into its band (ceiling rule):
 *   1 cp -> 1 cell,  100 cp -> 2 cells,  200 cp -> 3 cells,
 *   300 cp -> 4 cells, 400 cp -> 5 cells (max usable, almost full).
 * Cell 6 (index 0 or 11) is reserved for forced mate only.         */
#define EVAL_CP_PER_CELL      100       /* cp per bar cell; 5 cells = +-5.0 max */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* --- Application phase --------------------------------------------------- */
static App_Phase s_phase = PHASE_MENU;

/* --- Clock state --------------------------------------------------------- */
static volatile uint8_t  s_tick_flag    = 0;          /* Set by TIM2 ISR every 1 s  */
static          uint32_t s_white_ms     = 0;
static          uint32_t s_black_ms     = 0;
static          uint32_t s_increment_ms = 0;
static          uint32_t s_last_tick_ms = 0;           /* Captured in ISR             */
static          ActivePlayer s_active   = PLAYER_WHITE;

/* --- Eval visibility (from menu settings) -------------------------------- */
static uint8_t s_eval_visible = 0;

/* --- LCD2 mode ----------------------------------------------------------- */
static LCD2_Mode s_lcd2_mode = LCD2_MODE_A;

/* --- LCD4 display gating (times, row 3) ---------------------------------- */
static uint32_t s_last_white_disp_s = UINT32_MAX;
static uint32_t s_last_black_disp_s = UINT32_MAX;

/* --- LCD2 Mode A display gating ------------------------------------------ */
static uint32_t s_last_white_disp_s_lcd2 = UINT32_MAX;
static uint32_t s_last_black_disp_s_lcd2 = UINT32_MAX;

/* --- Button debounce ----------------------------------------------------- */
static uint32_t s_btn_white_last_ms = 0;
static uint32_t s_btn_black_last_ms = 0;
static uint8_t  s_btn_white_prev    = 1;
static uint8_t  s_btn_black_prev    = 1;

static uint32_t s_btn3_last_ms      = 0;
static uint8_t  s_btn3_prev         = 1;

/* --- DHT state ----------------------------------------------------------- */
static uint32_t s_last_dht_ms  = 0;
static int8_t   s_last_temp    = -128;                 /* Sentinel: forces first write */
static int8_t   s_last_hum     = -128;

/* --- Microphone state ---------------------------------------------------- */
static uint32_t s_last_mic_ms  = 0;
static uint8_t  s_last_bar_len = 0xFF;                 /* 0xFF forces first write      */

/* --- LCD2 Mode B forced redraw ------------------------------------------- */
static uint8_t  s_lcd2b_needs_redraw = 0;

/* --- Timeout LED flash phase --------------------------------------------- */
static uint8_t  s_timeout_led_phase = 0;

/* =========================================================================
 * Mock game data
 *
 * Real move notation and eval arrive from the Pi over UART in Step 10.
 * Until then, these mock arrays cycle on every clock button press so the
 * LCD4 in-game layout can be exercised and visually verified.
 * ========================================================================= */

/* Move strings — up to 6 chars + null.  Kept short so they fit cleanly in
 * the left portion of row 0 (cols 0-5 or so).                               */
static const char * const k_mock_moves[] =
{
    "e4",
    "e5",
    "Nf3",
    "Nc6",
    "Bb5",
    "a6",
    "Ba4",
    "Nf6",
    "O-O",
    "Be7",
};
#define MOCK_MOVE_COUNT  (sizeof(k_mock_moves) / sizeof(k_mock_moves[0]))

/* Quality token strings — always printed right-aligned into the last 2 cols
 * of row 0 (cols 18-19).  One-char tokens are padded with a leading space.  */
static const char * const k_mock_quality[] =
{
    "!!",   /* Best      */
    " !",   /* Good      */
    "!?",   /* Interest. */
    "?!",   /* Inaccurac */
    " ?",   /* Mistake   */
    "??",   /* Blunder   */
    " !",
    "!!",
    "?!",
    " !",
};
#define MOCK_QUALITY_COUNT  (sizeof(k_mock_quality) / sizeof(k_mock_quality[0]))

/* Centipawn eval — signed, in units of centipawns.
 * Positive = white better, negative = black better.
 * The eval bar maps ±(EVAL_BAR_HALF * EVAL_CP_PER_CELL) = ±600 cp.
 * Values beyond that saturate the bar (but do NOT fill the extreme cell
 * unless a forced-mate flag is set — forced mate is out of scope for mock). */
static const int16_t k_mock_eval_cp[] =
{
       0,    /*  0 cells -- equal                   */
      50,    /*  1 cell  white (+0.5)               */
    -100,    /*  2 cells black (-1.0, hits boundary)*/
     150,    /*  2 cells white (+1.5)               */
    -250,    /*  3 cells black (-2.5)               */
     300,    /*  4 cells white (+3.0, hits boundary)*/
    -400,    /*  5 cells black (-4.0, almost full)  */
     499,    /*  5 cells white (+4.99, almost full) */
    -500,    /*  5 cells black (-5.0, almost full)  */
     999,    /*  5 cells white (>>5.0, saturates)   */
};
#define MOCK_EVAL_COUNT  (sizeof(k_mock_eval_cp) / sizeof(k_mock_eval_cp[0]))

/* --- Live mock state ----------------------------------------------------- */
static uint8_t  s_game_started  = 0;    /* 0 until first button press             */
static uint8_t  s_move_number   = 0;    /* Increments on every button press        */
static uint8_t  s_mock_idx      = 0;    /* Cycles through mock arrays (mod count)  */

/* Display sentinels for LCD4 rows 0-2 (times handled separately in row 3)   */
static uint8_t  s_lcd4_row0_dirty = 1;
static uint8_t  s_lcd4_row1_dirty = 1;
static uint8_t  s_lcd4_row2_dirty = 1;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

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
static void LCD2_EnterMode(LCD2_Mode mode);
static void LCD2_HandleButton3(void);

static void LED_SetState(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* -------------------------------------------------------------------------
 * Custom character bitmaps
 *
 * Slots 0-3: box corners (used by pause screen in Step 9)
 * Slot 4:    full block  (eval bar — filled cell)
 * Slot 5:    understroke (eval bar — empty cell)
 * ------------------------------------------------------------------------- */
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

/**
 * @brief  Transition from PHASE_MENU to PHASE_RUNNING.
 */
static void GAME_Start(const MENU_Settings *cfg)
{
    s_white_ms     = cfg->time_per_side_ms;
    s_black_ms     = cfg->time_per_side_ms;
    s_increment_ms = cfg->increment_ms;
    s_eval_visible = cfg->eval_visible;
    s_active       = PLAYER_WHITE;

    /* Reset clock display sentinels */
    s_last_white_disp_s      = UINT32_MAX;
    s_last_black_disp_s      = UINT32_MAX;
    s_last_white_disp_s_lcd2 = UINT32_MAX;
    s_last_black_disp_s_lcd2 = UINT32_MAX;
    s_last_bar_len            = 0xFF;
    s_last_temp               = -128;
    s_last_hum                = -128;
    s_timeout_led_phase       = 0;

    /* Reset mock game data */
    s_game_started  = 0;
    s_move_number   = 0;
    s_mock_idx      = 0;

    /* Force all LCD4 content rows to redraw on first loop */
    s_lcd4_row0_dirty = 1;
    s_lcd4_row1_dirty = 1;
    s_lcd4_row2_dirty = 1;

    /* LCD4 in-game layout */
    LCD4_Clear();
    CLOCK_DrawStaticRows();
    LCD4_RefreshMoveRow();
    LCD4_RefreshEvalRow();
    LCD4_RefreshLabelRow();
    CLOCK_RefreshDisplay();

    /* LCD2 starts in Mode A (clock) */
    LCD2_EnterMode(LCD2_MODE_A);

    LED_SetState();

    s_phase = PHASE_RUNNING;

    char buf[64];
    snprintf(buf, sizeof(buf), "time=%lums inc=%lums eval=%d",
             cfg->time_per_side_ms, cfg->increment_ms,
             (int)cfg->eval_visible);
    ULOG_Info("MAIN", "GameStart", buf);
}

/**
 * @brief  Format milliseconds as "MM:SS" into buf (at least 6 bytes).
 */
static void CLOCK_FormatTime(uint32_t ms, char *buf, uint8_t buf_len)
{
    uint32_t total_s = ms / 1000UL;
    uint32_t minutes = total_s / 60UL;
    uint32_t seconds = total_s % 60UL;
    snprintf(buf, buf_len, "%02lu:%02lu", minutes, seconds);
}

/**
 * @brief  Build the 12-character eval bar from a centipawn value.
 *
 *         bar_out must be at least 12 bytes (not null-terminated).
 *
 *         Bar layout (spec 2.7):
 *           Positions 0-5  = black's side (left half)
 *           Positions 6-11 = white's side (right half)
 *
 *         Scaling: 5 usable cells per side, each cell = 200 cp band.
 *         Ceiling rule: any eval > 0 fills at least 1 cell.
 *           cells_filled = ceil(abs_cp / EVAL_CP_PER_CELL)
 *                        = (abs_cp + EVAL_CP_PER_CELL - 1) / EVAL_CP_PER_CELL
 *           Clamped to EVAL_BAR_HALF - 1 (= 5); position 0 / 11 reserved for mate.
 *
 *         White advantage: fills rightward from position 6.
 *         Black advantage: fills leftward from position 5.
 *         Equal (0 cp):    all 12 positions = understroke.
 */
static void LCD4_BuildEvalBar(int16_t eval_cp, char *bar_out)
{
    char full  = (char)LCD4_CUSTOM_BLOCK;       /* slot 4 */
    char under = (char)LCD4_CUSTOM_UNDERSTROKE; /* slot 5 */

    uint8_t i;
    for (i = 0; i < EVAL_BAR_LEN; i++)
        bar_out[i] = under;

    if (eval_cp > 0)
    {
        /* White advantage — fill rightward from position 6 */
        int16_t cells = (eval_cp + EVAL_CP_PER_CELL - 1) / EVAL_CP_PER_CELL;  /* ceiling */
        if (cells > (int16_t)(EVAL_BAR_HALF - 1U))
            cells = (int16_t)(EVAL_BAR_HALF - 1U);   /* position 11 reserved for mate */
        for (i = 0; i < (uint8_t)cells; i++)
            bar_out[EVAL_BAR_HALF + i] = full;
    }
    else if (eval_cp < 0)
    {
        /* Black advantage — fill leftward from position 5 */
        int16_t cp_abs = (int16_t)(-eval_cp);
        int16_t cells  = (cp_abs + EVAL_CP_PER_CELL - 1) / EVAL_CP_PER_CELL;  /* ceiling */
        if (cells > (int16_t)(EVAL_BAR_HALF - 1U))
            cells = (int16_t)(EVAL_BAR_HALF - 1U);   /* position 0 reserved for mate */
        for (i = 0; i < (uint8_t)cells; i++)
            bar_out[(EVAL_BAR_HALF - 1U) - i] = full;
    }
    /* eval_cp == 0: leave all understroke */
}

/**
 * @brief  Write row 0 of the in-game LCD4 display.
 *
 *         Layout (20 chars):
 *           - Before first move: 20 spaces.
 *           - After first move, eval HIDDEN:
 *               "e4                  "  (move left, rest spaces)
 *           - After first move, eval VISIBLE:
 *               "e4               !!"  (move left, quality right-aligned in cols 18-19)
 *
 *         Quality token is always exactly 2 chars (padded with a leading
 *         space for single-char tokens: " !", " ?").
 */
static void LCD4_RefreshMoveRow(void)
{
    if (!s_lcd4_row0_dirty)
        return;
    s_lcd4_row0_dirty = 0;

    char row[21];

    if (!s_game_started)
    {
        /* Blank before first move */
        memset(row, ' ', 20);
        row[20] = '\0';
    }
    else
    {
        const char *move    = k_mock_moves   [s_mock_idx % MOCK_MOVE_COUNT];
        const char *quality = k_mock_quality [s_mock_idx % MOCK_QUALITY_COUNT];

        if (s_eval_visible)
        {
            /* Move left-aligned in a 18-char field, quality in cols 18-19 */
            snprintf(row, sizeof(row), "%-18s%2s", move, quality);
        }
        else
        {
            /* Move left-aligned, no quality */
            snprintf(row, sizeof(row), "%-20s", move);
        }
    }

    LCD4_SetCursor(0, 0);
    LCD4_PrintString(row);

    ULOG_Info("LCD4", "MoveRow", row);
}

/**
 * @brief  Write row 1 of the in-game LCD4 display (eval bar + numerical eval).
 *
 *         Layout (20 chars):
 *           - eval HIDDEN or before first move: 20 spaces.
 *           - eval VISIBLE after first move:
 *               "[____________]  +0.0"   (equal)
 *               "[_____██████]  +2.0"   (white +200 cp)
 *               "[██___________]  -1.0"  (black -100 cp)
 *
 *         The bracket + 12-char bar + bracket occupies cols 0-13 (14 chars).
 *         The numerical eval occupies cols 14-19 (6 chars, right-aligned).
 *         Format: "%+d.%d" — e.g. "+1" for 100 cp becomes "+1.0".
 *         We store centipawns and display as tenths-of-a-pawn with one decimal.
 */
static void LCD4_RefreshEvalRow(void)
{
    if (!s_lcd4_row1_dirty)
        return;
    s_lcd4_row1_dirty = 0;

    char row[21];

    if (!s_eval_visible || !s_game_started)
    {
        memset(row, ' ', 20);
        row[20] = '\0';
    }
    else
    {
        int16_t eval_cp = k_mock_eval_cp[s_mock_idx % MOCK_EVAL_COUNT];

        /* Build the 12-char bar */
        char bar[EVAL_BAR_LEN];
        LCD4_BuildEvalBar(eval_cp, bar);

        /* Numerical eval: centipawns → ±N.N format (e.g. 130 → "+1.3") */
        int16_t abs_cp    = (eval_cp < 0) ? (int16_t)(-eval_cp) : eval_cp;
        int16_t whole     = abs_cp / 100;
        int16_t tenth     = (abs_cp % 100) / 10;
        char    sign      = (eval_cp >= 0) ? '+' : '-';
        char    eval_str[7];                              /* "+99.9\0" = 6+null */
        snprintf(eval_str, sizeof(eval_str), "%c%d.%d", sign, (int)whole, (int)tenth);

        /* Assemble: "[" + 12 bar chars + "]" + 6-char eval field
         * Total = 1 + 12 + 1 + 6 = 20 chars.
         * The bar chars may include custom char bytes (non-ASCII), so we
         * build the string manually rather than using snprintf for that part.
         */
        row[0] = '[';
        uint8_t i;
        for (i = 0; i < EVAL_BAR_LEN; i++)
            row[1U + i] = bar[i];
        row[13] = ']';
        /* Right-align eval_str into cols 14-19 (6 chars) */
        snprintf(&row[14], 7, "%6s", eval_str);
        row[20] = '\0';
    }

    LCD4_SetCursor(1, 0);
    LCD4_PrintString(row);

    ULOG_Info("LCD4", "EvalRow", row);
}

/**
 * @brief  Write row 2 of the in-game LCD4 display (labels + move number).
 *
 *         Layout (20 chars):
 *           Before first move: "Black          White"
 *           After first move:  "Black    #NN   White"
 *
 *         "Black" occupies cols 0-4, "White" cols 15-19 (5 chars each).
 *         The centre 10 chars (cols 5-14) hold the move number when present:
 *           "#NN" (3 chars) centred → 3 spaces on each side → "   #NN   " (9)
 *           padded to 10 with one more trailing space.
 *         For move numbers > 99 the format stays 3 chars ("#NN" mod 100).
 */
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
        /* Centre field: 10 chars wide, "#NN" centred within.
         * "   #NN   " = 3 spaces + 3 chars + 4 spaces — but that is 10; if
         * the move number is 1-digit we still use 2 digits (zero-pad): #01.
         * Move number wraps display at 99 for simplicity; real data from Pi
         * will replace this in Step 10 anyway.
         */
        uint8_t disp_move = (s_move_number > 99) ? 99 : s_move_number;
        char centre[11];
        snprintf(centre, sizeof(centre), "   #%02u    ", (unsigned)disp_move);
        /* "Black" + centre(10) + "White" = 5 + 10 + 5 = 20 */
        snprintf(row, sizeof(row), "Black%sWhite", centre);
    }

    LCD4_SetCursor(2, 0);
    LCD4_PrintString(row);

    ULOG_Info("LCD4", "LabelRow", row);
}

/**
 * @brief  Write static rows called once at game start.
 *         Row 2 is now handled by LCD4_RefreshLabelRow.
 *         This function is kept for the initial clear + times row structure.
 *         (Row 3 times are written by CLOCK_RefreshDisplay.)
 */
static void CLOCK_DrawStaticRows(void)
{
    /* Row 2 will be drawn by LCD4_RefreshLabelRow on first loop;
     * nothing else static to draw here now that row layout is data-driven. */
    (void)0;
}

/**
 * @brief  Refresh time row (row 3) on LCD4 when displayed value changes.
 *         Black time at col 0, White time at col 15.
 */
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
 * @brief  Poll both clock buttons and handle a validated press.
 *         On a valid press: apply increment, switch active player, advance
 *         mock game data, and mark LCD4 rows dirty for redraw.
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
            s_active = PLAYER_BLACK;
            ULOG_Info("CLOCK", "Btn", "White pressed -> Black active");

            /* Advance mock game data.
             * White's press ends White's turn and begins a new move pair.
             * Move number increments here only — both this press and Black's
             * response will display the same #NN until White presses again. */
            s_game_started = 1;
            s_move_number++;
            s_mock_idx = (uint8_t)((s_mock_idx + 1U) % MOCK_MOVE_COUNT);
            s_lcd4_row0_dirty = 1;
            s_lcd4_row1_dirty = 1;
            s_lcd4_row2_dirty = 1;

            LED_SetState();
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
            s_active = PLAYER_WHITE;
            ULOG_Info("CLOCK", "Btn", "Black pressed -> White active");

            /* Black's press does NOT increment move number — the pair counter
             * only advances when White presses.  We do advance the mock index
             * so the displayed move string updates on every half-move.        */
            s_game_started = 1;
            s_mock_idx = (uint8_t)((s_mock_idx + 1U) % MOCK_MOVE_COUNT);
            s_lcd4_row0_dirty = 1;
            s_lcd4_row1_dirty = 1;
            s_lcd4_row2_dirty = 1;

            LED_SetState();
        }
    }
    s_btn_black_prev = btn_black;
}

/**
 * @brief  Consume the tick flag and decrement the active player's clock by 1 s.
 */
static void CLOCK_HandleTick(void)
{
    s_tick_flag = 0;

    if (s_active == PLAYER_WHITE)
    {
        if (s_white_ms >= 1000UL)
            s_white_ms -= 1000UL;
        else
            s_white_ms = 0;
    }
    else
    {
        if (s_black_ms >= 1000UL)
            s_black_ms -= 1000UL;
        else
            s_black_ms = 0;
    }

    s_timeout_led_phase ^= 1;
    LED_SetState();
}

/**
 * @brief  Drive all four LEDs to match the current clock state.
 */
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

/**
 * @brief  Write static label row for LCD2 Mode A (Clock).
 */
static void LCD2A_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("Black      White");
    ULOG_Info("LCD2", "LCD2A_DrawStatic", "Mode A static rows drawn");
}

/**
 * @brief  Refresh time row on LCD2 Mode A when displayed value changes.
 */
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

/**
 * @brief  Write static content for LCD2 Mode B (Environmental).
 */
static void LCD2B_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("Noise:          ");
    ULOG_Info("LCD2", "LCD2B_DrawStatic", "Mode B static rows drawn");
}

/**
 * @brief  Poll DHT and microphone; refresh LCD2 Mode B rows when values change.
 */
static void LCD2B_Refresh(void)
{
    uint32_t now = HAL_GetTick();

    /* --- DHT read (5 s cadence) ---------------------------------------- */
    if (s_lcd2b_needs_redraw || (now - s_last_dht_ms) >= DHT_POLL_INTERVAL_MS)
    {
        if ((now - s_last_dht_ms) >= DHT_POLL_INTERVAL_MS)
        {
            s_last_dht_ms = now;

            DHT_Data d = DHT_Read();

            if (d.status != DHT_OK)
            {
                ULOG_Error("LCD2", "LCD2B_Refresh", "DHT read failed");
                s_lcd2b_needs_redraw = 0;
                return;
            }

            s_last_temp = (int8_t)d.temperature_c;
            s_last_hum  = (int8_t)d.humidity_pct;
        }

        if (s_last_temp != -128 && s_last_hum != -128)
        {
            char buf[17];
            snprintf(buf, sizeof(buf), "T:%2dC  H:%2d%%    ", s_last_temp, s_last_hum);
            LCD2_SetCursor(0, 0);
            LCD2_PrintString(buf);
            ULOG_Info("LCD2", "LCD2B_Refresh", "DHT display updated");
        }

        s_lcd2b_needs_redraw = 0;
    }

    /* --- Microphone ADC read (1 s cadence) ------------------------------ */
    if ((now - s_last_mic_ms) >= MIC_POLL_INTERVAL_MS)
    {
        s_last_mic_ms = now;

        ADC_ChannelConfTypeDef sConfig = {0};
        sConfig.Channel      = ADC_CHANNEL_15;
        sConfig.Rank         = 1;
        sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);
        uint32_t mic_val = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);

        uint8_t bar_len = (uint8_t)((mic_val * 9UL) / 4095UL);

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

            char log_buf[32];
            snprintf(log_buf, sizeof(log_buf), "adc=%lu bar=%u", mic_val, bar_len);
            ULOG_Info("MIC", "Read", log_buf);
        }
    }
}

/**
 * @brief  Write static placeholder for LCD2 Mode C (Opponent view, TODO until Step 10).
 */
static void LCD2C_DrawStatic(void)
{
    LCD2_Clear();
    LCD2_SetCursor(0, 0);
    LCD2_PrintString("MODE C          ");
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("TODO            ");
    ULOG_Info("LCD2", "LCD2C_DrawStatic", "Mode C static rows drawn");
}

/**
 * @brief  Transition LCD2 to the given mode.
 */
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
            LCD2C_DrawStatic();
            ULOG_Info("LCD2", "EnterMode", "C");
            break;

        default:
            break;
    }
}

/**
 * @brief  Poll Button 3 (PC4) and cycle LCD2 mode on a validated press.
 */
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
    }

    s_btn3_prev = btn3;
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

    /* Start TIM2 — 1-second interrupt */
    HAL_TIM_Base_Start_IT(&htim2);
    ULOG_Info("MAIN", "main", "TIM2 started");

    /* Initialise the pre-game menu — draws Stage 1 on LCD4 */
    MENU_Init();
    ULOG_Info("MAIN", "main", "Menu init OK -- entering loop");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
        if (s_phase == PHASE_MENU)
        {
            /* --- Menu phase ------------------------------------------------ */
            if (s_tick_flag)
                s_tick_flag = 0;

            if (MENU_Update())
            {
                GAME_Start(MENU_GetSettings());
            }
        }
        else
        {
            /* --- Running phase --------------------------------------------- */
            CLOCK_HandleButtons();
            LCD2_HandleButton3();

            if (s_tick_flag)
            {
                CLOCK_HandleTick();
            }

            /* LCD4 in-game rows */
            LCD4_RefreshMoveRow();
            LCD4_RefreshEvalRow();
            LCD4_RefreshLabelRow();
            CLOCK_RefreshDisplay();

            switch (s_lcd2_mode)
            {
                case LCD2_MODE_A:
                    LCD2A_Refresh();
                    break;

                case LCD2_MODE_B:
                    LCD2B_Refresh();
                    break;

                case LCD2_MODE_C:
                    /* No periodic refresh until Step 10 */
                    break;

                default:
                    break;
            }
        }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
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
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  */
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
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  */
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
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  */
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
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  */
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
  {
    Error_Handler();
  }
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, BLACK_TURN_Pin|BLACK_TIMEOUT_Pin|WHITE_TIMEOUT_Pin|WHITE_TURN_Pin, GPIO_PIN_RESET);
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

  GPIO_InitStruct.Pin = BLACK_TURN_Pin|BLACK_TIMEOUT_Pin|WHITE_TIMEOUT_Pin|WHITE_TURN_Pin
                          |DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/**
 * @brief  TIM2 period elapsed callback — fires every 1 second.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        s_tick_flag    = 1;
        s_last_tick_ms = HAL_GetTick();
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
