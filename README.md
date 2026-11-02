# 10BASE-T1S to USB bridge

Zephyr application for the MikroE UNI Clicker (STM32F429NI, MCU card 4) that
bridges a USB CDC NCM network interface to a 10BASE-T1S segment, using a
MikroE Two-Wire ETH Click (LAN865x) on mikroBUS 4.

```
LAN <--Wi-Fi--> host <--USB CDC NCM--> UNI Clicker <--10BASE-T1S--> T1S nodes
                (proxy ARP,            (L2 bridge,
                 DHCP relay)            PLCA coordinator, node 0)
```

- The bridge has a management address: static (`CONFIG_T1S_BRIDGE_IPV4_ADDR`,
  10.10.193.75 in `prj.conf`) or DHCPv4 when left empty. Its MAC address is
  derived from the STM32 unique ID.
- T1S nodes get their addresses from the LAN DHCP server through the host.
  They must have PLCA enabled with a node ID from 1 to 7.
- Shell: SEGGER RTT (J-Link) and telnet on port 23. The telnet shell is not
  authenticated; use it on a trusted network only.

## Requirements

- The `mikroe_uni_clicker` board, not yet in upstream Zephyr.
- An Ethernet bridge fix in `subsys/net/l2/ethernet/bridge/bridge.c`: since
  commit c526409cc605, broadcast and multicast IPv4 frames sent from the
  bridge interface are dropped (ARP lookup fails), so DHCP never completes.
  The fix is in `patches/`; apply it to the Zephyr tree:

  ```
  git -C $ZEPHYR_BASE am $PWD/patches/0001-net-ethernet-bridge-Do-not-ARP-resolve-broadcast-and.patch
  ```

## Build and flash

```
west build -b mikroe_uni_clicker/stm32f429xx/mcu_card_4 .
west flash -r jlink
west rtt -r jlink
```

The STM32F429 OTG FS controller has only 4 endpoints, too few for CDC NCM
and the board's CDC ACM console together, so the console uses RTT.

`usb_dwc2.overlay` switches from the STM32 HAL USB driver to the generic DWC2
driver: `-DEXTRA_DTC_OVERLAY_FILE=usb_dwc2.overlay`.

## Host setup (Linux)

`wyse/` holds the host side, for a host that is on the LAN over Wi-Fi (a
Wi-Fi interface cannot be bridged): `t1s-lan-proxy.sh` uses parprouted for
proxy ARP and dhcp-helper as DHCP relay, and `t1s-lan-proxy.service` keeps it
up across board resets. Adjust `WIFI`, `USB` and `DHCP_SERVER` in the script,
then see its header for installation.
