#include "eth.h"
#include "print.h"

static uint8_t g_mac[6];

void eth_init(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) g_mac[i] = mac[i];

    uint32_t lo = (uint32_t)mac[0]        | ((uint32_t)mac[1] << 8) |
                  ((uint32_t)mac[2] << 16)| ((uint32_t)mac[3] << 24);
    uint32_t hi = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8);

    ETH_MAC_LO   = lo;
    ETH_MAC_HI   = hi;
    ETH_IRQ_MASK = 0x1;
    ETH_IRQ_CLR  = 0x1;
    ETH_CTRL     = ETH_C_ENABLE | ETH_C_IRQ_EN |
                   ETH_C_ACCEPT_BCAST | ETH_C_ACCEPT_MCAST | ETH_C_ACCEPT_UNI;
}

void eth_get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = g_mac[i];
}

void eth_send(const uint8_t* frame, uint32_t len) {
    if (len < 14 || len > ETH_FRAME_MAX) return;
    ETH_TX_LEN = 0;
    for (uint32_t i = 0; i < len; i++) {
        ETH_TX_PUSH = frame[i];
    }
    ETH_TX_COMMIT = 1;
}

int eth_rx_ready(void) {
    return (ETH_STATUS & ETH_S_RX_AVAIL) ? 1 : 0;
}

int eth_recv(uint8_t* buf, uint32_t maxlen) {
    if (!eth_rx_ready()) return -1;
    uint32_t len = ETH_RX_LEN;
    if (len > maxlen) len = maxlen;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(ETH_RX_POP & 0xFF);
    }
    ETH_RX_NEXT = 1;
    return (int)len;
}

void eth_irq_handler(void) {
    ETH_IRQ_CLR = 0x1;
}

void eth_get_stats(uint32_t* txok, uint32_t* rxok, uint32_t* txer, uint32_t* rxer) {
    if (txok) *txok = ETH_STAT_TXOK;
    if (rxok) *rxok = ETH_STAT_RXOK;
    if (txer) *txer = ETH_STAT_TXER;
    if (rxer) *rxer = ETH_STAT_RXER;
}
