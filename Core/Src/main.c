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
/*
 * ============================================================================
 *  HAL LEARNING NOTES
 * ============================================================================
 *  Everything you add must go between "USER CODE BEGIN" / "USER CODE END"
 *  markers. CubeMX regenerates everything outside them and will erase your
 *  code otherwise.
 *
 *  HAL patterns you'll see everywhere:
 *    - Peripherals are controlled through a "handle" struct, e.g.
 *      UART_HandleTypeDef huart2. You pass &huart2 to every UART function.
 *    - Most functions return HAL_StatusTypeDef:
 *      HAL_OK, HAL_ERROR, HAL_BUSY or HAL_TIMEOUT. Check it.
 *    - Three flavours of I/O for most peripherals:
 *        HAL_xxx_Transmit()       blocking / polling (simplest)
 *        HAL_xxx_Transmit_IT()    interrupt driven, finishes in a callback
 *        HAL_xxx_Transmit_DMA()   DMA driven, finishes in a callback
 *    - Callbacks are "weak" functions in the HAL (e.g. HAL_GPIO_EXTI_Callback)
 *      that you override by defining a function with the same name.
 *
 *  Docs: each Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_*.c file starts
 *  with a "How to use this driver" section. Ctrl+click any HAL_ function.
 * ============================================================================
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define HEARTBEAT_PERIOD_MS   500U   /* LED blink half-period */
#define STATUS_PERIOD_MS      5000U  /* How often to print a status line */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* Button state */
#define BUTTON_RELEASED                    0U
#define BUTTON_PRESSED                     1U
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

__IO uint32_t BspButtonState = BUTTON_RELEASED;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* When 0, the heartbeat LED stops blinking. The blue USER button toggles it. */
static uint8_t heartbeatEnabled = 1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void UART_SendString(const char *str);
static void PrintBanner(void);
static void PrintStatus(void);
static void HandleSerialInput(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * UART_SendString — register-level version.
 *
 * Setup is still done by CubeMX's MX_USART2_UART_Init() (clock, pins,
 * baud rate). This function only replaces the *transmit* path.
 *
 * Reference: RM0390, USART chapter:
 *   - "Status register (USART_SR)"  -> the TXE and TC flags
 *   - "Data register (USART_DR)"
 *   - "Transmitter" / "Character transmission" section for the sequence
 */
static void UART_SendString(const char *str)
{
  if (str == NULL || *str == '\0')
  {
    return;  /* nothing to send */
  }

  while (*str != '\0')
  {
    while((USART2->SR & USART_SR_TXE) == 0)
    {
      // just wait
    }
    USART2->DR = (uint8_t) *str;

    str++;  /* move to the next character */
  }

  while((USART2->SR & USART_SR_TC) == 0) {
    // just wait
  }

}

/*
 * ---------------------------------------------------------------------------
 *  EXAMPLE 2: Redirecting printf() to the UART
 * ---------------------------------------------------------------------------
 *  printf -> newlib -> _write() in syscalls.c -> __io_putchar() (below).
 *  syscalls.c declares __io_putchar as "weak", so defining it here replaces
 *  the default and printf output goes to USART2.
 *
 *  Notes:
 *    - Use "\r\n" for line endings; most serial terminals need the \r.
 *    - %f is disabled by default in newlib-nano to save flash. To enable it,
 *      add the linker flag -u _printf_float.
 */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}

static void PrintBanner(void)
{
  /* Plain HAL transmit */
  UART_SendString("\r\n==============================\r\n");
  UART_SendString(" Tether - STM32 HAL playground\r\n");
  UART_SendString("==============================\r\n");

  /* The same thing with printf, which can also format numbers.
   * HAL_RCC_GetHCLKFreq() returns the CPU/AHB clock in Hz, as configured by
   * SystemClock_Config() (84 MHz here). */
  printf("Core clock : %lu Hz\r\n", HAL_RCC_GetHCLKFreq());
  printf("HAL version: 0x%08lX\r\n", HAL_GetHalVersion());
  printf("Device ID  : 0x%03lX, rev 0x%04lX\r\n", HAL_GetDEVID(), HAL_GetREVID());
  printf("\r\nType a key in the terminal:\r\n");
  printf("  h = help, s = status, l = toggle LED, b = toggle blinking\r\n");
  printf("  any other key is echoed back\r\n\r\n");
}

/*
 * ---------------------------------------------------------------------------
 *  EXAMPLE 3: Reading a GPIO input + reading the system tick
 * ---------------------------------------------------------------------------
 *  HAL_GPIO_ReadPin(port, pin) returns GPIO_PIN_SET (1) or GPIO_PIN_RESET (0).
 *  The Nucleo's blue USER button (PC13) is active-low: it reads RESET while
 *  it's held down.
 *
 *  HAL_GetTick() returns the number of milliseconds since HAL_Init(). The
 *  SysTick interrupt increments it every 1 ms.
 */
static void PrintStatus(void)
{
  GPIO_PinState button = HAL_GPIO_ReadPin(USER_BUTTON_GPIO_PORT, USER_BUTTON_PIN);
  GPIO_PinState led    = HAL_GPIO_ReadPin(LED2_GPIO_PORT, LED2_PIN);

  printf("[%8lu ms] button=%s led=%s blink=%s\r\n",
         HAL_GetTick(),
         (button == GPIO_PIN_RESET) ? "PRESSED" : "released",
         (led == GPIO_PIN_SET) ? "on" : "off",
         heartbeatEnabled ? "on" : "off");
}

/*
 * ---------------------------------------------------------------------------
 *  EXAMPLE 4: Receiving UART data by polling
 * ---------------------------------------------------------------------------
 *  HAL_UART_Receive(handle, buffer, length, timeout_ms)
 *    - With timeout 0 it checks once and returns immediately:
 *        HAL_OK      -> a byte was waiting and is now in rxByte
 *        HAL_TIMEOUT -> nothing received (normal, not an error)
 *    - This works fine if the main loop is fast. Bytes can be lost if the
 *      loop blocks for a long time.
 *
 *  The more robust approach is interrupt mode: HAL_UART_Receive_IT() plus
 *  a HAL_UART_RxCpltCallback() override. It needs "USART2 global interrupt"
 *  enabled in CubeMX (USART2 -> NVIC Settings), which this project doesn't
 *  have yet. That's a good next exercise.
 */
static void HandleSerialInput(void)
{
  uint8_t rxByte;

  if (HAL_UART_Receive(&huart2, &rxByte, 1, 0) != HAL_OK)
  {
    return; /* nothing arrived */
  }

  switch (rxByte)
  {
    case 'h':
      PrintBanner();
      break;

    case 's':
      PrintStatus();
      break;

    case 'l':
      /* EXAMPLE 5: Driving a GPIO output.
       * HAL_GPIO_TogglePin flips the pin. HAL_GPIO_WritePin sets it
       * explicitly: HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_SET). */
      HAL_GPIO_TogglePin(LED2_GPIO_PORT, LED2_PIN);
      printf("LED toggled\r\n");
      break;

    case 'b':
      heartbeatEnabled = !heartbeatEnabled;
      printf("Blinking %s\r\n", heartbeatEnabled ? "enabled" : "disabled");
      break;

    default:
      /* Echo the byte back so you can see what you typed */
      printf("You typed '%c' (0x%02X)\r\n", (rxByte >= 32 && rxByte < 127) ? rxByte : '?', rxByte);
      break;
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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* By the time we get here HAL_Init(), the clock setup and every MX_xxx_Init()
   * have run, so USART2 is ready to use.
   *
   * Turn off stdout buffering so every printf goes out immediately instead of
   * waiting for a newline or a full buffer. */
  setvbuf(stdout, NULL, _IONBF, 0);

  PrintBanner();
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED2);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* USER CODE BEGIN BSP */

  /* -- Sample board code to switch on leds ---- */
  BSP_LED_On(LED2);

  /* USER CODE END BSP */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  /* Timestamps for the non-blocking timers below */
  uint32_t lastHeartbeatTick = HAL_GetTick();
  uint32_t lastStatusTick    = HAL_GetTick();

  while (1)
  {
    /*
     * -------------------------------------------------------------------------
     *  EXAMPLE 6: Handling an interrupt (button press)
     * -------------------------------------------------------------------------
     *  Pressing the button fires the EXTI15_10 interrupt. stm32f4xx_it.c calls
     *  HAL_GPIO_EXTI_IRQHandler(), which calls HAL_GPIO_EXTI_Callback() near
     *  the bottom of this file, which sets BspButtonState.
     *
     *  Rule of thumb: keep interrupt callbacks tiny (set a flag), then do the
     *  real work here in the main loop. That's why the flag is declared __IO
     *  (volatile): it's shared between interrupt and main-loop code.
     */
    if (BspButtonState == BUTTON_PRESSED)
    {
      BspButtonState = BUTTON_RELEASED;
      heartbeatEnabled = !heartbeatEnabled;
      printf("Button pressed -> blinking %s\r\n", heartbeatEnabled ? "enabled" : "disabled");
    }

    /*
     * -------------------------------------------------------------------------
     *  EXAMPLE 7: Non-blocking timing with HAL_GetTick()
     * -------------------------------------------------------------------------
     *  HAL_Delay(500) would freeze the whole loop, so no UART input or button
     *  handling for 500 ms. Instead, check whether enough time has passed and
     *  keep looping. The subtraction (now - last) still works when the 32-bit
     *  tick counter wraps around after ~49 days.
     */
    uint32_t now = HAL_GetTick();

    if (heartbeatEnabled && (now - lastHeartbeatTick >= HEARTBEAT_PERIOD_MS))
    {
      lastHeartbeatTick = now;
      HAL_GPIO_TogglePin(LED2_GPIO_PORT, LED2_PIN);
    }

    if (now - lastStatusTick >= STATUS_PERIOD_MS)
    {
      lastStatusTick = now;
      PrintStatus();
    }

    /* Check the UART for a received character (EXAMPLE 4) */
    HandleSerialInput();

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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
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
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief EXTI line detection callbacks
  * @param GPIO_Pin: Specifies the pins connected EXTI line
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if(GPIO_Pin==USER_BUTTON_PIN)
  {
    BspButtonState = BUTTON_PRESSED;
  }
}

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
