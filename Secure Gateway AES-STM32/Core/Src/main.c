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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "enc28j60_driver.h"
#include "buffer_pool.h"
#include "aes.h"
#include "string.h"
#include "FreeRTOS.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
	uint8_t ip[4];
	uint8_t key[16];
} IP_Key_Map;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
const IP_Key_Map key_table[] = {
		{{10, 0, 0, 10}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}},
		{{10, 0, 0, 20}, {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0}}
};

#define KEY_TABLE_SIZE (sizeof(key_table)/sizeof(IP_Key_Map))

#define PAYLOAD_MAX_SIZE 512

#define TASK_ALIVE_RX1 (1 << 0)
#define TASK_ALIVE_RX2 (1 << 1)
#define TASK_ALIVE_AES (1 << 2)
#define TASK_ALIVE_TX  (1 << 3)

 #define TASK_ALIVE_ALL (TASK_ALIVE_RX1 | TASK_ALIVE_RX2 | TASK_ALIVE_AES | TASK_ALIVE_TX)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi2_rx;
DMA_HandleTypeDef hdma_spi2_tx;

osThreadId defaultTaskHandle;
osThreadId vRX_SPI1_TaskHandle;
uint32_t vRX_SPI1_TaskBuffer[ 256 ];
osStaticThreadDef_t vRX_SPI1_TaskControlBlock;
osThreadId vRX_SPI2_TaskHandle;
uint32_t vRX_SPI2_TaskBuffer[ 256 ];
osStaticThreadDef_t vRX_SPI2_TaskControlBlock;
osThreadId vTX_TaskHandle;
uint32_t vTX_TaskBuffer[ 256 ];
osStaticThreadDef_t vTX_TaskControlBlock;
osThreadId vPacket_Processing_TaskHandle;
uint32_t vPacket_Processing_TaskBuffer[ 512 ];
osStaticThreadDef_t vPacket_Processing_TaskControlBlock;
osThreadId vHearbeat_TaskHandle;
uint32_t vHearbeat_TaskBuffer[ 128 ];
osStaticThreadDef_t vHearbeat_TaskControlBlock;
osMessageQId xRX_QueueHandle;
uint8_t xRX_QueueBuffer[ 6 * sizeof( uint32_t ) ];
osStaticMessageQDef_t xRX_QueueControlBlock;
osMessageQId xTX_QueueHandle;
uint8_t xTX_QueueBuffer[ 4 * sizeof( uint32_t ) ];
osStaticMessageQDef_t xTX_QueueControlBlock;
/* USER CODE BEGIN PV */
// Semaphore
osSemaphoreId xSem_DMA_SPI1_Done;
osSemaphoreId xSem_DMA_SPI2_Done;
osSemaphoreId xSem_INT_SPI1;
osSemaphoreId xSem_INT_SPI2;

// Mutex
osMutexId spi1_mutex;
osMutexId spi2_mutex;
//osMutexId pool_mutex;

// Alive flag
volatile uint8_t task_alive_flags = 0;
volatile uint8_t is_init_done = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
static void MX_IWDG_Init(void);
void StartDefaultTask(void const * argument);
void vRX_SPI1_TaskFunc(void const * argument);
void vRX_SPI2_TaskFunc(void const * argument);
void vTX_TaskFunc(void const * argument);
void vPacket_Processing_TaskFunc(void const * argument);
void vHeartbeat_TaskFunc(void const * argument);

/* USER CODE BEGIN PFP */
void RX_HandlePacket(ENC28J60_Config *spi, uint8_t source_spi);
void Update_Ip_Checksum(PacketBuffer *packet, uint8_t ip_header_length);
void User_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern ENC28J60_Config spi1;
extern ENC28J60_Config spi2;

// Local MAC address
uint8_t mac1_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
uint8_t mac2_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};

uint8_t link_down_count_spi1 = 0;
uint8_t link_down_count_spi2 = 0;

void RX_HandlePacket(ENC28J60_Config *spi, uint8_t source_spi) {
	// Request buffer from pool (if the pool is full, skip packet)
	PacketBuffer *buffer = BufferPool_Acquire();
	if (buffer == NULL) {
		// Read and discard packet from the chip
//		static uint8_t packet[BUFFER_SIZE];
//		ENC28J60_ReceivePacket(spi, packet, BUFFER_SIZE);
		ENC28J60_DropPacket(spi);
		return;
	}

	// Read the packet from ENC28J60 to buffer
	uint16_t length = ENC28J60_ReceivePacket(spi, buffer->data, BUFFER_SIZE);
	if (length < 42) {
		BufferPool_Release(buffer);
		return;
	}

	// Write metadata into buffer
	buffer->length = length;
	buffer->source_spi = source_spi;

	// Place the buffer pointer into xRX_Queue
	if (osMessagePut(xRX_QueueHandle, (uint32_t)buffer, 0) != osOK) {
		BufferPool_Release(buffer);
		return;
	}
}

void Update_Ip_Checksum(PacketBuffer *packet, uint8_t ip_header_length) {
	  uint16_t udp_length = packet->length - 14 - ip_header_length;
	  uint16_t udp_length_offset = 14 + ip_header_length + 4;
	  // Update UDP Length
	  packet->data[udp_length_offset] = (udp_length >> 8) & 0xFF;
	  packet->data[udp_length_offset + 1] = udp_length & 0xFF;

	  // Update the new total length into the header (offset 16-17)
	  uint16_t total_length = packet->length - 14; // Skip Ethernet header
	  packet->data[16] = (total_length >> 8) & 0xFF;
	  packet->data[17] = total_length & 0xFF;

	  // Remove the old Checksum (offset 24-25)
	  packet->data[24] = 0x00;
	  packet->data[25] = 0x00;

	  // Calculate sum (16-bit)
	  uint32_t sum = 0;
	  uint16_t ip_header_end = ip_header_length + 14;
	  for (int i = 14; i < ip_header_end; i += 2) {
		  sum += (packet->data[i] << 8) | packet->data[i + 1];
	  }

	  while (sum >> 16) {
		  sum = (sum & 0xFFFF) + (sum >> 16);
	  }

	  uint16_t final_checksum = (uint16_t)(~sum);
	  packet->data[24] = (final_checksum >> 8) & 0xFF;
	  packet->data[25] = final_checksum & 0xFF;

	  // UDP Checksum (Set to 0 to bypass)
	  uint16_t udp_checksum_offset = 14 + ip_header_length + 6;
	  packet->data[udp_checksum_offset] = 0x00;
	  packet->data[udp_checksum_offset + 1] = 0x00;
}

void User_Init() {
	// Initialize ENC28J60 1 and 2
	ENC28J60_Init(&spi1, mac1_addr);
	HAL_IWDG_Refresh(&hiwdg);

	ENC28J60_Init(&spi2, mac2_addr);
	HAL_IWDG_Refresh(&hiwdg);

	BufferPool_Init();
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
  MX_SPI1_Init();
  MX_SPI2_Init();
  // MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  osMutexDef(spi1_mutex_def);
  osMutexDef(spi2_mutex_def);
  //osMutexDef(pool_mutex_def);

  spi1_mutex = osMutexCreate(osMutex(spi1_mutex_def));
  spi2_mutex = osMutexCreate(osMutex(spi2_mutex_def));
  //pool_mutex = osMutexCreate(osMutex(pool_mutex_def));

  // Mutex protects concurrent access


  // Define static muxtex
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  osSemaphoreDef(xSem_DMA_SPI1_Done_Def);
  osSemaphoreDef(xSem_DMA_SPI2_Done_Def);
  osSemaphoreDef(xSem_INT_SPI1_Def);
  osSemaphoreDef(xSem_INT_SPI2_Def);

  xSem_DMA_SPI1_Done = osSemaphoreCreate(osSemaphore(xSem_DMA_SPI1_Done_Def), 1);
  xSem_DMA_SPI2_Done = osSemaphoreCreate(osSemaphore(xSem_DMA_SPI2_Done_Def), 1);
  xSem_INT_SPI1 = osSemaphoreCreate(osSemaphore(xSem_INT_SPI1_Def), 1);
  xSem_INT_SPI2 = osSemaphoreCreate(osSemaphore(xSem_INT_SPI2_Def), 1);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* definition and creation of xRX_Queue */
  osMessageQStaticDef(xRX_Queue, 6, uint32_t, xRX_QueueBuffer, &xRX_QueueControlBlock);
  xRX_QueueHandle = osMessageCreate(osMessageQ(xRX_Queue), NULL);

  /* definition and creation of xTX_Queue */
  osMessageQStaticDef(xTX_Queue, 4, uint32_t, xTX_QueueBuffer, &xTX_QueueControlBlock);
  xTX_QueueHandle = osMessageCreate(osMessageQ(xTX_Queue), NULL);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityLow, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of vRX_SPI1_Task */
  osThreadStaticDef(vRX_SPI1_Task, vRX_SPI1_TaskFunc, osPriorityHigh, 0, 256, vRX_SPI1_TaskBuffer, &vRX_SPI1_TaskControlBlock);
  vRX_SPI1_TaskHandle = osThreadCreate(osThread(vRX_SPI1_Task), NULL);

  /* definition and creation of vRX_SPI2_Task */
  osThreadStaticDef(vRX_SPI2_Task, vRX_SPI2_TaskFunc, osPriorityHigh, 0, 256, vRX_SPI2_TaskBuffer, &vRX_SPI2_TaskControlBlock);
  vRX_SPI2_TaskHandle = osThreadCreate(osThread(vRX_SPI2_Task), NULL);

  /* definition and creation of vTX_Task */
  osThreadStaticDef(vTX_Task, vTX_TaskFunc, osPriorityAboveNormal, 0, 256, vTX_TaskBuffer, &vTX_TaskControlBlock);
  vTX_TaskHandle = osThreadCreate(osThread(vTX_Task), NULL);

  /* definition and creation of vPacket_Processing_Task */
  osThreadStaticDef(vPacket_Processing_Task, vPacket_Processing_TaskFunc, osPriorityHigh, 0, 512, vPacket_Processing_TaskBuffer, &vPacket_Processing_TaskControlBlock);
  vPacket_Processing_TaskHandle = osThreadCreate(osThread(vPacket_Processing_Task), NULL);

  /* definition and creation of vHearbeat_Task */
  osThreadStaticDef(vHearbeat_Task, vHeartbeat_TaskFunc, osPriorityLow, 0, 128, vHearbeat_TaskBuffer, &vHearbeat_TaskControlBlock);
  vHearbeat_TaskHandle = osThreadCreate(osThread(vHearbeat_Task), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */

  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {

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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
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
}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 1874;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

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
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
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
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(HEART_BEAT_GPIO_Port, HEART_BEAT_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RST_SPI1_Pin|RST_SPI2_Pin|NSS_SPI1_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(NSS_SPI2_GPIO_Port, NSS_SPI2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : HEART_BEAT_Pin */
  GPIO_InitStruct.Pin = HEART_BEAT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HEART_BEAT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : INT_SPI1_Pin INT_SPI2_Pin */
  GPIO_InitStruct.Pin = INT_SPI1_Pin|INT_SPI2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RST_SPI1_Pin RST_SPI2_Pin NSS_SPI1_Pin */
  GPIO_InitStruct.Pin = RST_SPI1_Pin|RST_SPI2_Pin|NSS_SPI1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : NSS_SPI2_Pin */
  GPIO_InitStruct.Pin = NSS_SPI2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(NSS_SPI2_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_vRX_SPI1_TaskFunc */
/**
* @brief Function implementing the vRX_SPI1_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vRX_SPI1_TaskFunc */
void vRX_SPI1_TaskFunc(void const * argument)
{
  /* USER CODE BEGIN vRX_SPI1_TaskFunc */
	while (!is_init_done) {
		osDelay(10);
	}
  /* Infinite loop */
  for(;;)
  {
	  // Report to vHeartbeat_Task
	  taskENTER_CRITICAL();
	  task_alive_flags |= TASK_ALIVE_RX1;
	  taskEXIT_CRITICAL();

	  // Wait for interrupt signal from the ENC28J60 1
	  osSemaphoreWait(xSem_INT_SPI1, 50);

	  /* There may be multiple packets in the ENC28J60 buffer -> Read maximum 6 packets before waiting for the next interrupt
	   * Because the ENC28J60 only sends an interrupt even if there are many packets
	   */
	  uint8_t rx_count = 0;
	  while ((ENC28J60_ReadRegGlo(&spi1, EPKTCNT) > 0) && rx_count < 6) {
		  RX_HandlePacket(&spi1, 1);
		  rx_count++;
	  }
	  ENC28J60_ClearErrors(&spi1);
    osDelay(1);
  }
  /* USER CODE END vRX_SPI1_TaskFunc */
}

/* USER CODE BEGIN Header_vRX_SPI2_TaskFunc */
/**
* @brief Function implementing the vRX_SPI2_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vRX_SPI2_TaskFunc */
void vRX_SPI2_TaskFunc(void const * argument)
{
  /* USER CODE BEGIN vRX_SPI2_TaskFunc */
	while (!is_init_done) {
		osDelay(10);
	}
  /* Infinite loop */
  for(;;)
  {
	  taskENTER_CRITICAL();
	  task_alive_flags |= TASK_ALIVE_RX2;
	  taskEXIT_CRITICAL();

	  osSemaphoreWait(xSem_INT_SPI2, 50);

	  uint8_t rx_count = 0;
	  while ((ENC28J60_ReadRegGlo(&spi2, EPKTCNT) > 0) && rx_count < 6) {
		  RX_HandlePacket(&spi2, 2);
		  rx_count++;
	  }

	  ENC28J60_ClearErrors(&spi2);
    osDelay(1);
  }
  /* USER CODE END vRX_SPI2_TaskFunc */
}

/* USER CODE BEGIN Header_vTX_TaskFunc */
/**
* @brief Function implementing the vTX_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vTX_TaskFunc */
void vTX_TaskFunc(void const * argument)
{
  /* USER CODE BEGIN vTX_TaskFunc */
	while (!is_init_done) {
		osDelay(10);
	}
	osEvent event;
	PacketBuffer *packet;
  /* Infinite loop */
  for(;;)
  {
	  taskENTER_CRITICAL();
	  task_alive_flags |= TASK_ALIVE_TX;
	  taskEXIT_CRITICAL();

	  event = osMessageGet(xTX_QueueHandle, 50);
	  if (event.status == osEventMessage) {
		  packet = (PacketBuffer*)event.value.p;

		  if (packet->source_spi == 1) {
			  ENC28J60_SendPacket(&spi2, packet->data, packet->length);
		  } else if (packet->source_spi == 2) {
			  ENC28J60_SendPacket(&spi1, packet->data, packet->length);
		  }

		  BufferPool_Release(packet);
	  }
  }
  /* USER CODE END vTX_TaskFunc */
}

/* USER CODE BEGIN Header_vPacket_Processing_TaskFunc */
/**
* @brief Function implementing the vPacket_Processing_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vPacket_Processing_TaskFunc */
void vPacket_Processing_TaskFunc(void const * argument)
{
  /* USER CODE BEGIN vPacket_Processing_TaskFunc */
	while (!is_init_done) {
		osDelay(10);
	}
	osEvent event;
	PacketBuffer *packet;
	AES_ctx precomputed_ctx[KEY_TABLE_SIZE];
	uint8_t iv[16] = {0};
	for (uint8_t i = 0; i < KEY_TABLE_SIZE; i++) {
		AES_init_ctx_iv(&precomputed_ctx[i], key_table[i].key, iv);
	}
  /* Infinite loop */
  for(;;)
  {
	  taskENTER_CRITICAL();
	  task_alive_flags |= TASK_ALIVE_AES;
	  taskEXIT_CRITICAL();

	  event = osMessageGet(xRX_QueueHandle, 50);
	  if (event.status == osEventMessage) {
		  packet = (PacketBuffer*)event.value.p;

		  // Step 1: Parse Ethernet and IP header
		  // Read Ethernet type (offset 12-13)
		  uint16_t ethernet_type = (packet->data[12] << 8) | packet->data[13];

		  // Read Flags and Fragment offset (offset 20-21)
		  uint16_t flag_offset = (packet->data[20] << 8) | packet->data[21];

		  // Read protocol (offset 23)
		  uint8_t ip_protocol = packet->data[23];

		  // Forward ARP packets
		  if (ethernet_type == 0x0806) {
		      if (osMessagePut(xTX_QueueHandle, (uint32_t)packet, 0) != osOK) {
		          BufferPool_Release(packet);
		      }
		      continue;
		  } else if (ethernet_type == 0x0800) { // IPv4
			  uint16_t real_ip_length = (packet->data[16] << 8) | packet->data[17];
			  if (14 + real_ip_length < packet->length) {
				  packet->length = 14 + real_ip_length;
			  }

			  // Accept ICMP packets (ping)
			  if (ip_protocol == 0x01) {
				  if (osMessagePut(xTX_QueueHandle, (uint32_t)packet, 0) != osOK) {
					  BufferPool_Release(packet);
				  }
				  continue;
			  }

			  // Remove packet if it is not a UDP packet or fragment
			  if (ip_protocol != 0x11 || (flag_offset & 0x3FFF) != 0) {
				  BufferPool_Release(packet);
				  continue;
			  }
		  } else {
			  BufferPool_Release(packet);
			  continue;
		  }

		  // Read IHL (Internet Header Length)
		  uint8_t ip_header_length = (packet->data[14] & 0x0F) * 4;
		  // Calculate the length of UDP header (Ethernet header (14), IP header, UDP header)
		  uint16_t udp_payload_offset = 14 + ip_header_length + 8;

		  if ((packet->length - udp_payload_offset) > PAYLOAD_MAX_SIZE || udp_payload_offset >= packet->length) {
			  BufferPool_Release(packet);
			  continue;
		  }

		  // Step 2: Determine IP key by source
		  uint8_t *source_ip = &packet->data[26];
		  uint8_t *dest_ip = &packet->data[30];

		  int8_t found_key_index = -1;
		  uint8_t dest_key_found = 0;
		  for (uint8_t i = 0; i < KEY_TABLE_SIZE; i++) {
			  if (memcmp(source_ip, key_table[i].ip, 4) == 0) {
				  found_key_index = i;
				  break;
			  }
		  }

		  for (uint8_t i = 0; i < KEY_TABLE_SIZE; i++) {
			  if (memcmp(dest_ip, key_table[i].ip, 4) == 0) {
				  dest_key_found = 1;
				  break;
			  }
		  }

		  if (found_key_index == -1 || dest_key_found == 0) {
			  BufferPool_Release(packet);
			  continue;
		  }

		  // Step 3: Encrypt payload
//		  uint8_t iv[16] = {0}; // static Initialization Vector
//		  AES_init_ctx_iv(&ctx, found_key, iv);
		  AES_ctx_set_iv(&precomputed_ctx[found_key_index], iv);
		  AES_CBC_PKCS7_Encrypt(&precomputed_ctx[found_key_index], packet, udp_payload_offset);

		  // Step 4: Recalculate checksum and length
		  Update_Ip_Checksum(packet, ip_header_length);

		  // Step 5: Put into the xTX_Queue
		  if (osMessagePut(xTX_QueueHandle, (uint32_t)packet, 0) != osOK) {
			  BufferPool_Release(packet);
		  }
	  }
  }
  /* USER CODE END vPacket_Processing_TaskFunc */
}

/* USER CODE BEGIN Header_vHeartbeat_TaskFunc */
/**
* @brief Function implementing the vHearbeat_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_vHeartbeat_TaskFunc */
void vHeartbeat_TaskFunc(void const * argument)
{
  /* USER CODE BEGIN vHeartbeat_TaskFunc */
	// HAL_IWDG_Refresh(&hiwdg);
	User_Init();
	MX_IWDG_Init();
	HAL_IWDG_Refresh(&hiwdg);

	is_init_done = 1;
  /* Infinite loop */
  for(;;)
  {
	  // Check the status of the Tasks
	  if ((task_alive_flags & TASK_ALIVE_ALL) == TASK_ALIVE_ALL) {
		  HAL_IWDG_Refresh(&hiwdg);
		  taskENTER_CRITICAL();
		  task_alive_flags = 0;
		  taskEXIT_CRITICAL();
	  }

	  // Blink heart led (PC13)
	  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	  HAL_IWDG_Refresh(&hiwdg);

	  // Monitor the Link status of Module 1 (SPI1)
	  uint16_t phstat1_1 = ENC28J60_ReadPhy(&spi1, PHSTAT1);
	  if (!(phstat1_1 & PHSTAT1_LLSTAT)) { // Link down
		  link_down_count_spi1++;
		  if (link_down_count_spi1 >= 8) { // Link down 4s
			  ENC28J60_Init(&spi1, mac1_addr);
			  HAL_IWDG_Refresh(&hiwdg);
			  link_down_count_spi1 = 0;
		  }
	  } else {
		  link_down_count_spi1 = 0; // Link up
	  }


	  // Monitor the Link status of Module 2 (SPI2)
	  uint16_t phstat1_2 = ENC28J60_ReadPhy(&spi2, PHSTAT1);
	  if (!(phstat1_2 & PHSTAT1_LLSTAT)) { // Link down
		  link_down_count_spi2++;
		  if (link_down_count_spi2 >= 8) {
			  ENC28J60_Init(&spi2, mac2_addr);
			  HAL_IWDG_Refresh(&hiwdg);
			  link_down_count_spi2 = 0;
		  }
	  } else {
		  link_down_count_spi2 = 0;
	  }

    osDelay(500);
  }
  /* USER CODE END vHeartbeat_TaskFunc */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
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
