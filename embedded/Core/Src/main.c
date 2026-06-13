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
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
    PLAYER_WHITE = 0,
    PLAYER_BLACK = 1
} ActivePlayer;

typedef enum
{
    LCD2_MODE_A = 0,   /* Clock display  */
    LCD2_MODE_B = 1    /* Environmental  */
} LCD2_Mode;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define CLOCK_START_MS      180000UL   /* Hard-coded start time: 3:00 (Blitz)  */
#define BTN_DEBOUNCE_MS     50U        /* Ignore button edges within this window */
#define DHT_POLL_INTERVAL_MS 5000U    /* Sample DHT every 5 seconds            */

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

/* --- Clock state --------------------------------------------------------- */
static volatile uint8_t  s_tick_flag    = 0;              /* Set by TIM2 ISR every 1 s  */
static          uint32_t s_white_ms     = CLOCK_START_MS;
static          uint32_t s_black_ms     = CLOCK_START_MS;
static          uint32_t s_last_tick_ms = 0;              /* Captured in ISR             */
static          ActivePlayer s_active   = PLAYER_WHITE;

/* --- LCD2 mode ----------------------------------------------------------- */
static LCD2_Mode s_lcd2_mode = LCD2_MODE_B;               /* Hardcoded for Step 2; Step 4 adds cycling */

/* --- LCD4 display gating ------------------------------------------------- */
static uint32_t s_last_white_disp_s = UINT32_MAX;         /* UINT32_MAX forces first write */
static uint32_t s_last_black_disp_s = UINT32_MAX;

/* --- Button debounce ----------------------------------------------------- */
static uint32_t s_btn_white_last_ms = 0;
static uint32_t s_btn_black_last_ms = 0;
static uint8_t  s_btn_white_prev    = 1;                  /* pull-up: idle = 1           */
static uint8_t  s_btn_black_prev    = 1;

/* --- DHT state ----------------------------------------------------------- */
static uint32_t s_last_dht_ms  = 0;                       /* Timestamp of last DHT read  */
static int8_t   s_last_temp    = -128;                    /* Sentinel: forces first write */
static int8_t   s_last_hum     = -128;

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

static void CLOCK_FormatTime(uint32_t ms, char *buf, uint8_t buf_len);
static void CLOCK_DrawStaticRows(void);
static void CLOCK_RefreshDisplay(void);
static void CLOCK_HandleButtons(void);
static void CLOCK_HandleTick(void);

static void LCD2B_DrawStatic(void);
static void LCD2B_Refresh(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Custom character bitmaps (carried over — needed for LCD4_DefineCustomChar) */
static uint8_t bmp_tl[8] = { 0b11111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b00000 };
static uint8_t bmp_tr[8] = { 0b11111, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00000 };
static uint8_t bmp_bl[8] = { 0b00000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 };
static uint8_t bmp_br[8] = { 0b00000, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b00001, 0b11111 };

/**
 * @brief  Format milliseconds as "MM:SS" into buf.
 * @note   Truncates to whole seconds — fractional ms discarded for display.
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
 * @brief  Write static label row to LCD4 only. Called once at startup.
 *
 *         LCD4 row 2 (20 chars): "Black          White"
 *
 * @note   LCD2 static content is now owned by LCD2B_DrawStatic() (Mode B)
 *         or the equivalent Mode A init (Step 4). This function no longer
 *         touches LCD2.
 */
static void CLOCK_DrawStaticRows(void)
{
    /* LCD4 — "Black"(5) + 10 spaces + "White"(5) = 20 chars */
    LCD4_SetCursor(2, 0);
    LCD4_PrintString("Black          White");
}

/**
 * @brief  Refresh time row on LCD4 only when displayed value changes.
 *
 *         LCD4 row 3: Black time at col 0, White time at col 15.
 *
 * @note   LCD2 clock display (Mode A) is intentionally absent here.
 *         Mode A path will be added in Step 4 alongside the mode-cycle button.
 */
static void CLOCK_RefreshDisplay(void)
{
    char buf[6];
    uint32_t white_s = s_white_ms / 1000UL;
    uint32_t black_s = s_black_ms / 1000UL;

    /* --- LCD4 row 3 -------------------------------------------------- */
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
 *
 *         Active-low, pull-up. Valid press = falling edge (prev=1, now=0)
 *         outside the debounce window, on the active player's button only.
 *
 *         On valid press: switch s_active. The ms counters are not touched —
 *         all time deduction is handled exclusively by CLOCK_HandleTick.
 *         The display always truncates _ms to whole seconds, so no jump
 *         occurs at the moment of the button press regardless of where the
 *         ms counter sits within the current second.
 */
static void CLOCK_HandleButtons(void)
{
    uint32_t now = HAL_GetTick();

    uint8_t btn_white = HAL_GPIO_ReadPin(WHITE_CLOCK_GPIO_Port, WHITE_CLOCK_Pin);
    uint8_t btn_black = HAL_GPIO_ReadPin(BLACK_CLOCK_GPIO_Port, BLACK_CLOCK_Pin);

    /* White button — falling edge, debounced, active player only */
    if (s_btn_white_prev == 1 && btn_white == 0 &&
        (now - s_btn_white_last_ms) >= BTN_DEBOUNCE_MS)
    {
        s_btn_white_last_ms = now;

        if (s_active == PLAYER_WHITE)
        {
            s_active = PLAYER_BLACK;
            ULOG_Info("CLOCK", "Btn", "White pressed -> Black active");
        }
    }
    s_btn_white_prev = btn_white;

    /* Black button — falling edge, debounced, active player only */
    if (s_btn_black_prev == 1 && btn_black == 0 &&
        (now - s_btn_black_last_ms) >= BTN_DEBOUNCE_MS)
    {
        s_btn_black_last_ms = now;

        if (s_active == PLAYER_BLACK)
        {
            s_active = PLAYER_WHITE;
            ULOG_Info("CLOCK", "Btn", "Black pressed -> White active");
        }
    }
    s_btn_black_prev = btn_black;
}

/**
 * @brief  Consume the tick flag and decrement the active player's clock by 1 s.
 *         Clamps at 0 — timeout handling deferred to Step 9.
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
}

/**
 * @brief  Write static content for LCD2 Mode B (Environmental).
 *         Row 1 noise placeholder is written once here and not touched
 *         again until Step 3 adds real microphone sampling.
 *
 *         Call once when entering Mode B (or at startup when Mode B is
 *         the hardcoded default).
 */
static void LCD2B_DrawStatic(void)
{
    /* Row 1 — noise placeholder (Step 3 will overwrite with real bar) */
    LCD2_SetCursor(1, 0);
    LCD2_PrintString("Noise: ----     ");
    ULOG_Info("LCD2", "LCD2B_DrawStatic", "Mode B static rows drawn");
}

/**
 * @brief  Poll DHT and refresh LCD2 Mode B row 0 only when values change.
 *
 *         Cadence: DHT_POLL_INTERVAL_MS (5 s). Uses s_last_dht_ms gated
 *         by HAL_GetTick(). On first call (s_last_dht_ms == 0) the
 *         subtraction wraps and the condition is immediately true, forcing
 *         a read on the first loop iteration.
 *
 *         Display gate: s_last_temp / s_last_hum track the last written
 *         values; LCD2 is only written when either changes.
 *
 *         Format — row 0, 16 chars: "T:%2dC  H:%2d%%    "
 *         cols:  T: 0-1 | value 2-3 | C 4 | gap 5-6 | H: 7-8 | value 9-10 | % 11 | pad 12-15
 */
static void LCD2B_Refresh(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - s_last_dht_ms) < DHT_POLL_INTERVAL_MS)
    {
        return;
    }
    s_last_dht_ms = now;

    DHT_Data d = DHT_Read();

    if (d.status != DHT_OK)
    {
        ULOG_Error("LCD2", "LCD2B_Refresh", "DHT read failed");
        return;
    }

    int8_t temp = (int8_t)d.temperature_c;
    int8_t hum  = (int8_t)d.humidity_pct;

    if (temp == s_last_temp && hum == s_last_hum)
    {
        return;
    }

    char buf[17];
    snprintf(buf, sizeof(buf), "T:%2dC  H:%2d%%    ", temp, hum);

    LCD2_SetCursor(0, 0);
    LCD2_PrintString(buf);

    s_last_temp = temp;
    s_last_hum  = hum;

    ULOG_Info("LCD2", "LCD2B_Refresh", "DHT display updated");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
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

    /* Draw static content for the active LCD2 mode */
    if (s_lcd2_mode == LCD2_MODE_B)
    {
        LCD2B_DrawStatic();
    }

    /* Draw static labels on LCD4, then initial times */
    LCD4_Clear();
    CLOCK_DrawStaticRows();
    CLOCK_RefreshDisplay();

    /* Start TIM2 — 1-second interrupt */
    HAL_TIM_Base_Start_IT(&htim2);
    ULOG_Info("MAIN", "main", "TIM2 started -- entering loop");

    /* USER CODE END 2 */

    /* USER CODE BEGIN WHILE */
    while (1)
    {
        CLOCK_HandleButtons();

        if (s_tick_flag)
        {
            CLOCK_HandleTick();
        }

        CLOCK_RefreshDisplay();

        if (s_lcd2_mode == LCD2_MODE_B)
        {
            LCD2B_Refresh();
        }

        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief  TIM2 period elapsed callback — fires every 1 second.
 * @note   Sets s_tick_flag and captures timestamp here in ISR context
 *         so s_last_tick_ms is always the exact hardware event time,
 *         not the main-loop poll time.
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

/* ---- CubeMX-generated functions below — do not edit -------------------- */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 8;
    RCC_OscInitStruct.PLL.PLLN            = 50;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = 2;
    RCC_OscInitStruct.PLL.PLLR            = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
    sConfig.Channel      = ADC_CHANNEL_0;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 100000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef  sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig      = {0};
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 49999;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 999;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) Error_Handler();
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
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
    HAL_GPIO_WritePin(LD2_GPIO_Port,   LD2_Pin,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);
    GPIO_InitStruct.Pin  = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin  = JOYSTICK_CLICK_Pin | WHITE_CLOCK_Pin | BLACK_CLOCK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin   = LD2_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin   = DHT11_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

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
