/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <libedataserver/libedataserver.h>

#include "camel-m365-settings.h"

#include "e-oauth2-service-microsoft365.h"

/* https://portal.azure.com/
   https://docs.microsoft.com/en-us/graph/auth/

   https://docs.microsoft.com/en-us/azure/active-directory/develop/active-directory-developers-guide
   https://tsmatz.wordpress.com/2016/10/07/application-permission-with-v2-endpoint-and-microsoft-graph/
*/

#define MICROSOFT365_SCOPE	"Calendars.ReadWrite " \
				"Calendars.ReadWrite.Shared " \
				"Contacts.ReadWrite " \
				"Contacts.ReadWrite.Shared " \
				"Mail.ReadWrite " \
				"Mail.ReadWrite.Shared " \
				"Mail.Send " \
				"Mail.Send.Shared " \
				"MailboxSettings.Read " \
				"Notes.Create " \
				"Notes.ReadWrite.All " \
				"offline_access " \
				"Tasks.ReadWrite " \
				"Tasks.ReadWrite.Shared "

struct _EOAuth2ServiceMicrosoft365Private
{
	GMutex string_cache_lock;
	GHashTable *string_cache;
};

/* Forward Declarations */
static void e_oauth2_service_microsoft365_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EOAuth2ServiceMicrosoft365, e_oauth2_service_microsoft365, E_TYPE_OAUTH2_SERVICE_BASE, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_microsoft365_oauth2_service_init)
	G_ADD_PRIVATE_DYNAMIC (EOAuth2ServiceMicrosoft365))

static const gchar *
eos_microsoft365_cache_string (EOAuth2ServiceMicrosoft365 *oauth2_microsoft365,
			       gchar *str) /* takes ownership of the 'str' */
{
	const gchar *cached_str;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE_MICROSOFT365 (oauth2_microsoft365), NULL);

	if (!str)
		return NULL;

	if (!*str)
		return "";

	g_mutex_lock (&oauth2_microsoft365->priv->string_cache_lock);

	cached_str = g_hash_table_lookup (oauth2_microsoft365->priv->string_cache, str);
	if (cached_str) {
		g_free (str);
	} else {
		g_hash_table_insert (oauth2_microsoft365->priv->string_cache, str, str);
		cached_str = str;
	}

	g_mutex_unlock (&oauth2_microsoft365->priv->string_cache_lock);

	return cached_str;
}

static CamelM365Settings *
eos_microsoft365_get_camel_settings (ESource *source)
{
	ESourceCamel *extension;

	if (!source)
		return NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	extension = e_source_get_extension (source, e_source_camel_get_extension_name ("microsoft365"));

	return CAMEL_M365_SETTINGS (e_source_camel_get_settings (extension));
}

static gboolean
eos_microsoft365_guess_can_process (EOAuth2Service *service,
				    const gchar *protocol,
				    const gchar *hostname)
{
	return e_oauth2_services_is_supported () &&
		protocol && g_ascii_strcasecmp (protocol, "microsoft365") == 0;
}

static const gchar *
eos_microsoft365_get_name (EOAuth2Service *service)
{
	return "Microsoft365";
}

static const gchar *
eos_microsoft365_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "Microsoft365");
}

static const gchar *
eos_microsoft365_get_client_id (EOAuth2Service *service,
				ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;

	m365_settings = eos_microsoft365_get_camel_settings (source);
	if (m365_settings && camel_m365_settings_get_override_oauth2 (m365_settings)) {
		gchar *client_id = camel_m365_settings_dup_oauth2_client_id (m365_settings);

		if (client_id && !*client_id) {
			g_free (client_id);
			client_id = NULL;
		}

		if (client_id)
			return eos_microsoft365_cache_string (oauth2_microsoft365, client_id);
	}

	return MICROSOFT365_CLIENT_ID;
}

static const gchar *
eos_microsoft365_get_client_secret (EOAuth2Service *service,
				    ESource *source)
{
	return NULL;
}

static const gchar *
eos_microsoft365_get_authentication_uri (EOAuth2Service *service,
					 ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;

	m365_settings = eos_microsoft365_get_camel_settings (source);
	if (m365_settings && camel_m365_settings_get_override_oauth2 (m365_settings)) {
		gchar *tenant;
		const gchar *res;

		tenant = camel_m365_settings_dup_oauth2_tenant (m365_settings);
		if (tenant && !*tenant) {
			g_free (tenant);
			tenant = NULL;
		}

		res = eos_microsoft365_cache_string (oauth2_microsoft365,
			g_strdup_printf ("https://login.microsoftonline.com/%s/oauth2/v2.0/authorize",
				tenant ? tenant : MICROSOFT365_TENANT));

		g_free (tenant);

		return res;
	}

	return "https://login.microsoftonline.com/" MICROSOFT365_TENANT "/oauth2/v2.0/authorize";
}

static const gchar *
eos_microsoft365_get_refresh_uri (EOAuth2Service *service,
				  ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;

	m365_settings = eos_microsoft365_get_camel_settings (source);
	if (m365_settings && camel_m365_settings_get_override_oauth2 (m365_settings)) {
		gchar *tenant;
		const gchar *res;

		tenant = camel_m365_settings_dup_oauth2_tenant (m365_settings);
		if (tenant && !*tenant) {
			g_free (tenant);
			tenant = NULL;
		}

		res = eos_microsoft365_cache_string (oauth2_microsoft365,
			g_strdup_printf ("https://login.microsoftonline.com/%s/oauth2/v2.0/token",
				tenant ? tenant : MICROSOFT365_TENANT));

		g_free (tenant);

		return res;
	}

	return "https://login.microsoftonline.com/" MICROSOFT365_TENANT "/oauth2/v2.0/token";
}

static const gchar *
eos_microsoft365_get_redirect_uri (EOAuth2Service *service,
				   ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;
	const gchar *res;

	m365_settings = eos_microsoft365_get_camel_settings (source);
	if (m365_settings && camel_m365_settings_get_override_oauth2 (m365_settings)) {
		gchar *redirect_uri;

		redirect_uri = camel_m365_settings_dup_oauth2_redirect_uri (m365_settings);

		if (redirect_uri && !*redirect_uri) {
			g_free (redirect_uri);
			redirect_uri = NULL;
		}

		if (redirect_uri)
			return eos_microsoft365_cache_string (oauth2_microsoft365, redirect_uri);
	}

	res = MICROSOFT365_REDIRECT_URI;
	if (res && *res)
		return res;

	return "https://login.microsoftonline.com/common/oauth2/nativeclient";
}

static void
eos_microsoft365_prepare_authentication_uri_query (EOAuth2Service *service,
						   ESource *source,
						   GHashTable *uri_query)
{
	g_return_if_fail (uri_query != NULL);

	e_oauth2_service_util_set_to_form (uri_query, "response_type", "code");
	e_oauth2_service_util_set_to_form (uri_query, "scope", MICROSOFT365_SCOPE);
	e_oauth2_service_util_set_to_form (uri_query, "response_mode", "query");
}

static gboolean
eos_microsoft365_extract_authorization_code (EOAuth2Service *service,
					     ESource *source,
					     const gchar *page_title,
					     const gchar *page_uri,
					     const gchar *page_content,
					     gchar **out_authorization_code)
{
	SoupURI *suri;
	gboolean known = FALSE;

	g_return_val_if_fail (out_authorization_code != NULL, FALSE);

	*out_authorization_code = NULL;

	if (!page_uri || !*page_uri)
		return FALSE;

	suri = soup_uri_new (page_uri);
	if (!suri)
		return FALSE;

	if (suri->query) {
		GHashTable *uri_query = soup_form_decode (suri->query);

		if (uri_query) {
			const gchar *code;

			code = g_hash_table_lookup (uri_query, "code");

			if (code && *code) {
				*out_authorization_code = g_strdup (code);
				known = TRUE;
			} else if (g_hash_table_lookup (uri_query, "error")) {
				known = TRUE;
				if (g_strcmp0 (g_hash_table_lookup (uri_query, "error"), "access_denied") != 0) {
					const gchar *description;

					description = g_hash_table_lookup (uri_query, "error_description");
					if (description) {
						g_warning ("%s: error:%s description:%s", G_STRFUNC,
							(const gchar *) g_hash_table_lookup (uri_query, "error"),
							description);
					}
				}
			}

			g_hash_table_unref (uri_query);
		}
	}

	soup_uri_free (suri);

	return known;
}

static void
eos_microsoft365_prepare_refresh_token_form (EOAuth2Service *service,
					     ESource *source,
					     const gchar *refresh_token,
					     GHashTable *form)
{
	g_return_if_fail (form != NULL);

	e_oauth2_service_util_set_to_form (form, "scope", MICROSOFT365_SCOPE);
	e_oauth2_service_util_set_to_form (form, "redirect_uri", e_oauth2_service_get_redirect_uri (service, source));
}

static void
eos_microsoft365_finalize (GObject *object)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (object);

	g_mutex_lock (&oauth2_microsoft365->priv->string_cache_lock);
	g_hash_table_destroy (oauth2_microsoft365->priv->string_cache);
	g_mutex_unlock (&oauth2_microsoft365->priv->string_cache_lock);
	g_mutex_clear (&oauth2_microsoft365->priv->string_cache_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_oauth2_service_microsoft365_parent_class)->finalize (object);
}

static void
e_oauth2_service_microsoft365_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->guess_can_process = eos_microsoft365_guess_can_process;
	iface->get_name = eos_microsoft365_get_name;
	iface->get_display_name = eos_microsoft365_get_display_name;
	iface->get_client_id = eos_microsoft365_get_client_id;
	iface->get_client_secret = eos_microsoft365_get_client_secret;
	iface->get_authentication_uri = eos_microsoft365_get_authentication_uri;
	iface->get_refresh_uri = eos_microsoft365_get_refresh_uri;
	iface->get_redirect_uri = eos_microsoft365_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_microsoft365_prepare_authentication_uri_query;
	iface->extract_authorization_code = eos_microsoft365_extract_authorization_code;
	iface->prepare_refresh_token_form = eos_microsoft365_prepare_refresh_token_form;
}

static void
e_oauth2_service_microsoft365_class_init (EOAuth2ServiceMicrosoft365Class *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = eos_microsoft365_finalize;
}

static void
e_oauth2_service_microsoft365_class_finalize (EOAuth2ServiceMicrosoft365Class *klass)
{
}

static void
e_oauth2_service_microsoft365_init (EOAuth2ServiceMicrosoft365 *oauth2_microsoft365)
{
	oauth2_microsoft365->priv = e_oauth2_service_microsoft365_get_instance_private (oauth2_microsoft365);

	g_mutex_init (&oauth2_microsoft365->priv->string_cache_lock);
	oauth2_microsoft365->priv->string_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

void
e_oauth2_service_microsoft365_type_register (GTypeModule *type_module)
{
	e_oauth2_service_microsoft365_register_type (type_module);
}
