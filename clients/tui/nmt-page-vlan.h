/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2013 Red Hat, Inc.
 */

#ifndef NMT_PAGE_VLAN_H
#define NMT_PAGE_VLAN_H

#include "nmt-editor-page-device.h"

G_BEGIN_DECLS

#define NMT_TYPE_PAGE_VLAN            (nmt_page_vlan_get_type ())
#define NMT_PAGE_VLAN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMT_TYPE_PAGE_VLAN, NmtPageVlan))
#define NMT_PAGE_VLAN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NMT_TYPE_PAGE_VLAN, NmtPageVlanClass))
#define NMT_IS_PAGE_VLAN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMT_TYPE_PAGE_VLAN))
#define NMT_IS_PAGE_VLAN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NMT_TYPE_PAGE_VLAN))
#define NMT_PAGE_VLAN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NMT_TYPE_PAGE_VLAN, NmtPageVlanClass))

typedef struct {
	NmtEditorPageDevice parent;

} NmtPageVlan;

typedef struct {
	NmtEditorPageDeviceClass parent;

} NmtPageVlanClass;

GType nmt_page_vlan_get_type (void);

NmtEditorPage *nmt_page_vlan_new (NMConnection   *conn,
                                  NmtDeviceEntry *deventry);

G_END_DECLS

#endif /* NMT_PAGE_VLAN_H */
