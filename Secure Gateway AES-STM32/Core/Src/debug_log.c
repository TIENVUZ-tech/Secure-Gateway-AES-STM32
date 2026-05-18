#include "debug_log.h"
#include "stdarg.h"
#include "string.h"
#include "buffer_pool.h"
#include "stm32f1xx_hal.h"

/* ── External diag variables (all declared in main.c / enc28j60_driver.c) ── */
extern volatile uint32_t diag_rx1_packets;
extern volatile uint32_t diag_rx2_packets;
extern volatile uint32_t diag_tx1_packets;
extern volatile uint32_t diag_tx2_packets;
extern volatile uint32_t diag_arp_packets;
extern volatile uint32_t diag_icmp_packets;
extern volatile uint32_t diag_drop_packets;
extern volatile uint32_t diag_link1_up;
extern volatile uint32_t diag_link2_up;
extern volatile uint32_t diag_arp_rx1;
extern volatile uint32_t diag_arp_rx2;
extern volatile uint32_t diag_icmp_rx1;
extern volatile uint32_t diag_icmp_rx2;
extern volatile uint32_t diag_arp1_opcode;
extern volatile uint32_t diag_arp1_sender_ip;
extern volatile uint32_t diag_arp1_target_ip;
extern volatile uint32_t diag_arp2_opcode;
extern volatile uint32_t diag_arp2_sender_ip;
extern volatile uint32_t diag_arp2_target_ip;

/* From enc28j60_driver.c */
extern volatile uint32_t diag_enc_next_ptr;
extern volatile uint32_t diag_enc_rx_len;
extern volatile uint32_t diag_enc_rxstat;
extern volatile uint32_t diag_enc_fail_reason;
extern volatile uint32_t diag_enc_tx_abort;
extern volatile uint32_t diag_enc_tx_timeout;
extern volatile uint32_t diag_enc_tx_fail_reason;

/* ── Internal state ──────────────────────────────────────────────────────── */
static osMutexId *s_uart_mutex = NULL;
static char s_linebuf[160];

/* ── Public API ──────────────────────────────────────────────────────────── */

void DebugLog_Init(osMutexId *uart_mutex)
{
    s_uart_mutex = uart_mutex;
    /* Print banner immediately (scheduler not yet running — no mutex needed) */
    printf("\r\n");
    printf("================================================\r\n");
    printf("  Secure Gateway AES-STM32 — Debug Console\r\n");
    printf("  Build: " __DATE__ " " __TIME__ "\r\n");
    printf("================================================\r\n");
}

void DebugLog_Print(const char *level_str, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(s_linebuf, sizeof(s_linebuf) - 3, fmt, args);
    va_end(args);

    /* Clamp and ensure CRLF */
    if (n < 0) n = 0;
    if (n > (int)(sizeof(s_linebuf) - 3)) n = sizeof(s_linebuf) - 3;
    s_linebuf[n++] = '\r';
    s_linebuf[n++] = '\n';
    s_linebuf[n]   = '\0';

    uint32_t ms = HAL_GetTick();
    char header[24];
    snprintf(header, sizeof(header), "[%6lu.%03lu][%s] ",
             ms / 1000, ms % 1000, level_str);

    /* Protect concurrent printf from multiple tasks */
    if (s_uart_mutex && osMutexWait(*s_uart_mutex, 20) == osOK) {
        printf("%s%s", header, s_linebuf);
        osMutexRelease(*s_uart_mutex);
    } else {
        /* Best-effort if mutex unavailable (e.g. very early boot) */
        printf("%s%s", header, s_linebuf);
    }
}

/* ── Readable fail-reason strings ───────────────────────────────────────── */
static const char* rx_fail_str(uint32_t r) {
    switch (r) {
        case 0: return "OK";
        case 1: return "bad_next_ptr";
        case 2: return "rxstat_error";
        case 3: return "too_long";
        case 4: return "exceeds_dummy";
        case 5: return "DMA_start_fail";
        case 6: return "DMA_timeout";
        case 7: return "EPKTCNT=0";
        default: return "unknown";
    }
}

static const char* tx_fail_str(uint32_t r) {
    switch (r) {
        case 0: return "OK";
        case 1: return "prev_tx_stuck";
        case 2: return "DMA_start_fail";
        case 3: return "DMA_timeout";
        case 4: return "SPI_BSY_timeout";
        case 5: return "TX_aborted(errata)";
        case 6: return "TX_timeout";
        case 7: return "retry_exhausted";
        case 8: return "length_invalid";
        default: return "unknown";
    }
}

static void print_ip(uint32_t ip) {
    printf("%lu.%lu.%lu.%lu",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
           (ip >>  8) & 0xFF,  ip        & 0xFF);
}

/* ── Periodic stats dump (call every 5s from vHeartbeat_TaskFunc) ─────────── */
void DebugLog_PrintStats(void)
{
    uint32_t ms = HAL_GetTick();

    if (s_uart_mutex && osMutexWait(*s_uart_mutex, 30) != osOK) return;

    printf("\r\n[%6lu.%03lu][STAT] ──────────────────────────────────\r\n",
           ms / 1000, ms % 1000);

    /* Link */
    printf("  Link  : dev1=%s  dev2=%s\r\n",
           diag_link1_up ? "UP  " : "DOWN",
           diag_link2_up ? "UP  " : "DOWN");

    /* Traffic */
    printf("  RX    : dev1=%-6lu  dev2=%-6lu\r\n", diag_rx1_packets, diag_rx2_packets);
    printf("  TX    : dev1=%-6lu  dev2=%-6lu\r\n", diag_tx1_packets, diag_tx2_packets);
    printf("  DROP  : %-6lu\r\n", diag_drop_packets);
    printf("  ARP   : total=%-4lu  dev1=%-4lu  dev2=%-4lu\r\n",
           diag_arp_packets, diag_arp_rx1, diag_arp_rx2);
    printf("  ICMP  : total=%-4lu  dev1=%-4lu  dev2=%-4lu\r\n",
           diag_icmp_packets, diag_icmp_rx1, diag_icmp_rx2);

    /* Last ARP seen on each interface */
    if (diag_arp1_opcode) {
        printf("  ARP1  : op=%s  sender=", diag_arp1_opcode==1?"REQ":"REP");
        print_ip(diag_arp1_sender_ip);
        printf("  target=");
        print_ip(diag_arp1_target_ip);
        printf("\r\n");
    }
    if (diag_arp2_opcode) {
        printf("  ARP2  : op=%s  sender=", diag_arp2_opcode==1?"REQ":"REP");
        print_ip(diag_arp2_sender_ip);
        printf("  target=");
        print_ip(diag_arp2_target_ip);
        printf("\r\n");
    }

    /* Driver-level */
    printf("  DRV RX: last_next_ptr=0x%04lX  last_len=%lu  rxstat=0x%04lX\r\n",
           diag_enc_next_ptr, diag_enc_rx_len, diag_enc_rxstat);
    printf("  DRV RX: last_fail=%lu (%s)\r\n",
           diag_enc_fail_reason, rx_fail_str(diag_enc_fail_reason));
    printf("  DRV TX: aborts=%lu  timeouts=%lu  last_fail=%lu (%s)\r\n",
           diag_enc_tx_abort, diag_enc_tx_timeout,
           diag_enc_tx_fail_reason, tx_fail_str(diag_enc_tx_fail_reason));

    /* Buffer pool */
    printf("  Pool  : free=%u/%u\r\n",
           BufferPool_FreeCount(), BUFFER_POOL_SIZE);

    printf("[%6lu.%03lu][STAT] ──────────────────────────────────\r\n\r\n",
           ms / 1000, ms % 1000);

    if (s_uart_mutex) osMutexRelease(*s_uart_mutex);
}
