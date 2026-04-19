#ifndef ENC28J60_DRIVER_H_
#define ENC28J60_DRIVER_H_

#include "stm32f1xx_hal.h"
#include "cmsis_os.h"

// SPI Opcodes
#define ENC28J60_READ_CTRL_REG    0x00
#define ENC28J60_READ_BUF_MEM     0x3A
#define ENC28J60_WRITE_CTRL_REG   0x40
#define ENC28J60_WRITE_BUF_MEM    0x7A
#define ENC28J60_BIT_FIELD_SET    0x80
#define ENC28J60_BIT_FIELD_CLR    0xA0
#define ENC28J60_SYS_RST_CMD      0xFF

// Mask
#define ADDR_MASK   0x1F
#define BANK_MASK   0x60
#define DUMMY_MASK  0x80  // MAC/MII need dummy byte when reading

// Common registers (all banks)
#define EIE     0x1B
#define EIR     0x1C
#define ESTAT   0x1D
#define ECON2   0x1E
#define ECON1   0x1F

// Bank 0
#define ERDPTL   (0x00|0x00)
#define ERDPTH   (0x01|0x00)
#define EWRPTL   (0x02|0x00)
#define EWRPTH   (0x03|0x00)
#define ETXSTL   (0x04|0x00)
#define ETXSTH   (0x05|0x00)
#define ETXNDL   (0x06|0x00)
#define ETXNDH   (0x07|0x00)
#define ERXSTL   (0x08|0x00)
#define ERXSTH   (0x09|0x00)
#define ERXNDL   (0x0A|0x00)
#define ERXNDH   (0x0B|0x00)
#define ERXRDPTL (0x0C|0x00)
#define ERXRDPTH (0x0D|0x00)

// Bank 1
#define ERXFCON  (0x18|0x20)
#define EPKTCNT  (0x19|0x20)

// Bank 2 (MAC/MII — bit 7 = need dummy byte)
#define MACON1   (0x00|0x40|0x80)
#define MACON2   (0x01|0x40|0x80)
#define MACON3   (0x02|0x40|0x80)
#define MABBIPG  (0x04|0x40|0x80)
#define MAIPGL   (0x06|0x40|0x80)
#define MAIPGH   (0x07|0x40|0x80)
#define MAMXFLL  (0x0A|0x40|0x80)
#define MAMXFLH  (0x0B|0x40|0x80)
#define MICMD    (0x12|0x40|0x80)
#define MIREGADR (0x14|0x40|0x80)
#define MIWRL    (0x16|0x40|0x80)
#define MIWRH    (0x17|0x40|0x80)
#define MIRDL    (0x18|0x40|0x80)
#define MIRDH    (0x19|0x40|0x80)

// Bank 3
#define MAADR1   (0x00|0x60|0x80)
#define MAADR2   (0x01|0x60|0x80)
#define MAADR3   (0x02|0x60|0x80)
#define MAADR4   (0x03|0x60|0x80)
#define MAADR5   (0x04|0x60|0x80)
#define MAADR6   (0x05|0x60|0x80)
#define MISTAT   (0x0A|0x60|0x80)
#define EREVID   (0x12|0x60)

// ESTAT bits
#define ESTAT_INT       0x80
#define ESTAT_RXBUSY    0x04
#define ESTAT_TXABRT    0x02
#define ESTAT_CLKRDY    0x01

// ECON1 bits
#define ECON1_TXRST     0x80
#define ECON1_RXRST     0x40
#define ECON1_TXRTS     0x08
#define ECON1_RXEN      0x04
#define ECON1_BSEL1     0x02
#define ECON1_BSEL0     0x01

// ECON2 bits
#define ECON2_AUTOINC   0x80
#define ECON2_PKTDEC    0x40
#define ECON2_PWRSV     0x20

// EIE bits
#define EIE_INTIE       0x80
#define EIE_PKTIE       0x40

// EIR bits
#define EIR_TXIF        0x08
#define EIR_TXERIF      0x02
#define EIR_RXERIF      0x01

// ERXFCON bits
#define ERXFCON_UCEN    0x80
#define ERXFCON_ANDOR   0x40
#define ERXFCON_CRCEN   0x20
#define ERXFCON_BCEN    0x01

// MACON1 bits
#define MACON1_TXPAUS   0x08
#define MACON1_RXPAUS   0x04
#define MACON1_MARXEN   0x01

// MACON3 bits
#define MACON3_PADCFG0  0x20
#define MACON3_TXCRCEN  0x10
#define MACON3_FRMLNEN  0x02
#define MACON3_FULDPX   0x01

// MISTAT bits
#define MISTAT_BUSY     0x01

// MICMD bits
#define MICMD_MIIRD     0x01

// PHY registers
#define PHCON1          0x00
#define PHCON2          0x10
#define PHSTAT1         0x01
#define PHSTAT2         0x11
#define PHLCON          0x14

// PHY bits
#define PHCON1_PDPXMD   0x0100   // Full duplex
#define PHCON2_HDLDIS   0x0100   // Disable loopback
#define PHSTAT1_LLSTAT  0x0004   // Link status (used for Heartbeat task)

// Memory map
#define RX_START        0x0000
#define RX_END          0x19FF   // 6.5KB RX buffer
#define TX_START        0x1A00   // TX buffer
#define MAX_FRAME_LEN   598      // 512 payload + 86 header

// extern Semaphore
extern osSemaphoreId xSem_DMA_SPI1_RX_Done;
extern osSemaphoreId xSem_DMA_SPI2_RX_Done;
extern osSemaphoreId xSem_DMA_SPI1_TX_Done;
extern osSemaphoreId xSem_DMA_SPI2_TX_Done;
extern osMutexId spi1_mutex;
extern osMutexId spi2_mutex;

// Configure structure
typedef struct {
    SPI_HandleTypeDef *hspi; // SPI peripheral handle (SPI1 or SPI2)
    GPIO_TypeDef *NSS_Port; // Negative slave Select Port
    uint16_t NSS_Pin; // Negative slave Select Pin
    uint16_t next_packet_ptr; // Pointer managing the position of the next packet
    GPIO_TypeDef *RST_Port; // Reset Port
    uint16_t RST_Pin; // Reset Pin
    uint8_t current_bank; // Store the current register bank
} ENC28J60_Config;

/*
// Read Control Register Command
uint8_t ENC28J60_ReadOp (ENC28J60_Config *spi, uint8_t opcode, uint8_t address);

// Write Control Register Command
void ENC28J60_WriteOp (ENC28J60_Config *spi, uint8_t opcode, uint8_t address, uint8_t data);

// Set Bank
void ENC28J60_SetBank (ENC28J60_Config *spi, uint8_t address);

// Read a register
uint8_t ENC28J60_ReadReg (ENC28J60_Config *spi, uint8_t address);

// Write data into a register
void ENC28J60_WriteReg (ENC28J60_Config *spi, uint8_t address, uint8_t data);

// Write data into a PHY register
void ENC28J60_WritePhy (ENC28J60_Config *spi, uint8_t address, uint16_t data);
*/

// Read a register
uint8_t ENC28J60_ReadRegGlo (ENC28J60_Config *spi, uint8_t address);

// Read data from a PHY register
uint16_t ENC28J60_ReadPhy (ENC28J60_Config *spi, uint8_t address);

// Clear Receive Error Interrupt Flag bit
void ENC28J60_ClearErrors(ENC28J60_Config *spi);

// Initialize ENC28J60
void ENC28J60_Init (ENC28J60_Config *spi, uint8_t *mac_address);

// Send a packet
void ENC28J60_SendPacket (ENC28J60_Config *spi, uint8_t *packet_data, uint16_t length);

// Receive a packet
uint16_t ENC28J60_ReceivePacket (ENC28J60_Config *spi, uint8_t *pBuffer, uint16_t max_length);

// Drop a packet
void ENC28J60_DropPacket(ENC28J60_Config *spi);

#endif /* ENC28J60_DRIVER_H_ */
