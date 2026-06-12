/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uart_log.h"
#include "joystick.h"
#include "lcd_2x16.h"
#include "lcd_4x20.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * Custom character bitmaps — 5 columns x 8 rows, bits 4-0 used.
 * Loaded into LCD4 CGRAM slots 0-3 after LCD4_Init.
 */

/* Slot 0 — top-left corner */
static uint8_t bmp_tl[8] = {
    0b11111,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b00000
};

/* Slot 1 — top-right corner */
static uint8_t bmp_tr[8] = {
    0b11111,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b00000
};

/* Slot 2 — bottom-left corner */
static uint8_t bmp_bl[8] = {
    0b00000,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b10000,
    0b11111
};

/* Slot 3 — bottom-right corner */
static uint8_t bmp_br[8] = {
    0b00000,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b00001,
    0b11111
};

/*
 * Draw a full-screen border box using custom corner chars and ASCII edges.
 *
 * Row 0: [TL] + 18x'-' + [TR]
 * Row 1: '|'  + 18x' ' + '|'
 * Row 2: '|'  + 18x' ' + '|'
 * Row 3: [BL] + 18x'-' + [BR]
 */
static void draw_box(void)
{
    uint8_t c;

    /* Row 0: top edge */
    LCD4_SetCursor(0, 0);
    LCD4_PrintChar(LCD4_CUSTOM_CORNER_TL);
    for (c = 1; c < 19; c++) LCD4_PrintChar('-');
    LCD4_PrintChar(LCD4_CUSTOM_CORNER_TR);

    /* Rows 1-2: side edges with blank interior */
    for (uint8_t r = 1; r <= 2; r++)
    {
        LCD4_SetCursor(r, 0);
        LCD4_PrintChar('|');
        for (c = 1; c < 19; c++) LCD4_PrintChar(' ');
        LCD4_PrintChar('|');
    }

    /* Row 3: bottom edge */
    LCD4_SetCursor(3, 0);
    LCD4_PrintChar(LCD4_CUSTOM_CORNER_BL);
    for (c = 1; c < 19; c++) LCD4_PrintChar('-');
    LCD4_PrintChar(LCD4_CUSTOM_CORNER_BR);
}

/*
 * Write all joystick fields to LCD4 — one field per row, 20 chars wide.
 * Each line is padded with spaces to overwrite any stale characters.
 *
 * Row 0: X raw value
 * Row 1: Y raw value
 * Row 2: Direction
 * Row 3: Click state
 */
static void display_joystick_lcd4(JOY_Data *j)
{
    char buf[21]; /* 20 chars + null */

    /* Row 0 — X axis */
    snprintf(buf, sizeof(buf), "X raw: %-13d", (int)j->x_raw);
    LCD4_SetCursor(0, 0);
    LCD4_PrintString(buf);

    /* Row 1 — Y axis */
    snprintf(buf, sizeof(buf), "Y raw: %-13d", (int)j->y_raw);
    LCD4_SetCursor(1, 0);
    LCD4_PrintString(buf);

    /* Row 2 — Direction */
    const char *dir_str;
    switch (j->direction)
    {
        case JOY_UP:     dir_str = "UP     "; break;
        case JOY_DOWN:   dir_str = "DOWN   "; break;
        case JOY_LEFT:   dir_str = "LEFT   "; break;
        case JOY_RIGHT:  dir_str = "RIGHT  "; break;
        case JOY_CENTRE: dir_str = "CENTRE "; break;
        case JOY_NONE:   dir_str = "NONE   "; break;
        default:         dir_str = "UNKNOWN"; break;
    }
    snprintf(buf, sizeof(buf), "Dir:   %-13s", dir_str);
    LCD4_SetCursor(2, 0);
    LCD4_PrintString(buf);

    /* Row 3 — Click */
    snprintf(buf, sizeof(buf), "Click: %-13s", j->click ? "PRESSED" : "OPEN");
    LCD4_SetCursor(3, 0);
    LCD4_PrintString(buf);
}

/* USER CODE END 0 */

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

  /* USER CODE BEGIN 2 */
  ULOG_Init(&huart2);
  ULOG_Info("MAIN", "main", "Boot OK");

  if (LCD4_Init(&hi2c1) != LCD4_OK)
  {
      ULOG_Error("MAIN", "main", "LCD4 init failed");
      Error_Handler();
  }
  ULOG_Info("MAIN", "main", "LCD4 init OK");

  /* Load custom corner chars into CGRAM before first use */
  LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TL, bmp_tl);
  LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_TR, bmp_tr);
  LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BL, bmp_bl);
  LCD4_DefineCustomChar(LCD4_CUSTOM_CORNER_BR, bmp_br);

  if (LCD2_Init(&hi2c1) != LCD2_OK)
  {
      ULOG_Error("MAIN", "main", "LCD2 init failed");
      Error_Handler();
  }
  LCD2_Clear();
  ULOG_Info("MAIN", "main", "LCD2 init OK");

  JOY_Init(&hadc1, JOYSTICK_CLICK_GPIO_Port, JOYSTICK_CLICK_Pin);
  ULOG_Info("MAIN", "main", "Joystick init OK");

  LCD4_Clear();
  ULOG_Info("MAIN", "main", "Entering loop");

  uint8_t box_active = 0;  /* 0 = joystick data view, 1 = box view */
  uint8_t prev_click = 0;  /* previous click state for edge detection */

  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    JOY_Data j = JOY_Read();

    if (j.status == JOY_ERROR)
    {
        ULOG_Error("JOY", "Read", "ADC read failed");
    }
    else
    {
        /* Direction string — used by both LCD2 and UART */
        const char *dir_str;
        switch (j.direction)
        {
            case JOY_UP:     dir_str = "UP      "; ULOG_Info("JOY", "Read", "Direction: UP");      break;
            case JOY_DOWN:   dir_str = "DOWN    "; ULOG_Info("JOY", "Read", "Direction: DOWN");    break;
            case JOY_LEFT:   dir_str = "LEFT    "; ULOG_Info("JOY", "Read", "Direction: LEFT");    break;
            case JOY_RIGHT:  dir_str = "RIGHT   "; ULOG_Info("JOY", "Read", "Direction: RIGHT");   break;
            case JOY_CENTRE: dir_str = "CENTRE  "; ULOG_Info("JOY", "Read", "Direction: CENTRE");  break;
            case JOY_NONE:   dir_str = "NONE    "; ULOG_Info("JOY", "Read", "Direction: NONE");    break;
            default:         dir_str = "UNKNOWN "; ULOG_Info("JOY", "Read", "Direction: UNKNOWN"); break;
        }

        /* LCD2: direction on row 0, always updated */
        LCD2_SetCursor(0, 0);
        LCD2_PrintString(dir_str);

        /* Detect rising edge of click: was open, now pressed */
        uint8_t click_edge = (j.click && !prev_click);
        prev_click = j.click;

        if (click_edge)
        {
            box_active = !box_active;

            LCD4_Clear();

            if (box_active)
            {
                draw_box();
                ULOG_Info("MAIN", "loop", "Box ON");
            }
            else
            {
                display_joystick_lcd4(&j);
                ULOG_Info("MAIN", "loop", "Box OFF");
            }
        }
        else if (!box_active)
        {
            /* Normal mode: refresh joystick data on LCD4 every loop */
            display_joystick_lcd4(&j);
        }
        /* Box active + no click: do nothing, preserve box on LCD4 */
    }

    HAL_Delay(100);

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
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
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
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = JOYSTICK_CLICK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(JOYSTICK_CLICK_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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
