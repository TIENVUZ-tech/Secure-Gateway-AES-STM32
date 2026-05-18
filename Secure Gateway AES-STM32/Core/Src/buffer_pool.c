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

	if (osMutexWait(pool_mutex, 10) == osOK) {
		for (int i = 0; i < BUFFER_COUNT; i++) {
			if (pool_status[i] == BUFFER_FREE) {
				pool_status[i] = BUFFER_IN_USE;
				pool_buffers[i].length = 0;
				pool_buffers[i].source_spi = 0;
				result = &pool_buffers[i];
				break;
			}
		}
		osMutexRelease(pool_mutex);
	}
	return result;
}

void BufferPool_Release(PacketBuffer* buffer) {
	if (buffer == NULL) return;

	intptr_t index = buffer - pool_buffers;

	if (index >= 0 && index < BUFFER_COUNT) {
		osMutexWait(pool_mutex, osWaitForever);
		pool_buffers[index].length = 0;
		pool_buffers[index].source_spi = 0;
		pool_status[index] = BUFFER_FREE;
		osMutexRelease(pool_mutex);
	}
}

uint8_t BufferPool_FreeCount(void) {
	uint8_t count = 0;
	if (osMutexWait(pool_mutex, 5) == osOK) {
		for (int i = 0; i < BUFFER_COUNT; i++) {
			if (pool_status[i] == BUFFER_FREE) count++;
		}
		osMutexRelease(pool_mutex);
	}
	return count;
}
