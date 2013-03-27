/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2005 - 2012 Red Hat, Inc.
 * Copyright (C) 2006 - 2008 Novell, Inc.
 */

#include "config.h"
#include <glib.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_infiniband.h>
#include <netinet/ether.h>
#include <linux/sockios.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "nm-device-wired.h"
#include "nm-device-private.h"
#include "nm-dhcp-manager.h"
#include "nm-logging.h"
#include "nm-netlink-monitor.h"
#include "nm-netlink-utils.h"
#include "nm-system.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"


G_DEFINE_TYPE (NMDeviceWired, nm_device_wired, NM_TYPE_DEVICE)

#define NM_DEVICE_WIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_WIRED, NMDeviceWiredPrivate))

#define NM_DEVICE_WIRED_LOG_LEVEL(dev) ((nm_device_get_device_type (dev) == NM_DEVICE_TYPE_INFINIBAND) ? LOGD_INFINIBAND : LOGD_ETHER)

typedef struct {
	gboolean            carrier;
	guint32             speed;

	NMNetlinkMonitor *  monitor;
	gulong              link_connected_id;
	gulong              link_disconnected_id;

} NMDeviceWiredPrivate;


/* Returns speed in Mb/s */
static guint32
ethtool_get_speed (NMDeviceWired *self)
{
	int fd;
	struct ifreq ifr;
	struct ethtool_cmd edata = {
		.cmd = ETHTOOL_GSET,
	};
	guint32 speed = 0;

	g_return_val_if_fail (self != NULL, 0);

	fd = socket (PF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		nm_log_warn (LOGD_HW, "couldn't open control socket.");
		return 0;
	}

	memset (&ifr, 0, sizeof (struct ifreq));
	strncpy (ifr.ifr_name, nm_device_get_iface (NM_DEVICE (self)), IFNAMSIZ);
	ifr.ifr_data = (char *) &edata;

	if (ioctl (fd, SIOCETHTOOL, &ifr) < 0)
		goto out;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	speed = edata.speed;
#else
	speed = ethtool_cmd_speed (&edata);
#endif

	if (speed == G_MAXUINT16 || speed == G_MAXUINT32)
		speed = 0;

out:
	close (fd);
	return speed;
}

static void
set_speed (NMDeviceWired *self, const guint32 speed)
{
	NMDeviceWiredPrivate *priv;

	g_return_if_fail (NM_IS_DEVICE (self));

	priv = NM_DEVICE_WIRED_GET_PRIVATE (self);
	if (priv->speed == speed)
		return;

	priv->speed = speed;
	g_object_notify (G_OBJECT (self), "speed");

	nm_log_dbg (LOGD_HW | NM_DEVICE_WIRED_LOG_LEVEL (NM_DEVICE (self)),
	             "(%s): speed is now %d Mb/s",
	             nm_device_get_iface (NM_DEVICE (self)),
	             speed);
}

static void
set_carrier (NMDeviceWired *self,
             gboolean carrier)
{
	NMDevice *device = NM_DEVICE (self);
	NMDeviceWiredPrivate *priv = NM_DEVICE_WIRED_GET_PRIVATE (self);
	guint32 caps;

	if (priv->carrier == carrier)
		return;

	/* Warn if we try to set carrier down on a device that
	 * doesn't support carrier detect.  These devices assume
	 * the carrier is always up.
	 */
	caps = nm_device_get_capabilities (device);
	g_return_if_fail (caps & NM_DEVICE_CAP_CARRIER_DETECT);

	priv->carrier = carrier;
	g_object_notify (G_OBJECT (self), "carrier");
}

static void
carrier_on (NMNetlinkMonitor *monitor,
            int idx,
            gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);
	NMDeviceWired *self = NM_DEVICE_WIRED (device);

	/* Make sure signal is for us */
	if (idx == nm_device_get_ifindex (device)) {
		set_carrier (self, TRUE);
		set_speed (self, ethtool_get_speed (self));
	}
}

static void
carrier_off (NMNetlinkMonitor *monitor,
             int idx,
             gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);
	NMDeviceWired *self = NM_DEVICE_WIRED (device);

	/* Make sure signal is for us */
	if (idx == nm_device_get_ifindex (device))
		set_carrier (self, FALSE);
}

static gboolean
get_carrier_sync (NMDeviceWired *self)
{
	NMDeviceWiredPrivate *priv = NM_DEVICE_WIRED_GET_PRIVATE (self);
	GError *error = NULL;
	guint32 ifflags = 0;

	/* Get initial link state */
	if (!nm_netlink_monitor_get_flags_sync (priv->monitor,
	                                        nm_device_get_ip_ifindex (NM_DEVICE (self)),
	                                        &ifflags,
	                                        &error)) {
		nm_log_warn (LOGD_HW | NM_DEVICE_WIRED_LOG_LEVEL (NM_DEVICE (self)),
		             "(%s): couldn't get carrier state: (%d) %s",
		             nm_device_get_ip_iface (NM_DEVICE (self)),
		             error ? error->code : -1,
		             (error && error->message) ? error->message : "unknown");
		g_clear_error (&error);
	}

	return !!(ifflags & IFF_LOWER_UP);
}

static GObject*
constructor (GType type,
			 guint n_construct_params,
			 GObjectConstructParam *construct_params)
{
	GObject *object;
	NMDeviceWiredPrivate *priv;
	NMDevice *self;
	guint32 caps;

	object = G_OBJECT_CLASS (nm_device_wired_parent_class)->constructor (type,
	                                                                     n_construct_params,
	                                                                     construct_params);
	if (!object)
		return NULL;

	self = NM_DEVICE (object);
	priv = NM_DEVICE_WIRED_GET_PRIVATE (self);

	nm_log_dbg (LOGD_HW | NM_DEVICE_WIRED_LOG_LEVEL (NM_DEVICE (self)),
	            "(%s): kernel ifindex %d",
	            nm_device_get_iface (NM_DEVICE (self)),
	            nm_device_get_ifindex (NM_DEVICE (self)));

	caps = nm_device_get_capabilities (self);
	if (caps & NM_DEVICE_CAP_CARRIER_DETECT) {
		/* Only listen to netlink for cards that support carrier detect */
		priv->monitor = nm_netlink_monitor_get ();

		priv->link_connected_id = g_signal_connect (priv->monitor, "carrier-on",
		                                            G_CALLBACK (carrier_on),
		                                            self);
		priv->link_disconnected_id = g_signal_connect (priv->monitor, "carrier-off",
		                                               G_CALLBACK (carrier_off),
		                                               self);

		priv->carrier = get_carrier_sync (NM_DEVICE_WIRED (self));

		nm_log_info (LOGD_HW | NM_DEVICE_WIRED_LOG_LEVEL (NM_DEVICE (self)),
		             "(%s): carrier is %s",
		             nm_device_get_iface (NM_DEVICE (self)),
		             priv->carrier ? "ON" : "OFF");

		/* Request link state again just in case an error occurred getting the
		 * initial link state.
		 */
		nm_netlink_monitor_request_status (priv->monitor);
	} else {
		nm_log_info (LOGD_HW | NM_DEVICE_WIRED_LOG_LEVEL (NM_DEVICE (self)),
		             "(%s): driver '%s' does not support carrier detection.",
		             nm_device_get_iface (self),
		             nm_device_get_driver (self));
		priv->carrier = TRUE;
	}

	return object;
}

static void
nm_device_wired_init (NMDeviceWired * self)
{
}

static gboolean
hw_bring_up (NMDevice *dev, gboolean *no_firmware)
{
	gboolean result, carrier;
	guint32 caps;

	result = NM_DEVICE_CLASS(nm_device_wired_parent_class)->hw_bring_up (dev, no_firmware);
	if (result) {
		caps = nm_device_get_capabilities (dev);
		if (caps & NM_DEVICE_CAP_CARRIER_DETECT) {
			carrier = get_carrier_sync (NM_DEVICE_WIRED (dev));
			set_carrier (NM_DEVICE_WIRED (dev), carrier);
		}
	}
	return result;
}

/* Returns %TRUE if @self is unavailable for connections because it
 * needs carrier but does not have it.
 */
static gboolean
nm_device_wired_unavailable_because_of_carrier (NMDeviceWired *self)
{
	NMDeviceWiredPrivate *priv = NM_DEVICE_WIRED_GET_PRIVATE (self);

	return !priv->carrier && !nm_device_ignore_carrier (NM_DEVICE (self));
}

static gboolean
can_interrupt_activation (NMDevice *dev)
{
	NMDeviceWired *self = NM_DEVICE_WIRED (dev);

	/* Devices that support carrier detect can interrupt activation
	 * if the link becomes inactive.
	 */
	return nm_device_wired_unavailable_because_of_carrier (self);
}

static gboolean
is_available (NMDevice *dev)
{
	NMDeviceWired *self = NM_DEVICE_WIRED (dev);

	return !nm_device_wired_unavailable_because_of_carrier (self);
}

static NMActStageReturn
act_stage3_ip4_config_start (NMDevice *device,
                             NMIP4Config **out_config,
                             NMDeviceStateReason *reason)
{
	if (   nm_device_is_master (device)
	    && nm_device_wired_unavailable_because_of_carrier (NM_DEVICE_WIRED (device))) {
		nm_log_info (LOGD_IP4 | NM_DEVICE_WIRED_LOG_LEVEL (device),
		             "(%s): IPv4 config waiting until carrier is on",
		             nm_device_get_ip_iface (device));
		return NM_ACT_STAGE_RETURN_WAIT;
	}

	return NM_DEVICE_CLASS (nm_device_wired_parent_class)->act_stage3_ip4_config_start (device, out_config, reason);
}

static NMActStageReturn
act_stage3_ip6_config_start (NMDevice *device,
                             NMIP6Config **out_config,
                             NMDeviceStateReason *reason)
{
	if (   nm_device_is_master (device)
	    && nm_device_wired_unavailable_because_of_carrier (NM_DEVICE_WIRED (device))) {
		nm_log_info (LOGD_IP6 | NM_DEVICE_WIRED_LOG_LEVEL (device),
		             "(%s): IPv6 config waiting until carrier is on",
		             nm_device_get_ip_iface (device));
		return NM_ACT_STAGE_RETURN_WAIT;
	}

	return NM_DEVICE_CLASS (nm_device_wired_parent_class)->act_stage3_ip6_config_start (device, out_config, reason);
}

static void
dispose (GObject *object)
{
	NMDeviceWired *self = NM_DEVICE_WIRED (object);
	NMDeviceWiredPrivate *priv = NM_DEVICE_WIRED_GET_PRIVATE (self);

	if (priv->link_connected_id) {
		g_signal_handler_disconnect (priv->monitor, priv->link_connected_id);
		priv->link_connected_id = 0;
	}
	if (priv->link_disconnected_id) {
		g_signal_handler_disconnect (priv->monitor, priv->link_disconnected_id);
		priv->link_disconnected_id = 0;
	}

	G_OBJECT_CLASS (nm_device_wired_parent_class)->dispose (object);
}

static void
nm_device_wired_class_init (NMDeviceWiredClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMDeviceWiredPrivate));

	/* virtual methods */
	object_class->constructor = constructor;
	object_class->dispose = dispose;

	parent_class->hw_bring_up = hw_bring_up;
	parent_class->can_interrupt_activation = can_interrupt_activation;
	parent_class->is_available = is_available;
	parent_class->act_stage3_ip4_config_start = act_stage3_ip4_config_start;
	parent_class->act_stage3_ip6_config_start = act_stage3_ip6_config_start;
}

/**
 * nm_device_wired_get_carrier:
 * @dev: an #NMDeviceWired
 *
 * Get @dev's carrier status
 *
 * Return value: @dev's carrier
 */
gboolean
nm_device_wired_get_carrier (NMDeviceWired *dev)
{
	NMDeviceWiredPrivate *priv;

	g_return_val_if_fail (dev != NULL, FALSE);

	priv = NM_DEVICE_WIRED_GET_PRIVATE (dev);
	return priv->carrier;
}

/**
 * nm_device_wired_get_speed:
 * @dev: an #NMDeviceWired
 *
 * Get @dev's speed
 *
 * Return value: @dev's speed in Mb/s
 */
guint32
nm_device_wired_get_speed (NMDeviceWired *dev)
{
	NMDeviceWiredPrivate *priv;

	g_return_val_if_fail (dev != NULL, 0);

	priv = NM_DEVICE_WIRED_GET_PRIVATE (dev);
	return priv->speed;
}