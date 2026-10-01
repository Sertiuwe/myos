#ifndef NET_H
#define NET_H

#include "types.h"

void net_init(void);
void net_poll(void);

// Информация
void net_get_myip(uint8_t out[4]);
void net_get_gwip(uint8_t out[4]);
void net_set_myip(const uint8_t ip[4]);
void net_set_gwip(const uint8_t ip[4]);
void net_show_arp(void);

// Операции
int  net_arp_resolve(const uint8_t ip[4], uint8_t out_mac[6]);
int  net_ping(const uint8_t ip[4], int count);
int  net_dns_resolve(const char* hostname, uint8_t out_ip[4]);

// Утилиты для печати IP
void net_print_ip(const uint8_t ip[4]);

#endif
