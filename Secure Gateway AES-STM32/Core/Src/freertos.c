/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "buffer_pool.h"
#include "enc28j60_driver.h"
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
/* USER CODE BEGIN Variables */
extern ENC28J60_Config spi1;
extern ENC28J60_Config spi2;

extern osMessageQId xRX_QueueHandle;
extern osMessageQId xTX_QueueHandle;

extern osSemaphoreId xSem_INT_SPI1;
extern osSemaphoreId xSem_INT_SPI2;
extern osSemaphoreId xSem_DMA_SPI1_Done;
extern osSemaphoreId xSem_DMA_SPI2_Done;
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
// EXTI Callback - automatically called by HAL when ENC28J60 pulls the INT pin to low to indicate a new packet
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if (GPIO_Pin == GPIO_PIN_0) {
		// Wake up vRX_SPI1_Task
		xSemaphoreGiveFromISR(xSem_INT_SPI1, &xHigherPriorityTaskWoken);
	} else if (GPIO_Pin == GPIO_PIN_1) {
		// Wake up vRX_SPI2_Task
		xSemaphoreGiveFromISR(xSem_INT_SPI2, &xHigherPriorityTaskWoken);
	}

	// Wake up the higher priority task
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
// The automatic callback is invoked when the DMA is complete
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if (hspi->Instance == SPI1) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI1_Done, &xHigherPriorityTaskWoken);
	} else if (hspi->Instance == SPI2) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI2_Done, &xHigherPriorityTaskWoken);
	}

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if (hspi->Instance == SPI1) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI1_Done, &xHigherPriorityTaskWoken);
	} else if (hspi->Instance == SPI2) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI2_Done, &xHigherPriorityTaskWoken);
	}

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if (hspi->Instance == SPI1) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI1_Done, &xHigherPriorityTaskWoken);
	} else if (hspi->Instance == SPI2) {
		xSemaphoreGiveFromISR(xSem_DMA_SPI2_Done, &xHigherPriorityTaskWoken);
	}

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
/* USER CODE END Application */

