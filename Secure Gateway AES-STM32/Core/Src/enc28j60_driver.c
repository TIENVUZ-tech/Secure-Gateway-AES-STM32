#include "enc28j60_driver.h"

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;

//  next_packet_ptr=0,  current_bank=0xFF (Start ENC28J60_SetBank for the first time)
ENC28J60_Config spi1 = {&hspi1, GPIOA, GPIO_PIN_4, 0, GPIOA, GPIO_PIN_2, 0xFF};
ENC28J60_Config spi2 = {&hspi2, GPIOB, GPIO_PIN_12, 0, GPIOA, GPIO_PIN_3, 0xFF};

static uint8_t ENC28J60_ReadOp(ENC28J60_Config *spi, uint8_t opcode, uint8_t address) {
    uint8_t result = 0;
    uint8_t header = opcode | (address & ADDR_MASK);

    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(spi->hspi, &header, 1, 5);

    // MAC/MII registers need to remove a dummy byte
    if (address & DUMMY_MASK) {
        HAL_SPI_Receive(spi->hspi, &result, 1, 5);
    }
    HAL_SPI_Receive(spi->hspi, &result, 1, 5);

    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);
    return result;
}

static void ENC28J60_WriteOp(ENC28J60_Config *spi, uint8_t opcode, uint8_t address, uint8_t data) {
    uint8_t header = opcode | (address & ADDR_MASK);

    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(spi->hspi, &header, 1, 5);
    HAL_SPI_Transmit(spi->hspi, &data,   1, 5);
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);
}

static void ENC28J60_SetBank(ENC28J60_Config *spi, uint8_t address) {
    uint8_t bank_bits = (address & BANK_MASK) >> 5;

    if (bank_bits != spi->current_bank) {
        ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_BSEL1 | ECON1_BSEL0);
        ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON1, bank_bits);
        spi->current_bank = bank_bits;
    }
}

static uint8_t ENC28J60_ReadReg(ENC28J60_Config *spi, uint8_t address) {
	uint8_t data;

    ENC28J60_SetBank(spi, address);
    data = ENC28J60_ReadOp(spi, ENC28J60_READ_CTRL_REG, address);

    return data;
}

uint8_t ENC28J60_ReadRegGlo(ENC28J60_Config *spi, uint8_t address) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 5) : osMutexWait(spi2_mutex, 5);
	if (status != osOK) {
		return 0;
	}
	uint8_t data = ENC28J60_ReadReg(spi, address);
	spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
	return data;
}

static void ENC28J60_WriteReg(ENC28J60_Config *spi, uint8_t address, uint8_t data) {
    ENC28J60_SetBank(spi, address);
    ENC28J60_WriteOp(spi, ENC28J60_WRITE_CTRL_REG, address, data);
}

static void ENC28J60_WritePhy(ENC28J60_Config *spi, uint8_t address, uint16_t data) {
    ENC28J60_WriteReg(spi, MIREGADR, address);
    ENC28J60_WriteReg(spi, MIWRL, data & 0xFF);
    ENC28J60_WriteReg(spi, MIWRH, data >> 8);

    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadReg(spi, MISTAT) & MISTAT_BUSY) {
    	if (HAL_GetTick() - t0 > 20) return;
    }
}

uint16_t ENC28J60_ReadPhy(ENC28J60_Config *spi, uint8_t address) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 5) : osMutexWait(spi2_mutex, 5);
    if (status != osOK) {
    	return 0xFFFF;
    }
	ENC28J60_WriteReg(spi, MIREGADR, address);
    ENC28J60_WriteReg(spi, MICMD, MICMD_MIIRD);

    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadReg(spi, MISTAT) & MISTAT_BUSY) {
    	if (HAL_GetTick() - t0 > 10) break;
    }
    ENC28J60_WriteReg(spi, MICMD, 0x00);

    uint16_t data = ENC28J60_ReadReg(spi, MIRDL);
    data |= (uint16_t)ENC28J60_ReadReg(spi, MIRDH) << 8;

    spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
    return data;
}

void ENC28J60_ClearErrors(ENC28J60_Config *spi) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 5) : osMutexWait(spi2_mutex, 5);
	if (status != osOK) {
		return;
	}
	if (ENC28J60_ReadReg(spi, EIR) & EIR_RXERIF) {
		ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_CLR, EIR, EIR_RXERIF);
	}
	spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
}

void ENC28J60_Init(ENC28J60_Config *spi, uint8_t *mac_address) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 10) : osMutexWait(spi2_mutex, 10);
	if (status != osOK) {
		return;
	}
    // Hardware Reset
    HAL_GPIO_WritePin(spi->RST_Port, spi->RST_Pin, GPIO_PIN_RESET);
    osDelay(1);
    HAL_GPIO_WritePin(spi->RST_Port, spi->RST_Pin, GPIO_PIN_SET);
    osDelay(1);

    // Soft Reset
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
    uint8_t reset = ENC28J60_SYS_RST_CMD;
    HAL_SPI_Transmit(spi->hspi, &reset, 1, 5);
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

    // Wait for oscillator
    uint32_t t0 = HAL_GetTick();
    while (!(ENC28J60_ReadOp(spi, ENC28J60_READ_CTRL_REG, ESTAT) & ESTAT_CLKRDY)) {
        if (HAL_GetTick() - t0 > 10) return;  // timeout 10ms
    }

    // Initialize RX
    spi->next_packet_ptr = RX_START;

    // RX buffer: 0x0000 -> 0x19FF
    ENC28J60_WriteReg(spi, ERXSTL, RX_START & 0xFF);
    ENC28J60_WriteReg(spi, ERXSTH, RX_START >> 8);
    ENC28J60_WriteReg(spi, ERXNDL, RX_END & 0xFF);
    ENC28J60_WriteReg(spi, ERXNDH, RX_END >> 8);
    // ERXRDPT must be an odd number
    ENC28J60_WriteReg(spi, ERXRDPTL, RX_END & 0xFF);
    ENC28J60_WriteReg(spi, ERXRDPTH, RX_END >> 8);

    // Configure MAC
    // MARXEN = receive frame, TXPAUS+RXPAUS = flow control
    ENC28J60_WriteReg(spi, MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
    // MACON2=0 (take the MAC out of reset
    ENC28J60_WriteReg(spi, MACON2, 0x00);
    // Auto-padding 60B + CRC + frame length check
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX);

    // Max frame = 598 bytes
    ENC28J60_WriteReg(spi, MAMXFLL, MAX_FRAME_LEN & 0xFF);
    ENC28J60_WriteReg(spi, MAMXFLH, MAX_FRAME_LEN >> 8);

    // Inter-packet gap
    ENC28J60_WriteReg(spi, MABBIPG, 0x15);
    ENC28J60_WriteReg(spi, MAIPGL,  0x12);
    ENC28J60_WriteReg(spi, MAIPGH,  0x0C);

    // MAC address
    ENC28J60_WriteReg(spi, MAADR1, mac_address[0]);
    ENC28J60_WriteReg(spi, MAADR2, mac_address[1]);
    ENC28J60_WriteReg(spi, MAADR3, mac_address[2]);
    ENC28J60_WriteReg(spi, MAADR4, mac_address[3]);
    ENC28J60_WriteReg(spi, MAADR5, mac_address[4]);
    ENC28J60_WriteReg(spi, MAADR6, mac_address[5]);

    // PHY: full duplex and disable loopback
    ENC28J60_WritePhy(spi, PHCON1, PHCON1_PDPXMD);
    ENC28J60_WritePhy(spi, PHCON2, PHCON2_HDLDIS);
    // LED A = Link status, LED B = TX/RX activity, stretch 140ms
    ENC28J60_WritePhy(spi, PHLCON, 0x347A);

    // Receive filter: Unicast + Broadcast + CRC valid
    // ENC28J60_WriteReg(spi, ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);
    ENC28J60_WriteReg(spi, ERXFCON, ERXFCON_CRCEN);

    // Enable receive
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);

    // Enable interrupt: INTIE + PKTIE
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE | EIE_PKTIE);

    spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);

}

void ENC28J60_SendPacket(ENC28J60_Config *spi, uint8_t *packet_data, uint16_t length) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 10) : osMutexWait(spi2_mutex, 10);
	if (status != osOK) {
		return;
	}
    // Wait for the previous transmission to complete
    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadOp(spi, ENC28J60_READ_CTRL_REG, ECON1) & ECON1_TXRTS) {
        if (ENC28J60_ReadReg(spi, EIR) & EIR_TXERIF || (HAL_GetTick() - t0 > 10)) { // Transmit error interrupt flag bit
            ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRST);
            ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
            break;
        }
        ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);
    }

    // Clear TX error from last time
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);

    // Point EWRPT to the TX buffer
    ENC28J60_WriteReg(spi, EWRPTL, TX_START & 0xFF);
    ENC28J60_WriteReg(spi, EWRPTH, TX_START >> 8);

    // Write Per-Packet Control Byte
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
    uint8_t cmd = ENC28J60_WRITE_BUF_MEM;
    uint8_t ctrl = 0x00;  // Used configuration from MACON3
    HAL_SPI_Transmit(spi->hspi, &cmd, 1, 5);
    HAL_SPI_Transmit(spi->hspi, &ctrl, 1, 5);
    if (spi->hspi->Instance == SPI1) {
    	while (osSemaphoreWait(xSem_DMA_SPI1_TX_Done, 0) == osOK); // Clear old semaphore
    	HAL_SPI_Transmit_DMA(spi->hspi, packet_data, length);
    	if (osSemaphoreWait(xSem_DMA_SPI1_TX_Done, 5) != osOK) {
    		HAL_SPI_DMAStop(spi->hspi);
    		HAL_SPI_Abort(spi->hspi);
    		HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);
    		osMutexRelease(spi1_mutex);
    		return;
    	}
    } else {
    	while (osSemaphoreWait(xSem_DMA_SPI2_TX_Done, 0) == osOK);
    	HAL_SPI_Transmit_DMA(spi->hspi, packet_data, length);
    	if (osSemaphoreWait(xSem_DMA_SPI2_TX_Done, 5) != osOK) {
    		HAL_SPI_DMAStop(spi->hspi);
    		HAL_SPI_Abort(spi->hspi);
    		HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);
    		osMutexRelease(spi2_mutex);
    		return;
    	}
    }

    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

    // Set ETXST và ETXND
    ENC28J60_WriteReg(spi, ETXSTL, TX_START & 0xFF);
    ENC28J60_WriteReg(spi, ETXSTH, TX_START >> 8);

    uint16_t end_addr = TX_START + length;
    ENC28J60_WriteReg(spi, ETXNDL, end_addr & 0xFF);
    ENC28J60_WriteReg(spi, ETXNDH, end_addr >> 8);

    // Enable transmit
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

    spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
}

uint16_t ENC28J60_ReceivePacket(ENC28J60_Config *spi, uint8_t *pBuffer, uint16_t max_length) {
	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 10) : osMutexWait(spi2_mutex, 10);
	if (status != osOK) {
		return 0;
	}
	// Don't have any packet
    if (ENC28J60_ReadReg(spi, EPKTCNT) == 0) {
    	spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
    	return 0;
    }

    // Point the ERDPT to the current packet head
    ENC28J60_WriteReg(spi, ERDPTL, spi->next_packet_ptr & 0xFF);
    ENC28J60_WriteReg(spi, ERDPTH, spi->next_packet_ptr >> 8);

    // Read 6 bytes header
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
    uint8_t cmd = ENC28J60_READ_BUF_MEM;
    HAL_SPI_Transmit(spi->hspi, &cmd, 1, 5);

    uint8_t  header[6];
    HAL_SPI_Receive(spi->hspi, header, 6, 5);

    uint16_t next_ptr = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    uint16_t len = ((uint16_t)header[2] | ((uint16_t)header[3] << 8)) - 4; // Remove 4 bytes CRC
    uint16_t rxstat = (uint16_t)header[4] | ((uint16_t)header[5] << 8);

    // Check length and Receive Status Vector bit 7
    if (!(rxstat & 0x0080) || len > max_length) {
        HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

        if (next_ptr >= RX_START && next_ptr <= RX_END) {
        	goto release;
        } else {
        	// next_ptr is an invalid value
        	spi->next_packet_ptr = RX_START;
        	ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
        	spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
        	return 0;
        }
    }

    // Receive payload
    if (spi->hspi->Instance == SPI1) {
    	while (osSemaphoreWait(xSem_DMA_SPI1_RX_Done, 0) == osOK);
    	HAL_SPI_Receive_DMA(spi->hspi, pBuffer, len);
    	if (osSemaphoreWait(xSem_DMA_SPI1_RX_Done, 5) != osOK) {
    		HAL_SPI_DMAStop(spi->hspi);
    		HAL_SPI_Abort(spi->hspi);
    		HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

    		// Remove the error packet
    		spi->next_packet_ptr = next_ptr;
    		uint16_t erxrdpt = (next_ptr == RX_START) ? RX_END : next_ptr - 1;
    		ENC28J60_WriteReg(spi, ERXRDPTL, erxrdpt & 0xFF);
    		ENC28J60_WriteReg(spi, ERXRDPTH, erxrdpt >> 8);
    		ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
    		osMutexRelease(spi1_mutex);
    		return 0;
    	}
    } else {
    	while (osSemaphoreWait(xSem_DMA_SPI2_RX_Done, 0) == osOK);
    	HAL_SPI_Receive_DMA(spi->hspi, pBuffer, len);
    	if (osSemaphoreWait(xSem_DMA_SPI2_RX_Done, 5) != osOK) {
    		HAL_SPI_DMAStop(spi->hspi);
    		HAL_SPI_Abort(spi->hspi);
    		HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

    		// Remove the error packet
			spi->next_packet_ptr = next_ptr;
			uint16_t erxrdpt = (next_ptr == RX_START) ? RX_END : next_ptr - 1;
			ENC28J60_WriteReg(spi, ERXRDPTL, erxrdpt & 0xFF);
			ENC28J60_WriteReg(spi, ERXRDPTH, erxrdpt >> 8);
			ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
    		osMutexRelease(spi2_mutex);
    		return 0;
    	}
    }
    HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

release:
    // Update next_packet_ptr
    spi->next_packet_ptr = next_ptr;

    // Update ERXRDPT (must be an odd number)
    uint16_t erxrdpt = (next_ptr == RX_START) ? RX_END : next_ptr - 1;
    if (erxrdpt < RX_START || erxrdpt > RX_END) {
    	erxrdpt = RX_END;
    }
    ENC28J60_WriteReg(spi, ERXRDPTL, erxrdpt & 0xFF);
    ENC28J60_WriteReg(spi, ERXRDPTH, erxrdpt >> 8);

    // Decrease EPKTCNT — chip reports that it has finished processing 1 packet
    ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);

    uint16_t length = (!(rxstat & 0x0080) || len > max_length) ? 0 : len;

    spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);

    return length;
}

void ENC28J60_DropPacket(ENC28J60_Config *spi) {

	osStatus status = spi->hspi->Instance == SPI1 ? osMutexWait(spi1_mutex, 5) : osMutexWait(spi2_mutex, 5);
	if (status != osOK) {
		return;
	}
	ENC28J60_WriteReg(spi, ERDPTL, spi->next_packet_ptr & 0xFF);
	ENC28J60_WriteReg(spi, ERDPTH, spi->next_packet_ptr >> 8);
	uint8_t header[2];
	uint16_t next_packet_ptr;

	HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_RESET);
	uint8_t cmd = ENC28J60_READ_BUF_MEM;
	HAL_SPI_Transmit(spi->hspi, &cmd, 1, 5);
	HAL_SPI_Receive(spi->hspi, header, 2, 5);
	next_packet_ptr = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
	HAL_GPIO_WritePin(spi->NSS_Port, spi->NSS_Pin, GPIO_PIN_SET);

	uint16_t erxrdpt = RX_END;
	if (next_packet_ptr == RX_START) {
		erxrdpt = RX_END;
	} else if (next_packet_ptr > RX_START && next_packet_ptr <= RX_END) {
		erxrdpt = next_packet_ptr - 1;
	} else {
		next_packet_ptr = RX_START;
		erxrdpt = RX_END;
	}

	ENC28J60_WriteReg(spi, ERXRDPTL, erxrdpt & 0xFF);
	ENC28J60_WriteReg(spi, ERXRDPTH, erxrdpt >> 8);

	ENC28J60_WriteOp(spi, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);

	spi->next_packet_ptr = next_packet_ptr;

	spi->hspi->Instance == SPI1 ? osMutexRelease(spi1_mutex) : osMutexRelease(spi2_mutex);
}
