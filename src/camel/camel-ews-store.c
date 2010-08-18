/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.c : class for an groupwise store */

/*
 *  Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-ews-folder.h"
#include "camel-ews-store-summary.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x) x
#define CURSOR_ITEM_LIMIT 100
#define JUNK_ENABLE 1
#define JUNK_PERSISTENCE 14

#define CAMEL_EWS_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_STORE, CamelEwsStorePrivate))

const gchar * CREATE_CURSOR_VIEW = "peek id default recipient threading hasAttachment subject status priority startDate created delivered size recurrenceKey";

struct _CamelEwsStorePrivate {
	gchar *server_name;
	gchar *port;
	gchar *user;
	gchar *use_ssl;

	gchar *base_url;
	gchar *storage_path;

	GHashTable *id_hash; /*get names from ids*/
	GHashTable *name_hash;/*get ids from names*/
	GHashTable *parent_hash;
	EEwsConnection *cnc;
};

extern CamelServiceAuthType camel_ews_password_authtype; /*for the query_auth_types function*/
static CamelFolderInfo *convert_to_folder_info (CamelEwsStore *store, EEwsContainer *container, const gchar *url, GError **error);
static gboolean ews_folders_sync (CamelEwsStore *store, GError **error);
static gint match_path(const gchar *path, const gchar *name);

G_DEFINE_TYPE (CamelEwsStore, camel_ews_store, CAMEL_TYPE_OFFLINE_STORE)

static gboolean
ews_store_construct (CamelService *service, CamelSession *session,
			   CamelProvider *provider, CamelURL *url,
			   GError **error)
{
	CamelServiceClass *service_class;
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	const gchar *property_value;
	CamelEwsStorePrivate *priv = ews_store->priv;
	gchar *path = NULL;

	d(printf ("\nin groupwise store constrcut\n"));

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	if (!(url->host || url->user)) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Host or user not available in url"));
	}

	/*storage path*/
	priv->storage_path = camel_session_get_storage_path (session, service, error);
	if (!priv->storage_path)
		return FALSE;

	/*store summary*/
	path = g_alloca (strlen (priv->storage_path) + 32);
	sprintf (path, "%s/.summary", priv->storage_path);
	ews_store->summary = camel_ews_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *)ews_store->summary, path);
	camel_store_summary_touch ((CamelStoreSummary *)ews_store->summary);
	camel_store_summary_load ((CamelStoreSummary *) ews_store->summary);

	/*host and user*/
	priv->server_name = g_strdup (url->host);
	priv->user = g_strdup (url->user);

	/*base url*/
	priv->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
						       CAMEL_URL_HIDE_PARAMS   |
						       CAMEL_URL_HIDE_AUTH)  );

	/*soap port*/
	property_value =  camel_url_get_param (url, "soap_port");
	if (property_value == NULL)
		priv->port = g_strdup ("7191");
	else if (strlen(property_value) == 0)
		priv->port = g_strdup ("7191");
	else
		priv->port = g_strdup (property_value);

	/*filter*/
	if (camel_url_get_param (url, "filter"))
		store->flags |= CAMEL_STORE_FILTER_INBOX;

	/*Hash Table*/
	priv->id_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->parent_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/*ssl*/
	priv->use_ssl = g_strdup (camel_url_get_param (url, "use_ssl"));

	store->flags &= ~CAMEL_STORE_VJUNK;
	store->flags &= ~CAMEL_STORE_VTRASH;

	return TRUE;
}

static guint
ews_hash_folder_name (gconstpointer key)
{
	return g_str_hash (key);
}

static gint
ews_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	return g_str_equal (aname, bname);
}

static gboolean
ews_auth_loop (CamelService *service, GError **error)
{
#if 0
	CamelSession *session = camel_service_get_session (service);
	CamelStore *store = CAMEL_STORE (service);
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate *priv = ews_store->priv;
	gboolean authenticated = FALSE;
	gchar *uri;
	guint32 prompt_flags = CAMEL_SESSION_PASSWORD_SECRET;
	EEwsConnectionErrors errors = {E_EWS_CONNECTION_STATUS_INVALID_OBJECT, NULL};

	if (priv->use_ssl && !g_str_equal (priv->use_ssl, "never"))
		uri = g_strconcat ("https://", priv->server_name, ":", priv->port, "/soap", NULL);
	else
		uri = g_strconcat ("http://", priv->server_name, ":", priv->port, "/soap", NULL);
	service->url->passwd = NULL;

	while (!authenticated) {

		if (!service->url->passwd && !(store->flags & CAMEL_STORE_PROXY)) {
			gchar *prompt;

			prompt = camel_session_build_password_prompt (
				"GroupWise", service->url->user, service->url->host);
			service->url->passwd =
				camel_session_get_password (session, service, "Ews",
							    prompt, "password", prompt_flags, error);
			g_free (prompt);

			if (!service->url->passwd) {
				g_set_error (
					error, G_IO_ERROR,
					G_IO_ERROR_CANCELLED,
					_("You did not enter a password."));
				return FALSE;
			}
		}

		priv->cnc = e_ews_connection_new_with_error_handler (uri, priv->user, service->url->passwd, &errors);
		if (!E_IS_GW_CONNECTION(priv->cnc) && priv->use_ssl && g_str_equal (priv->use_ssl, "when-possible")) {
			gchar *http_uri = g_strconcat ("http://", uri + 8, NULL);
			priv->cnc = e_ews_connection_new (http_uri, priv->user, service->url->passwd);
			g_free (http_uri);
		}
		if (!E_IS_GW_CONNECTION(priv->cnc)) {
			if (errors.status == E_EWS_CONNECTION_STATUS_INVALID_PASSWORD) {
				/* We need to un-cache the password before prompting again */
				prompt_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
				g_free (service->url->passwd);
				service->url->passwd = NULL;
			} else {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_UNAVAILABLE,
					"%s", errors.description ?
					errors.description :
					_("You must be working online to complete this operation"));
				return FALSE;
			}
		} else
			authenticated = TRUE;

	}
#endif

	return TRUE;
}

static gboolean
check_for_connection (CamelService *service, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (service);
	CamelEwsStorePrivate *priv = ews_store->priv;
	struct addrinfo hints, *ai;
	GError *local_error = NULL;

	memset (&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	ai = camel_getaddrinfo(priv->server_name, "groupwise", &hints, &local_error);
	if (ai == NULL && priv->port != NULL && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error (&local_error);
		ai = camel_getaddrinfo(priv->server_name, priv->port, &hints, &local_error);
	}

	if (ai == NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	camel_freeaddrinfo (ai);

	return TRUE;

}

/* resets the current folder. To just free current folder, pass NULL for folder */
void
ews_store_set_current_folder (CamelEwsStore *ews_store, CamelFolder *folder)
{

	camel_service_lock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (ews_store->current_folder) {
		g_object_unref (ews_store->current_folder);
		ews_store->current_folder = NULL;
	}

	if (folder)
		ews_store->current_folder = g_object_ref (folder);

	camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
}

static gboolean
ews_connect (CamelService *service, GError **error)
{
	CamelEwsStore *store = CAMEL_EWS_STORE (service);
	CamelEwsStorePrivate *priv = store->priv;
	CamelEwsStoreNamespace *ns;
	CamelSession *session = service->session;

	d("in ews store connect\n");

#if 0

/*	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL ||
	     (service->status == CAMEL_SERVICE_DISCONNECTED))
		return FALSE; */
	if (service->status == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	if (!priv) {
		store->priv = g_new0 (CamelEwsStorePrivate, 1);
		priv = store->priv;
		camel_service_construct (service, service->session, service->provider, service->url, error);
	}

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (priv->cnc) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		return TRUE;
	}

	if (!check_for_connection (service, error) || !ews_auth_loop (service, error)) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}

	service->status = CAMEL_SERVICE_CONNECTED;
	((CamelOfflineStore *) store)->state = CAMEL_OFFLINE_STORE_NETWORK_AVAIL;

	if (!e_ews_connection_get_version (priv->cnc)) {
		camel_session_alert_user(session,
				CAMEL_SESSION_ALERT_WARNING,
				_("Some features may not work correctly with your current server version"),
				FALSE);

	}

	ns = camel_ews_store_summary_namespace_new (store->summary, priv->storage_path, '/');
	camel_ews_store_summary_namespace_set (store->summary, ns);

	if (camel_store_summary_count ((CamelStoreSummary *)store->summary) == 0) {
		/*Settting the refresh stamp to the current time*/
		store->refresh_stamp = time (NULL);
	}

	camel_store_summary_save ((CamelStoreSummary *) store->summary);

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
	if (E_IS_GW_CONNECTION (priv->cnc)) {
		return TRUE;
	}
#endif
	return FALSE;

}
#if 0
static void
ews_disconnect_cleanup (CamelService *service, gboolean clean, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (service);
	CamelEwsStorePrivate *priv = ews_store->priv;

	if (ews_store->summary) {
		camel_store_summary_save ((CamelStoreSummary *)ews_store->summary);
		g_object_unref (ews_store->summary);
	}

	if (priv) {
		if (priv->user) {
			g_free (priv->user);
			priv->user = NULL;
		}
		if (priv->server_name) {
			g_free (priv->server_name);
			priv->server_name = NULL;
		}
		if (priv->port) {
			g_free (priv->port);
			priv->port = NULL;
		}
		if (priv->use_ssl) {
			g_free (priv->use_ssl);
			priv->use_ssl = NULL;
		}
		if (priv->base_url) {
			g_free (priv->base_url);
			priv->base_url = NULL;
		}

		if (priv->storage_path)
			g_free(priv->storage_path);

		if (ews_store->root_container)
			g_free (ews_store->root_container);

		if (priv->id_hash)
			g_hash_table_destroy (priv->id_hash);

		if (priv->name_hash)
			g_hash_table_destroy (priv->name_hash);

		if (priv->parent_hash)
			g_hash_table_destroy (priv->parent_hash);

		g_free (ews_store->priv);
		ews_store->priv = NULL;
	}
}
#endif

static gboolean
ews_disconnect (CamelService *service, gboolean clean, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (service);

	if (clean) {
		camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		if (ews_store->priv && ews_store->priv->cnc) {
			g_object_unref (ews_store->priv->cnc);
			ews_store->priv->cnc = NULL;
		}

		ews_store_set_current_folder (ews_store, NULL);

		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
	}

	/* ews_disconnect_cleanup (service, clean, ex); */
	return TRUE;
}

static  GList*
ews_store_query_auth_types (CamelService *service, GError **error)
{
	GList *auth_types = NULL;

	d("in query auth types\n");
	auth_types = g_list_prepend (auth_types,  &camel_ews_password_authtype);
	return auth_types;
}

static gboolean
ews_is_system_folder (const gchar *folder_name)
{
	if (!strcmp (folder_name, "Mailbox") ||
	    !strcmp (folder_name, "Trash") ||
	    !strcmp (folder_name, "Junk Mail") ||
	    !strcmp (folder_name, "Sent Items") ||
	    !strcmp (folder_name, "Cabinet") ||
	    !strcmp (folder_name, "Documents") )
		return TRUE;
	else
		return FALSE;
}

/*Build/populate CamelFolderInfo structure based on the imap_build_folder_info function*/
static CamelFolderInfo *
ews_build_folder_info(CamelEwsStore *ews_store, const gchar *parent_name, const gchar *folder_name)
{
	CamelURL *url;
	const gchar *name;
	CamelFolderInfo *fi;
	CamelEwsStorePrivate *priv = ews_store->priv;

	fi = camel_folder_info_new();

	fi->unread = -1;
	fi->total = -1;

	if (parent_name) {
		if (strlen(parent_name) > 0)
			fi->full_name = g_strconcat(parent_name, "/", folder_name, NULL);
		else
			fi->full_name = g_strdup (folder_name);
	} else
		fi->full_name = g_strdup(folder_name);

	url = camel_url_new(priv->base_url,NULL);
	g_free(url->path);
	url->path = g_strdup_printf("/%s", fi->full_name);
	fi->uri = camel_url_to_string(url,CAMEL_URL_HIDE_ALL);
	camel_url_free(url);

	name = strrchr(fi->full_name,'/');
	if (name == NULL)
		name = fi->full_name;
	else
		name++;
	if (!strcmp (folder_name, "Sent Items"))
		fi->flags |= CAMEL_FOLDER_TYPE_SENT;
	else if (!strcmp (folder_name, "Mailbox"))
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	else if (!strcmp (folder_name, "Trash"))
		fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	else if (!strcmp (folder_name, "Junk Mail"))
		fi->flags |= CAMEL_FOLDER_TYPE_JUNK;

	if (ews_is_system_folder (folder_name))
		fi->flags |= CAMEL_FOLDER_SYSTEM;

	fi->name = g_strdup(name);
	return fi;
}

static void
ews_forget_folder (CamelEwsStore *ews_store, const gchar *folder_name, GError **error)
{
	CamelEwsStorePrivate *priv = ews_store->priv;
	gchar *state_file;
	gchar *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const gchar *name;

	name = folder_name;

	storage_path = g_strdup_printf ("%s/folders", priv->storage_path);
	folder_dir = e_path_to_physical (storage_path,folder_name);

	if (g_access(folder_dir, F_OK) != 0) {
		g_free(folder_dir);
		return;
	}

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

	camel_store_summary_remove_path ( (CamelStoreSummary *)ews_store->summary, folder_name);
	camel_store_summary_save ( (CamelStoreSummary *)ews_store->summary);

	fi = ews_build_folder_info(ews_store, NULL, folder_name);
	camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
	camel_folder_info_free (fi);
}

static CamelFolder *
ews_get_folder_from_disk (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate *priv = ews_store->priv;
	CamelFolder *folder;
	gchar *folder_dir, *storage_path;

	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
	folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	if (!folder_dir || g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
		return NULL;
	}

	folder = camel_ews_folder_new (store, folder_name, folder_dir, error);
	g_free (folder_dir);

	return folder;
}

static CamelFolder *
ews_get_folder (CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate *priv = ews_store->priv;
	CamelFolder *folder;
	CamelEwsSummary *summary;
	gchar *container_id, *folder_dir, *storage_path;
	EEwsConnectionStatus status;
	GList *list = NULL;
	gboolean done = FALSE, all_ok = TRUE;
	const gchar *position = E_EWS_CURSOR_POSITION_END;
	gint count = 0, cursor, summary_count = 0;
	CamelStoreInfo *si = NULL;
	guint total = 0;
	GError *local_error = NULL;

#if 0
	folder = ews_get_folder_from_disk (
		store, folder_name, flags, &local_error);
	if (folder) {
		ews_store_set_current_folder (ews_store, folder);
		return folder;

	/* Ignore "no such folder" errors, fail on any other error. */
	} else if (!g_error_matches (local_error,
		CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER)) {
		g_propagate_error (error, local_error);
		return NULL;
	} else
		g_clear_error (&local_error);

	camel_service_lock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	ews_store_set_current_folder (ews_store, NULL);

	if (!camel_ews_store_connected (ews_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return NULL;
	}

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!ews_connect (CAMEL_SERVICE(store), error)) {
			camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			return NULL;
		}
	}

	container_id =	g_strdup (g_hash_table_lookup (priv->name_hash, folder_name));

	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
	folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	folder = camel_ews_folder_new (store, folder_name, folder_dir, NULL);
	if (!folder) {
		camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Authentication failed"));
		g_free (folder_dir);
		g_free (container_id);
		return NULL;
	}
	g_free (folder_dir);

	si = camel_store_summary_path ((CamelStoreSummary *)ews_store->summary, folder_name);
	if (si) {
		total = si->total;
		camel_store_summary_info_free ((CamelStoreSummary *)(ews_store)->summary, si);
	}

	summary = (CamelEwsSummary *) folder->summary;

	summary_count = camel_folder_summary_count (folder->summary);
	if (!summary_count || !summary->time_string) {
		d(g_print ("\n\n** %s **: No summary as yet : using get cursor request\n\n", folder->name);)

		status = e_ews_connection_create_cursor (priv->cnc, container_id,
				CREATE_CURSOR_VIEW,
				NULL,
				&cursor);
		if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_ews_connection_create_cursor (priv->cnc, container_id,
				CREATE_CURSOR_VIEW,
				NULL,
				&cursor);

		if (status != E_EWS_CONNECTION_STATUS_OK) {
			camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			g_free (container_id);
			return NULL;
		}

		camel_operation_start (
			NULL, _("Fetching summary information for new messages in %s"),
			camel_folder_get_name (folder));
		camel_folder_summary_clear (folder->summary);

		while (!done) {
				status = e_ews_connection_read_cursor (priv->cnc, container_id,
								cursor, FALSE,
								CURSOR_ITEM_LIMIT, position, &list);
				if (status != E_EWS_CONNECTION_STATUS_OK) {
						all_ok = FALSE;
						break;
				}

				/* This full block is repeated in the reload_folder code as well. We need
				better modularity */
				count += CURSOR_ITEM_LIMIT;

				if (total > 0) {
						d(printf ("Doing readcursor : [total: %d] [count: %d]\n", total, count));

						if (count > total)
								count = total;

						camel_operation_progress (NULL, (100*count)/total);
				}

				ews_update_summary (folder, list,  error);

				/* For shared-folders created by the user, we don't get the total number of messages,
				in the getFolderList call. So, we need to wait until an empty list is returned in the
				read cursor call. Hence, we need the !list checking in the code below */
				if (count == total || !list)
						done = TRUE;

				g_list_foreach (list, (GFunc)g_object_unref, NULL);
				g_list_free (list);
				list = NULL;
				position = E_EWS_CURSOR_POSITION_CURRENT;
		}

		e_ews_connection_destroy_cursor (priv->cnc, container_id, cursor);

		camel_operation_end (NULL);
	}
	if (done && all_ok) {
		if (summary->time_string)
			g_free (summary->time_string);
		summary->time_string = g_strdup (e_ews_connection_get_server_time (priv->cnc));
	}

	camel_folder_summary_save_to_db (folder->summary, NULL);

	ews_store_set_current_folder (ews_store, folder);

	g_free (container_id);
	camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return folder;
#endif
	return NULL;
}

gboolean
ews_store_reload_folder (CamelEwsStore *ews_store, CamelFolder *folder, guint32 flags, GError **error)
{
#if 0
	CamelEwsStorePrivate *priv = ews_store->priv;
	CamelEwsSummary *summary;
	gchar *container_id;
	EEwsConnectionStatus status;
	GList *list = NULL;
	gboolean done = FALSE;
	const gchar *position = E_EWS_CURSOR_POSITION_END;
	gint count = 0, cursor, summary_count = 0;
	CamelStoreInfo *si = NULL;
	const gchar *full_name;
	const gchar *name;
	guint total = 0;

	name = camel_folder_get_name (folder);
	full_name = camel_folder_get_full_name (folder);

	camel_service_lock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_ews_store_connected (ews_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return FALSE;
	}

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!ews_connect (CAMEL_SERVICE((CamelStore*)ews_store), error)) {
			camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
			return FALSE;
		}
	}

	container_id =	g_strdup (g_hash_table_lookup (priv->name_hash, full_name));

	si = camel_store_summary_path ((CamelStoreSummary *)ews_store->summary, name);
	if (si) {
		total = si->total;
		camel_store_summary_info_free ((CamelStoreSummary *)(ews_store)->summary, si);
	}

	summary = (CamelEwsSummary *) folder->summary;
	camel_folder_summary_clear (folder->summary);
	camel_folder_summary_save_to_db (folder->summary, NULL);

	summary_count = camel_folder_summary_count (folder->summary);
	if (!summary_count || !summary->time_string) {
			d(g_print ("\n\n** %s **: Summary missing???? Reloading summary....\n\n", name);)

					status = e_ews_connection_create_cursor (priv->cnc, container_id,
									CREATE_CURSOR_VIEW,
									NULL,
									&cursor);
			if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_ews_connection_create_cursor (priv->cnc, container_id,
									CREATE_CURSOR_VIEW,
									NULL,
									&cursor);
			if (status != E_EWS_CONNECTION_STATUS_OK) {
					camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
					g_free (container_id);
					return FALSE;
			}

			camel_operation_start (
				NULL, _("Fetching summary information for new messages in %s"),
				camel_folder_get_name (folder));

			while (!done) {
					status = e_ews_connection_read_cursor (priv->cnc, container_id,
									cursor, FALSE,
									CURSOR_ITEM_LIMIT, position, &list);
					if (status != E_EWS_CONNECTION_STATUS_OK) {
							camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
							e_ews_connection_destroy_cursor (priv->cnc, container_id, cursor);
							camel_folder_summary_save_to_db (folder->summary, NULL);
							g_set_error (
								error, CAMEL_SERVICE_ERROR,
								CAMEL_SERVICE_ERROR_INVALID,
								_("Authentication failed"));
							camel_operation_end (NULL);
							g_free (container_id);
							return FALSE;
					}
					/* This full block is repeated in the get_folder code as well. We need
					   better modularity */
					count += CURSOR_ITEM_LIMIT;

					if (total > 0) {
							d(printf ("Doing readcursor : [total: %d] [count: %d]\n", total, count));

							if (count > total)
									count = total;

							camel_operation_progress (NULL, (100*count)/total);
					}

					ews_update_summary (folder, list,  error);

					/* For shared-folders created by the user, we don't get the total number of messages,
					   in the getFolderList call. So, we need to wait until an empty list is returned in the
					   read cursor call. Hence, we need the !list checking in the code below */
					if (count == total || !list)
							done = TRUE;

					g_list_foreach (list, (GFunc)g_object_unref, NULL);
					g_list_free (list);
					list = NULL;
					position = E_EWS_CURSOR_POSITION_CURRENT;
			}

			e_ews_connection_destroy_cursor (priv->cnc, container_id, cursor);

			camel_operation_end (NULL);
	}

	if (done) {
		if (summary->time_string)
			g_free (summary->time_string);
		summary->time_string = g_strdup (e_ews_connection_get_server_time (priv->cnc));
	}

	camel_folder_summary_save_to_db (folder->summary, NULL);

	ews_store_set_current_folder (ews_store, NULL);

	g_free (container_id);
	camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
#endif
	return TRUE;
}

static CamelFolderInfo *
convert_to_folder_info (CamelEwsStore *store, EEwsContainer *container, const gchar *url, GError **error)
{
	const gchar *name = NULL, *id = NULL, *parent = NULL;
	gchar *par_name = NULL;
	CamelFolderInfo *fi;
	CamelEwsStoreInfo *si = NULL;
	CamelEwsStorePrivate *priv = store->priv;
	EEwsContainerType type;

	name = e_ews_container_get_name (container);
	id = e_ews_container_get_id (container);
	type = e_ews_container_get_container_type (container);

	fi = camel_folder_info_new ();

	if (type == E_EWS_CONTAINER_TYPE_INBOX)
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	if (type == E_EWS_CONTAINER_TYPE_TRASH)
		fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	if (type == E_EWS_CONTAINER_TYPE_SENT)
		fi->flags |= CAMEL_FOLDER_TYPE_SENT;

	if ( (type == E_EWS_CONTAINER_TYPE_INBOX) ||
		(type == E_EWS_CONTAINER_TYPE_SENT) ||
		(type == E_EWS_CONTAINER_TYPE_DOCUMENTS) ||
		(type == E_EWS_CONTAINER_TYPE_QUERY) ||
		(type == E_EWS_CONTAINER_TYPE_CHECKLIST) ||
		(type == E_EWS_CONTAINER_TYPE_DRAFT) ||
		(type == E_EWS_CONTAINER_TYPE_CABINET) ||
		(type == E_EWS_CONTAINER_TYPE_JUNK) ||
		(type == E_EWS_CONTAINER_TYPE_TRASH) )
		fi->flags |= CAMEL_FOLDER_SYSTEM;
	/*
	   parent_hash contains the "parent id <-> container id" combination. So we form
	   the path for the full name in camelfolder info by looking up the hash table until
	   NULL is found
	 */

	parent = e_ews_container_get_parent_id (container);
	par_name = g_hash_table_lookup (priv->id_hash, parent);

	if (par_name != NULL) {
		gchar *temp_parent = NULL, *temp = NULL;
		gchar *str = g_strconcat (par_name, "/", name, NULL);

		fi->name = g_strdup (name);

		temp_parent = g_hash_table_lookup (priv->parent_hash, parent);
		while (temp_parent) {
			temp = g_hash_table_lookup (priv->id_hash, temp_parent );
			if (temp == NULL) {
				break;
			}
			str = g_strconcat ( temp, "/", str, NULL);

			temp_parent = g_hash_table_lookup (priv->parent_hash, temp_parent);

		}
		fi->full_name = g_strdup (str);
		fi->uri = g_strconcat (url, str, NULL);
		g_free (str);
	}
	else {
		fi->name =  g_strdup (name);
		fi->full_name = g_strdup (name);
		fi->uri = g_strconcat (url, "", name, NULL);
	}

	si = camel_ews_store_summary_add_from_full (store->summary, fi->full_name, '/');
	if (si == NULL) {
		camel_folder_info_free (fi);
		return NULL;
	}

	/*name_hash returns the container id given the name */
	g_hash_table_insert (priv->name_hash, g_strdup(fi->full_name), g_strdup(id));

	if (e_ews_container_get_is_shared_to_me (container))
		fi->flags |= CAMEL_FOLDER_SHARED_TO_ME;

	if (e_ews_container_get_is_shared_by_me (container))
		fi->flags |= CAMEL_FOLDER_SHARED_BY_ME;

	if (e_ews_container_get_is_system_folder (container))
		fi->flags |= CAMEL_FOLDER_SYSTEM;

	si->info.flags = fi->flags;
	/*refresh info*/
	if (store->current_folder
	    && !strcmp (camel_folder_get_full_name (store->current_folder), fi->full_name)
	    && type != E_EWS_CONTAINER_TYPE_INBOX) {
		CAMEL_FOLDER_GET_CLASS (store->current_folder)->refresh_info (store->current_folder, error);
	}
	return fi;
}

static void
get_folders_free (gpointer k, gpointer v, gpointer d)
{
	CamelFolderInfo *fi = v;
	camel_folder_info_free (fi);
}

static gboolean
ews_folders_sync (CamelEwsStore *store, GError **error)
{
	CamelEwsStorePrivate  *priv = store->priv;
	gint status;
	GList *folder_list = NULL, *temp_list = NULL, *list = NULL;
	gchar *url, *temp_url;
	CamelFolderInfo *info = NULL, *hfi = NULL;
	GHashTable *present;
	CamelStoreInfo *si = NULL;
	gint count, i;

#if 0
	status = e_ews_connection_get_container_list (priv->cnc, "folders", &folder_list);
	if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_ews_connection_get_container_list (priv->cnc, "folders", &folder_list);
	if (status != E_EWS_CONNECTION_STATUS_OK) {
		g_warning ("Could not get folder list..\n");
		return TRUE;
	}

	temp_list = folder_list;
	list = folder_list;

	url = camel_url_to_string (CAMEL_SERVICE(store)->url,
				   (CAMEL_URL_HIDE_PASSWORD|
				    CAMEL_URL_HIDE_PARAMS|
				    CAMEL_URL_HIDE_AUTH) );

	if ( url[strlen(url) - 1] != '/') {
		temp_url = g_strconcat (url, "/", NULL);
		g_free ((gchar *)url);
		url = temp_url;
	}

	/*populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL; temp_list = g_list_next (temp_list) ) {
		const gchar *name, *id, *parent;
		name = e_ews_container_get_name (E_EWS_CONTAINER (temp_list->data));
		id = e_ews_container_get_id(E_EWS_CONTAINER(temp_list->data));
		parent = e_ews_container_get_parent_id (E_EWS_CONTAINER(temp_list->data));

		if (e_ews_container_is_root (E_EWS_CONTAINER(temp_list->data))) {
			if (store->root_container)
				g_free (store->root_container);
			store->root_container = g_strdup (id);
			continue;
		}

		/*id_hash returns the name for a given container id*/
		g_hash_table_insert (priv->id_hash, g_strdup(id), g_strdup(name));
		/*parent_hash returns the parent container id, given an id*/
		g_hash_table_insert (priv->parent_hash, g_strdup(id), g_strdup(parent));
	}

	present = g_hash_table_new (g_str_hash, g_str_equal);

	for (;folder_list != NULL; folder_list = g_list_next (folder_list)) {
		EEwsContainerType type;
		EEwsContainer *container = E_EWS_CONTAINER (folder_list->data);

		type = e_ews_container_get_container_type (container);

		if (e_ews_container_is_root(container))
			continue;
		if ((type == E_EWS_CONTAINER_TYPE_CALENDAR) || (type == E_EWS_CONTAINER_TYPE_CONTACTS))
			continue;

		info = convert_to_folder_info (store, E_EWS_CONTAINER (folder_list->data), (const gchar *)url, error);
		if (info) {
			hfi = g_hash_table_lookup (present, info->full_name);
			if (hfi == NULL)
				g_hash_table_insert (present, info->full_name, info);
			else {
				camel_folder_info_free (info);
				info = NULL;
			}
		}
	}

	g_free ((gchar *)url);
	e_ews_connection_free_container_list (list);
	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);

	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index ((CamelStoreSummary *)store->summary, i);
		if (si == NULL)
			continue;

		info = g_hash_table_lookup (present, camel_store_info_path (store->summary, si));
		if (info != NULL) {
			camel_store_summary_touch ((CamelStoreSummary *)store->summary);
		} else {
			camel_store_summary_remove ((CamelStoreSummary *)store->summary, si);
			count--;
			i--;
		}
		camel_store_summary_info_free ((CamelStoreSummary *)store->summary, si);
	}

	g_hash_table_foreach (present, get_folders_free, NULL);
	g_hash_table_destroy (present);
#endif
	return TRUE;
}

static CamelFolderInfo *
ews_get_folder_info_offline (CamelStore *store, const gchar *top,
			 guint32 flags, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelFolderInfo *fi;
	GPtrArray *folders;
	gchar *path, *name;
	gint i;

	folders = g_ptr_array_new ();

	if (top == NULL)
		top = "";

	/* get starting point */
	if (top[0] == 0) {
			name = g_strdup("");
	} else {
		name = camel_ews_store_summary_full_from_path(ews_store->summary, top);
		if (name == NULL)
			name = camel_ews_store_summary_path_to_full(ews_store->summary, top, '/');
	}

	path = ews_concat (name, "*");

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)ews_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)ews_store->summary, i);

		if (si == NULL)
			continue;

		if ( !strcmp(name, camel_ews_store_info_full_name (ews_store->summary, si))
		     || match_path (path, camel_ews_store_info_full_name (ews_store->summary, si))) {
			fi = ews_build_folder_info(ews_store, NULL, camel_store_info_path((CamelStoreSummary *)ews_store->summary, si));
			fi->unread = si->unread;
			fi->total = si->total;
			fi->flags = si->flags;
			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)ews_store->summary, si);
	}

	g_free(name);
	g_free (path);
	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	return fi;
}

static CamelFolderInfo *
ews_get_folder_info (CamelStore *store, const gchar *top, guint32 flags, GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelFolderInfo *info = NULL;

	/* Do not call ews_store_connected function as it would internall call folders_sync
	   to populate the hash table which is used for mapping container id */
	if (!(((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL
	    && camel_service_connect ((CamelService *)store, error)))
		goto offline;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!ews_folders_sync (ews_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return NULL;
	}

	camel_store_summary_touch ((CamelStoreSummary *)ews_store->summary);
	camel_store_summary_save ((CamelStoreSummary *)ews_store->summary);

	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

offline:
	info = ews_get_folder_info_offline (store, top, flags, error);
	return info;
}

/* To create a junk mail folder in case  we want it and it isn't there*/
CamelFolderInfo *
create_junk_folder (CamelStore *store)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate  *priv = ews_store->priv;
	CamelFolderInfo *root = NULL;
	const gchar *parent_name, *folder_name, *child_container_id, *parent_id;
	gint status;

	parent_name = "";
	folder_name = "Junk Mail";
	parent_id = "";
	/* TODO: check for offlining*/
#if 0
	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	status = e_ews_connection_modify_junk_settings (priv->cnc, JUNK_ENABLE, 0, 0,  JUNK_PERSISTENCE);
	if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_ews_connection_modify_junk_settings (priv->cnc, JUNK_ENABLE, 0, 0,  JUNK_PERSISTENCE);
	if (status == E_EWS_CONNECTION_STATUS_OK) {
		root = ews_build_folder_info(ews_store, parent_name, folder_name);
		camel_store_summary_save((CamelStoreSummary *)ews_store->summary);

		child_container_id = e_ews_connection_get_container_id (priv->cnc, "Junk Mail");
		if (!child_container_id)
			g_warning("failed to retrieve id for junk folder");

		g_hash_table_insert (priv->id_hash, g_strdup(child_container_id), g_strdup(folder_name));
		g_hash_table_insert (priv->name_hash, g_strdup(folder_name), g_strdup(child_container_id));
		g_hash_table_insert (priv->parent_hash, g_strdup(child_container_id), g_strdup(parent_id));
		camel_store_folder_created (store, root);
	}
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return root;
#endif
	return NULL;
}

static CamelFolderInfo*
ews_create_folder(CamelStore *store,
		const gchar *parent_name,
		const gchar *folder_name,
		GError **error)
{
#if 0
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate  *priv = ews_store->priv;
	CamelFolderInfo *root = NULL;
	const gchar *parent_id;
	gchar *child_container_id;
	gint status;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create GroupWise folders in offline mode."));
		return NULL;
	}

	if (parent_name == NULL) {
		parent_name = "";
		if (ews_is_system_folder (folder_name)) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create a special system folder"));
			return NULL;
		}
	}

	if (parent_name && (strlen(parent_name) > 0) ) {
		if (strcmp (parent_name, "Cabinet") && ews_is_system_folder (parent_name)) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_STATE,
				_("The parent folder is not allowed to contain subfolders"));
			return NULL;
		}
		parent_id = g_hash_table_lookup (priv->name_hash, parent_name);
	} else
		parent_id = "";

	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!ews_connect (CAMEL_SERVICE(store), error)) {
			return NULL;
		}
	}
	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	status = e_ews_connection_create_folder(priv->cnc,parent_id,folder_name, &child_container_id);
	if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_ews_connection_create_folder(priv->cnc,parent_id,folder_name, &child_container_id);
	if (status == E_EWS_CONNECTION_STATUS_OK) {
		root = ews_build_folder_info(ews_store, parent_name,folder_name);
		camel_store_summary_save((CamelStoreSummary *)ews_store->summary);

		g_hash_table_insert (priv->id_hash, g_strdup(child_container_id), g_strdup(folder_name));
		g_hash_table_insert (priv->name_hash, g_strdup(root->full_name), g_strdup(child_container_id));
		g_hash_table_insert (priv->parent_hash, g_strdup(child_container_id), g_strdup(parent_id));

		camel_store_folder_created (store, root);
	}
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
	return root;
#endif
	return NULL;
}

static gboolean
ews_delete_folder(CamelStore *store,
				   const gchar *folder_name,
				   GError **error)
{
#if 0	
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate  *priv = ews_store->priv;
	EEwsConnectionStatus status;
	const gchar * container;

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_ews_store_connected (ews_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return FALSE;
	}

	container = g_hash_table_lookup (priv->name_hash, folder_name);

	status = e_ews_connection_remove_item (priv->cnc, container, container);
	if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_ews_connection_remove_item (priv->cnc, container, container);

	if (status == E_EWS_CONNECTION_STATUS_OK) {
		ews_store_set_current_folder (ews_store, NULL);

		ews_forget_folder(ews_store,folder_name,NULL);

		g_hash_table_remove (priv->id_hash, container);
		g_hash_table_remove (priv->name_hash, folder_name);

		g_hash_table_remove (priv->parent_hash, container);
	}
	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
#endif
	return TRUE;
}

static gboolean
ews_rename_folder(CamelStore *store,
			const gchar *old_name,
			const gchar *new_name,
			GError **error)
{
#if 0
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStorePrivate  *priv = ews_store->priv;
	gchar *oldpath, *newpath, *storepath;
	const gchar *container_id;
	gchar *temp_new = NULL;

	if (ews_is_system_folder (old_name)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot rename GroupWise folder '%s' to '%s'"),
			old_name, new_name);
		return FALSE;
	}

	camel_service_lock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (!camel_ews_store_connected (ews_store, error)) {
		camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return FALSE;
	}

	ews_store_set_current_folder (ews_store, NULL);

	container_id = camel_ews_store_container_id_lookup (ews_store, old_name);
	temp_new = strrchr (new_name, '/');
	if (temp_new)
		temp_new++;
	else
		temp_new = (gchar *)new_name;

	if (!container_id || e_ews_connection_rename_folder (priv->cnc, container_id , temp_new) != E_EWS_CONNECTION_STATUS_OK)
	{
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot rename GroupWise folder '%s' to '%s'"),
			old_name, new_name);
		camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return FALSE;
	}

	g_hash_table_replace (priv->id_hash, g_strdup(container_id), g_strdup(temp_new));

	g_hash_table_insert (priv->name_hash, g_strdup(new_name), g_strdup(container_id));
	g_hash_table_remove (priv->name_hash, old_name);
	/*FIXME:Update all the id in the parent_hash*/

	storepath = g_strdup_printf ("%s/folders", priv->storage_path);
	oldpath = e_path_to_physical (storepath, old_name);
	newpath = e_path_to_physical (storepath, new_name);
	g_free (storepath);

	/*XXX: make sure the summary is also renamed*/
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
				oldpath, newpath, g_strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);
	camel_service_unlock (CAMEL_SERVICE (ews_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
#endif
	g_print ("Rename not implemented yet");
	return TRUE;

}

gchar *
ews_get_name(CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf(_("Exchange server %s"), service->url->host);
	else
		return g_strdup_printf(_("Exchange service for %s on %s"),
				       service->url->user, service->url->host);
}

const gchar *
camel_ews_store_container_id_lookup (CamelEwsStore *ews_store, const gchar *folder_name)
{
	CamelEwsStorePrivate *priv = ews_store->priv;

	return g_hash_table_lookup (priv->name_hash, folder_name);
}

const gchar *
camel_ews_store_folder_lookup (CamelEwsStore *ews_store, const gchar *container_id)
{
	CamelEwsStorePrivate *priv = ews_store->priv;

	return g_hash_table_lookup (priv->id_hash, container_id);
}

EEwsConnection *
cnc_lookup (CamelEwsStorePrivate *priv)
{
	return priv->cnc;
}

gchar *
storage_path_lookup (CamelEwsStorePrivate *priv)
{
	return priv->storage_path;
}

const gchar *
ews_base_url_lookup (CamelEwsStorePrivate *priv)
{
	return priv->base_url;
}

static CamelFolder *
ews_get_trash (CamelStore *store, GError **error)
{
	CamelFolder *folder = camel_store_get_folder(store, "Trash", 0, error);
	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		 gchar *state = g_build_filename((CAMEL_EWS_STORE(store))->priv->storage_path, "folders", "Trash", "cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free(state);
		camel_object_state_read (object);

		return folder;
	} else
		return NULL;
}

static gboolean
ews_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	gboolean res;

	res = CAMEL_STORE_CLASS(camel_ews_store_parent_class)->can_refresh_folder (store, info, error) ||
	      (camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL);

	return res;
}

/*
 * Function to check if we are both connected and are _actually_
 * online. Based on an equivalient function in IMAP
 */
gboolean
camel_ews_store_connected (CamelEwsStore *store, GError **error)
{
	return TRUE;
}
#if 0
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL
	    && camel_service_connect ((CamelService *)store, error)) {
		CamelEwsStore *ews_store = (CamelEwsStore *) store;
		CamelEwsStorePrivate *priv = ews_store->priv;

		if (g_hash_table_size (priv->name_hash) == 0)
			return ews_folders_sync ((CamelEwsStore *) ews_store, error);

		return TRUE;
	}
	/*Not online, so return FALSE*/
	return FALSE;
}

static gint
match_path(const gchar *path, const gchar *name)
{
	gchar p, n;

	p = *path++;
	n = *name++;
	while (n && p) {
		if (n == p) {
			p = *path++;
			n = *name++;
		} else if (p == '%') {
			if (n != '/') {
				n = *name++;
			} else {
				p = *path++;
			}
		} else if (p == '*') {
			return TRUE;
		} else
			return FALSE;
	}

	return n == 0 && (p == '%' || p == 0);
}
#endif
static void
ews_store_dispose (GObject *object)
{
	CamelEwsStore *ews_store;

	ews_store = CAMEL_EWS_STORE (object);

	if (ews_store->summary != NULL) {
		camel_store_summary_save (
			CAMEL_STORE_SUMMARY (ews_store->summary));
		g_object_unref (ews_store->summary);
		ews_store->summary = NULL;
	}

	if (ews_store->priv->cnc != NULL) {
		g_object_unref (ews_store->priv->cnc);
		ews_store->priv->cnc = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->dispose (object);
}

static void
ews_store_finalize (GObject *object)
{
	CamelEwsStore *ews_store;

	ews_store = CAMEL_EWS_STORE (object);

	g_free (ews_store->priv->user);
	g_free (ews_store->priv->server_name);
	g_free (ews_store->priv->port);
	g_free (ews_store->priv->use_ssl);
	g_free (ews_store->priv->base_url);
	g_free (ews_store->priv->storage_path);
	g_free (ews_store->root_container);

	if (ews_store->priv->id_hash != NULL)
		g_hash_table_destroy (ews_store->priv->id_hash);

	if (ews_store->priv->name_hash != NULL)
		g_hash_table_destroy (ews_store->priv->name_hash);

	if (ews_store->priv->parent_hash != NULL)
		g_hash_table_destroy (ews_store->priv->parent_hash);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->finalize (object);
}

static void
camel_ews_store_class_init (CamelEwsStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	g_type_class_add_private (class, sizeof (CamelEwsStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_store_dispose;
	object_class->finalize = ews_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = ews_store_construct;
	service_class->query_auth_types = ews_store_query_auth_types;
	service_class->get_name = ews_get_name;
	service_class->connect = ews_connect;
	service_class->disconnect = ews_disconnect;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = ews_hash_folder_name;
	store_class->compare_folder_name = ews_compare_folder_name;
	store_class->get_folder = ews_get_folder;
	store_class->create_folder = ews_create_folder;
	store_class->delete_folder = ews_delete_folder;
	store_class->rename_folder = ews_rename_folder;
	store_class->get_folder_info = ews_get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->get_trash = ews_get_trash;
	store_class->can_refresh_folder = ews_can_refresh_folder;
}

static void
camel_ews_store_init (CamelEwsStore *ews_store)
{
	ews_store->priv =
		CAMEL_EWS_STORE_GET_PRIVATE (ews_store);

	d("in ews store init\n");
	ews_store->priv->server_name = NULL;
	ews_store->priv->port = NULL;
	ews_store->priv->use_ssl = NULL;
	ews_store->priv->user = NULL;
	ews_store->priv->cnc = NULL;
	ews_store->current_folder = NULL;
}
