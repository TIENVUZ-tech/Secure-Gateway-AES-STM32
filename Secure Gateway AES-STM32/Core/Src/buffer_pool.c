#include "buffer_pool.h"

// Static memory
static PacketBuffer pool_buffers[BUFFER_COUNT];
static BufferStatus pool_status[BUFFER_COUNT];

void BufferPool_Init() {
	// Initial status
	for (int i = 0; i < BUFFER_COUNT; i++) {
		pool_status[i] = BUFFER_FREE;
		pool_buffers[i].length = 0;
		pool_buffers[i].source_spi = 0;
	}
}

PacketBuffer* BufferPool_Acquire() {
	PacketBuffer *result = NULL;

	taskENTER_CRITICAL();
//	if (osMutexWait(pool_mutex, 10) == osOK) {
		for (int i = 0; i < BUFFER_COUNT; i++) {
			if (pool_status[i] == BUFFER_FREE) {
				pool_status[i] = BUFFER_IN_USE;
//				pool_buffers[i].length = 0;
//				pool_buffers[i].source_spi = 0;
				result = &pool_buffers[i];
				break;
			}
		}
//		osMutexRelease(pool_mutex);
//	}
		taskEXIT_CRITICAL();
		if (result != NULL) {
			result->length = 0;
			result->source_spi = 0;
		}
	return result;
}

void BufferPool_Release(PacketBuffer* buffer) {
	if (buffer == NULL) return;

//	osMutexWait(pool_mutex, osWaitForever);
	taskENTER_CRITICAL();
	for (int i = 0; i < BUFFER_COUNT; i++) {
		if (buffer == &pool_buffers[i]) {
			pool_status[i] = BUFFER_FREE;
			break;
		}
	}
	taskEXIT_CRITICAL();
//	osMutexRelease(pool_mutex);
}
