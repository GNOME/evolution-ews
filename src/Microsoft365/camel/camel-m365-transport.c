/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "camel-m365-store.h"
#include "camel-m365-utils.h"

#include "camel-m365-transport.h"

#define LOCK(_transport) g_mutex_lock (&_transport->priv->property_lock)
#define UNLOCK(_transport) g_mutex_unlock (&_transport->priv->property_lock)

struct _CamelM365TransportPrivate
{
	GMutex property_lock;
	EM365Connection *cnc;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelM365Transport, camel_m365_transport, CAMEL_TYPE_TRANSPORT)

/* This is copy of e_mail_folder_uri_parse(), to not depend on the evolution code
   in the library code (and to not bring gtk+ into random processes). */
static gboolean
m365_transport_mail_folder_uri_parse (CamelSession *session,
				      const gchar *folder_uri,
				      CamelStore **out_store,
				      gchar **out_folder_name,
				      GError **error)
{
	CamelURL *url;
	CamelService *service = NULL;
	gchar *folder_name = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (folder_uri != NULL, FALSE);

	url = camel_url_new (folder_uri, error);
	if (url == NULL)
		return FALSE;

	/* Current URI Format: 'folder://' STORE_UID '/' FOLDER_PATH */
	if (g_strcmp0 (url->protocol, "folder") == 0) {

		if (url->host != NULL) {
			gchar *uid;

			if (url->user == NULL || *url->user == '\0')
				uid = g_strdup (url->host);
			else
				uid = g_strconcat (
					url->user, "@", url->host, NULL);

			service = camel_session_ref_service (session, uid);
			g_free (uid);
		}

		if (url->path != NULL && *url->path == '/')
			folder_name = camel_url_decode_path (url->path + 1);

	/* This style was used to reference accounts by UID before
	 * CamelServices themselves had UIDs.  Some examples are:
	 *
	 * Special cases:
	 *
	 *   'email://local@local/' FOLDER_PATH
	 *   'email://vfolder@local/' FOLDER_PATH
	 *
	 * General case:
	 *
	 *   'email://' ACCOUNT_UID '/' FOLDER_PATH
	 *
	 * Note: ACCOUNT_UID is now equivalent to STORE_UID, and
	 *       the STORE_UIDs for the special cases are 'local'
	 *       and 'vfolder'.
	 */
	} else if (g_strcmp0 (url->protocol, "email") == 0) {
		gchar *uid = NULL;

		/* Handle the special cases. */
		if (g_strcmp0 (url->host, "local") == 0) {
			if (g_strcmp0 (url->user, "local") == 0)
				uid = g_strdup ("local");
			if (g_strcmp0 (url->user, "vfolder") == 0)
				uid = g_strdup ("vfolder");
		}

		/* Handle the general case. */
		if (uid == NULL && url->host != NULL) {
			if (url->user == NULL)
				uid = g_strdup (url->host);
			else
				uid = g_strdup_printf (
					"%s@%s", url->user, url->host);
		}

		if (uid != NULL) {
			service = camel_session_ref_service (session, uid);
			g_free (uid);
		}

		if (url->path != NULL && *url->path == '/')
			folder_name = camel_url_decode_path (url->path + 1);

	/* CamelFolderInfo URIs used to embed the store's URI, so the
	 * folder name is appended as either a path part or a fragment
	 * part, depending whether the store's URI used the path part.
	 * To determine which it is, you have to check the provider
	 * flags for CAMEL_URL_FRAGMENT_IS_PATH. */
	} else {
		gboolean local_mbox_folder;

		/* In Evolution 2.x, the local mail store used mbox
		 * format.  camel_session_ref_service_by_url() won't
		 * match "mbox:///.../mail/local" folder URIs, since
		 * the local mail store is now Maildir format.  Test
		 * for this corner case and work around it.
		 *
		 * The folder path is kept in the fragment part of the
		 * URL which makes it easy to test the filesystem path.
		 * The suffix "evolution/mail/local" should match both
		 * the current XDG-compliant location and the old "dot
		 * folder" location (~/.evolution/mail/local). */
		local_mbox_folder =
			(g_strcmp0 (url->protocol, "mbox") == 0) &&
			(url->path != NULL) &&
			g_str_has_suffix (url->path, "evolution/mail/local");

		if (local_mbox_folder) {
			service = camel_session_ref_service (session, "local");
		} else {
			service = camel_session_ref_service_by_url (
				session, url, CAMEL_PROVIDER_STORE);
		}

		if (CAMEL_IS_STORE (service)) {
			CamelProvider *provider;

			provider = camel_service_get_provider (service);

			if (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH)
				folder_name = g_strdup (url->fragment);
			else if (url->path != NULL && *url->path == '/')
				folder_name = g_strdup (url->path + 1);
		}
	}

	if (CAMEL_IS_STORE (service) && folder_name != NULL) {
		if (out_store != NULL)
			*out_store = CAMEL_STORE (g_object_ref (service));

		if (out_folder_name != NULL) {
			*out_folder_name = folder_name;
			folder_name = NULL;
		}

		success = TRUE;
	} else {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID,
			_("Invalid folder URI “%s”"),
			folder_uri);
	}

	if (service != NULL)
		g_object_unref (service);

	g_free (folder_name);

	camel_url_free (url);

	return success;
}

static gboolean
m365_transport_is_server_side_sent_folder (CamelService *service,
					   GCancellable *cancellable)
{
	ESourceRegistry *registry;
	ESource *sibling, *source = NULL;
	gboolean is_server_side = FALSE;

	g_return_val_if_fail (CAMEL_IS_M365_TRANSPORT (service), FALSE);

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
			    m365_transport_mail_folder_uri_parse (session,
				e_source_mail_submission_get_sent_folder (subm_extension),
				&store, &folder_name, NULL) && CAMEL_IS_M365_STORE (store)) {
				CamelM365Store *m365_store = CAMEL_M365_STORE (store);
				CamelM365StoreSummary *m365_store_summary;
				gchar *folder_id_str;

				m365_store_summary = camel_m365_store_ref_store_summary (m365_store);
				folder_id_str = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store_summary, folder_name);
				if (folder_id_str && *folder_id_str) {
					guint32 flags;

					flags = camel_m365_store_summary_get_folder_flags (m365_store_summary, folder_id_str);

					if ((flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_SENT) {
						is_server_side = TRUE;
					}
				}

				g_clear_object (&m365_store_summary);
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

static EM365Connection *
m365_transport_ref_connection (CamelM365Transport *m365_transport)
{
	EM365Connection *cnc = NULL;

	g_return_val_if_fail (CAMEL_IS_M365_TRANSPORT (m365_transport), NULL);

	LOCK (m365_transport);

	if (m365_transport->priv->cnc)
		cnc = g_object_ref (m365_transport->priv->cnc);

	UNLOCK (m365_transport);

	return cnc;
}

static gboolean
m365_transport_connect_sync (CamelService *service,
			     GCancellable *cancellable,
			     GError **error)
{
	CamelM365Transport *m365_transport;
	EM365Connection *cnc;
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_m365_transport_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	m365_transport = CAMEL_M365_TRANSPORT (service);
	cnc = m365_transport_ref_connection (m365_transport);

	if (!cnc) {
		cnc = camel_m365_utils_new_connection (service, NULL);

		if (cnc) {
			LOCK (m365_transport);

			m365_transport->priv->cnc = g_object_ref (cnc);

			UNLOCK (m365_transport);
		}
	}

	if (cnc) {
		CamelSession *session;

		session = camel_service_ref_session (service);

		success = camel_session_authenticate_sync (session, service, "Microsoft365", cancellable, error);

		g_clear_object (&session);
		g_clear_object (&cnc);
	} else {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, _("Failed to create connection"));
	}

	return success;
}

static gboolean
m365_transport_disconnect_sync (CamelService *service,
				gboolean clean,
				GCancellable *cancellable,
				GError **error)
{
	CamelM365Transport *m365_transport = CAMEL_M365_TRANSPORT (service);
	EM365Connection *cnc;
	gboolean success = TRUE;

	cnc = m365_transport_ref_connection (m365_transport);

	if (cnc) {
		success = e_m365_connection_disconnect_sync (cnc, cancellable, error);

		g_clear_object (&cnc);
	}

	if (!success)
		return FALSE;

	/* Chain up to parent's method. */
	return CAMEL_SERVICE_CLASS (camel_m365_transport_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
m365_transport_authenticate_sync (CamelService *service,
				  const gchar *mechanism,
				  GCancellable *cancellable,
				  GError **error)
{
	CamelAuthenticationResult result;
	CamelM365Transport *m365_transport;
	EM365Connection *cnc;

	m365_transport = CAMEL_M365_TRANSPORT (service);
	cnc = m365_transport_ref_connection (m365_transport);

	if (!cnc)
		return CAMEL_AUTHENTICATION_ERROR;

	switch (e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_MAIL, NULL, NULL, NULL, NULL, cancellable, error)) {
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

static gchar *
m365_transport_get_name (CamelService *service,
			 gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Microsoft 365 server"));
	else
		name = g_strdup (_("Mail delivery via Microsoft 365"));

	return name;
}

static gboolean
m365_send_to_sync (CamelTransport *transport,
		   CamelMimeMessage *message,
		   CamelAddress *from,
		   CamelAddress *recipients,
		   gboolean *out_sent_message_saved,
		   GCancellable *cancellable,
		   GError **error)
{
	CamelInternetAddress *use_from;
	CamelService *service;
	EM365Connection *cnc;
	JsonBuilder *builder;
	gchar *appended_id = NULL;
	gboolean is_server_side_sent;
	gboolean success = FALSE;

	service = CAMEL_SERVICE (transport);

	if (CAMEL_IS_INTERNET_ADDRESS (from))
		use_from = CAMEL_INTERNET_ADDRESS (from);
	else
		use_from = camel_mime_message_get_from (message);

	if (!use_from || camel_address_length (CAMEL_ADDRESS (use_from)) == 0) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot send message with no From address"));
		return FALSE;

	} else if (camel_address_length (CAMEL_ADDRESS (use_from)) > 1) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Microsoft 365 server cannot send message with multiple From addresses"));
		return FALSE;

	} else {
		const gchar *used_email = NULL;

		if (!camel_internet_address_get (use_from, 0, NULL, &used_email)) {
			g_set_error_literal (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Failed to read From address"));
			return FALSE;
		}
	}

	cnc = m365_transport_ref_connection (CAMEL_M365_TRANSPORT (service));

	if (!cnc) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Service not connected"));
		return FALSE;
	}

	is_server_side_sent = m365_transport_is_server_side_sent_folder (service, cancellable);

	if (is_server_side_sent && out_sent_message_saved)
		*out_sent_message_saved = TRUE;

	builder = json_builder_new_immutable ();
	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_begin_object_member (builder, "message");

	success = camel_m365_utils_fill_message_object_sync (builder, message, NULL, from, recipients, TRUE, NULL, cancellable, error);

	e_m365_json_end_object_member (builder); /* message */

	if (!is_server_side_sent)
		e_m365_json_add_boolean_member (builder, "saveToSentItems", FALSE);

	e_m365_json_end_object_member (builder);

	success = success && e_m365_connection_send_mail_sync (cnc, NULL, builder, cancellable, error);

	g_object_unref (cnc);
	g_free (appended_id);

	return success;
}

static void
m365_transport_dispose (GObject *object)
{
	CamelM365Transport *m365_transport = CAMEL_M365_TRANSPORT (object);

	LOCK (m365_transport);

	g_clear_object (&m365_transport->priv->cnc);

	UNLOCK (m365_transport);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_transport_parent_class)->dispose (object);
}

static void
m365_transport_finalize (GObject *object)
{
	CamelM365Transport *m365_transport = CAMEL_M365_TRANSPORT (object);

	g_mutex_clear (&m365_transport->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_transport_parent_class)->finalize (object);
}

static void
camel_m365_transport_class_init (CamelM365TransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = m365_transport_dispose;
	object_class->finalize = m365_transport_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_M365_SETTINGS;
	service_class->get_name = m365_transport_get_name;
	service_class->connect_sync = m365_transport_connect_sync;
	service_class->disconnect_sync = m365_transport_disconnect_sync;
	service_class->authenticate_sync = m365_transport_authenticate_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = m365_send_to_sync;
}

static void
camel_m365_transport_init (CamelM365Transport *m365_transport)
{
	m365_transport->priv = camel_m365_transport_get_instance_private (m365_transport);

	g_mutex_init (&m365_transport->priv->property_lock);
}
