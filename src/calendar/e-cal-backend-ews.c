/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include "libedataserver/e-xml-hash-utils.h"
#include "libedataserver/e-url.h"
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-file-store.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include "e-cal-backend-ews.h"
#include "e-cal-backend-ews-utils.h"
#include "e-ews-connection.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

G_DEFINE_TYPE (ECalBackendEws, e_cal_backend_ews, E_TYPE_CAL_BACKEND_SYNC)

/* Private part of the CalBackendEws structure */
struct _ECalBackendEwsPrivate {
	/* Fields required for online server requests */
	EEwsConnection *cnc;
	gchar *host_url;
	gchar *username;
	gchar *password;
	gchar *folder_id;
	
	CalMode mode;
	gboolean mode_changed;
	ECalBackendStore *store;
	gboolean read_only;

	/* fields for storing info while offline */
	gchar *user_email;
	gchar *local_attachments_store;

	/* A mutex to control access to the private structure for the following */
	GStaticRecMutex rec_mutex;
	icaltimezone *default_zone;
	guint timeout_id;
};

#define PRIV_LOCK(p)   (g_static_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_static_rec_mutex_unlock (&(p)->rec_mutex))

#define PARENT_TYPE E_TYPE_CAL_BACKEND_SYNC
static ECalBackendClass *parent_class = NULL;

static void
e_cal_backend_ews_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			      const gchar *username, const gchar *password, GError **perror)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	const gchar *cache_dir;
	
	cbews = (ECalBackendEws *) backend;
	priv = cbews->priv;

	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (backend));
	
	e_cal_backend_store_load (priv->store);
}

/* Dispose handler for the file backend */
static void
e_cal_backend_ews_dispose (GObject *object)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (object);
	priv = cbews->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_ews_finalize (GObject *object)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (object));

	cbews = E_CAL_BACKEND_EWS (object);
	priv = cbews->priv;

	/* Clean up */
	g_static_rec_mutex_free (&priv->rec_mutex);

	/* TODO Cancel all the server requests */
	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	if (priv->host_url) {
		g_free (priv->host_url);
		priv->host_url = NULL;
	}
	
	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->folder_id) {
		g_free (priv->folder_id);
		priv->folder_id = NULL;
	}

	if (priv->user_email) {
		g_free (priv->user_email);
		priv->user_email = NULL;
	}

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	g_free (priv);
	cbews->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Object initialization function for the file backend */
static void
e_cal_backend_ews_init (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;

	priv = g_new0 (ECalBackendEwsPrivate, 1);

	/* create the mutex for thread safety */
	g_static_rec_mutex_init (&priv->rec_mutex);
	cbews->priv = priv;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbews), TRUE);
}

/* Class initialization function for the gw backend */
static void
e_cal_backend_ews_class_init (ECalBackendEwsClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_ews_dispose;
	object_class->finalize = e_cal_backend_ews_finalize;

	sync_class->open_sync = e_cal_backend_ews_open;
/*	sync_class->is_read_only_sync = e_cal_backend_ews_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_ews_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_ews_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_ews_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_ews_get_static_capabilities;
	sync_class->remove_sync = e_cal_backend_ews_remove;
	sync_class->create_object_sync = e_cal_backend_ews_create_object;
	sync_class->modify_object_sync = e_cal_backend_ews_modify_object;
	sync_class->remove_object_sync = e_cal_backend_ews_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_ews_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_ews_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_ews_send_objects;
	sync_class->get_default_object_sync = e_cal_backend_ews_get_default_object;
	sync_class->get_object_sync = e_cal_backend_ews_get_object;
	sync_class->get_object_list_sync = e_cal_backend_ews_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_ews_get_attachment_list;
	sync_class->add_timezone_sync = e_cal_backend_ews_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_ews_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_ews_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_ews_get_changes;

	backend_class->is_loaded = e_cal_backend_ews_is_loaded;
	backend_class->start_query = e_cal_backend_ews_start_query;
	backend_class->get_mode = e_cal_backend_ews_get_mode;
	backend_class->set_mode = e_cal_backend_ews_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_ews_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_ews_internal_get_timezone;
*/
}
