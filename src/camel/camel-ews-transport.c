/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-transport.c : class for an ews transport */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *	    Parthasarathi Susarla <sparthasarathi@novell.com>
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

#include <glib/gi18n-lib.h>

#include <libemail-engine/libemail-engine.h>

#include "server/camel-ews-settings.h"

#include "utils/ews-camel-common.h"

#include "camel-ews-store.h"
#include "camel-ews-transport.h"
#include "camel-ews-utils.h"

#define REPLY_VIEW "default message attachments threading"

G_DEFINE_TYPE (CamelEwsTransport, camel_ews_transport, CAMEL_TYPE_TRANSPORT)

static gboolean
ews_transport_can_server_side_sent_folder (CamelService *service,
					   EwsFolderId **folder_id,
					   GCancellable *cancellable)
{
	CamelSession *session;
	ESourceRegistry *registry;
	ESource *sibling, *source = NULL;
	gboolean is_server_side = FALSE;

	g_return_val_if_fail (CAMEL_IS_EWS_TRANSPORT (service), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);

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
	}

	g_object_unref (registry);
	g_clear_object (&session);

	return is_server_side;
}

static gboolean
ews_transport_connect_sync (CamelService *service,
                            GCancellable *cancellable,
                            GError **error)
{
	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_ews_transport_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	return TRUE;
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

	used_from = camel_mime_message_get_from (message);

	if (!used_from && CAMEL_IS_INTERNET_ADDRESS (from))
		used_from = CAMEL_INTERNET_ADDRESS (from);

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
		gboolean addresses_match;

		if (!camel_internet_address_get (used_from, 0, NULL, &used_email)) {
			g_set_error_literal (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Failed to read From address"));
			goto exit;
		}

		addresses_match =
			(ews_email != NULL) &&
			(used_email != NULL) &&
			(g_ascii_strcasecmp (ews_email, used_email) == 0);

		if (!addresses_match) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Exchange server cannot send message as "
				"'%s', when the account was configured for "
				"address '%s'"),
				used_email ? used_email : "NULL",
				ews_email ? ews_email : "NULL");
			goto exit;
		}
	}

	cnc = e_ews_connection_find (host_url, user);
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
camel_ews_transport_class_init (CamelEwsTransportClass *class)
{
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_EWS_SETTINGS;
	service_class->connect_sync = ews_transport_connect_sync;
	service_class->get_name = ews_transport_get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = ews_send_to_sync;
}

static void
camel_ews_transport_init (CamelEwsTransport *ews_transport)
{
}
