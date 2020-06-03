/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "common/camel-o365-settings.h"

#include "camel-o365-store.h"

struct _CamelO365StorePrivate {
	GRecMutex property_lock;
	gchar *storage_path;
};

static void camel_o365_store_initable_init (GInitableIface *iface);
static void camel_o365_subscribable_init (CamelSubscribableInterface *iface);
static GInitableIface *parent_initable_interface;

enum {
	PROP_0,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE
};

G_DEFINE_TYPE_WITH_CODE (CamelO365Store, camel_o365_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_o365_store_initable_init)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SERVICE, NULL)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_SUBSCRIBABLE, camel_o365_subscribable_init)
	G_ADD_PRIVATE (CamelO365Store))

static void
o365_store_set_property (GObject *object,
			 guint property_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			camel_network_service_set_connectable (
				CAMEL_NETWORK_SERVICE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_store_get_property (GObject *object,
			 guint property_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			g_value_take_object (
				value,
				camel_network_service_ref_connectable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
		case PROP_HOST_REACHABLE:
			g_value_set_boolean (
				value,
				camel_network_service_get_host_reachable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static gboolean
o365_store_construct (CamelService *service,
		      CamelSession *session,
		      CamelProvider *provider,
		      GError **error)
{
	CamelO365Store *o365_store;
	gchar /**summary_file,*/ *session_storage_path;
	guint32 store_flags;

	o365_store = (CamelO365Store *) service;

	store_flags = camel_store_get_flags (CAMEL_STORE (o365_store));

	/* Disable virtual trash and junk folders. Office365 has real
	 * folders for that */
	store_flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	store_flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	camel_store_set_flags (CAMEL_STORE (o365_store), store_flags);

	/*storage path*/
	session_storage_path = g_strdup (camel_service_get_user_cache_dir (service));
	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}

	o365_store->priv->storage_path = session_storage_path;

	g_mkdir_with_parents (o365_store->priv->storage_path, 0700);
	/*summary_file = g_build_filename (o365_store->storage_path, "folder-tree", NULL);
	o365_store->summary = camel_o365_store_summary_new (summary_file);
	camel_o365_store_summary_load (o365_store->summary, NULL);

	g_free (summary_file);*/

	return TRUE;
}

static gboolean
o365_store_initable_init (GInitable *initable,
			  GCancellable *cancellable,
			  GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	gboolean ret;

	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	camel_store_set_flags (store, camel_store_get_flags (store) |
		CAMEL_STORE_USE_CACHE_DIR |
		CAMEL_STORE_SUPPORTS_INITIAL_SETUP |
		CAMEL_STORE_CAN_DELETE_FOLDERS_AT_ONCE);

	/* Chain up to parent interface's method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	session = camel_service_ref_session (service);

	ret = o365_store_construct (service, session, NULL, error);

	g_object_unref (session);

	return ret;
}

static GList *
o365_store_query_auth_types_sync (CamelService *service,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (CAMEL_IS_O365_STORE (service), NULL);

	return NULL;
}

static gchar *
o365_store_get_name (CamelService *service,
		     gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Office365 server"));
	else
		name = g_strdup (_("Mail receive via Microsoft Office365"));

	return name;
}

static gboolean
o365_store_connect_sync (CamelService *service,
			 GCancellable *cancellable,
			 GError **error)
{
	/*CamelO365Store *o365_store;*/
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	/*o365_store = CAMEL_O365_STORE (service);

	g_signal_connect_swapped (
		o365_settings,
		"notify::listen-notifications",
		G_CALLBACK (camel_o365_store_listen_notifications_cb),
		o365_store);

	g_signal_connect_swapped (
		o365_settings,
		"notify::check-all",
		G_CALLBACK (camel_o365_store_check_all_cb),
		o365_store);*/

	return success;
}

static gboolean
o365_store_disconnect_sync (CamelService *service,
			    gboolean clean,
			    GCancellable *cancellable,
			    GError **error)
{
	/*CamelO365Store *o365_store = CAMEL_O365_STORE (service);

	g_mutex_lock (&o365_store->priv->connection_lock);
	o365_store_unset_connection_locked (o365_store, TRUE);
	g_mutex_unlock (&o365_store->priv->connection_lock);*/

	return CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
o365_store_authenticate_sync (CamelService *service,
			      const gchar *mechanism,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelAuthenticationResult result;
	/*CamelO365Store *o365_store;

	o365_store = CAMEL_O365_STORE (service);*/

	result = CAMEL_AUTHENTICATION_ERROR;

	return result;
}

gboolean
camel_o365_store_connected (CamelO365Store *o365_store,
			    GCancellable *cancellable,
			    GError **error)
{
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (o365_store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect_sync ((CamelService *) o365_store, cancellable, error))
		return FALSE;

	return TRUE;
}

void
camel_o365_store_maybe_disconnect (CamelO365Store *store,
				   const GError *error)
{
	CamelService *service;

	g_return_if_fail (store != NULL);

	if (!error)
		return;

	service = CAMEL_SERVICE (store);

	if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
		return;

#if 0
	if (g_error_matches (error, O365_CONNECTION_ERROR, O365_CONNECTION_ERROR_NORESPONSE) ||
	    g_error_matches (error, O365_CONNECTION_ERROR, O365_CONNECTION_ERROR_AUTHENTICATION_FAILED))
		camel_service_disconnect_sync (service, FALSE, NULL, NULL);
#endif
}

static void
o365_store_dispose (GObject *object)
{
	/*CamelO365Store *o365_store = CAMEL_O365_STORE (object);*/

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_parent_class)->dispose (object);
}

static void
o365_store_finalize (GObject *object)
{
	CamelO365Store *o365_store;

	o365_store = CAMEL_O365_STORE (object);

	g_rec_mutex_clear (&o365_store->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_parent_class)->finalize (object);
}

static void
camel_o365_store_class_init (CamelO365StoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	/*CamelStoreClass *store_class;*/

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = o365_store_set_property;
	object_class->get_property = o365_store_get_property;
	object_class->dispose = o365_store_dispose;
	object_class->finalize = o365_store_finalize;

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_CONNECTABLE,
		"connectable");

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_HOST_REACHABLE,
		"host-reachable");

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_O365_SETTINGS;
	service_class->query_auth_types_sync = o365_store_query_auth_types_sync;
	service_class->get_name = o365_store_get_name;
	service_class->connect_sync = o365_store_connect_sync;
	service_class->disconnect_sync = o365_store_disconnect_sync;
	service_class->authenticate_sync = o365_store_authenticate_sync;
#if 0
	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = o365_get_folder_sync;
	store_class->create_folder_sync = o365_create_folder_sync;
	store_class->delete_folder_sync = o365_delete_folder_sync;
	store_class->rename_folder_sync = o365_rename_folder_sync;
	store_class->get_folder_info_sync = o365_get_folder_info_sync;
	store_class->initial_setup_sync = o365_initial_setup_sync;
	store_class->get_trash_folder_sync = o365_get_trash_folder_sync;
	store_class->get_junk_folder_sync = o365_get_junk_folder_sync;
	store_class->can_refresh_folder = o365_can_refresh_folder;
#endif
}

static void
camel_o365_store_initable_init (GInitableIface *iface)
{
	parent_initable_interface = g_type_interface_peek_parent (iface);

	iface->init = o365_store_initable_init;
}

static void
camel_o365_subscribable_init (CamelSubscribableInterface *iface)
{
#if 0
	iface->folder_is_subscribed = o365_store_folder_is_subscribed;
	iface->subscribe_folder_sync = o365_store_subscribe_folder_sync;
	iface->unsubscribe_folder_sync = o365_store_unsubscribe_folder_sync;
#endif
}

static void
camel_o365_store_init (CamelO365Store *o365_store)
{
	o365_store->priv = camel_o365_store_get_instance_private (o365_store);

	g_rec_mutex_init (&o365_store->priv->property_lock);
}
