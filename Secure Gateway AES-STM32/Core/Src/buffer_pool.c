#include "buffer_pool.h"

// Static memory
static PacketBuffer pool_buffers[BUFFER_COUNT];
static BufferStatus pool_status[BUFFER_COUNT];

// Mutex protects concurrent access
static osMutexId pool_mutex;

// Define static muxtex
osMutexDef(pool_mutex_def);

void BufferPool_Init() {
	// Initial status
	for (int i = 0; i < BUFFER_COUNT; i++) {
		pool_status[i] = BUFFER_FREE;
		pool_buffers[i].length = 0;
		pool_buffers[i].source_spi = 0;
	}

	// Create static mutex
	pool_mutex = osMutexCreate(osMutex(pool_mutex_def));
}

PacketBuffer* BufferPool_Acquire() {
	PacketBuffer *result = NULL;

	if (osMutexWait(pool_mutex, 0) == osOK) {
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

	osMutexWait(pool_mutex, osWaitForever);
	for (int i = 0; i < BUFFER_COUNT; i++) {
		if (buffer == &pool_buffers[i]) {
			pool_status[i] = BUFFER_FREE;
			break;
		}
	}
	osMutexRelease(pool_mutex);
}
