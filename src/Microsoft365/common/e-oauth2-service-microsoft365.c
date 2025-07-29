/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <libedataserver/libedataserver.h>

#include "camel-m365-settings.h"
#include "e-ms-oapxbc-util.h"

#include "e-oauth2-service-microsoft365.h"

/* https://portal.azure.com/
   https://docs.microsoft.com/en-us/graph/auth/

   https://docs.microsoft.com/en-us/azure/active-directory/develop/active-directory-developers-guide
   https://tsmatz.wordpress.com/2016/10/07/application-permission-with-v2-endpoint-and-microsoft-graph/
   https://learn.microsoft.com/en-us/azure/active-directory/develop/permissions-consent-overview
   https://learn.microsoft.com/en-us/azure/active-directory/develop/v2-oauth2-auth-code-flow
*/

#define MICROSOFT365_SCOPE	"Calendars.ReadWrite" \
				" Calendars.ReadWrite.Shared" \
				" Contacts.ReadWrite" \
				" Contacts.ReadWrite.Shared" \
				" Mail.ReadWrite" \
				" Mail.ReadWrite.Shared" \
				" Mail.Send" \
				" Mail.Send.Shared" \
				" MailboxSettings.ReadWrite" \
				/* " Notes.Create" */ \
				/* " Notes.ReadWrite.All" */ \
				" offline_access" \
				" People.Read" \
				" Tasks.ReadWrite" \
				" Tasks.ReadWrite.Shared" \
				" User.Read" \
				" User.ReadBasic.All" /* User.Read.All requires admin approval */

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
eos_microsoft365_cache_string_take (EOAuth2ServiceMicrosoft365 *oauth2_microsoft365,
				    gchar *str) /* takes ownership of the 'str' */
{
	const gchar *cached_str;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE_MICROSOFT365 (oauth2_microsoft365), NULL);

	if (!str)
		return NULL;

	if (!*str) {
		g_free (str);
		return "";
	}

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


static const gchar *
eos_microsoft365_cache_string (EOAuth2ServiceMicrosoft365 *oauth2_microsoft365,
			       const gchar *str)
{
	gchar *cached_str;

	g_return_val_if_fail (E_IS_OAUTH2_SERVICE_MICROSOFT365 (oauth2_microsoft365), NULL);

	if (!str || !*str)
		return str;

	g_mutex_lock (&oauth2_microsoft365->priv->string_cache_lock);

	cached_str = g_hash_table_lookup (oauth2_microsoft365->priv->string_cache, str);

	if (!cached_str) {
		cached_str = g_strdup (str);
		g_hash_table_insert (oauth2_microsoft365->priv->string_cache, cached_str, cached_str);
	}

	g_mutex_unlock (&oauth2_microsoft365->priv->string_cache_lock);

	return cached_str;
}

static void
eos_microsoft365_get_endpoint_host_and_tenant_locked (CamelM365Settings *m365_settings,
						      const gchar **out_endpoint_host,
						      const gchar **out_tenant)
{
	if (out_endpoint_host) {
		*out_endpoint_host = camel_m365_settings_get_oauth2_endpoint_host (m365_settings);

		if (e_util_strcmp0 (*out_endpoint_host, NULL) == 0)
			*out_endpoint_host = MICROSOFT365_ENDPOINT_HOST;
	}

	if (out_tenant) {
		*out_tenant = camel_m365_settings_get_oauth2_tenant (m365_settings);

		if (e_util_strcmp0 (*out_tenant, NULL) == 0)
			*out_tenant = MICROSOFT365_TENANT;
	}
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

	if (m365_settings) {
		const gchar *res = NULL;

		camel_m365_settings_lock (m365_settings);

		if (camel_m365_settings_get_override_oauth2 (m365_settings)) {
			const gchar *client_id;

			client_id = camel_m365_settings_get_oauth2_client_id (m365_settings);

			if (e_util_strcmp0 (client_id, NULL) != 0)
				res = eos_microsoft365_cache_string (oauth2_microsoft365, client_id);
		}

		camel_m365_settings_unlock (m365_settings);

		if (res)
			return res;
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

	if (m365_settings) {
		const gchar *res = NULL;

		camel_m365_settings_lock (m365_settings);

		if (camel_m365_settings_get_override_oauth2 (m365_settings)) {
			const gchar *endpoint_host = NULL;
			const gchar *tenant = NULL;

			eos_microsoft365_get_endpoint_host_and_tenant_locked (m365_settings, &endpoint_host, &tenant);

			res = eos_microsoft365_cache_string_take (oauth2_microsoft365,
				g_strdup_printf ("https://%s/%s/oauth2/v2.0/authorize", endpoint_host, tenant));
		}

		camel_m365_settings_unlock (m365_settings);

		if (res)
			return res;
	}

	return "https://" MICROSOFT365_ENDPOINT_HOST "/" MICROSOFT365_TENANT "/oauth2/v2.0/authorize";
}

static const gchar *
eos_microsoft365_get_refresh_uri (EOAuth2Service *service,
				  ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;

	m365_settings = eos_microsoft365_get_camel_settings (source);

	if (m365_settings) {
		const gchar *res = NULL;

		camel_m365_settings_lock (m365_settings);

		if (camel_m365_settings_get_override_oauth2 (m365_settings)) {
			const gchar *endpoint_host = NULL;
			const gchar *tenant = NULL;

			eos_microsoft365_get_endpoint_host_and_tenant_locked (m365_settings, &endpoint_host, &tenant);

			res = eos_microsoft365_cache_string_take (oauth2_microsoft365,
				g_strdup_printf ("https://%s/%s/oauth2/v2.0/token", endpoint_host, tenant));
		}

		camel_m365_settings_unlock (m365_settings);

		if (res)
			return res;
	}

	return "https://" MICROSOFT365_ENDPOINT_HOST "/" MICROSOFT365_TENANT "/oauth2/v2.0/token";
}

static const gchar *
eos_microsoft365_get_redirect_uri (EOAuth2Service *service,
				   ESource *source)
{
	EOAuth2ServiceMicrosoft365 *oauth2_microsoft365 = E_OAUTH2_SERVICE_MICROSOFT365 (service);
	CamelM365Settings *m365_settings;

	m365_settings = eos_microsoft365_get_camel_settings (source);

	if (m365_settings) {
		const gchar *res = NULL;

		camel_m365_settings_lock (m365_settings);

		if (camel_m365_settings_get_override_oauth2 (m365_settings)) {
			const gchar *redirect_uri;

			redirect_uri = camel_m365_settings_get_oauth2_redirect_uri (m365_settings);

			if (e_util_strcmp0 (redirect_uri, NULL) != 0)
				res = eos_microsoft365_cache_string (oauth2_microsoft365, redirect_uri);

			if (!res) {
				const gchar *endpoint_host = NULL;

				eos_microsoft365_get_endpoint_host_and_tenant_locked (m365_settings, &endpoint_host, NULL);

				if (endpoint_host && g_strcmp0 (endpoint_host, MICROSOFT365_ENDPOINT_HOST) != 0) {
					res = eos_microsoft365_cache_string_take (oauth2_microsoft365,
						g_strdup_printf ("https://%s/common/oauth2/nativeclient", endpoint_host));
				}
			}
		}

		camel_m365_settings_unlock (m365_settings);

		if (res)
			return res;
	}

	if (e_util_strcmp0 (MICROSOFT365_REDIRECT_URI, NULL) != 0)
		return MICROSOFT365_REDIRECT_URI;

	return "https://" MICROSOFT365_ENDPOINT_HOST "/common/oauth2/nativeclient";
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

static SoupCookie *
eos_microsoft365_get_prt_sso_cookie_sync (EOAuth2Service *service,
					  ESource *source,
					  GCancellable *cancellable)
{
	SoupCookie *prtsso_cookie = NULL;
	const gchar *sso_uri = eos_microsoft365_get_authentication_uri (service, source);
	const gchar *client_id = eos_microsoft365_get_client_id (service, source);
	const gchar *redirect_uri = eos_microsoft365_get_redirect_uri (service, source);
	CamelM365Settings *camel_settings = NULL;
	gchar *auth_user = NULL;
	GError *local_error = NULL;

	camel_settings = eos_microsoft365_get_camel_settings (source);
	auth_user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (camel_settings));

	prtsso_cookie = e_ms_oapxbc_util_get_prt_sso_cookie_sync (service, source, client_id, sso_uri,
								  redirect_uri, auth_user, cancellable, &local_error);
	g_free (auth_user);
	if (!prtsso_cookie) {
		/* no error reporting yet, infra is missing */
		g_clear_error (&local_error);
	}

	return prtsso_cookie;
}

static GSList *
eos_microsoft365_dup_credentials_prompter_cookies_sync (EOAuth2Service *service,
						        ESource *source,
						        GCancellable *cancellable)
{
	SoupCookie *prtsso_cookie;
	GSList *cookies = NULL;
	prtsso_cookie = eos_microsoft365_get_prt_sso_cookie_sync (service, source, cancellable);
	if (!prtsso_cookie) {
		return NULL;
	}
	cookies = g_slist_append (cookies, prtsso_cookie);
	return cookies;
}

static void
eos_microsoft365_prepare_refresh_token_message (EOAuth2Service *service,
						ESource *source,
						SoupMessage *message)
{
	SoupMessageHeaders *request_headers;
	SoupCookie *cookie;

	cookie = eos_microsoft365_get_prt_sso_cookie_sync (service, source, NULL);
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
	iface->prepare_refresh_token_form = eos_microsoft365_prepare_refresh_token_form;
	iface->prepare_refresh_token_message = eos_microsoft365_prepare_refresh_token_message;
	iface->dup_credentials_prompter_cookies_sync = eos_microsoft365_dup_credentials_prompter_cookies_sync;
}

static void
e_oauth2_service_microsoft365_class_init (EOAuth2ServiceMicrosoft365Class *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = eos_microsoft365_finalize;

	/* Make sure the ESourceCamel knows about it, even when no Microsoft365 mail account is created */
	e_source_camel_generate_subtype ("microsoft365", CAMEL_TYPE_M365_SETTINGS);
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
