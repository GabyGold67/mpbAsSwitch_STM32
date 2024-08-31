/**
  ******************************************************************************
  * @file	: 11_SldrDALtchMPBttn_1e.cpp
  * @brief  : Example for the ButtonToSwitch for STM32 library SldrDALtchMPBttn class
  *
  * The test instantiates a SldrDALtchMPBttn object using:
  * 	- The Nucleo board user pushbutton attached to GPIO_B00
  * 	- The Nucleo board user LED attached to GPIO_A05 to visualize the isOn attribute flag status
  * 	- A digital output to GPIO_PC00 to visualize the _isEnabled attribute flag status
  * 	- A digital output to GPIO_B13 to visualize the "Task While MPB is On" activity
  * 	- A digital output to GPIO_B14 to visualize the FnWhnTrnOn() and FnWhnTrnOff() activity
  *
  * ### This example creates three Tasks, a timer, and two dedicated functions:
  *
  * - The first task instantiates the TmLtchMPBttn object in it and checks it's
  * attribute flags locally through the getters methods.
  *
  * - The second task is created and blocked before the scheduler is started. It's purpose it's to
  * manage the loads and resources that the switch turns On and Off, in this example case are the
  * output level of some GPIO pins.
  * When a change in the object's output attribute flags is detected the second task is unblocked
  * through a xTaskNotify() to update the output GPIO pins and blocks again until next notification.
  * The xTaskNotify() macro is limited to pass a 32 bit notifications value, the object takes care
  * of encoding of the MPBttn state in a 32 bits value.
  * A function, **otptsSttsUnpkg()**, is provided for the notified task to be able to decode the 32 bits
  * notification value into flag values.
  *
  * - The third task is started and blocked, like the second, it's purpose is to execute while the MPB
  * is in "isOn state". Please read the library documentation regarding the consequences of executing
  * a task that is resumed and paused externally and without previous alert!!
  *
  * In this example the "Secondary Mode" that simulates the behavior of a slider potentiometer
  * produces a variation in the PWM output of a GPIO pin, showing as a change of intensity of
  * the on-board led. The isOn attribute flag status enables the pin output, while the curOtpVal
  * attribute will set the PWM parameters of that same pin.
  *
  * A software timer is created so that it periodically toggles the isEnabled attribute flag
  * value, showing the behavior of the instantiated object when enabled and when disabled.
  *
  * - The first functions is to be executed when the MPB enters the "isOn state", please refer to the
  * **setFnWhnTrnOnPtr()** method for details.
  *
  * - The second functions is to be executed when the MPB enters the "isOff state", please refer to the
  * **setFnWhnTrnOffPtr()** method for details.
  *
  * 	@author	: Gabriel D. Goldman
  *
  * 	@date	: 	01/01/2024 First release
  * 				11/06/2024 Last update
  *
  ******************************************************************************
  * @attention	This file is part of the Examples folder for the ButtonToSwitch for STM32
  * library. All files needed are provided as part of the source code for the library.
  ******************************************************************************
  */
//----------------------- BEGIN Specific to use STM32F4xxyy testing platform
#define MCU_SPEC
//======================> Replace the following two lines with the files corresponding with the used STM32 configuration files
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"
//----------------------- End Specific to use STM32F4xxyy testing platform

/* Private includes ----------------------------------------------------------*/
//===========================>> Next lines used to avoid CMSIS wrappers
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
//===========================>> Previous lines used to avoid CMSIS wrappers
/* USER CODE BEGIN Includes */
#include "../../ButtonToSwitch_STM32/src/ButtonToSwitch_STM32.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
TIM_HandleTypeDef htim2;
gpioPinId_t tstMpbOnBoard{GPIOC, GPIO_PIN_13};	// Pin 0b 0010 0000 0000 0000
gpioPinId_t tstLedOnBoard{GPIOA, GPIO_PIN_5};	// Pin 0b 0000 0000 0010 0000
gpioPinId_t ledIsEnabled{GPIOC, GPIO_PIN_0};		// Pin 0b 0000 0000 0000 0001
gpioPinId_t ledTskWhlOn{GPIOB, GPIO_PIN_13};		// Pin 0b 0010 0000 0000 0000
gpioPinId_t ledFnTrnOnOff{GPIOB, GPIO_PIN_14};	// Pin 0b 0100 0000 0000 0000

TaskHandle_t mainCtrlTskHndl {NULL};
TaskHandle_t dmpsOutputTskHdl;
TaskHandle_t dmpsActWhlOnTskHndl;
BaseType_t xReturned;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
void Error_Handler(void);
// Init functions related to PWM timer
static void MX_TIM2_Init(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* USER CODE BEGIN FP */
// Tasks
void mainCtrlTsk(void *pvParameters);
void dmpsOutputTsk(void *pvParameters);
void dmpsActWhlOnTsk(void *pvParameters);
// SW Timers Callbacks
void swpEnableCb(TimerHandle_t  pvParam);
// Functions
MpbOtpts_t otptsSttsUnpkg(uint32_t pkgOtpts);
void fnExecTrnOn();
void fnExecTrnOff();
/* USER CODE END FP */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);	//WPM output for PA05: on-board green LED ============>> Turns PWM generation On

  /* Create the thread(s) */
  /* USER CODE BEGIN RTOS_THREADS */
  xReturned = xTaskCreate(
		  mainCtrlTsk, //taskFunction
		  "MainControlTask", //Task function legible name
		  256, // Stack depth in words
		  NULL,	//Parameters to pass as arguments to the taskFunction
		  configTIMER_TASK_PRIORITY,	//Set to the same priority level as the software timers
		  &mainCtrlTskHndl);
  if(xReturned != pdPASS)
	  Error_Handler();

  xReturned = xTaskCreate(
		  dmpsOutputTsk,
		  "DMpSwitchOutputUpd",
		  256,
		  NULL,
		  configTIMER_TASK_PRIORITY,
		  &dmpsOutputTskHdl
		  );
  if(xReturned != pdPASS)
	  Error_Handler();

  xReturned = xTaskCreate(
		  dmpsActWhlOnTsk,
		  "ExecWhileOnTask",
		  256,
		  NULL,
		  configTIMER_TASK_PRIORITY,
		  &dmpsActWhlOnTskHndl
		  );
  if(xReturned != pdPASS)
	  Error_Handler();
/* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN BEFORE STARTING SCHEDULER: SETUPS, OBJECTS CREATION, ETC. */
	vTaskSuspend(dmpsOutputTskHdl);	//Holds the task to start them all in proper order
	vTaskSuspend(dmpsActWhlOnTskHndl);
/* USER CODE END BEFORE STARTING SCHEDULER: SETUPS, OBJECTS CREATION, ETC. */

  /* Start scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  while (1)
  {
  }
}
/* USER CODE BEGIN */
void mainCtrlTsk(void *pvParameters)
{
	TimerHandle_t enableSwpTmrHndl{NULL};
	BaseType_t tmrModRslt{pdFAIL};

	SldrDALtchMPBttn tstBttn(tstMpbOnBoard.portId, tstMpbOnBoard.pinNum, true, true, 50, 100);
	DblActnLtchMPBttn* tstBttnPtr {&tstBttn};

	tstBttn.setScndModActvDly(2000);
	tstBttn.setSldrDirDn();
	tstBttn.setSwpDirOnPrss(true);
	tstBttn.setOtptValMin(0);
	tstBttn.setOtptValMax(2000);
	tstBttn.setOtptSldrStpSize(1);
	tstBttn.setOtptCurVal(1000);

	enableSwpTmrHndl = xTimerCreate(
			"isEnabledSwapTimer",
			15000,
			pdTRUE,
			tstBttnPtr,
			swpEnableCb
			);
	if (enableSwpTmrHndl != NULL){
      tmrModRslt = xTimerStart(enableSwpTmrHndl, portMAX_DELAY);
   }
	if(tmrModRslt == pdFAIL){
	    Error_Handler();
	}

	vTaskResume(dmpsOutputTskHdl);	//Resumes the task to start now in proper order
	tstBttn.setTaskToNotify(dmpsOutputTskHdl);
	tstBttn.setTaskWhileOn(dmpsActWhlOnTskHndl);
	tstBttn.setFnWhnTrnOnPtr(&fnExecTrnOn);
	tstBttn.setFnWhnTrnOffPtr(&fnExecTrnOff);

	tstBttn.begin(5);

	for(;;)
	{
	}
}

void dmpsOutputTsk(void *pvParameters){
	uint32_t mpbSttsRcvd{0};
	MpbOtpts_t mpbCurStateDcdd;

	for(;;){
		xReturned = xTaskNotifyWait(
						0x00,	//uint32_t ulBitsToClearOnEntry
		            0xFFFFFFFF,	//uint32_t ulBitsToClearOnExit,
		            &mpbSttsRcvd,	// uint32_t *pulNotificationValue,
						portMAX_DELAY//TickType_t xTicksToWait
		);
		 if (xReturned != pdPASS){
			 Error_Handler();
		 }
		mpbCurStateDcdd = otptsSttsUnpkg(mpbSttsRcvd);

		if(mpbCurStateDcdd.isOn)
			__HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_1, mpbCurStateDcdd.otptCurVal);
		else
			__HAL_TIM_SET_COMPARE(&htim2,TIM_CHANNEL_1, 0);

		if(mpbCurStateDcdd.isEnabled)
			HAL_GPIO_WritePin(ledIsEnabled.portId, ledIsEnabled.pinNum, GPIO_PIN_RESET);
		else
			HAL_GPIO_WritePin(ledIsEnabled.portId, ledIsEnabled.pinNum, GPIO_PIN_SET);
	}
}

void dmpsActWhlOnTsk(void *pvParameters){
	const unsigned long int swapTimeMs{250};
	unsigned long int strtTime{0};
	unsigned long int curTime{0};
	unsigned long int elapTime{0};
	bool blinkOn {false};

	strtTime = xTaskGetTickCount() / portTICK_RATE_MS;
	for(;;){
		curTime = (xTaskGetTickCount() / portTICK_RATE_MS);
		elapTime = curTime - strtTime;
		if (elapTime > swapTimeMs){
			blinkOn = !blinkOn;
			if(blinkOn)
				HAL_GPIO_WritePin(ledTskWhlOn.portId, ledTskWhlOn.pinNum, GPIO_PIN_SET);
			else
				HAL_GPIO_WritePin(ledTskWhlOn.portId, ledTskWhlOn.pinNum, GPIO_PIN_RESET);
			strtTime = curTime;
		}
	}

}

void swpEnableCb(TimerHandle_t  pvParam){
	DbncdMPBttn* bttnArg = (LtchMPBttn*) pvTimerGetTimerID(pvParam);

	bool curEnable = bttnArg->getIsEnabled();

	if(curEnable)
		bttnArg->disable();
	else
		bttnArg->enable();

  return;
}
/* USER CODE END */

/* USER CODE FUNCTIONS BEGIN */
void fnExecTrnOn(){
	HAL_GPIO_WritePin(ledFnTrnOnOff.portId, ledFnTrnOnOff.pinNum, GPIO_PIN_SET);

	return;
}

void fnExecTrnOff(){
	HAL_GPIO_WritePin(ledFnTrnOnOff.portId, ledFnTrnOnOff.pinNum, GPIO_PIN_RESET);

	return;
}
/* USER CODE FUNCTIONS END */

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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

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
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  * @brief TIM2 Initialization Function
  *
  * Needed to generate PWM output for PA05: on-board green, Timmer2, Channel1
  *
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)	//myProjectResource
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 840-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 2000-1;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */
  HAL_TIM_Base_Start(&htim2);
  /* USER CODE END TIM2_Init 2 */

  HAL_TIM_MspPostInit(&htim2);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : tstMpbOnBoard_Pin */
  GPIO_InitStruct.Pin = tstMpbOnBoard.pinNum;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(tstMpbOnBoard.portId, &GPIO_InitStruct);

  /*Configure GPIO pin Output Level for ledIsEnabled))*/
  HAL_GPIO_WritePin(ledIsEnabled.portId, ledIsEnabled.pinNum, GPIO_PIN_RESET);
  /*Configure GPIO pin : ledIsEnabled_Pin */
  GPIO_InitStruct.Pin = ledIsEnabled.pinNum;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ledIsEnabled.portId, &GPIO_InitStruct);

  /*Configure GPIO pin Output Level for ledTskWhlOn*/
  HAL_GPIO_WritePin(ledTskWhlOn.portId, ledTskWhlOn.pinNum, GPIO_PIN_RESET);
  /*Configure GPIO pin : ledIsEnabled_Pin */
  GPIO_InitStruct.Pin = ledTskWhlOn.pinNum;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ledTskWhlOn.portId, &GPIO_InitStruct);

  /*Configure GPIO pin Output Level for ledFnTrnOnOff*/
  HAL_GPIO_WritePin(ledFnTrnOnOff.portId, ledFnTrnOnOff.pinNum, GPIO_PIN_RESET);
  /*Configure GPIO pin : ledFnTrnOnOff*/
  GPIO_InitStruct.Pin = ledFnTrnOnOff.pinNum;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ledFnTrnOnOff.portId, &GPIO_InitStruct);
}

/**
* @brief TIM_Base MSP Initialization
* This function configures the hardware resources used in this example
* @param htim_base: TIM_Base handle pointer
* @retval None
*/
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)
{
	if(htim_base->Instance==TIM2)	//myProjectResource
	{
		/* USER CODE BEGIN TIM2_MspInit 0 */
		/* USER CODE END TIM2_MspInit 0 */

		/* Peripheral clock enable */
		__HAL_RCC_TIM2_CLK_ENABLE();
	  /* USER CODE BEGIN TIM2_MspInit 1 */
	  /* USER CODE END TIM2_MspInit 1 */
	}
}

void HAL_TIM_MspPostInit(TIM_HandleTypeDef* htim)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

	if(htim->Instance==TIM2)
  {
  /* USER CODE BEGIN TIM2_MspPostInit 0 */
  /* USER CODE END TIM2_MspPostInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**TIM2 GPIO Configuration
    PA5     ------> TIM2_CH1
    */
    GPIO_InitStruct.Pin = tstLedOnBoard.pinNum;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(tstLedOnBoard.portId, &GPIO_InitStruct);
  }
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM9 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM9) {
    HAL_IncTick();
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

#ifdef  USE_FULL_ASSERT
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
