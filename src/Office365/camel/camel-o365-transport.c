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

#include <string.h>

#include <glib/gi18n-lib.h>

#include <libemail-engine/libemail-engine.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"
#include "camel-o365-store.h"
#include "camel-o365-utils.h"

#include "camel-o365-transport.h"

#define LOCK(_transport) g_mutex_lock (&_transport->priv->property_lock)
#define UNLOCK(_transport) g_mutex_unlock (&_transport->priv->property_lock)

struct _CamelO365TransportPrivate
{
	GMutex property_lock;
	EO365Connection *cnc;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365Transport, camel_o365_transport, CAMEL_TYPE_TRANSPORT)

static gboolean
o365_transport_is_server_side_sent_folder (CamelService *service,
					   GCancellable *cancellable)
{
	CamelSession *session;
	ESourceRegistry *registry;
	ESource *sibling, *source = NULL;
	gboolean is_server_side = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_TRANSPORT (service), FALSE);

	session = camel_service_ref_session (service);

	if (session && E_IS_MAIL_SESSION (session))
		registry = g_object_ref (e_mail_session_get_registry (E_MAIL_SESSION (session)));
	else
		registry = e_source_registry_new_sync (cancellable, NULL);

	if (!registry) {
		g_clear_object (&session);
		return FALSE;
	}

	sibling = e_source_registry_ref_source (registry, camel_service_get_uid (service));

	if (sibling) {
		GList *sources, *siter;

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
			    e_mail_folder_uri_parse (session,
				e_source_mail_submission_get_sent_folder (subm_extension),
				&store, &folder_name, NULL) && CAMEL_IS_O365_STORE (store)) {
				CamelO365Store *o365_store = CAMEL_O365_STORE (store);
				CamelO365StoreSummary *o365_store_summary;
				gchar *folder_id_str;

				o365_store_summary = camel_o365_store_ref_store_summary (o365_store);
				folder_id_str = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store_summary, folder_name);
				if (folder_id_str && *folder_id_str) {
					guint32 flags;

					flags = camel_o365_store_summary_get_folder_flags (o365_store_summary, folder_id_str);

					if ((flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_SENT) {
						is_server_side = TRUE;
					}
				}

				g_clear_object (&o365_store_summary);
				g_free (folder_id_str);
			}

			g_clear_object (&store);
			g_free (folder_name);
		}

		g_list_free_full (sources, g_object_unref);
		g_object_unref (sibling);
	}

	g_object_unref (registry);
	g_clear_object (&session);

	return is_server_side;
}

static EO365Connection *
o365_transport_ref_connection (CamelO365Transport *o365_transport)
{
	EO365Connection *cnc = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_TRANSPORT (o365_transport), NULL);

	LOCK (o365_transport);

	if (o365_transport->priv->cnc)
		cnc = g_object_ref (o365_transport->priv->cnc);

	UNLOCK (o365_transport);

	return cnc;
}

static gboolean
o365_transport_connect_sync (CamelService *service,
			     GCancellable *cancellable,
			     GError **error)
{
	CamelO365Transport *o365_transport;
	EO365Connection *cnc;
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_o365_transport_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	o365_transport = CAMEL_O365_TRANSPORT (service);
	cnc = o365_transport_ref_connection (o365_transport);

	if (!cnc) {
		cnc = camel_o365_utils_new_connection (service, NULL);

		if (cnc) {
			LOCK (o365_transport);

			o365_transport->priv->cnc = g_object_ref (cnc);

			UNLOCK (o365_transport);
		}
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
o365_transport_disconnect_sync (CamelService *service,
				gboolean clean,
				GCancellable *cancellable,
				GError **error)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (service);
	EO365Connection *cnc;
	gboolean success = TRUE;

	cnc = o365_transport_ref_connection (o365_transport);

	if (cnc) {
		success = e_o365_connection_disconnect_sync (cnc, cancellable, error);

		g_clear_object (&cnc);
	}

	if (!success)
		return FALSE;

	/* Chain up to parent's method. */
	return CAMEL_SERVICE_CLASS (camel_o365_transport_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
o365_transport_authenticate_sync (CamelService *service,
				  const gchar *mechanism,
				  GCancellable *cancellable,
				  GError **error)
{
	CamelAuthenticationResult result;
	CamelO365Transport *o365_transport;
	EO365Connection *cnc;

	o365_transport = CAMEL_O365_TRANSPORT (service);
	cnc = o365_transport_ref_connection (o365_transport);

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

static gchar *
o365_transport_get_name (CamelService *service,
			 gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Office 365 server"));
	else
		name = g_strdup (_("Mail delivery via Microsoft Office 365"));

	return name;
}

static gboolean
o365_send_to_sync (CamelTransport *transport,
		   CamelMimeMessage *message,
		   CamelAddress *from,
		   CamelAddress *recipients,
		   gboolean *out_sent_message_saved,
		   GCancellable *cancellable,
		   GError **error)
{
	CamelInternetAddress *use_from;
	CamelService *service;
	EO365Connection *cnc;
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
			_("Office 365 server cannot send message with multiple From addresses"));
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

	cnc = o365_transport_ref_connection (CAMEL_O365_TRANSPORT (service));

	if (!cnc) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Service not connected"));
		return FALSE;
	}

	is_server_side_sent = o365_transport_is_server_side_sent_folder (service, cancellable);

	if (is_server_side_sent && out_sent_message_saved)
		*out_sent_message_saved = TRUE;

	builder = json_builder_new_immutable ();
	e_o365_json_begin_object_member (builder, NULL);
	e_o365_json_begin_object_member (builder, "message");

	success = camel_o365_utils_fill_message_object_sync (builder, message, NULL, from, recipients, TRUE, NULL, cancellable, error);

	e_o365_json_end_object_member (builder); /* message */

	if (!is_server_side_sent)
		e_o365_json_add_boolean_member (builder, "saveToSentItems", FALSE);

	e_o365_json_end_object_member (builder);

	success = success && e_o365_connection_send_mail_sync (cnc, NULL, builder, cancellable, error);

	g_object_unref (cnc);
	g_free (appended_id);

	return success;
}

static void
o365_transport_dispose (GObject *object)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (object);

	LOCK (o365_transport);

	g_clear_object (&o365_transport->priv->cnc);

	UNLOCK (o365_transport);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_transport_parent_class)->dispose (object);
}

static void
o365_transport_finalize (GObject *object)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (object);

	g_mutex_clear (&o365_transport->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_transport_parent_class)->finalize (object);
}

static void
camel_o365_transport_class_init (CamelO365TransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = o365_transport_dispose;
	object_class->finalize = o365_transport_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_O365_SETTINGS;
	service_class->get_name = o365_transport_get_name;
	service_class->connect_sync = o365_transport_connect_sync;
	service_class->disconnect_sync = o365_transport_disconnect_sync;
	service_class->authenticate_sync = o365_transport_authenticate_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = o365_send_to_sync;
}

static void
camel_o365_transport_init (CamelO365Transport *o365_transport)
{
	o365_transport->priv = camel_o365_transport_get_instance_private (o365_transport);

	g_mutex_init (&o365_transport->priv->property_lock);
}
