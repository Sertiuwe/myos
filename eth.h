#ifndef ETH_H
#define ETH_H

#include "types.h"

#define ETH_BASE      0x10016000u

#define ETH_MAC_LO    (*(volatile uint32_t*)(ETH_BASE + 0x00))
#define ETH_MAC_HI    (*(volatile uint32_t*)(ETH_BASE + 0x04))
#define ETH_CTRL      (*(volatile uint32_t*)(ETH_BASE + 0x08))
#define ETH_STATUS    (*(volatile uint32_t*)(ETH_BASE + 0x0C))
#define ETH_IRQ_STAT  (*(volatile uint32_t*)(ETH_BASE + 0x10))
#define ETH_IRQ_MASK  (*(volatile uint32_t*)(ETH_BASE + 0x14))
#define ETH_IRQ_CLR   (*(volatile uint32_t*)(ETH_BASE + 0x18))
#define ETH_TX_LEN    (*(volatile uint32_t*)(ETH_BASE + 0x20))
#define ETH_TX_PUSH   (*(volatile uint32_t*)(ETH_BASE + 0x24))
#define ETH_TX_COMMIT (*(volatile uint32_t*)(ETH_BASE + 0x28))
#define ETH_TX_ABORT  (*(volatile uint32_t*)(ETH_BASE + 0x2C))
#define ETH_RX_LEN    (*(volatile uint32_t*)(ETH_BASE + 0x30))
#define ETH_RX_POP    (*(volatile uint32_t*)(ETH_BASE + 0x34))
#define ETH_RX_NEXT   (*(volatile uint32_t*)(ETH_BASE + 0x38))
#define ETH_RX_COUNT  (*(volatile uint32_t*)(ETH_BASE + 0x3C))
#define ETH_STAT_TXOK (*(volatile uint32_t*)(ETH_BASE + 0x40))
#define ETH_STAT_RXOK (*(volatile uint32_t*)(ETH_BASE + 0x44))
#define ETH_STAT_TXER (*(volatile uint32_t*)(ETH_BASE + 0x48))
#define ETH_STAT_RXER (*(volatile uint32_t*)(ETH_BASE + 0x4C))

#define ETH_C_ENABLE       0x01
#define ETH_C_PROMISC      0x02
#define ETH_C_LOOPBACK     0x04
#define ETH_C_IRQ_EN       0x08
#define ETH_C_ACCEPT_BCAST 0x10
#define ETH_C_ACCEPT_MCAST 0x20
#define ETH_C_ACCEPT_UNI   0x40

#define ETH_S_LINK     0x01
#define ETH_S_TX_RDY   0x02
#define ETH_S_RX_AVAIL 0x04

#define ETH_FRAME_MAX  1518

void eth_init(const uint8_t mac[6]);
void eth_get_mac(uint8_t out[6]);
void eth_send(const uint8_t* frame, uint32_t len);
int  eth_recv(uint8_t* buf, uint32_t maxlen);
int  eth_rx_ready(void);
void eth_irq_handler(void);
void eth_get_stats(uint32_t* txok, uint32_t* rxok, uint32_t* txer, uint32_t* rxer);

#endif
