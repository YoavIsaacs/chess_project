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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
    PHASE_MENU    = 0,   /* Pre-game menu (stages 1–4, and later 5–6)        */
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
static          uint32_t s_white_ms     = 0;           /* Initialised from menu settings at game start */
static          uint32_t s_black_ms     = 0;
static          uint32_t s_increment_ms = 0;           /* Fischer increment per move, from menu        */
static          uint32_t s_last_tick_ms = 0;           /* Captured in ISR             */
static          ActivePlayer s_active   = PLAYER_WHITE;

/* --- LCD2 mode ----------------------------------------------------------- */
static LCD2_Mode s_lcd2_mode = LCD2_MODE_A;            /* Default: clock; Button 3 cycles A->B->C->A */

/* --- LCD4 display gating ------------------------------------------------- */
static uint32_t s_last_white_disp_s = UINT32_MAX;     /* UINT32_MAX forces first write */
static uint32_t s_last_black_disp_s = UINT32_MAX;

/* --- LCD2 Mode A display gating ------------------------------------------ */
static uint32_t s_last_white_disp_s_lcd2 = UINT32_MAX;
static uint32_t s_last_black_disp_s_lcd2 = UINT32_MAX;

/* --- Button debounce ----------------------------------------------------- */
static uint32_t s_btn_white_last_ms = 0;
static uint32_t s_btn_black_last_ms = 0;
static uint8_t  s_btn_white_prev    = 1;               /* pull-up: idle = 1           */
static uint8_t  s_btn_black_prev    = 1;

static uint32_t s_btn3_last_ms      = 0;
static uint8_t  s_btn3_prev         = 1;               /* pull-up: idle = 1           */

/* --- DHT state ----------------------------------------------------------- */
static uint32_t s_last_dht_ms  = 0;                   /* Timestamp of last DHT read  */
static int8_t   s_last_temp    = -128;                 /* Sentinel: forces first write */
static int8_t   s_last_hum     = -128;

/* --- Microphone state ---------------------------------------------------- */
static uint32_t s_last_mic_ms  = 0;
static uint8_t  s_last_bar_len = 0xFF;                 /* 0xFF forces first write      */

/* --- LCD2 Mode B forced redraw ------------------------------------------- */
static uint8_t  s_lcd2b_needs_redraw = 0;

/* --- Timeout LED flash phase --------------------------------------------- */
static uint8_t  s_timeout_led_phase = 0;               /* Flipped every tick; drives timeout LED blink */

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

/* Custom character bitmaps */
static uint8_t bmp_tl[8] = { 0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b00000 };
static uint8_t bmp_tr[8] = { 0b11111, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00000 };
static uint8_t bmp_bl[8] = { 0b00000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 };
static uint8_t bmp_br[8] = { 0b00000, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b11111 };

/**
 * @brief  Transition from PHASE_MENU to PHASE_RUNNING.
 *         Loads confirmed settings into the clock state, draws the LCD4
 *         in-game static rows, enters LCD2 default mode, and sets initial LEDs.
 */
static void GAME_Start(const MENU_Settings *cfg)
{
    s_white_ms     = cfg->time_per_side_ms;
    s_black_ms     = cfg->time_per_side_ms;
    s_increment_ms = cfg->increment_ms;
    s_active       = PLAYER_WHITE;

    /* Reset all display sentinels to force a first write */
    s_last_white_disp_s      = UINT32_MAX;
    s_last_black_disp_s      = UINT32_MAX;
    s_last_white_disp_s_lcd2 = UINT32_MAX;
    s_last_black_disp_s_lcd2 = UINT32_MAX;
    s_last_bar_len            = 0xFF;
    s_last_temp               = -128;
    s_last_hum                = -128;
    s_timeout_led_phase       = 0;

    /* LCD4 in-game layout — static rows only; CLOCK_RefreshDisplay fills times */
    LCD4_Clear();
    CLOCK_DrawStaticRows();
    CLOCK_RefreshDisplay();

    /* LCD2 starts in Mode A (clock) */
    LCD2_EnterMode(LCD2_MODE_A);

    LED_SetState();

    s_phase = PHASE_RUNNING;

    char buf[48];
    snprintf(buf, sizeof(buf), "time=%lums inc=%lums eval=%d",
             cfg->time_per_side_ms, cfg->increment_ms,
             (int)cfg->eval_visible);
    ULOG_Info("MAIN", "GameStart", buf);
}

/**
 * @brief  Format milliseconds as "MM:SS" into buf.
 *         buf must be at least 6 bytes (5 chars + null).
 */
static void CLOCK_FormatTime(uint32_t ms, char *buf, uint8_t buf_len)
{
    uint32_t total_s = ms / 1000UL;
    uint32_t minutes = total_s / 60UL;
    uint32_t seconds = total_s % 60UL;
    snprintf(buf, buf_len, "%02lu:%02lu", minutes, seconds);
}

/**
 * @brief  Write static label row to LCD4. Called once at game start.
 *         Row 2: "Black          White"
 */
static void CLOCK_DrawStaticRows(void)
{
    LCD4_SetCursor(2, 0);
    LCD4_PrintString("Black          White");
}

/**
 * @brief  Refresh time row on LCD4 when displayed value changes.
 *         Row 3: Black time at col 0, White time at col 15.
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
 *         Active-low, pull-up. Valid press = falling edge outside debounce window,
 *         on the active player's button only.
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
            LED_SetState();
        }
    }
    s_btn_black_prev = btn_black;
}

/**
 * @brief  Consume the tick flag and decrement the active player's clock by 1 s.
 *         Clamps at 0. Timeout handling deferred to Step 9.
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
 *         Row 0: "Black      White"
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
 *         Row 1: Black time at col 0, White time at col 11.
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
 *         Row 1: noise label with blank bar field.
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
 *         Only called during PHASE_RUNNING.
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
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TL, bmp_tl);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TR, bmp_tr);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BL, bmp_bl);
    LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BR, bmp_br);
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

    /* Start TIM2 — 1-second interrupt (runs throughout menu and game) */
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
            /* Consume (and discard) any tick flags that fire during the menu
             * so the clock doesn't accumulate phantom ticks before game start. */
            if (s_tick_flag)
                s_tick_flag = 0;

            if (MENU_Update())
            {
                /* Menu complete — transition to RUNNING */
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
