#ifndef INC_BUFFER_POOL_H_
#define INC_BUFFER_POOL_H_

#include "cmsis_os.h"
#include "stddef.h"
#include "stdint.h"

#define BUFFER_COUNT 10
#define BUFFER_SIZE 614

extern osMutexId pool_mutex;

typedef enum {
	BUFFER_FREE = 0,
	BUFFER_IN_USE = 1
} BufferStatus;

typedef struct {
	uint8_t data[BUFFER_SIZE]; // Static memory
	uint8_t source_spi; // 1 = SPI1, 2 = SPI2
	uint16_t length; // Length of the data
} PacketBuffer;

void BufferPool_Init(void);

PacketBuffer* BufferPool_Acquire(void);

void BufferPool_Release(PacketBuffer *buffer);

#endif /* INC_BUFFER_POOL_H_ */
