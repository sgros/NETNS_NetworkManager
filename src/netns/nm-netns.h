/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
 *
 * Søren Sandmann <sandmann@daimi.au.dk>
 * Dan Williams <dcbw@redhat.com>
 * Tambet Ingo <tambet@gmail.com>
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
 * (C) Copyright 2016 Red Hat, Inc.
 */

#ifndef __NM_NETNS_H__
#define __NM_NETNS_H__

#include "nm-connection.h"
#include "nm-exported-object.h"

#define NM_TYPE_NETNS            (nm_netns_get_type ())
#define NM_NETNS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_NETNS, NMNetns))
#define NM_NETNS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NM_TYPE_NETNS, NMNetnsClass))
#define NM_IS_NETNS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_NETNS))
#define NM_IS_NETNS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  NM_TYPE_NETNS))
#define NM_NETNS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  NM_TYPE_NETNS, NMNetnsClass))

#define NM_NETNS_VERSION                        "version"
#define NM_NETNS_STATE                          "state"
#define NM_NETNS_STARTUP                        "startup"
#define NM_NETNS_NETWORKING_ENABLED             "networking-enabled"
#define NM_NETNS_WIRELESS_ENABLED               "wireless-enabled"
#define NM_NETNS_WIRELESS_HARDWARE_ENABLED      "wireless-hardware-enabled"
#define NM_NETNS_WWAN_ENABLED                   "wwan-enabled"
#define NM_NETNS_WWAN_HARDWARE_ENABLED          "wwan-hardware-enabled"
#define NM_NETNS_WIMAX_ENABLED                  "wimax-enabled"
#define NM_NETNS_WIMAX_HARDWARE_ENABLED         "wimax-hardware-enabled"
#define NM_NETNS_ACTIVE_CONNECTIONS             "active-connections"
#define NM_NETNS_CONNECTIVITY                   "connectivity"
#define NM_NETNS_PRIMARY_CONNECTION             "primary-connection"
#define NM_NETNS_PRIMARY_CONNECTION_TYPE        "primary-connection-type"
#define NM_NETNS_ACTIVATING_CONNECTION          "activating-connection"
#define NM_NETNS_DEVICES                        "devices"
#define NM_NETNS_METERED                        "metered"
#define NM_NETNS_GLOBAL_DNS_CONFIGURATION       "global-dns-configuration"
#define NM_NETNS_ALL_DEVICES                    "all-devices"

#define NM_NETNS_DEVICE_ADDED                   "netns-device-added"
#define NM_NETNS_DEVICE_REMOVED                 "netns-device-removed"
#define NM_NETNS_INTERNAL_DEVICE_ADDED          "netns-internal-device-added"
#define NM_NETNS_INTERNAL_DEVICE_REMOVED        "netns-internal-device-removed"
#define NM_NETNS_ACTIVE_CONNECTION_ADDED        "netns-active-connection-added"
#define NM_NETNS_ACTIVE_CONNECTION_REMOVED      "netns-active-connection-removed"

struct _NMNetns {
	NMExportedObject parent_instance;
};

typedef struct {
	NMExportedObjectClass parent_class;
} NMNetnsClass;

#define NM_NETNS_NAME                            "name"

GType nm_netns_get_type (void);

NMNetns *nm_netns_new (const char *name);

gboolean nm_netns_setup (NMNetns *netns);

void nm_netns_stop (NMNetns *netns);

gboolean nm_netns_push (NMNetns *self, NMPNetns **netnsp);

const char *nm_netns_get_name (NMNetns *netns);

NMPNetns *nm_netns_get_ns (NMNetns *self);
NMDefaultRouteManager *nm_netns_get_default_route_manager (NMNetns *self);
NMRouteManager *nm_netns_get_route_manager (NMNetns *self);
NMPlatform *nm_netns_get_platform (NMNetns *self);


NMDevice *nm_netns_get_device_by_ifindex (NMNetns *self, int ifindex);
NMDevice *nm_netns_get_device_by_path (NMNetns *self, const char *device_path);

char *nm_netns_get_connection_iface (NMNetns *self, NMConnection *connection, NMDevice **out_parent, GError **error);

GSList *nm_netns_get_activatable_connections (NMNetns *self);
NMActiveConnection *nm_netns_activate_connection (NMNetns *self,
                                                  NMSettingsConnection *connection,
                                                  const char *specific_object,
                                                  NMDevice *device,
                                                  NMAuthSubject *subject,
                                                  GError **error);

void nm_netns_remove_device (NMNetns *self, NMDevice *device);
void nm_netns_add_device (NMNetns *self, NMDevice *device);

gboolean nm_netns_take_device (NMNetns *self, NMDevice *device, int timeout, void (*callback)(gpointer user_data, gboolean timeout), gpointer user_data);

void nm_netns_device_change_callback_activate_and_remove(NMNetns *self, NMDevice *device);

const GSList *nm_netns_get_devices (NMNetns *netns);


#endif  /* __NM_NETNS_H__ */
