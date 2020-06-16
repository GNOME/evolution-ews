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
#include "common/e-o365-connection.h"
#include "common/e-o365-json-utils.h"
#include "camel-o365-store-summary.h"
#include "camel-o365-utils.h"

#include "camel-o365-store.h"

#define LOCK(_store) g_rec_mutex_lock (&(_store->priv->property_lock))
#define UNLOCK(_store) g_rec_mutex_unlock (&(_store->priv->property_lock))

struct _CamelO365StorePrivate {
	GRecMutex property_lock;
	gchar *storage_path;
	CamelO365StoreSummary *summary;
	EO365Connection *cnc;
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

static gboolean
o365_store_construct (CamelService *service,
		      CamelSession *session,
		      CamelProvider *provider,
		      GError **error)
{
	CamelO365Store *o365_store;
	gchar *summary_file, *session_storage_path;
	guint32 store_flags;
	GError *local_error = NULL;

	o365_store = (CamelO365Store *) service;

	store_flags = camel_store_get_flags (CAMEL_STORE (o365_store));

	/* Disable virtual trash and junk folders. Office365 has real folders for that */
	store_flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	store_flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	camel_store_set_flags (CAMEL_STORE (o365_store), store_flags);

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

	summary_file = g_build_filename (o365_store->priv->storage_path, "folder-tree", NULL);
	o365_store->priv->summary = camel_o365_store_summary_new (summary_file);

	if (!camel_o365_store_summary_load (o365_store->priv->summary, &local_error))
		g_warning ("%s: Failed to load store summary '%s': %s", G_STRFUNC, summary_file, local_error ? local_error->message : "Unknown error");

	g_clear_error (&local_error);
	g_free (summary_file);

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

static EO365Connection *
o365_store_ref_connection (CamelO365Store *o365_store)
{
	EO365Connection *cnc = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), NULL);

	LOCK (o365_store);

	if (o365_store->priv->cnc)
		cnc = g_object_ref (o365_store->priv->cnc);

	UNLOCK (o365_store);

	return cnc;
}

static gboolean
o365_store_connect_sync (CamelService *service,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelO365Store *o365_store;
	EO365Connection *cnc;
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	o365_store = CAMEL_O365_STORE (service);
	cnc = o365_store_ref_connection (o365_store);

	if (!cnc) {
		LOCK (o365_store);

		o365_store->priv->cnc = camel_o365_utils_new_connection (service, NULL);

		UNLOCK (o365_store);
	}

	if (cnc) {
		CamelSession *session;

		session = camel_service_ref_session (service);

		success = camel_session_authenticate_sync (session, service, "Office365", cancellable, error);

		g_clear_object (&session);
		g_clear_object (&cnc);
	} else {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, _("Failed to create connection"));
	}

	return success;
}

static gboolean
o365_store_disconnect_sync (CamelService *service,
			    gboolean clean,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelO365Store *o365_store = CAMEL_O365_STORE (service);
	EO365Connection *cnc;
	gboolean success = TRUE;

	cnc = o365_store_ref_connection (o365_store);

	if (cnc) {
		success = e_o365_connection_disconnect_sync (cnc, cancellable, error);

		g_clear_object (&cnc);
	}

	if (!success)
		return FALSE;

	/* Chain up to parent's method. */
	return CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
o365_store_authenticate_sync (CamelService *service,
			      const gchar *mechanism,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelAuthenticationResult result;
	EO365Connection *cnc;

	cnc = o365_store_ref_connection (CAMEL_O365_STORE (service));

	if (!cnc)
		return CAMEL_AUTHENTICATION_ERROR;

	switch (e_o365_connection_authenticate_sync (cnc, cancellable, error)) {
	case E_SOURCE_AUTHENTICATION_ERROR:
	case E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED:
	default:
		result = CAMEL_AUTHENTICATION_ERROR;
		break;
	case E_SOURCE_AUTHENTICATION_ACCEPTED:
		result = CAMEL_AUTHENTICATION_ACCEPTED;
		break;
	case E_SOURCE_AUTHENTICATION_REJECTED:
	case E_SOURCE_AUTHENTICATION_REQUIRED:
		result = CAMEL_AUTHENTICATION_REJECTED;
		break;
	}

	g_clear_object (&cnc);

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
o365_store_save_summary_locked (CamelO365StoreSummary *summary,
				const gchar *where)
{
	GError *error = NULL;

	if (!camel_o365_store_summary_save (summary, &error))
		g_warning ("%s: Failed to save store summary: %s", where, error ? error->message : "Unknown error");

	g_clear_error (&error);
}

typedef struct _FolderRenamedData {
	gchar *id;
	gchar *old_name;
} FolderRenamedData;

static FolderRenamedData *
folder_renamed_data_new (gchar *id,
			 gchar *old_name)
{
	FolderRenamedData *frd;

	frd = g_slice_new (FolderRenamedData);
	frd->id = id;
	frd->old_name = old_name;

	return frd;
}

static void
folder_renamed_data_free (gpointer ptr)
{
	FolderRenamedData *frd = ptr;

	if (frd) {
		g_free (frd->id);
		g_free (frd->old_name);
		g_slice_free (FolderRenamedData, frd);
	}
}

typedef struct _FoldersDeltaData {
	CamelO365Store *o365_store;
	GSList *added_ids; /* gchar *, folder ids */
	GSList *renamed_data; /* FolderRenamedData * */
	GSList *removed_fis; /* CamelFolderInfo * */
} FoldersDeltaData;

static gboolean
camel_o365_got_folders_delta_cb (EO365Connection *cnc,
				 const GSList *results, /* JsonObject * - the returned objects from the server */
				 gpointer user_data, /* expects GSList **, aka pointer to a GSList *, where it copies the 'results' */
				 GCancellable *cancellable,
				 GError **error)
{
	FoldersDeltaData *fdd = user_data;
	GSList *link;

	g_return_val_if_fail (fdd, FALSE);

	LOCK (fdd->o365_store);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *object = link->data;
		const gchar *id = e_o365_mail_folder_get_id (object);

		if (e_o365_delta_is_removed_object (object)) {
			CamelFolderInfo *info;

			info = camel_o365_store_summary_build_folder_info_for_id (fdd->o365_store->priv->summary, id);

			if (info)
				fdd->removed_fis = g_slist_prepend (fdd->removed_fis, info);

			camel_o365_store_summary_remove_folder (fdd->o365_store->priv->summary, id);
		} else {
			gchar *old_full_name = NULL;
			guint32 flags;

			if (camel_o365_store_summary_has_folder (fdd->o365_store->priv->summary, id))
				old_full_name = camel_o365_store_summary_dup_folder_full_name (fdd->o365_store->priv->summary, id);

			flags = e_o365_mail_folder_get_child_folder_count (object) ? CAMEL_STORE_INFO_FOLDER_CHILDREN : CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

			camel_o365_store_summary_set_folder (fdd->o365_store->priv->summary, FALSE, id,
				e_o365_mail_folder_get_parent_folder_id (object),
				e_o365_mail_folder_get_display_name (object),
				e_o365_mail_folder_get_total_item_count (object),
				e_o365_mail_folder_get_unread_item_count (object),
				flags, E_O365_FOLDER_KIND_MAIL, FALSE, FALSE);

			if (old_full_name)
				fdd->renamed_data = g_slist_prepend (fdd->renamed_data, folder_renamed_data_new (g_strdup (id), old_full_name));
			else
				fdd->added_ids = g_slist_prepend (fdd->added_ids, g_strdup (id));
		}
	}

	UNLOCK (fdd->o365_store);

	return TRUE;
}

static CamelFolderInfo *
o365_get_folder_info_sync (CamelStore *store,
			   const gchar *top,
			   guint32 flags,
			   GCancellable *cancellable,
			   GError **error)
{
	CamelO365Store *o365_store;
	CamelFolderInfo *fi;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), NULL);

	o365_store = CAMEL_O365_STORE (store);

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (o365_store))) {
		gboolean refresh_online;

		refresh_online = !(flags & CAMEL_STORE_FOLDER_INFO_FAST) ||
				  (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0;

		if (!refresh_online) {
			gchar *delta_link;

			LOCK (o365_store);

			delta_link = camel_o365_store_summary_dup_delta_link (o365_store->priv->summary);
			refresh_online = !delta_link || !*delta_link;
			g_free (delta_link);

			UNLOCK (o365_store);
		}

		if (refresh_online) {
			EO365Connection *cnc;

			cnc = o365_store_ref_connection (o365_store);

			if (cnc) {
				FoldersDeltaData fdd;
				gchar *old_delta_link, *new_delta_link = NULL;

				LOCK (o365_store);

				old_delta_link = camel_o365_store_summary_dup_delta_link (o365_store->priv->summary);

				UNLOCK (o365_store);

				fdd.o365_store = o365_store;
				fdd.added_ids = NULL;
				fdd.renamed_data = NULL;
				fdd.removed_fis = NULL;

				success = e_o365_connection_get_mail_folders_delta_sync (cnc, NULL, NULL, old_delta_link, 0,
					camel_o365_got_folders_delta_cb, &fdd, &new_delta_link, cancellable, error);

				if (success) {
					CamelSubscribable *subscribable = CAMEL_SUBSCRIBABLE (o365_store);
					CamelFolderInfo *info;
					GSList *link;

					LOCK (o365_store);

					camel_o365_store_summary_set_delta_link (o365_store->priv->summary, new_delta_link);
					o365_store_save_summary_locked (o365_store->priv->summary, G_STRFUNC);

					fdd.added_ids = g_slist_reverse (fdd.added_ids);
					fdd.renamed_data = g_slist_reverse (fdd.renamed_data);
					fdd.removed_fis = g_slist_reverse (fdd.removed_fis);

					if (fdd.added_ids || fdd.renamed_data || fdd.removed_fis)
						camel_o365_store_summary_rebuild_hashes (o365_store->priv->summary);

					for (link = fdd.removed_fis; link; link = g_slist_next (link)) {
						info = link->data;

						camel_subscribable_folder_unsubscribed (subscribable, info);
						camel_store_folder_deleted (store, info);
					}

					for (link = fdd.added_ids; link; link = g_slist_next (link)) {
						const gchar *id = link->data;

						info = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, id);

						if (info) {
							camel_store_folder_created (store, info);
							camel_subscribable_folder_subscribed (subscribable, info);
							camel_folder_info_free (info);
						}
					}

					for (link = fdd.renamed_data; link; link = g_slist_next (link)) {
						const FolderRenamedData *frd = link->data;

						info = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, frd->id);

						if (info) {
							camel_store_folder_renamed (store, frd->old_name, info);
							camel_folder_info_free (info);
						}
					}

					UNLOCK (o365_store);
				}

				g_slist_free_full (fdd.added_ids, g_free);
				g_slist_free_full (fdd.renamed_data, folder_renamed_data_free);
				g_slist_free_full (fdd.removed_fis, (GDestroyNotify) camel_folder_info_free);

				g_clear_object (&cnc);
				g_free (old_delta_link);
				g_free (new_delta_link);
			}
		}
	}

	if (success) {
		LOCK (o365_store);

		fi = camel_o365_store_summary_build_folder_info (o365_store->priv->summary, top, (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0);

		UNLOCK (o365_store);
	} else {
		fi = NULL;
	}

	return fi;
}

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

static void
o365_store_dispose (GObject *object)
{
	CamelO365Store *o365_store = CAMEL_O365_STORE (object);

	LOCK (o365_store);

	if (o365_store->priv->summary) {
		o365_store_save_summary_locked (o365_store->priv->summary, G_STRFUNC);
		g_clear_object (&o365_store->priv->summary);
	}

	g_clear_object (&o365_store->priv->cnc);

	UNLOCK (o365_store);

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
	CamelStoreClass *store_class;

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

	store_class = CAMEL_STORE_CLASS (class);
#if 0
	store_class->get_folder_sync = o365_get_folder_sync;
	store_class->create_folder_sync = o365_create_folder_sync;
	store_class->delete_folder_sync = o365_delete_folder_sync;
	store_class->rename_folder_sync = o365_rename_folder_sync;
#endif
	store_class->get_folder_info_sync = o365_get_folder_info_sync;
#if 0
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
