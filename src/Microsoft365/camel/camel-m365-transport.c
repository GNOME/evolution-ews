/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "e-ews-common-utils.h"
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
			    e_ews_common_utils_mail_folder_uri_parse (session,
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
	CamelStream *mem_stream;
	EM365Connection *cnc;
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

	/* The server re-encodes the HTML messages into base64, but as of 2024-07-11 they do not
	   decode quoted-printable encoding properly, thus re-encode to base64 instead.
	   The multipart/signed messages are not affected, luckily (it would break the signature anyway). */
	if (!e_m365_connection_util_reencode_parts_to_base64_sync (CAMEL_MIME_PART (message), cancellable, error)) {
		g_prefix_error (error, "%s", _("Failed to re-encode parts to base64: "));
		g_object_unref (cnc);
		return FALSE;
	}

	is_server_side_sent = m365_transport_is_server_side_sent_folder (service, cancellable);

	if (is_server_side_sent && out_sent_message_saved)
		*out_sent_message_saved = TRUE;

	mem_stream = camel_stream_mem_new ();

	success = camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (message), mem_stream, cancellable, error) >= 0 &&
		camel_stream_flush (mem_stream, cancellable, error) != -1;

	if (success) {
		GByteArray *mime_data;
		gchar *base64_data;

		mime_data = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (mem_stream));
		base64_data = g_base64_encode (mime_data->data, mime_data->len);

		success = e_m365_connection_send_mail_mime_sync (cnc, NULL, base64_data, strlen (base64_data), cancellable, error);

		g_free (base64_data);
	}

	g_clear_object (&mem_stream);
	g_object_unref (cnc);

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
