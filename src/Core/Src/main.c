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

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
IWDG_HandleTypeDef hiwdg;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
uint8_t TxBuffer[PROTO_LARGO_ENTRADAS];
uint8_t RxBuffer;
volatile bool spiRxReady = false;
volatile bool spiTxReady = false;

uint16_t entradasD[8] ={
  ENT_DIG_1,
  ENT_DIG_2,
  ENT_DIG_3,
  ENT_DIG_4,
  ENT_DIG_5,
  ENT_DIG_6,
  ENT_DIG_7,
  ENT_DIG_8
};
estAct_t estAct;
uint8_t RxMod = 0x00;
volatile uint16_t adcBuffer[2];
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Protocolo con el CPU: ver induproto.h y docs/PROTOCOLO.md */

/* Watchdog: si el firmware se cuelga mas de ~250 ms, el micro se reinicia.
 * LSI ~40 kHz / 64 = 625 Hz -> 156 cuentas = 250 ms (166..375 ms segun el LSI). */
static void iwdg_init(void)
{
  __HAL_DBGMCU_FREEZE_IWDG();   /* el watchdog se detiene con el depurador en pausa */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 156;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
}

static uint8_t leer_digitales(void)
{
  uint8_t dig = 0;
  for (int i = 0; i < 4; i++)
  {
    estAct.modId[i] = HAL_GPIO_ReadPin(GPIOA, entradasD[i]);
  }
  for (int i = 4; i < 8; i++)
  {
    estAct.modId[i] = HAL_GPIO_ReadPin(GPIOB, entradasD[i]);
  }
  for (int i = 0; i < 8; i++)
  {
    dig |= (uint8_t)((estAct.modId[i] ? 1 : 0) << i);
  }
  return dig;
}

/* NSS (chip select) del SPI1: PA4. Se puede leer como GPIO aunque lo use el SPI. */
#define NSS_PORT GPIOA
#define NSS_PIN  GPIO_PIN_4

/* Contadores para verificar la comunicacion (se pueden ver con el depurador). */
volatile uint32_t tramasOk = 0;
volatile uint32_t tramasError = 0;

static bool nss_activo(void)
{
  return HAL_GPIO_ReadPin(NSS_PORT, NSS_PIN) == GPIO_PIN_RESET;
}

/* Espera a que el CPU termine la trama (NSS en alto) y deja el SPI limpio
 * para la siguiente: asi un error nunca se arrastra a la trama que sigue. */
static void spi_fin_de_trama(void)
{
  uint32_t t0 = HAL_GetTick();
  while (nss_activo() && (HAL_GetTick() - t0) < 5) {
  }
  __HAL_SPI_DISABLE(&hspi1);
  (void)hspi1.Instance->DR;   // descarta un byte recibido a medias
  (void)hspi1.Instance->SR;   // junto con la lectura de DR, limpia el overrun (OVR)
  __HAL_SPI_ENABLE(&hspi1);
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

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  HAL_ADCEx_Calibration_Start(&hadc1);
  iwdg_init();
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adcBuffer, 2); // primera lectura analogica

  /* La deteccion del modulo ya no se hace solo al arrancar: el loop responde
   * al comando 0x48 en cualquier momento. Asi, si el CPU o este modulo se
   * reinician por separado, se pueden volver a detectar. */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 1. Esperar a que el CPU seleccione este modulo (NSS en bajo) */
    while (!nss_activo()) {
      HAL_IWDG_Refresh(&hiwdg);
    }

    /* 2. Recibir el comando y responder */
    uint8_t cmd = 0;
    if (HAL_SPI_Receive(&hspi1, &cmd, 1, 2) != HAL_OK)
    {
      cmd = 0;   // no llego el comando: se cuenta como error mas abajo
    }

    if (cmd == PROTO_CMD_DETECCION)
    {
      proto_armar_deteccion(PROTO_ID_ENTRADAS, TxBuffer);
      if (HAL_SPI_Transmit(&hspi1, TxBuffer, PROTO_LARGO_DETECCION, 2) == HAL_OK) {
        tramasOk++;
      } else {
        tramasError++;
      }
    }
    else if (cmd == PROTO_CMD_LEER)
    {
      proto_datos_t d;
      d.dig = leer_digitales();
      d.ana[0] = estAct.modIa[AN_1];
      d.ana[1] = estAct.modIa[AN_2];
      proto_armar_datos(PROTO_CMD_LEER, &d, TxBuffer);

      /* La respuesta se carga apenas llega el comando; el CPU espera
       * PROTO_PAUSA_US antes de leerla. */
      if (HAL_SPI_Transmit(&hspi1, TxBuffer, PROTO_LARGO_ENTRADAS, 2) == HAL_OK) {
        tramasOk++;
      } else {
        tramasError++;
      }

      /* Nueva conversion del ADC para la proxima lectura */
      HAL_ADC_Stop_DMA(&hadc1);
      HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adcBuffer, 2);
    }
    else
    {
      tramasError++;   // timeout o comando desconocido
    }

    /* 3. Esperar el fin de la trama y dejar el SPI limpio */
    spi_fin_de_trama();
    HAL_IWDG_Refresh(&hiwdg);
  }

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
}
  /* USER CODE END 3 */


  void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
      /* Rank 1 = canal 9 (PB1, InAn1), rank 2 = canal 8 (PB0, InAn2). 12 bits. */
      estAct.modIa[AN_1] = adcBuffer[0] & 0x0FFF;
      estAct.modIa[AN_2] = adcBuffer[1] & 0x0FFF;
    }
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  /* InAn1 de la bornera esta en PB1 (canal 9) y InAn2 en PB0 (canal 8).
   * Se convierte primero InAn1 para que adcBuffer[0] = AN_1. */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;//estaba en hard
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PB12 PB13 PB14 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;   /* la placa tiene pull-up externo de 10k */
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA9 PA10 PA11 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;   /* la placa tiene pull-up externo de 10k */
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
