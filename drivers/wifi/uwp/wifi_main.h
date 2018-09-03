/*
 * (C) Copyright 2017
 * Dong Xiang, <dong.xiang@spreadtrum.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __WIFI_MAIN_H_
#define __WIFI_MAIN_H_

#include <zephyr.h>
#include <net/wifi_mgmt.h>
#include "wifi_cmdevt.h"
#include "wifi_txrx.h"
//#include "wifi_msg.h"
#include "wifi_ipc.h"
#include "wifi_rf.h"

#define ETH_ALEN 6

#define WIFI_MODE_NONE		0
#define WIFI_MODE_STA       1
#define WIFI_MODE_AP        2
#define WIFI_MODE_APSTA     3
#define WIFI_MODE_MONITOR   4


struct wifi_priv {
	struct net_if *iface;
	u32_t cp_version;
	u8_t mode;
	unsigned char mac[ETH_ALEN];
	scan_result_cb_t scan_cb;
	u8_t scan_result;
	bool connecting;
	bool connected;
	bool opened;

	struct wifi_conf_t conf;
};

static inline void uwp_save_addr_before_payload(u32_t payload, void *addr)
{
	u32_t *pkt_ptr;

	pkt_ptr = (u32_t *)(payload - 4);
	*pkt_ptr = (u32_t)addr;
}

static inline u32_t uwp_get_addr_from_payload(u32_t payload)
{
	u32_t * ptr;
	ptr = (u32_t *)(payload - 4);

	return *ptr;
}
//extern struct adapter wifi_pAd;

//int wifi_ifnet_sta_init(struct adapter *pAd);
//int wifi_ifnet_ap_init(struct adapter *pAd);
//struct netif *wifi_ifnet_get_interface(struct adapter *pAd,int ctx_id);
extern int wifi_ipc_send(int ch,int prio,void *data,int len, int offset);
extern int wifi_get_mac(u8_t *mac,int idx);
extern int wifi_ipc_init(void);

#endif /* __WIFI_MAIN_H_ */