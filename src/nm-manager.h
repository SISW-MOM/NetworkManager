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
 * Copyright (C) 2007 - 2008 Novell, Inc.
 * Copyright (C) 2007 - 2010 Red Hat, Inc.
 */

#ifndef __NETWORKMANAGER_MANAGER_H__
#define __NETWORKMANAGER_MANAGER_H__

#include "nm-exported-object.h"
#include "nm-settings-connection.h"

#define NM_TYPE_MANAGER            (nm_manager_get_type ())
#define NM_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_MANAGER, NMManager))
#define NM_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_MANAGER, NMManagerClass))
#define NM_IS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_MANAGER))
#define NM_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_MANAGER))
#define NM_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_MANAGER, NMManagerClass))

#define NM_MANAGER_VERSION "version"
#define NM_MANAGER_CAPABILITIES "capabilities"
#define NM_MANAGER_STATE "state"
#define NM_MANAGER_STARTUP "startup"
#define NM_MANAGER_NETWORKING_ENABLED "networking-enabled"
#define NM_MANAGER_WIRELESS_ENABLED "wireless-enabled"
#define NM_MANAGER_WIRELESS_HARDWARE_ENABLED "wireless-hardware-enabled"
#define NM_MANAGER_WWAN_ENABLED "wwan-enabled"
#define NM_MANAGER_WWAN_HARDWARE_ENABLED "wwan-hardware-enabled"
#define NM_MANAGER_WIMAX_ENABLED "wimax-enabled"
#define NM_MANAGER_WIMAX_HARDWARE_ENABLED "wimax-hardware-enabled"
#define NM_MANAGER_ACTIVE_CONNECTIONS "active-connections"
#define NM_MANAGER_CONNECTIVITY "connectivity"
#define NM_MANAGER_PRIMARY_CONNECTION "primary-connection"
#define NM_MANAGER_PRIMARY_CONNECTION_TYPE "primary-connection-type"
#define NM_MANAGER_ACTIVATING_CONNECTION "activating-connection"
#define NM_MANAGER_DEVICES "devices"
#define NM_MANAGER_METERED "metered"
#define NM_MANAGER_GLOBAL_DNS_CONFIGURATION "global-dns-configuration"
#define NM_MANAGER_ALL_DEVICES "all-devices"

/* Not exported */
#define NM_MANAGER_HOSTNAME "hostname"
#define NM_MANAGER_SLEEPING "sleeping"

/* signals */
#define NM_MANAGER_CHECK_PERMISSIONS         "check-permissions"
#define NM_MANAGER_DEVICE_ADDED              "device-added"
#define NM_MANAGER_DEVICE_REMOVED            "device-removed"
#define NM_MANAGER_STATE_CHANGED             "state-changed"
#define NM_MANAGER_USER_PERMISSIONS_CHANGED  "user-permissions-changed"

/* Internal signals */
#define NM_MANAGER_ACTIVE_CONNECTION_ADDED   "active-connection-added"
#define NM_MANAGER_ACTIVE_CONNECTION_REMOVED "active-connection-removed"
#define NM_MANAGER_CONFIGURE_QUIT            "configure-quit"
#define NM_MANAGER_INTERNAL_DEVICE_ADDED     "internal-device-added"
#define NM_MANAGER_INTERNAL_DEVICE_REMOVED   "internal-device-removed"


GType nm_manager_get_type (void);

/* nm_manager_setup() should only be used by main.c */
NMManager *   nm_manager_setup                         (void);

NMManager *   nm_manager_get                           (void);

gboolean      nm_manager_start                         (NMManager *manager,
                                                        GError **error);
void          nm_manager_stop                          (NMManager *manager);
NMState       nm_manager_get_state                     (NMManager *manager);
const GSList *nm_manager_get_active_connections        (NMManager *manager);
GSList *      nm_manager_get_activatable_connections   (NMManager *manager);

void          nm_manager_write_device_state (NMManager *manager);

/* Device handling */

const GSList *      nm_manager_get_devices             (NMManager *manager);
const char **       nm_manager_get_device_paths        (NMManager *self);

NMDevice *          nm_manager_get_device_by_ifindex   (NMManager *manager,
                                                        int ifindex);
NMDevice *          nm_manager_get_device_by_path      (NMManager *manager,
                                                        const char *path);

char *              nm_manager_get_connection_iface (NMManager *self,
                                                     NMConnection *connection,
                                                     NMDevice **out_parent,
                                                     GError **error);

NMActiveConnection *nm_manager_activate_connection     (NMManager *manager,
                                                        NMSettingsConnection *connection,
                                                        const char *specific_object,
                                                        NMDevice *device,
                                                        NMAuthSubject *subject,
                                                        GError **error);

gboolean            nm_manager_deactivate_connection   (NMManager *manager,
                                                        const char *connection_path,
                                                        NMDeviceStateReason reason,
                                                        GError **error);

void                nm_manager_set_capability   (NMManager *self, NMCapability cap);

#endif /* __NETWORKMANAGER_MANAGER_H__ */
