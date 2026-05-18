#include "enc28j60_driver.h"

#define ENC28J60_DMA_DUMMY_LEN 614U
#define ENC28J60_TX_MAX_LEN 614U
#define ENC28J60_TX_TIMEOUT_MS 50U
#define ENC28J60_TX_RETRIES 5U

//  next_packet_ptr=0,  current_bank=0xFF (Start ENC28J60_SetBank for the first time)
ENC28J60_Config dev1 = {&hspi1, GPIOA, GPIO_PIN_4, 0, GPIOA, GPIO_PIN_2, 0xFF};
ENC28J60_Config dev2 = {&hspi2, GPIOB, GPIO_PIN_12, 0, GPIOA, GPIO_PIN_3, 0xFF};
static uint8_t dummy_tx[ENC28J60_DMA_DUMMY_LEN] = {0};

volatile uint32_t diag_enc_next_ptr = 0;
volatile uint32_t diag_enc_rx_len = 0;
volatile uint32_t diag_enc_rxstat = 0;
volatile uint32_t diag_enc_fail_reason = 0;
volatile uint32_t diag_enc_tx_abort = 0;
volatile uint32_t diag_enc_tx_timeout = 0;
volatile uint32_t diag_enc_tx_fail_reason = 0;

static void ENC28J60_ResetTxLogic(ENC28J60_Config *dev) {
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRST);
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);

    // Fix errata B1
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);
}

uint8_t ENC28J60_ReadOp(ENC28J60_Config *dev, uint8_t opcode, uint8_t address) {
    uint8_t result = 0;
    uint8_t header = opcode | (address & ADDR_MASK);
    uint8_t dummy = 0x00;

    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &header, 1, 100);

    // MAC/MII registers need to remove a dummy byte
    if (address & DUMMY_MASK) {
        HAL_SPI_TransmitReceive(dev->hspi, &dummy, &result, 1, 100);
    }
    HAL_SPI_TransmitReceive(dev->hspi, &dummy, &result, 1, 100);

    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    return result;
}

void ENC28J60_WriteOp(ENC28J60_Config *dev, uint8_t opcode, uint8_t address, uint8_t data) {
    uint8_t header = opcode | (address & ADDR_MASK);

    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &header, 1, 100);
    HAL_SPI_Transmit(dev->hspi, &data,   1, 100);
    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
}

void ENC28J60_SetBank(ENC28J60_Config *dev, uint8_t address) {
    uint8_t bank_bits = (address & BANK_MASK) >> 5;

    if (bank_bits != dev->current_bank) {
        ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_BSEL1 | ECON1_BSEL0);
        ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, bank_bits);
        dev->current_bank = bank_bits;
    }
}

uint8_t ENC28J60_ReadReg(ENC28J60_Config *dev, uint8_t address) {
	uint8_t data;

    ENC28J60_SetBank(dev, address);
    data = ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, address);

    return data;
}

void ENC28J60_WriteReg(ENC28J60_Config *dev, uint8_t address, uint8_t data) {
    ENC28J60_SetBank(dev, address);
    ENC28J60_WriteOp(dev, ENC28J60_WRITE_CTRL_REG, address, data);
}

void ENC28J60_WritePhy(ENC28J60_Config *dev, uint8_t address, uint16_t data) {
    ENC28J60_WriteReg(dev, MIREGADR, address);
    ENC28J60_WriteReg(dev, MIWRL, data & 0xFF);
    ENC28J60_WriteReg(dev, MIWRH, data >> 8);

    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadReg(dev, MISTAT) & MISTAT_BUSY) {
    	if (HAL_GetTick() - t0 > 20) return;
    }
}

uint16_t ENC28J60_ReadPhy(ENC28J60_Config *dev, uint8_t address) {
    ENC28J60_WriteReg(dev, MIREGADR, address);
    ENC28J60_WriteReg(dev, MICMD, MICMD_MIIRD);

    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadReg(dev, MISTAT) & MISTAT_BUSY) {
    	if (HAL_GetTick() - t0 > 10) break;
    }
    ENC28J60_WriteReg(dev, MICMD, 0x00);

    uint16_t data = ENC28J60_ReadReg(dev, MIRDL);
    data |= (uint16_t)ENC28J60_ReadReg(dev, MIRDH) << 8;

    return data;
}

void ENC28J60_ClearErrors(ENC28J60_Config *dev) {
	if (ENC28J60_ReadReg(dev, EIR) & EIR_RXERIF) {
		ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_CLR, EIR, EIR_RXERIF);
	}
}

uint8_t ENC28J60_Init(ENC28J60_Config *dev, uint8_t *mac_address) {
    // Hardware Reset
    HAL_GPIO_WritePin(dev->RST_Port, dev->RST_Pin, GPIO_PIN_RESET);
    osDelay(10);
    HAL_GPIO_WritePin(dev->RST_Port, dev->RST_Pin, GPIO_PIN_SET);
    osDelay(10);
    dev->current_bank = 0xFF;

    // Soft Reset
    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
    uint8_t reset = ENC28J60_SYS_RST_CMD;
    HAL_SPI_Transmit(dev->hspi, &reset, 1, 100);
    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    // Wait for oscillator
    uint32_t t0 = HAL_GetTick();
    while (!(ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, ESTAT) & ESTAT_CLKRDY)) {
        if (HAL_GetTick() - t0 > 10) {
            return 0;  // timeout 10ms
        }
    }

    // Initialize RX
    dev->next_packet_ptr = RX_START;

    // RX buffer: 0x0000 -> 0x19FF
    ENC28J60_WriteReg(dev, ERXSTL, RX_START & 0xFF);
    ENC28J60_WriteReg(dev, ERXSTH, RX_START >> 8);
    ENC28J60_WriteReg(dev, ERXNDL, RX_END & 0xFF);
    ENC28J60_WriteReg(dev, ERXNDH, RX_END >> 8);
    // ERXRDPT must be an odd number
    ENC28J60_WriteReg(dev, ERXRDPTL, RX_END & 0xFF);
    ENC28J60_WriteReg(dev, ERXRDPTH, RX_END >> 8);

    // Configure MAC (MARXEN = receive frame, TXPAUS+RXPAUS = flow control)
    ENC28J60_WriteReg(dev, MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);

    // Auto-padding 60B + CRC + frame length check.
    ENC28J60_WriteReg(dev, MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN);

    ENC28J60_WriteReg(dev, MAMXFLL, MAX_FRAME_LEN & 0xFF);
    ENC28J60_WriteReg(dev, MAMXFLH, MAX_FRAME_LEN >> 8);

    // Inter-packet gap
    ENC28J60_WriteReg(dev, MABBIPG, 0x12);
	ENC28J60_WriteReg(dev, MAIPGL,  0x12);
	ENC28J60_WriteReg(dev, MAIPGH,  0x0C);
	ENC28J60_WriteReg(dev, MACLCON1, 0x0F);
	ENC28J60_WriteReg(dev, MACLCON2, 0x37);

    // MAC address
    ENC28J60_WriteReg(dev, MAADR1, mac_address[0]);
    ENC28J60_WriteReg(dev, MAADR2, mac_address[1]);
    ENC28J60_WriteReg(dev, MAADR3, mac_address[2]);
    ENC28J60_WriteReg(dev, MAADR4, mac_address[3]);
    ENC28J60_WriteReg(dev, MAADR5, mac_address[4]);
    ENC28J60_WriteReg(dev, MAADR6, mac_address[5]);

    ENC28J60_WritePhy(dev, PHCON1, 0x0000);

    // Disable internal TX loopback so the bridge does not receive its own frames.
    ENC28J60_WritePhy(dev, PHCON2, PHCON2_HDLDIS);

    // LED A = Link status, LED B = TX/RX activity, stretch 140ms
    ENC28J60_WritePhy(dev, PHLCON, 0x347A);

    // Accept every frame with a valid CRC.
//    ENC28J60_WriteReg(dev, ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);
    ENC28J60_WriteReg(dev, ERXFCON, ERXFCON_CRCEN);

    // Enable receive
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);

    // Clear old interrupt flags
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_CLR, EIR, 0xFF);

    // Enable interrupt: INTIE + PKTIE
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE | EIE_PKTIE);

    return 1;
}

uint8_t ENC28J60_SendPacket(ENC28J60_Config *dev, uint8_t *packet_data, uint16_t length) {
    if (length == 0 || length > ENC28J60_TX_MAX_LEN) {
        diag_enc_tx_fail_reason = 8;
        return 0;
    }

    // Wait for the previous transmission to complete
    uint32_t t0 = HAL_GetTick();
    while (ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, ECON1) & ECON1_TXRTS) {
        if (ENC28J60_ReadReg(dev, EIR) & EIR_TXERIF || (HAL_GetTick() - t0 > ENC28J60_TX_TIMEOUT_MS)) { // Transmit error interrupt flag bit
            diag_enc_tx_fail_reason = 1;
            break;
        }
    }

    // Start each frame from a clean TX state. This also clears stale TXABRT/TXERIF.
    ENC28J60_ResetTxLogic(dev);

    // Point EWRPT to the TX buffer
    ENC28J60_WriteReg(dev, EWRPTL, TX_START & 0xFF);
    ENC28J60_WriteReg(dev, EWRPTH, TX_START >> 8);

    // Write Per-Packet Control Byte
    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
    uint8_t cmd = ENC28J60_WRITE_BUF_MEM;
    uint8_t ctrl = 0x00;  // Used configuration from MACON3
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, 100);
    HAL_SPI_Transmit(dev->hspi, &ctrl, 1, 100);
    if (dev->hspi->Instance == SPI1) {
    	while (osSemaphoreWait(xSem_DMA_SPI1_Done, 0) == osOK); // Clear old semaphore
    	if (HAL_SPI_Transmit_DMA(dev->hspi, packet_data, length) != HAL_OK) {
    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		diag_enc_tx_fail_reason = 2;
    		return 0;
    	}

    	if (osSemaphoreWait(xSem_DMA_SPI1_Done, 50) != osOK) {
    		HAL_SPI_DMAStop(dev->hspi);
    		// Reset SPI
    		__HAL_SPI_DISABLE(dev->hspi);
    	    __HAL_SPI_ENABLE(dev->hspi);

    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		diag_enc_tx_fail_reason = 3;
    		return 0;
    	}
    } else {
    	while (osSemaphoreWait(xSem_DMA_SPI2_Done, 0) == osOK);
    	if (HAL_SPI_Transmit_DMA(dev->hspi, packet_data, length) != HAL_OK) {
    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		diag_enc_tx_fail_reason = 2;
    		return 0;
    	}

    	if (osSemaphoreWait(xSem_DMA_SPI2_Done, 50) != osOK) {
    		HAL_SPI_DMAStop(dev->hspi);
    		// Reset SPI
    		__HAL_SPI_DISABLE(dev->hspi);
    		__HAL_SPI_ENABLE(dev->hspi);

    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		diag_enc_tx_fail_reason = 3;
    		return 0;
    	}
    }

    t0 = HAL_GetTick();
    while (__HAL_SPI_GET_FLAG(dev->hspi, SPI_FLAG_BSY)) {
    	if (HAL_GetTick() - t0 > 10) {
    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		diag_enc_tx_fail_reason = 4;
    		return 0;
    	}
    }

    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

    // Set ETXST và ETXND
    ENC28J60_WriteReg(dev, ETXSTL, TX_START & 0xFF);
    ENC28J60_WriteReg(dev, ETXSTH, TX_START >> 8);

    uint16_t end_addr = TX_START + length;
    ENC28J60_WriteReg(dev, ETXNDL, end_addr & 0xFF);
    ENC28J60_WriteReg(dev, ETXNDH, end_addr >> 8);

    // Enable transmit
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

    // Silicon Errata #12
    uint8_t tx_ok = 0;
    for (uint8_t retry = 0; retry < ENC28J60_TX_RETRIES; retry++) {
		// Wait for TXRTS to clear (TX done or aborted), max 10ms (Ethernet frame <= 1.2ms @ 10Mbps)
		t0 = HAL_GetTick();
		while (ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, ECON1) & ECON1_TXRTS) {
			if (HAL_GetTick() - t0 > ENC28J60_TX_TIMEOUT_MS) break;
		}

		uint8_t tx_still_busy = ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, ECON1) & ECON1_TXRTS;
		uint8_t tx_aborted = ENC28J60_ReadOp(dev, ENC28J60_READ_CTRL_REG, ESTAT) & ESTAT_TXABRT;
		uint8_t tx_error = ENC28J60_ReadReg(dev, EIR) & EIR_TXERIF;
		if (!tx_still_busy && !tx_aborted && !tx_error) {
			tx_ok = 1;
			break;
		}

		if (tx_aborted || tx_error) {
			diag_enc_tx_abort++;
			diag_enc_tx_fail_reason = 5;
		} else {
			diag_enc_tx_timeout++;
			diag_enc_tx_fail_reason = 6;
		}

		if (retry + 1U < ENC28J60_TX_RETRIES) {
			// Reset TX logic (clears TXABRT)
			ENC28J60_ResetTxLogic(dev);
			// Reload TX pointers (may be corrupted after abort)
			ENC28J60_WriteReg(dev, ETXSTL, TX_START & 0xFF);
			ENC28J60_WriteReg(dev, ETXSTH, TX_START >> 8);
			ENC28J60_WriteReg(dev, ETXNDL, end_addr & 0xFF);
			ENC28J60_WriteReg(dev, ETXNDH, end_addr >> 8);
			// Retry TX — data still intact in ENC28J60 TX SRAM
			ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);
		}
	}

    if (!tx_ok) {
        ENC28J60_ResetTxLogic(dev);
    }

    return tx_ok;
}

uint16_t ENC28J60_ReceivePacket(ENC28J60_Config *dev, uint8_t *pBuffer, uint16_t max_length) {
	// Clear receive error (EIR_RXERIF bit)
	ENC28J60_ClearErrors(dev);

    // Don't have any packet
    if (ENC28J60_ReadReg(dev, EPKTCNT) == 0) {
    	diag_enc_fail_reason = 7;
    	return 0;
    }

    // Point the ERDPT to the current packet head
    ENC28J60_WriteReg(dev, ERDPTL, dev->next_packet_ptr & 0xFF);
    ENC28J60_WriteReg(dev, ERDPTH, dev->next_packet_ptr >> 8);

    // Read 6 bytes header
    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
    uint8_t cmd = ENC28J60_READ_BUF_MEM;
    HAL_SPI_Transmit(dev->hspi, &cmd, 1, 100);

    uint8_t  header[6];
    HAL_SPI_Receive(dev->hspi, header, 6, 100);

    uint16_t next_ptr = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    uint16_t len = ((uint16_t)header[2] | ((uint16_t)header[3] << 8)) - 4; // Remove 4 bytes CRC
    uint16_t rxstat = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
    diag_enc_next_ptr = next_ptr;
    diag_enc_rx_len = len;
    diag_enc_rxstat = rxstat;
    diag_enc_fail_reason = 0;

    // Verify next_ptr
    if (next_ptr < RX_START || next_ptr > RX_END) {
    	HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    	dev->next_packet_ptr = RX_START;
    	ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
    	diag_enc_fail_reason = 1;
    	return 0;
    }

    // Check length and Receive Status Vector bit 7
    if (!(rxstat & 0x0080)) {
        HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

		len = 0;
		diag_enc_fail_reason = 2;
		goto release;
    }

    if (len > max_length) {
        HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

		len = 0;
		diag_enc_fail_reason = 3;
		goto release;
    }

    if (len > sizeof(dummy_tx)) {
           HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

   		len = 0;
   		diag_enc_fail_reason = 4;
   		goto release;
    }

    // Receive payload
    if (dev->hspi->Instance == SPI1) {
    	while (osSemaphoreWait(xSem_DMA_SPI1_Done, 0) == osOK);
    	if (HAL_SPI_TransmitReceive_DMA(dev->hspi, dummy_tx, pBuffer, len) != HAL_OK) {
    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		len = 0;
    		diag_enc_fail_reason = 5;
    		goto release;
    	}
    	if (osSemaphoreWait(xSem_DMA_SPI1_Done, 50) != osOK) {
    		HAL_SPI_DMAStop(dev->hspi);
    		// Reset SPI
    		__HAL_SPI_DISABLE(dev->hspi);
    		__HAL_SPI_ENABLE(dev->hspi);

    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		len = 0;
    		diag_enc_fail_reason = 6;
    		goto release;
    	}
    } else {
    	while (osSemaphoreWait(xSem_DMA_SPI2_Done, 0) == osOK);
    	if (HAL_SPI_TransmitReceive_DMA(dev->hspi, dummy_tx, pBuffer, len) != HAL_OK) {
    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		len = 0;
    		diag_enc_fail_reason = 5;
    		goto release;
    	}

    	if (osSemaphoreWait(xSem_DMA_SPI2_Done, 50) != osOK) {
    		HAL_SPI_DMAStop(dev->hspi);
    		// Reset SPI
    		__HAL_SPI_DISABLE(dev->hspi);
    		__HAL_SPI_ENABLE(dev->hspi);

    		HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);
    		len = 0;
    		diag_enc_fail_reason = 6;
    		goto release;
    	}
    }

    uint32_t t0 = HAL_GetTick();
    while (__HAL_SPI_GET_FLAG(dev->hspi, SPI_FLAG_BSY)) {
    	if (HAL_GetTick() - t0 > 10) break;
    }

    HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

release:
    // Update next_packet_ptr
    dev->next_packet_ptr = next_ptr;

    // Update ERXRDPT (must be an odd number)
    uint16_t erxrdpt = (next_ptr == RX_START) ? RX_END : next_ptr - 1;
    if (!(erxrdpt & 0x01)) {
    	erxrdpt = erxrdpt == RX_START ? RX_END : erxrdpt - 1;
    }
    ENC28J60_WriteReg(dev, ERXRDPTL, erxrdpt & 0xFF);
    ENC28J60_WriteReg(dev, ERXRDPTH, erxrdpt >> 8);

    // Decrease EPKTCNT — chip reports that it has finished processing 1 packet
    ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);

    return len;
}

void ENC28J60_DropPacket(ENC28J60_Config *dev) {
	ENC28J60_WriteReg(dev, ERDPTL, dev->next_packet_ptr & 0xFF);
	ENC28J60_WriteReg(dev, ERDPTH, dev->next_packet_ptr >> 8);
	uint8_t header[2];
	uint16_t next_packet_ptr;

	HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_RESET);
	uint8_t cmd = ENC28J60_READ_BUF_MEM;
	HAL_SPI_Transmit(dev->hspi, &cmd, 1, 100);
	HAL_SPI_Receive(dev->hspi, header, 2, 100);
	next_packet_ptr = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
	HAL_GPIO_WritePin(dev->NSS_Port, dev->NSS_Pin, GPIO_PIN_SET);

	uint16_t erxrdpt = RX_END;
	if (next_packet_ptr == RX_START) {
		erxrdpt = RX_END;
	} else if (next_packet_ptr > RX_START && next_packet_ptr <= RX_END) {
		erxrdpt = next_packet_ptr - 1;
	} else {
		next_packet_ptr = RX_START;
		erxrdpt = RX_END;
	}

	if (!(erxrdpt & 0x01)) {
		erxrdpt = erxrdpt == RX_START ? RX_END : erxrdpt - 1;
	}
	ENC28J60_WriteReg(dev, ERXRDPTL, erxrdpt & 0xFF);
	ENC28J60_WriteReg(dev, ERXRDPTH, erxrdpt >> 8);

	ENC28J60_WriteOp(dev, ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);

	dev->next_packet_ptr = next_packet_ptr;
}
