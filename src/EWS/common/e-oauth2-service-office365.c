/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileCopyrightText: (C) 2024 Siemens AG
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <libedataserver/libedataserver.h>

#include "camel-ews-settings.h"
#include "e-ews-debug.h"
#include "e-ms-oapxbc-util.h"

#include "e-oauth2-service-office365.h"

/* https://portal.azure.com/
   https://docs.microsoft.com/en-us/azure/active-directory/develop/active-directory-developers-guide
   https://tsmatz.wordpress.com/2016/10/07/application-permission-with-v2-endpoint-and-microsoft-graph/
   https://learn.microsoft.com/en-us/azure/active-directory/develop/authentication-flows-app-scenarios
*/

#define AUTHENTICATION_PATH_V1 "oauth2/authorize"
#define AUTHENTICATION_PATH_V2 "oauth2/v2.0/authorize"

#define REFRESH_PATH_V1 "oauth2/token"
#define REFRESH_PATH_V2 "oauth2/v2.0/token"

#define OFFICE365_SCOPE "offline_access" \
			" openid"
#define OFFICE365_HOST_SCOPE_SUFFIX "EWS.AccessAsUser.All"

struct _EOAuth2ServiceOffice365Private
{
	GMutex string_cache_lock;
	GHashTable *string_cache;
};

/* Forward Declarations */
static void e_oauth2_service_office365_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EOAuth2ServiceOffice365, e_oauth2_service_office365, E_TYPE_OAUTH2_SERVICE_BASE, 0,
	G_ADD_PRIVATE_DYNAMIC (EOAuth2ServiceOffice365)
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_office365_oauth2_service_init))

static const gchar *
eos_office365_cache_string_take (EOAuth2ServiceOffice365 *oauth2_office365,
				 gchar *str) /* takes ownership of the 'str' */
{
	const gchar *cached_str;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE_OFFICE365 (oauth2_office365), NULL);

	if (!str)
		return NULL;

	if (!*str) {
		g_free (str);
		return "";
	}

	g_mutex_lock (&oauth2_office365->priv->string_cache_lock);

	cached_str = g_hash_table_lookup (oauth2_office365->priv->string_cache, str);
	if (cached_str) {
		g_free (str);
	} else {
		g_hash_table_insert (oauth2_office365->priv->string_cache, str, str);
		cached_str = str;
	}

	g_mutex_unlock (&oauth2_office365->priv->string_cache_lock);

	return cached_str;
}

static const gchar *
eos_office365_cache_string (EOAuth2ServiceOffice365 *oauth2_office365,
			    const gchar *str)
{
	gchar *cached_str;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE_OFFICE365 (oauth2_office365), NULL);

	if (!str || !*str)
		return str;

	g_mutex_lock (&oauth2_office365->priv->string_cache_lock);

	cached_str = g_hash_table_lookup (oauth2_office365->priv->string_cache, str);

	if (!cached_str) {
		cached_str = g_strdup (str);
		g_hash_table_insert (oauth2_office365->priv->string_cache, cached_str, cached_str);
	}

	g_mutex_unlock (&oauth2_office365->priv->string_cache_lock);

	return cached_str;
}

static void
eos_office365_get_endpoint_host_and_tenant_locked (CamelEwsSettings *ews_settings,
						   const gchar **out_endpoint_host,
						   const gchar **out_tenant)
{
	if (out_endpoint_host) {
		*out_endpoint_host = camel_ews_settings_get_oauth2_endpoint_host (ews_settings);

		if (e_util_strcmp0 (*out_endpoint_host, NULL) == 0)
			*out_endpoint_host = OFFICE365_ENDPOINT_HOST;
	}

	if (out_tenant) {
		*out_tenant = camel_ews_settings_get_oauth2_tenant (ews_settings);

		if (e_util_strcmp0 (*out_tenant, NULL) == 0)
			*out_tenant = OFFICE365_TENANT;
	}
}

static CamelEwsSettings *
eos_office365_get_camel_settings (ESource *source)
{
	ESourceCamel *extension;

	if (!source)
		return NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	extension = e_source_get_extension (source, e_source_camel_get_extension_name ("ews"));

	return CAMEL_EWS_SETTINGS (e_source_camel_get_settings (extension));
}

static gboolean
eos_office365_guess_can_process (EOAuth2Service *service,
				 const gchar *protocol,
				 const gchar *hostname)
{
	return e_oauth2_services_is_supported () &&
		protocol && g_ascii_strcasecmp (protocol, "ews") == 0;
}

static const gchar *
eos_office365_get_name (EOAuth2Service *service)
{
	return "Office365";
}

static const gchar *
eos_office365_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "Office365");
}

static const gchar *
eos_office365_get_client_id (EOAuth2Service *service,
			     ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);
	CamelEwsSettings *ews_settings;

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings) {
		const gchar *res = NULL;

		camel_ews_settings_lock (ews_settings);

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			const gchar *client_id;

			client_id = camel_ews_settings_get_oauth2_client_id (ews_settings);

			if (e_util_strcmp0 (client_id, NULL) != 0)
				res = eos_office365_cache_string (oauth2_office365, client_id);
		}

		camel_ews_settings_unlock (ews_settings);

		if (res)
			return res;
	}

	return OFFICE365_CLIENT_ID;
}

static const gchar *
eos_office365_get_client_secret (EOAuth2Service *service,
				 ESource *source)
{
	return NULL;
}

static const gchar *
eos_office365_get_authentication_uri (EOAuth2Service *service,
				      ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);
	CamelEwsSettings *ews_settings;
	gboolean use_oauth2_v2 = FALSE;

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings) {
		const gchar *res = NULL;

		camel_ews_settings_lock (ews_settings);

		use_oauth2_v2 = camel_ews_settings_get_use_oauth2_v2 (ews_settings);

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			const gchar *endpoint_host = NULL;
			const gchar *tenant = NULL;
			const gchar *uri_path;

			eos_office365_get_endpoint_host_and_tenant_locked (ews_settings, &endpoint_host, &tenant);

			if (use_oauth2_v2)
				uri_path = AUTHENTICATION_PATH_V2;
			else
				uri_path = AUTHENTICATION_PATH_V1;

			res = eos_office365_cache_string_take (oauth2_office365,
				g_strdup_printf ("https://%s/%s/%s", endpoint_host, tenant, uri_path));
		}

		camel_ews_settings_unlock (ews_settings);

		if (res)
			return res;
	}

	if (use_oauth2_v2)
		return "https://" OFFICE365_ENDPOINT_HOST "/" OFFICE365_TENANT "/" AUTHENTICATION_PATH_V2;

	return "https://" OFFICE365_ENDPOINT_HOST "/" OFFICE365_TENANT "/" AUTHENTICATION_PATH_V1;
}

static const gchar *
eos_office365_get_refresh_uri (EOAuth2Service *service,
			       ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);
	CamelEwsSettings *ews_settings;
	gboolean use_oauth2_v2 = FALSE;

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings) {
		const gchar *res = NULL;

		camel_ews_settings_lock (ews_settings);

		use_oauth2_v2 = camel_ews_settings_get_use_oauth2_v2 (ews_settings);

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			const gchar *endpoint_host = NULL;
			const gchar *tenant = NULL;
			const gchar *uri_path;

			eos_office365_get_endpoint_host_and_tenant_locked (ews_settings, &endpoint_host, &tenant);

			if (use_oauth2_v2)
				uri_path = REFRESH_PATH_V2;
			else
				uri_path = REFRESH_PATH_V1;

			res = eos_office365_cache_string_take (oauth2_office365,
				g_strdup_printf ("https://%s/%s/%s", endpoint_host, tenant, uri_path));
		}

		camel_ews_settings_unlock (ews_settings);

		if (res)
			return res;
	}

	if (use_oauth2_v2)
		return "https://" OFFICE365_ENDPOINT_HOST "/" OFFICE365_TENANT "/" REFRESH_PATH_V2;

	return "https://" OFFICE365_ENDPOINT_HOST "/" OFFICE365_TENANT "/" REFRESH_PATH_V1;
}

static const gchar *
eos_office365_get_redirect_uri (EOAuth2Service *service,
				ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);
	CamelEwsSettings *ews_settings;

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings) {
		const gchar *res = NULL;

		camel_ews_settings_lock (ews_settings);

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			const gchar *redirect_uri;

			redirect_uri = camel_ews_settings_get_oauth2_redirect_uri (ews_settings);

			if (e_util_strcmp0 (redirect_uri, NULL) != 0)
				res = eos_office365_cache_string (oauth2_office365, redirect_uri);

			if (!res) {
				const gchar *endpoint_host = NULL;

				eos_office365_get_endpoint_host_and_tenant_locked (ews_settings, &endpoint_host, NULL);

				if (endpoint_host && g_strcmp0 (endpoint_host, OFFICE365_ENDPOINT_HOST) != 0) {
					res = eos_office365_cache_string_take (oauth2_office365,
						g_strdup_printf ("https://%s/common/oauth2/nativeclient", endpoint_host));
				}
			}
		}

		camel_ews_settings_unlock (ews_settings);

		if (res)
			return res;
	}

	if (e_util_strcmp0 (OFFICE365_REDIRECT_URI, NULL) != 0)
		return OFFICE365_REDIRECT_URI;

	return "https://" OFFICE365_ENDPOINT_HOST "/common/oauth2/nativeclient";
}

static gboolean
eos_office365_matches_fallback_resource_uri (const gchar *uri,
					     guint len)
{
	return uri && strlen (OFFICE365_FALLBACK_RESOURCE_URI) == len &&
		strncmp (uri, OFFICE365_FALLBACK_RESOURCE_URI, len) == 0;
}

static const gchar *
eos_office365_get_resource_uri (EOAuth2Service *service,
				ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);
	CamelEwsSettings *ews_settings;

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings) {
		const gchar *res = NULL;

		camel_ews_settings_lock (ews_settings);

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			const gchar *resource_uri;

			resource_uri = camel_ews_settings_get_oauth2_resource_uri (ews_settings);

			if (e_util_strcmp0 (resource_uri, NULL) != 0)
				res = eos_office365_cache_string (oauth2_office365, resource_uri);

		}

		if (!res) {
			const gchar *host_url;

			host_url = camel_ews_settings_get_hosturl (ews_settings);

			if (host_url && *host_url) {
				const gchar *ptr;

				ptr = strstr (host_url, "://");
				ptr = ptr ? strchr (ptr + 3, '/') : NULL;

				if (ptr && !eos_office365_matches_fallback_resource_uri (host_url, ptr - host_url)) {
					gchar *resource_uri;

					resource_uri = g_strndup (host_url, ptr - host_url);

					res = eos_office365_cache_string_take (oauth2_office365, resource_uri);
				}
			}
		}

		camel_ews_settings_unlock (ews_settings);

		if (res)
			return res;
	}

	return OFFICE365_FALLBACK_RESOURCE_URI;
}

static const gchar*
eos_office365_get_oauth2_v2_scope (EOAuth2Service *service,
				   ESource *source)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (service);

	return eos_office365_cache_string_take (oauth2_office365,
		g_strdup_printf ("%s %s/%s", OFFICE365_SCOPE, eos_office365_get_resource_uri (service, source), OFFICE365_HOST_SCOPE_SUFFIX));
}

static void
eos_office365_prepare_authentication_uri_query (EOAuth2Service *service,
						ESource *source,
						GHashTable *uri_query)
{
	CamelEwsSettings *ews_settings;

	g_return_if_fail (uri_query != NULL);

	e_oauth2_service_util_set_to_form (uri_query, "response_mode", "query");

	ews_settings = eos_office365_get_camel_settings (source);

	if (ews_settings && camel_ews_settings_get_use_oauth2_v2 (ews_settings))
		e_oauth2_service_util_set_to_form (uri_query, "scope", eos_office365_get_oauth2_v2_scope (service, source));
	else
		e_oauth2_service_util_set_to_form (uri_query, "resource", eos_office365_get_resource_uri (service, source));
}

static void
eos_office365_prepare_refresh_token_form (EOAuth2Service *service,
					  ESource *source,
					  const gchar *refresh_token,
					  GHashTable *form)
{
	CamelEwsSettings *ews_settings;

	g_return_if_fail (form != NULL);

	ews_settings = eos_office365_get_camel_settings (source);

	e_oauth2_service_util_set_to_form (form, "redirect_uri", e_oauth2_service_get_redirect_uri (service, source));

	if (ews_settings && camel_ews_settings_get_use_oauth2_v2 (ews_settings))
		e_oauth2_service_util_set_to_form (form, "scope", eos_office365_get_oauth2_v2_scope (service, source));
	else
		e_oauth2_service_util_set_to_form (form, "resource", eos_office365_get_resource_uri (service, source));
}

static void
eos_office365_finalize (GObject *object)
{
	EOAuth2ServiceOffice365 *oauth2_office365 = E_OAUTH2_SERVICE_OFFICE365 (object);

	g_mutex_lock (&oauth2_office365->priv->string_cache_lock);
	g_hash_table_destroy (oauth2_office365->priv->string_cache);
	g_mutex_unlock (&oauth2_office365->priv->string_cache_lock);
	g_mutex_clear (&oauth2_office365->priv->string_cache_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_oauth2_service_office365_parent_class)->finalize (object);
}

static SoupCookie *
eos_office365_get_prt_sso_cookie_sync (EOAuth2Service *service,
				       ESource *source,
				       GCancellable *cancellable)
{
	SoupCookie *prtsso_cookie = NULL;
	const gchar *sso_uri = eos_office365_get_authentication_uri (service, source);
	const gchar *client_id = eos_office365_get_client_id (service, source);
	const gchar *redirect_uri = eos_office365_get_redirect_uri (service, source);
	CamelEwsSettings *ews_settings = NULL;
	gchar *auth_user = NULL;
	GError *local_error = NULL;

	ews_settings = eos_office365_get_camel_settings (source);
	auth_user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (ews_settings));

	prtsso_cookie = e_ms_oapxbc_util_get_prt_sso_cookie_sync (service, source, client_id, sso_uri,
								  redirect_uri, auth_user, cancellable, &local_error);
	g_free (auth_user);
	if (!prtsso_cookie) {
		e_ews_debug_print (_("Failed to acquire PRT SSO cookie: %s"), local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
	}

	return prtsso_cookie;
}

static GSList *
eos_office365_dup_credentials_prompter_cookies_sync (EOAuth2Service *service,
						     ESource *source,
						     GCancellable *cancellable)
{
	SoupCookie *prtsso_cookie;
	GSList *cookies = NULL;
	prtsso_cookie = eos_office365_get_prt_sso_cookie_sync (service, source, cancellable);
	if (!prtsso_cookie) {
		return NULL;
	}
	cookies = g_slist_append (cookies, prtsso_cookie);
	return cookies;
}

static void
eos_office365_prepare_refresh_token_message (EOAuth2Service *service,
					     ESource *source,
					     SoupMessage *message)
{
	SoupMessageHeaders *request_headers;
	SoupCookie *cookie;

	cookie = eos_office365_get_prt_sso_cookie_sync (service, source, NULL);
	if (!cookie) {
		return;
	}
	request_headers = soup_message_get_request_headers (message);
	soup_message_headers_append (request_headers,
				     soup_cookie_get_name (cookie),
				     soup_cookie_get_value (cookie));
	soup_cookie_free (cookie);
}

static void
e_oauth2_service_office365_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->guess_can_process = eos_office365_guess_can_process;
	iface->get_name = eos_office365_get_name;
	iface->get_display_name = eos_office365_get_display_name;
	iface->get_client_id = eos_office365_get_client_id;
	iface->get_client_secret = eos_office365_get_client_secret;
	iface->get_authentication_uri = eos_office365_get_authentication_uri;
	iface->get_refresh_uri = eos_office365_get_refresh_uri;
	iface->get_redirect_uri = eos_office365_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_office365_prepare_authentication_uri_query;
	iface->prepare_refresh_token_form = eos_office365_prepare_refresh_token_form;
	iface->prepare_refresh_token_message = eos_office365_prepare_refresh_token_message;
	iface->dup_credentials_prompter_cookies_sync = eos_office365_dup_credentials_prompter_cookies_sync;
}

static void
e_oauth2_service_office365_class_init (EOAuth2ServiceOffice365Class *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = eos_office365_finalize;
}

static void
e_oauth2_service_office365_class_finalize (EOAuth2ServiceOffice365Class *klass)
{
}

static void
e_oauth2_service_office365_init (EOAuth2ServiceOffice365 *oauth2_office365)
{
	oauth2_office365->priv = e_oauth2_service_office365_get_instance_private (oauth2_office365);

	g_mutex_init (&oauth2_office365->priv->string_cache_lock);
	oauth2_office365->priv->string_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

void
e_oauth2_service_office365_type_register (GTypeModule *type_module)
{
	e_oauth2_service_office365_register_type (type_module);
}
