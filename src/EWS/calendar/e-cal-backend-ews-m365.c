/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-m365-json-utils.h"
#include "common/e-m365-tz-utils.h"
#include "calendar/e-cal-backend-m365-utils.h"

#include "e-cal-backend-ews-m365.h"

static gboolean
ecb_ews_m365_authenticate (EM365Connection *m365_cnc,
			   GCancellable *cancellable,
			   GError **error)
{
	ESourceAuthenticationResult auth_result;
	ESourceCredentialsReason cred_reason;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	ESource *source;
	GError *local_error = NULL;

	auth_result = e_m365_connection_authenticate_sync (m365_cnc, NULL, E_M365_FOLDER_KIND_CALENDAR,
		NULL, NULL, &certificate_pem, &certificate_errors, cancellable, &local_error);

	if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED)
		return TRUE;

	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED,
		_("Cannot connect to the server, repeat the action once you login to the server."));

	source = e_m365_connection_get_source (m365_cnc);

	switch (auth_result) {
	case E_SOURCE_AUTHENTICATION_ERROR:
		cred_reason = E_SOURCE_CREDENTIALS_REASON_ERROR;
		break;
	case E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED:
		cred_reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
		break;
	case E_SOURCE_AUTHENTICATION_REJECTED:
		cred_reason = E_SOURCE_CREDENTIALS_REASON_REJECTED;
		break;
	default:
		cred_reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;
		break;
	}

	e_source_invoke_credentials_required (source, cred_reason, certificate_pem, certificate_errors, local_error, NULL, NULL, NULL);

	g_clear_error (&local_error);

	return FALSE;
}

static ESource *
ecb_ews_m365_find_helper_source (ESourceRegistry *registry,
				 ESource *calendar_source)
{
	ESource *result = NULL;
	ESource *collection_source;
	GList *sources, *link;
	const gchar *parent_uid;

	collection_source = e_source_registry_find_extension (registry, calendar_source, E_SOURCE_EXTENSION_COLLECTION);
	if (!collection_source)
		return NULL;

	parent_uid = e_source_get_uid (collection_source);
	sources = e_source_registry_list_sources (registry, E_SOURCE_EXTENSION_AUTHENTICATION);

	for (link = sources; link; link = g_list_next (link)) {
		ESource *adept = link->data;

		if (g_strcmp0 (parent_uid, e_source_get_parent (adept)) == 0) {
			ESourceAuthentication *auth_extension;

			auth_extension = e_source_get_extension (adept, E_SOURCE_EXTENSION_AUTHENTICATION);

			if (g_strcmp0 ("Microsoft365", e_source_authentication_get_method (auth_extension)) == 0) {
				result = g_object_ref (adept);
				break;
			}
		}
	}

	g_list_free_full (sources, g_object_unref);

	return result;
}

gboolean
ecb_ews_save_as_online_meeting_sync (ESourceRegistry *registry,
				     EEwsConnection *ews_cnc,
				     ETimezoneCache *timezone_cache,
				     ECalComponent *comp,
				     gchar **out_new_uid,
				     GCancellable *cancellable,
				     GError **error)
{
	CamelEwsSettings *ews_settings;
	CamelM365Settings *m365_settings;
	EM365Connection *m365_cnc;
	ESource *source;
	gboolean success = FALSE;

	ews_settings = e_ews_connection_ref_settings (ews_cnc);
	m365_settings = g_object_new (CAMEL_TYPE_M365_SETTINGS, NULL);

	camel_m365_settings_set_concurrent_connections (m365_settings, 1);

	e_binding_bind_property (
		ews_settings, "user",
		m365_settings, "user",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		ews_settings, "timeout",
		m365_settings, "timeout",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		ews_settings, "use-impersonation",
		m365_settings, "use-impersonation",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		ews_settings, "impersonate-user",
		m365_settings, "impersonate-user",
		G_BINDING_SYNC_CREATE);

	/* Only the above properties are used in the m365_cnc */

	source = ecb_ews_m365_find_helper_source (registry, e_ews_connection_get_source (ews_cnc));

	if (!source) {
		/* Translators: The '%s' is replaced with a UID of the collection source */
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("Cannot find Microsoft365 helper source for calendar “%s”"),
			e_source_get_uid (e_ews_connection_get_source (ews_cnc)));

		g_clear_object (&ews_settings);
		g_clear_object (&m365_settings);

		return FALSE;
	}

	m365_cnc = e_m365_connection_new_full (source, m365_settings, FALSE);
	g_clear_object (&source);

	e_binding_bind_property (
		ews_cnc, "proxy-resolver",
		m365_cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	success = ecb_ews_m365_authenticate (m365_cnc, cancellable, error);

	if (success) {
		ICalComponent *new_comp;
		JsonBuilder *builder;

		new_comp = e_cal_component_get_icalcomponent (comp);

		e_m365_tz_utils_ref_windows_zones ();

		builder = e_cal_backend_m365_utils_ical_to_json (m365_cnc, NULL, NULL, timezone_cache, I_CAL_VEVENT_COMPONENT,
			new_comp, NULL, cancellable, error);

		if (builder) {
			JsonObject *created_item = NULL;

			success = e_m365_connection_create_event_sync (m365_cnc, NULL, NULL, NULL, builder, &created_item, cancellable, error);

			if (success && created_item) {
				const gchar *m365_id = e_m365_event_get_id (created_item);

				success = e_cal_backend_m365_utils_ical_to_json_2nd_go (m365_cnc, NULL, NULL, timezone_cache,
					I_CAL_VEVENT_COMPONENT, new_comp, NULL, m365_id, cancellable, error);
			}

			if (success && created_item) {
				*out_new_uid = g_strdup (e_m365_event_get_id (created_item));

				/* Convert from Microsoft Graph ID into EWS ID */
				if (*out_new_uid && strchr (*out_new_uid, '-') != NULL) {
					gchar *ptr;

					for (ptr = *out_new_uid; *ptr; ptr++) {
						if (*ptr == '-')
							*ptr = '/';
					}
				}
			}

			g_clear_pointer (&created_item, json_object_unref);
			g_clear_object (&builder);
		}

		e_m365_tz_utils_unref_windows_zones ();
	}

	g_clear_object (&ews_settings);
	g_clear_object (&m365_settings);
	g_clear_object (&m365_cnc);

	return success;
}
