/*
 * Copyright (c) 2026 Mario Paja
 * SPDX-License-Identifier: Apache-2.0
 *
 * Layer 2 bridge between a USB CDC NCM interface (to the host) and a
 * LAN865x 10BASE-T1S interface. The bridge interface takes a static
 * management address (CONFIG_T1S_BRIDGE_IPV4_ADDR) or one by DHCPv4.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_bridge.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/usb/usbd.h>

#include <sample_usbd.h>

LOG_MODULE_REGISTER(t1s_usb_bridge, LOG_LEVEL_INF);

static void bridge_find_cb(struct eth_bridge_iface_context *br, void *user_data)
{
	struct net_if **bridge = user_data;

	if (*bridge == NULL) {
		*bridge = br->iface;
	}
}

static void bridge_add_cb(struct net_if *iface, void *user_data)
{
	struct net_if *bridge = user_data;
	int ret;

	if (iface == bridge || net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET)) {
		return;
	}

	ret = eth_bridge_iface_add(bridge, iface);
	if (ret < 0) {
		LOG_ERR("Adding iface %d (%s) to the bridge failed: %d",
			net_if_get_by_iface(iface), iface->if_dev->dev->name, ret);
		return;
	}

	LOG_INF("Bridge port: iface %d (%s)", net_if_get_by_iface(iface),
		iface->if_dev->dev->name);
}

/* Restart DHCPv4 on the bridge when a port comes up */
static void port_up_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			    struct net_if *iface)
{
	struct ethernet_context *eth_ctx;

	ARG_UNUSED(cb);

	if (IS_ENABLED(CONFIG_T1S_BRIDGE_STATIC_IPV4) || mgmt_event != NET_EVENT_IF_UP ||
	    net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET)) {
		return;
	}

	eth_ctx = net_if_l2_data(iface);
	if (net_eth_iface_is_bridged(eth_ctx)) {
		net_dhcpv4_restart(net_eth_get_bridge(eth_ctx));
	}
}

static int bridge_set_static_ipv4(struct net_if *bridge)
{
	struct net_in_addr addr;
	struct net_in_addr netmask;
	struct net_in_addr gw;

	if (net_addr_pton(NET_AF_INET, CONFIG_T1S_BRIDGE_IPV4_ADDR, &addr) < 0 ||
	    net_addr_pton(NET_AF_INET, CONFIG_T1S_BRIDGE_IPV4_NETMASK, &netmask) < 0) {
		LOG_ERR("Invalid static IPv4 address or netmask");
		return -EINVAL;
	}

	if (net_if_ipv4_addr_add(bridge, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_ERR("Adding %s failed", CONFIG_T1S_BRIDGE_IPV4_ADDR);
		return -ENOMEM;
	}

	net_if_ipv4_set_netmask_by_addr(bridge, &addr, &netmask);

	if (net_addr_pton(NET_AF_INET, CONFIG_T1S_BRIDGE_IPV4_GW, &gw) == 0) {
		net_if_ipv4_set_gw(bridge, &gw);
	}

	LOG_INF("Bridge address %s", CONFIG_T1S_BRIDGE_IPV4_ADDR);
	return 0;
}

int main(void)
{
	static struct net_mgmt_event_callback mgmt_cb;
	struct usbd_context *usbd;
	struct net_if *bridge = NULL;
	int ret;

	/* USB device with the CDC NCM network interface */
	usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL) {
		LOG_ERR("USB device init failed");
		return -ENODEV;
	}

	ret = usbd_enable(usbd);
	if (ret != 0) {
		LOG_ERR("USB device enable failed: %d", ret);
		return ret;
	}

	net_eth_bridge_foreach(bridge_find_cb, &bridge);
	if (bridge == NULL) {
		LOG_ERR("No bridge interface");
		return -ENODEV;
	}

	net_if_foreach(bridge_add_cb, bridge);

	net_mgmt_init_event_callback(&mgmt_cb, port_up_handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&mgmt_cb);

	ret = net_if_up(bridge);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Bridge up failed: %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_T1S_BRIDGE_STATIC_IPV4)) {
		ret = bridge_set_static_ipv4(bridge);
		if (ret < 0) {
			return ret;
		}
	} else {
		net_dhcpv4_start(bridge);
	}

	LOG_INF("Bridge iface %d up", net_if_get_by_iface(bridge));

	return 0;
}
