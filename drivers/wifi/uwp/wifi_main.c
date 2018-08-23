#define SYS_LOG_LEVEL CONFIG_SYS_LOG_WIFI_LEVEL
#define SYS_LOG_DOMAIN "dev/wifi"
#if (SYS_LOG_LEVEL > SYS_LOG_LEVEL_OFF)
#define NET_LOG_ENABLED 1
#endif

#include <logging/sys_log.h>

#include <zephyr.h>
#include <kernel.h>
#include <device.h>
#include <string.h>
#include <errno.h>
#include <net/net_pkt.h>
#include <net/net_if.h>
#include <net/net_l2.h>
#include <net/net_context.h>
#include <net/net_offload.h>
#include <net/wifi_mgmt.h>
#include <net/ethernet.h>

#include "wifi_main.h"
#include "sipc.h"

/* We do not need <socket/include/socket.h>
 * It seems there is a bug in ASF side: if OS is already defining sockaddr
 * and all, ASF will not need to define it. Unfortunately its socket.h does
 * but also defines some NM API functions there (??), so we need to redefine
 * those here.
 */
#define __SOCKET_H__
#define HOSTNAME_MAX_SIZE	(64)
static struct wifi_priv uwp_wifi_priv;
#define DEV_DATA(dev) \
	((struct wifi_priv *)(dev)->driver_data)

typedef enum cp_runing_bit_t{
	CP_RUNNING_BIT = 0,
	CP_WIFI_RUNNING_BIT = 1,
}cp_runing_bit_t;

int wifi_get_mac(u8_t *mac,int idx)
{
	//1 get mac address from flash.
	//2 copy to mac
	// if idx == 0,get sta mac,
	// if idx == 1,get ap mac
	mac[0]=0x46;
	mac[1]=0xd1;
	mac[2]=0xf9;
	mac[3]=0x44;
	mac[4]=0x73;
	mac[5]=0x2c;

	return 0;
}

static int uwp_mgmt_scan(struct device *dev, scan_result_cb_t cb)
{
	struct wifi_priv *priv=DEV_DATA(dev);

	SYS_LOG_INF("%s %d.\n", __func__, __LINE__);
	if (priv->scan_cb) {
		return -EALREADY;
	}

	if (!priv->opened) {
		wifi_cmd_start_sta(priv);
		priv->opened = true;

		wifi_tx_empty_buf(MAX_EMPTY_BUF_COUNT);
		k_sleep(50);
	}

	priv->scan_cb = cb;

	if (wifi_cmd_scan(priv)) {
		priv->scan_cb = NULL;
		return -EIO;
	}

	return 0;
}

static int uwp_mgmt_connect(struct device *dev,
		struct wifi_connect_req_params *params)
{
	struct wifi_priv *priv=DEV_DATA(dev);
	SYS_LOG_INF("%s %d.\n", __func__, __LINE__);

	return wifi_cmd_connect(priv, params);
}

static int uwp_mgmt_disconnect(struct device *device)
{
	SYS_LOG_ERR("%s %d.\n", __func__, __LINE__);

	return 0;
}

static void uwp_iface_init(struct net_if *iface)
{
	struct wifi_priv *priv=DEV_DATA(iface->if_dev->dev);

	k_sleep(50);
	wifi_cmd_get_cp_info(priv);

	wifi_get_mac(priv->mac, 0);

	SYS_LOG_WRN("eth_init:net_if_set_link_addr:"
			"MAC Address %02X:%02X:%02X:%02X:%02X:%02X",
			priv->mac[0], priv->mac[1], priv->mac[2],
			priv->mac[3], priv->mac[4], priv->mac[5]);

	net_if_set_link_addr(iface, priv->mac, sizeof(priv->mac),
			NET_LINK_ETHERNET);

	priv->iface = iface;
}

int wifi_tx_fill_msdu_dscr(struct wifi_priv *priv,
		struct net_pkt *pkt, u8_t type, u8_t offset)
{
	u32_t addr = 0;
	u32_t reserve_len = net_pkt_ll_reserve(pkt);
	struct tx_msdu_dscr *dscr = NULL;

	net_pkt_set_ll_reserve(pkt,
			sizeof(struct tx_msdu_dscr) + net_pkt_ll_reserve(pkt));
	SYS_LOG_INF("size msdu: %d", sizeof(struct tx_msdu_dscr));

	dscr = (struct tx_msdu_dscr *)net_pkt_ll(pkt);
	memset(dscr, 0x00, sizeof(struct tx_msdu_dscr));
	addr = (u32_t)dscr;
	SPRD_AP_TO_CP_ADDR(addr);
	dscr->next_buf_addr_low = addr;
	dscr->next_buf_addr_high = 0x0;

	dscr->tx_ctrl.checksum_offload = 0;
	dscr->common.type =
		(type == SPRDWL_TYPE_CMD ? SPRDWL_TYPE_CMD : SPRDWL_TYPE_DATA);
	dscr->common.direction_ind = TRANS_FOR_TX_PATH;
	dscr->common.next_buf = 0;

	dscr->common.interface = 0;

	dscr->pkt_len = reserve_len + net_pkt_get_len(pkt);
	dscr->offset = 11;
	/*TODO*/
	dscr->tx_ctrl.sw_rate = (type == SPRDWL_TYPE_DATA_SPECIAL ? 1 : 0);
	dscr->tx_ctrl.wds = 0;
	/*TBD*/ dscr->tx_ctrl.swq_flag = 0;
	/*TBD*/ dscr->tx_ctrl.rsvd = 0;
	/*TBD*/ dscr->tx_ctrl.pcie_mh_readcomp = 1;
	dscr->buffer_info.msdu_tid = 0;
	dscr->buffer_info.mac_data_offset = 0;
	dscr->buffer_info.sta_lut_idx = 0;
	dscr->buffer_info.encrypt_bypass = 0;
	dscr->buffer_info.ap_buf_flag = 1;
	dscr->tx_ctrl.checksum_offload = 0;
	dscr->tx_ctrl.checksum_type = 0;
	dscr->tcp_udp_header_offset = 0;

	return 0;
}

static int uwp_iface_tx(struct net_if *iface, struct net_pkt *pkt)
{
	struct wifi_priv *priv = DEV_DATA(iface->if_dev->dev);
	struct net_buf *frag;
	bool first_frag = true;
	u8_t *data_ptr;
	u16_t data_len;
	u16_t total_len;
	u32_t addr;

	wifi_tx_fill_msdu_dscr(priv, pkt, SPRDWL_TYPE_DATA, 0);

	total_len = net_pkt_ll_reserve(pkt) + net_pkt_get_len(pkt);

	SYS_LOG_DBG("wifi tx data: %d bytes, reserve: %d bytes",
			total_len, net_pkt_ll_reserve(pkt));


	for (frag = pkt->frags; frag; frag = frag->frags) {
		if (first_frag) {
			data_ptr = net_pkt_ll(pkt);
			data_len = net_pkt_ll_reserve(pkt) + frag->len;
			first_frag = false;
		} else {
			data_ptr = frag->data;
			data_len = frag->len;

		}

		addr = (u32_t)data_ptr;
		/* FIXME Save pkt addr before payload. */
		uwp_save_addr_before_payload(addr, (void *)pkt);

		SPRD_AP_TO_CP_ADDR(addr);
		wifi_tx_data((void *)addr, data_len);
	}

	return 0;
}

static enum ethernet_hw_caps uwp_iface_get_capabilities(struct device *dev)
{
	ARG_UNUSED(dev);

	return ETHERNET_LINK_10BASE_T | ETHERNET_LINK_100BASE_T;
}

static const struct net_wifi_mgmt_api uwp_api = {
	.iface_api.init = uwp_iface_init,
	.iface_api.send = uwp_iface_tx,
	.get_capabilities = uwp_iface_get_capabilities,
	.scan		= uwp_mgmt_scan,
	.connect	= uwp_mgmt_connect,
	.disconnect	= uwp_mgmt_disconnect,
};

#define SEC1    1
#define SEC2    2
extern int cp_mcu_init(void);
static int uwp_init(struct device *dev)
{
	int ret;
	struct wifi_priv *priv=DEV_DATA(dev);
//	struct wifi_conf_sec1_t *sec1;
//	struct wifi_conf_sec2_t *sec2;

	priv->connecting = false;
	priv->connected = false;
	priv->opened = false;

	ret = cp_mcu_init();
	if (ret) {
		SYS_LOG_ERR("firmware download failed %i.", ret);
		return ret;
	}

	wifi_cmdevt_init();
	wifi_txrx_init(priv);
	wifi_irq_init();

	SYS_LOG_DBG("UWP WIFI driver Initialized");

	return 0;
}

NET_DEVICE_INIT(uwp, CONFIG_WIFI_UWP_NAME,
		uwp_init, &uwp_wifi_priv, NULL,
		CONFIG_WIFI_INIT_PRIORITY,
		&uwp_api,
		ETHERNET_L2, NET_L2_GET_CTX_TYPE(ETHERNET_L2),
		1550);