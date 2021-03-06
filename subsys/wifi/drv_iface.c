/*
 * @file
 * @brief Driver interface implementation for Zephyr
 */

/*
 * Copyright (c) 2018, Unisoc Communications Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_LEVEL CONFIG_WIFIMGR_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_DECLARE(wifimgr);

#include "wifimgr.h"

static int wifi_drv_iface_notify_event(unsigned int evt_id, void *buf,
				       int buf_len)
{
	struct mq_des *mq;
	struct mq_attr attr;
	struct evt_message msg;
	int ret;

	attr.mq_maxmsg = 16;
	attr.mq_msgsize = sizeof(struct evt_message);
	attr.mq_flags = 0;
	mq = mq_open(WIFIMGR_EVT_MQUEUE, O_WRONLY | O_CREAT, 0666, &attr);
	if (!mq) {
		wifimgr_err("failed to open event queue %s!\n",
		       WIFIMGR_EVT_MQUEUE);
		return -errno;
	}

	msg.evt_id = evt_id;
	msg.buf_len = buf_len;
	msg.buf = NULL;
	if (buf_len) {
		msg.buf = malloc(buf_len);
		memcpy(msg.buf, buf, buf_len);
	}

	ret = mq_send(mq, (const char *)&msg, sizeof(msg), 0);
	if (ret < 0) {
		free(msg.buf);
		wifimgr_err("failed to send [%s]: %d, errno %d!\n",
		       wifimgr_evt2str(msg.evt_id), ret, errno);
	} else {
		wifimgr_dbg("send [%s], buf: 0x%08x\n",
		       wifimgr_evt2str(msg.evt_id), *(int *)msg.buf);
	}

	mq_close(mq);

	return ret;
}

int wifi_drv_iface_get_mac(void *iface, char *mac)
{
	memcpy(mac, net_if_get_link_addr(iface)->addr, WIFIMGR_ETH_ALEN);

	return 0;
}

int wifi_drv_iface_open_station(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->open)
		return -EIO;

	return drv_api->open(dev);
}

int wifi_drv_iface_close_station(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->close)
		return -EIO;

	return drv_api->close(dev);
}

static void wifi_drv_iface_scan_result_cb(void *iface, int status,
					  struct wifi_drv_scan_result *entry)
{
	struct wifimgr_evt_scan_result scan_res;
	struct wifimgr_evt_scan_done scan_done;

	if (!entry) {
		scan_done.result = status;
		wifi_drv_iface_notify_event(WIFIMGR_EVT_SCAN_DONE, &scan_done,
					    sizeof(scan_done));
	} else {
		strcpy(scan_res.ssid, entry->ssid);
		strncpy(scan_res.bssid, entry->bssid, WIFIMGR_ETH_ALEN);
		scan_res.channel = entry->channel;
		scan_res.rssi = entry->rssi;

		wifi_drv_iface_notify_event(WIFIMGR_EVT_SCAN_RESULT, &scan_res,
					    sizeof(scan_res));
	}
}

int wifi_drv_iface_scan(void *iface, unsigned char band, unsigned char channel)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;
	struct wifi_drv_scan_params params;

	if (!drv_api->scan)
		return -EIO;

	params.band = band;
	params.channel = channel;

	return drv_api->scan(dev, &params, wifi_drv_iface_scan_result_cb);
}

void wifi_drv_iface_connect_cb(void *iface, int status)
{
	struct wifimgr_evt_connect conn;

	conn.status = status;

	wifi_drv_iface_notify_event(WIFIMGR_EVT_CONNECT, &conn, sizeof(conn));
}

int wifi_drv_iface_connect(void *iface, char *ssid, char *passwd)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;
	struct wifi_drv_connect_params params;

	if (!drv_api->connect)
		return -EIO;

	params.ssid = ssid;
	params.ssid_length = strlen(ssid);
	params.psk = passwd;
	params.psk_length = strlen(passwd);

	return drv_api->connect(dev, &params, wifi_drv_iface_connect_cb);
}

void wifi_drv_iface_disconnect_cb(void *iface, int status)
{
	struct wifimgr_evt_disconnect disc;

	disc.reason_code = status;

	wifi_drv_iface_notify_event(WIFIMGR_EVT_DISCONNECT, &disc,
				    sizeof(disc));
}

int wifi_drv_iface_disconnect(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->disconnect)
		return -EIO;

	return drv_api->disconnect(dev, wifi_drv_iface_disconnect_cb);
}

int wifi_drv_iface_get_station(void *iface, char *signal)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!signal)
		return -EINVAL;

	if (!drv_api->get_station)
		return -EIO;

	return drv_api->get_station(dev, signal);
}

int wifi_drv_iface_notify_ip(void *iface, char *ipaddr, char len)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!ipaddr)
		return -EINVAL;

	if (!drv_api->notify_ip)
		return -EIO;

	return drv_api->notify_ip(dev, ipaddr, len);
}

int wifi_drv_iface_open_softap(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->open)
		return -EIO;

	return drv_api->open(dev);
}

int wifi_drv_iface_close_softap(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->close)
		return -EIO;

	return drv_api->close(dev);
}

void wifi_drv_iface_new_station(void *iface, int status, char *mac)
{
	struct wifimgr_evt_new_station new_sta;

	new_sta.is_connect = status;
	if (mac && !is_zero_ether_addr(mac))
		memcpy(new_sta.mac, mac, WIFIMGR_ETH_ALEN);

	wifi_drv_iface_notify_event(WIFIMGR_EVT_NEW_STATION, &new_sta,
				    sizeof(new_sta));
}

int wifi_drv_iface_start_softap(void *iface, char *ssid, char *passwd,
				char channel)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;
	struct wifi_drv_start_ap_params params;

	if (!drv_api->start_ap)
		return -EIO;

	params.ssid = ssid;
	params.ssid_length = strlen(ssid);
	params.psk = passwd;
	params.psk_length = strlen(passwd);
	params.channel = channel;

	return drv_api->start_ap(dev, &params, wifi_drv_iface_new_station);
}

int wifi_drv_iface_stop_softap(void *iface)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!drv_api->stop_ap)
		return -EIO;

	return drv_api->stop_ap(dev);
}

int wifi_drv_iface_del_station(void *iface, char *mac)
{
	struct device *dev = net_if_get_device((struct net_if *)iface);
	struct wifi_drv_api *drv_api =
	    (struct wifi_drv_api *)dev->driver_api;

	if (!mac)
		return -EINVAL;

	if (!drv_api->del_station)
		return -EIO;

	return drv_api->del_station(dev, mac);
}
