/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Sivaiah Nallagatla <snallagatla@novell.com>
 * SPDX-FileContributor: Parthasarathi Susarla <sparthasarathi@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "e-ews-common-utils.h"
#include "common/camel-ews-settings.h"
#include "common/e-ews-camel-common.h"

#include "camel-ews-store.h"
#include "camel-ews-transport.h"
#include "camel-ews-utils.h"

#define REPLY_VIEW "default message attachments threading"

struct _CamelEwsTransportPrivate
{
	GMutex connection_lock;
	EEwsConnection *connection;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelEwsTransport, camel_ews_transport, CAMEL_TYPE_TRANSPORT)

static gboolean
ews_transport_can_server_side_sent_folder (CamelService *service,
					   EwsFolderId **folder_id,
					   GCancellable *cancellable)
{
	ESourceRegistry *registry;
	ESource *sibling, *source = NULL;
	gboolean is_server_side = FALSE;

	g_return_val_if_fail (CAMEL_IS_EWS_TRANSPORT (service), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);

	registry = e_source_registry_new_sync (cancellable, NULL);
	if (!registry)
		return FALSE;

	sibling = e_source_registry_ref_source (registry, camel_service_get_uid (service));
	if (sibling) {
		CamelSession *session;
		GList *sources, *siter;

		session = camel_service_ref_session (service);
		sources = e_source_registry_list_sources (registry, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
		for (siter = sources; siter; siter = siter->next) {
			source = siter->data;

			if (!source || g_strcmp0 (e_source_get_parent (source), e_source_get_parent (sibling)) != 0 ||
			    !e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION) ||
			    !e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_COMPOSITION))
				source = NULL;
			else
				break;
		}

		if (source &&
		    e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION) &&
		    e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_COMPOSITION)) {
			ESourceMailSubmission *subm_extension;
			CamelStore *store = NULL;
			gchar *folder_name = NULL;

			subm_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);

			/* Copy messages on the server side only if the replies
			   might not be saved to the original folder, which is handled
			   by the evolution itself. */
			if (!e_source_mail_submission_get_replies_to_origin_folder (subm_extension) &&
			    e_source_mail_submission_get_sent_folder (subm_extension) &&
			    e_ews_common_utils_mail_folder_uri_parse (session,
				e_source_mail_submission_get_sent_folder (subm_extension),
				&store, &folder_name, NULL) & CAMEL_IS_EWS_STORE (store)) {
				CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
				gchar *folder_id_str;

				folder_id_str = camel_ews_store_summary_get_folder_id_from_name (
					ews_store->summary, folder_name);
				if (folder_id_str) {
					gchar *change_key;

					change_key = camel_ews_store_summary_get_change_key (ews_store->summary, folder_name, NULL);
					*folder_id = e_ews_folder_id_new (folder_id_str, change_key, FALSE);
					g_free (change_key);

					is_server_side = *folder_id != NULL;
				}

				g_free (folder_id_str);
			}

			g_clear_object (&store);
			g_free (folder_name);
		}

		g_list_free_full (sources, g_object_unref);
		g_object_unref (sibling);
		g_clear_object (&session);
	}

	g_object_unref (registry);

	return is_server_side;
}

static EEwsConnection *
ews_transport_ref_connection (CamelEwsTransport *ews_transport)
{
	EEwsConnection *connection = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_TRANSPORT (ews_transport), NULL);

	g_mutex_lock (&ews_transport->priv->connection_lock);

	if (ews_transport->priv->connection)
		connection = g_object_ref (ews_transport->priv->connection);

	g_mutex_unlock (&ews_transport->priv->connection_lock);

	return connection;
}

static gboolean
ews_transport_connect_sync (CamelService *service,
			    GCancellable *cancellable,
			    GError **error)
{
	EEwsConnection *connection;
	CamelSession *session;
	CamelSettings *settings;
	const gchar *auth_mech;
	gboolean success;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_ews_transport_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	connection = ews_transport_ref_connection (CAMEL_EWS_TRANSPORT (service));
	if (connection) {
		g_object_unref (connection);
		return TRUE;
	}

	session = camel_service_ref_session (service);
	settings = camel_service_ref_settings (service);

	/* Try running an operation that requires authentication
	 * to make sure we have valid credentials available. */
	auth_mech = camel_ews_settings_get_auth_mechanism_string (CAMEL_EWS_SETTINGS (settings));

	success = camel_session_authenticate_sync (session, service,
			   auth_mech ? auth_mech : "NTLM", cancellable, error);

	g_object_unref (session);
	g_object_unref (settings);

	return success;
}

static gboolean
ews_transport_disconnect_sync (CamelService *service,
			       gboolean clean,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelEwsTransport *ews_transport = CAMEL_EWS_TRANSPORT (service);

	g_mutex_lock (&ews_transport->priv->connection_lock);
	g_clear_object (&ews_transport->priv->connection);
	g_mutex_unlock (&ews_transport->priv->connection_lock);

	return CAMEL_SERVICE_CLASS (camel_ews_transport_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
ews_transport_authenticate_sync (CamelService *service,
				 const gchar *mechanism,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelAuthenticationResult result;
	CamelEwsTransport *ews_transport;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	EEwsConnection *connection;
	ESource *source;
	const gchar *password;
	gchar *hosturl, *new_sync_state = NULL;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder = FALSE;
	GError *local_error = NULL;

	ews_transport = CAMEL_EWS_TRANSPORT (service);

	password = camel_service_get_password (service);

	settings = camel_service_ref_settings (service);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);
	source = camel_ews_utils_ref_corresponding_source (service, cancellable);

	connection = e_ews_connection_new (source, hosturl, ews_settings);
	e_ews_connection_set_password (connection, password);

	g_clear_object (&source);
	g_free (hosturl);

	g_object_unref (settings);

	e_binding_bind_property (
		service, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	/* XXX We need to run some operation that requires authentication
	 *     but does not change any server-side state, so we can check
	 *     the error status and determine if our password is valid.
	 *     David suggested e_ews_connection_sync_folder_hierarchy(),
	 *     since we have to do that eventually anyway. */

	e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, NULL,
		&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	g_slist_free_full (folders_created, g_object_unref);
	g_slist_free_full (folders_updated, g_object_unref);
	g_slist_free_full (folders_deleted, g_free);
	g_free (new_sync_state);

	if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_UNAVAILABLE)) {
		local_error->domain = CAMEL_SERVICE_ERROR;
		local_error->code = CAMEL_SERVICE_ERROR_UNAVAILABLE;
	}

	if (!local_error) {
		g_mutex_lock (&ews_transport->priv->connection_lock);
		g_clear_object (&ews_transport->priv->connection);
		ews_transport->priv->connection = g_object_ref (connection);
		g_mutex_unlock (&ews_transport->priv->connection_lock);
	} else {
		g_mutex_lock (&ews_transport->priv->connection_lock);
		g_clear_object (&ews_transport->priv->connection);
		g_mutex_unlock (&ews_transport->priv->connection_lock);
	}

	if (!local_error) {
		result = CAMEL_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED)) {
		g_clear_error (&local_error);
		result = CAMEL_AUTHENTICATION_REJECTED;
	} else {
		g_propagate_error (error, local_error);
		result = CAMEL_AUTHENTICATION_ERROR;
	}

	g_object_unref (connection);

	return result;
}

static gchar *
ews_transport_get_name (CamelService *service,
                        gboolean brief)
{
	CamelSettings *settings;
	gchar *host;
	gchar *name;

	settings = camel_service_ref_settings (service);

	host = camel_ews_utils_get_host_name (settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (
			_("Exchange server %s"), host);
	else
		name = g_strdup_printf (
			_("Exchange mail delivery via %s"), host);

	g_free (host);

	return name;
}

static gboolean
ews_send_to_sync (CamelTransport *transport,
                  CamelMimeMessage *message,
                  CamelAddress *from,
                  CamelAddress *recipients,
		  gboolean *out_sent_message_saved,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelEwsSettings *ews_settings;
	CamelInternetAddress *used_from;
	CamelSettings *settings;
	CamelService *service;
	EEwsConnection *cnc;
	EwsFolderId *folder_id = NULL;
	gchar *ews_email;
	gchar *host_url;
	gchar *user;
	gboolean success = FALSE;

	service = CAMEL_SERVICE (transport);

	settings = camel_service_ref_settings (service);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	ews_email = camel_ews_settings_dup_email (ews_settings);
	host_url = camel_ews_settings_dup_hosturl (ews_settings);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (CAMEL_IS_INTERNET_ADDRESS (from))
		used_from = CAMEL_INTERNET_ADDRESS (from);
	else
		used_from = camel_mime_message_get_from (message);

	if (!used_from || camel_address_length (CAMEL_ADDRESS (used_from)) == 0) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot send message with no From address"));
		goto exit;

	} else if (camel_address_length (CAMEL_ADDRESS (used_from)) > 1) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Exchange server cannot send message with "
			"multiple From addresses"));
		goto exit;

	} else {
		const gchar *used_email = NULL;

		if (!camel_internet_address_get (used_from, 0, NULL, &used_email)) {
			g_set_error_literal (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Failed to read From address"));
			goto exit;
		}
	}

	cnc = ews_transport_ref_connection (CAMEL_EWS_TRANSPORT (service));
	if (!cnc) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Service not connected"));
		goto exit;
	}

	if (ews_transport_can_server_side_sent_folder (service, &folder_id, cancellable)) {
		if (out_sent_message_saved)
			*out_sent_message_saved = TRUE;
	}

	success = camel_ews_utils_create_mime_message (
		cnc, folder_id ? "SendAndSaveCopy" : "SendOnly", folder_id, message, NULL,
		from, recipients, NULL, NULL, cancellable, error);

	g_object_unref (cnc);
	e_ews_folder_id_free (folder_id);

exit:
	g_free (ews_email);
	g_free (host_url);
	g_free (user);

	return success;
}

static void
ews_transport_dispose (GObject *object)
{
	CamelEwsTransport *ews_transport;

	ews_transport = CAMEL_EWS_TRANSPORT (object);

	g_mutex_lock (&ews_transport->priv->connection_lock);
	g_clear_object (&ews_transport->priv->connection);
	g_mutex_unlock (&ews_transport->priv->connection_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_ews_transport_parent_class)->dispose (object);
}

static void
ews_transport_finalize (GObject *object)
{
	CamelEwsTransport *ews_transport;

	ews_transport = CAMEL_EWS_TRANSPORT (object);

	g_mutex_clear (&ews_transport->priv->connection_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_ews_transport_parent_class)->finalize (object);
}

static void
camel_ews_transport_class_init (CamelEwsTransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_transport_dispose;
	object_class->finalize = ews_transport_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_EWS_SETTINGS;
	service_class->get_name = ews_transport_get_name;
	service_class->connect_sync = ews_transport_connect_sync;
	service_class->disconnect_sync = ews_transport_disconnect_sync;
	service_class->authenticate_sync = ews_transport_authenticate_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = ews_send_to_sync;
}

static void
camel_ews_transport_init (CamelEwsTransport *ews_transport)
{
	ews_transport->priv = camel_ews_transport_get_instance_private (ews_transport);

	g_mutex_init (&ews_transport->priv->connection_lock);
}
