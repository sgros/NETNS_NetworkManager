/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* nm-platform.c - Handle runtime kernel networking configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>

#include "nm-platform.h"
#include "nm-logging.h"

#define debug(...) nm_log_dbg (LOGD_PLATFORM, __VA_ARGS__)

#define NM_PLATFORM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_PLATFORM, NMPlatformPrivate))

G_DEFINE_TYPE (NMPlatform, nm_platform, G_TYPE_OBJECT)

/* NMPlatform signals */
enum {
	LINK_ADDED,
	LINK_CHANGED,
	LINK_REMOVED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/******************************************************************/

/* Singleton NMPlatform subclass instance and cached class object */
static NMPlatform *platform = NULL;
static NMPlatformClass *klass = NULL;

/**
 * nm_platform_setup:
 * @type: The #GType for a subclass of #NMPlatform
 *
 * Do not use this function directly, it is intended to be called by
 * NMPlatform subclasses. For the linux platform initialization use
 * nm_linux_platform_setup() instead.
 *
 * Failing to set up #NMPlatform singleton results in a fatal error,
 * as well as trying to initialize it multiple times without freeing
 * it.
 *
 * NetworkManager will typically use only one platform object during
 * its run. Test programs might want to switch platform implementations,
 * though. This is done with a combination of nm_platform_free() and
 * nm_*_platform_setup().
 */
void
nm_platform_setup (GType type)
{
	gboolean status;

	g_assert (platform == NULL);

	platform = g_object_new (type, NULL);
	g_assert (NM_IS_PLATFORM (platform));

	klass = NM_PLATFORM_GET_CLASS (platform);
	g_assert (klass->setup);

	status = klass->setup (platform);
	g_assert (status);
}

/**
 * nm_platform_free:
 *
 * Free #NMPlatform singleton created by nm_*_platform_setup().
 */
void
nm_platform_free (void)
{
	g_assert (platform);

	g_object_unref (platform);
	platform = NULL;
}

/**
 * nm_platform_get:
 *
 * Retrieve #NMPlatform singleton. Use this whenever you want to connect to
 * #NMPlatform signals. It is an error to call it before nm_*_platform_setup()
 * or after nm_platform_free().
 *
 * Returns: (transfer none): The #NMPlatform singleton reference.
 */
NMPlatform *
nm_platform_get (void)
{
	g_assert (platform);

	return platform;
}

/******************************************************************/

/**
 * nm_platform_get_error:
 *
 * Convenience function to quickly retrieve the error code of the last
 * operation.
 *
 * Returns: Integer error code.
 */
int
nm_platform_get_error (void)
{
	g_assert (platform);

	return platform->error;
}

/**
 * nm_platform_get_error_message:
 *
 * Returns: Static human-readable string for the error. Don't free.
 */
const char *
nm_platform_get_error_msg (void)
{
	g_assert (platform);

	switch (platform->error) {
	case NM_PLATFORM_ERROR_NONE:
		return "unknown error";
	case NM_PLATFORM_ERROR_NOT_FOUND:
		return "object not found";
	case NM_PLATFORM_ERROR_EXISTS:
		return "object already exists";
	default:
		return "invalid error number";
	}
}

static void
reset_error (void)
{
	g_assert (platform);
	platform->error = NM_PLATFORM_ERROR_NONE;
}

/******************************************************************/

/**
 * nm_platform_link_get_all:
 *
 * Retrieve a snapshot of configuration for all links at once. The result is
 * owned by the caller and should be freed with g_array_unref().
 */
GArray *
nm_platform_link_get_all ()
{
	reset_error ();

	g_return_val_if_fail (klass->link_get_all, NULL);

	return klass->link_get_all (platform);
}

/**
 * nm_platform_link_add:
 * @name: Interface name
 * @type: Interface type
 *
 * Add a software interface. Sets platform->error to NM_PLATFORM_ERROR_EXISTS
 * if interface is already already exists.
 */
static gboolean
nm_platform_link_add (const char *name, NMLinkType type)
{
	reset_error ();

	g_return_val_if_fail (name, FALSE);
	g_return_val_if_fail (klass->link_add, FALSE);

	if (nm_platform_link_exists (name)) {
		debug ("link: already exists");
		platform->error = NM_PLATFORM_ERROR_EXISTS;
		return FALSE;
	}

	return klass->link_add (platform, name, type);
}

/**
 * nm_platform_dummy_add:
 * @name: New interface name
 *
 * Create a software ethernet-like interface
 */
gboolean
nm_platform_dummy_add (const char *name)
{
	g_return_val_if_fail (name, FALSE);

	debug ("link: adding dummy '%s'", name);
	return nm_platform_link_add (name, NM_LINK_TYPE_DUMMY);
}

/**
 * nm_platform_link_exists:
 * @name: Interface name
 *
 * Returns: %TRUE if an interface of this name exists, %FALSE otherwise.
 */
gboolean
nm_platform_link_exists (const char *name)
{
	int ifindex = nm_platform_link_get_ifindex (name);

	reset_error();
	return ifindex > 0;
}

/**
 * nm_platform_link_delete:
 * @ifindex: Interface index
 *
 * Delete a software interface. Sets platform->error to
 * NM_PLATFORM_ERROR_NOT_FOUND if ifindex not available.
 */
gboolean
nm_platform_link_delete (int ifindex)
{
	const char *name;

	reset_error ();

	g_return_val_if_fail (ifindex > 0, FALSE);
	g_return_val_if_fail (klass->link_delete, FALSE);

	name = nm_platform_link_get_name (ifindex);

	if (!name)
		return FALSE;

	debug ("link: deleting '%s' (%d)", name, ifindex);
	return klass->link_delete (platform, ifindex);
}

/**
 * nm_platform_link_delete_by_name:
 * @name: Interface name
 *
 * Delete a software interface.
 */
gboolean
nm_platform_link_delete_by_name (const char *name)
{
	int ifindex = nm_platform_link_get_ifindex (name);

	if (!ifindex)
		return FALSE;

	return nm_platform_link_delete (ifindex);
}

/**
 * nm_platform_link_get_index:
 * @name: Interface name
 *
 * Returns: The interface index corresponding to the given interface name
 * or 0. Inteface name is owned by #NMPlatform, don't free it.
 */
int
nm_platform_link_get_ifindex (const char *name)
{
	int ifindex;

	reset_error ();

	g_return_val_if_fail (name, 0);
	g_return_val_if_fail (klass->link_get_ifindex, 0);

	ifindex = klass->link_get_ifindex (platform, name);

	if (!ifindex) {
		debug ("link not found: %s", name);
		platform->error = NM_PLATFORM_ERROR_NOT_FOUND;
	}

	return ifindex;
}

/**
 * nm_platform_link_get_name:
 * @name: Interface name
 *
 * Returns: The interface name corresponding to the given interface index
 * or NULL.
 */
const char *
nm_platform_link_get_name (int ifindex)
{
	const char *name;

	reset_error ();

	g_return_val_if_fail (ifindex > 0, NULL);
	g_return_val_if_fail (klass->link_get_name, NULL);

	name = klass->link_get_name (platform, ifindex);

	if (!name) {
		debug ("link not found: %d", ifindex);
		platform->error = NM_PLATFORM_ERROR_NOT_FOUND;
		return FALSE;
	}

	return name;
}

/**
 * nm_platform_link_get_type:
 * @ifindex: Interface index.
 *
 * Returns: Link type constant as defined in nm-platform.h. On error,
 * NM_LINK_TYPE_NONE is returned.
 */
NMLinkType
nm_platform_link_get_type (int ifindex)
{
	reset_error ();

	g_return_val_if_fail (klass->link_get_type, NM_LINK_TYPE_NONE);

	return klass->link_get_type (platform, ifindex);
}

/******************************************************************/

static void
log_link_added (NMPlatform *p, NMPlatformLink *info, gpointer user_data)
{
	debug ("signal: link address: '%s' (%d)", info->name, info->ifindex);
}

static void
log_link_changed (NMPlatform *p, NMPlatformLink *info, gpointer user_data)
{
	debug ("signal: link changed: '%s' (%d)", info->name, info->ifindex);
}

static void
log_link_removed (NMPlatform *p, NMPlatformLink *info, gpointer user_data)
{
	debug ("signal: link removed: '%s' (%d)", info->name, info->ifindex);
}

/******************************************************************/

static void
nm_platform_init (NMPlatform *object)
{
}

#define SIGNAL(signal_id, method) signals[signal_id] = \
	g_signal_new_class_handler (NM_PLATFORM_ ## signal_id, \
		G_OBJECT_CLASS_TYPE (object_class), \
		G_SIGNAL_RUN_FIRST, \
		G_CALLBACK (method), \
		NULL, NULL, \
		g_cclosure_marshal_VOID__POINTER, \
		G_TYPE_NONE, 1, G_TYPE_POINTER); \

static void
nm_platform_class_init (NMPlatformClass *platform_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (platform_class);

	/* Signals */
	SIGNAL (LINK_ADDED, log_link_added)
	SIGNAL (LINK_CHANGED, log_link_changed)
	SIGNAL (LINK_REMOVED, log_link_removed)
}