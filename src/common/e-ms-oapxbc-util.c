/*
 * SPDX-FileCopyrightText: (C) 2024 Siemens AG
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "e-ms-oapxbc-util.h"

#define MICROSOFT_ENTRA_ID_HOST "login.microsoftonline.com"

SoupCookie *
e_ms_oapxbc_util_get_prt_sso_cookie_sync (EOAuth2Service *service,
					  ESource *source,
					  const gchar *client_id,
					  const gchar *sso_uri,
					  const gchar *redirect_uri,
					  const gchar *auth_user,
					  GCancellable *cancellable,
					  GError **error)
{
	EMsOapxbc *app;
	JsonObject *accounts = NULL, *account = NULL;
	JsonArray *scopes, *accounts_member;
	SoupCookie *prtsso_cookie = NULL;
	/* The broker workflow [ms-oapxbc] is only supported on Azure Entra ID */
	const gchar *entra_sso_uri = "https://" MICROSOFT_ENTRA_ID_HOST;
	const gchar *authority = "https://" MICROSOFT_ENTRA_ID_HOST "/common";
	const gchar *account_user = NULL;
	guint accounts_len = 0, ii = 0;
	GError *local_error = NULL;

	/* Check if the SSO URI is pointing to Azure Entra ID */
	if (!sso_uri || g_ascii_strncasecmp (sso_uri, entra_sso_uri, strlen (entra_sso_uri)) != 0) {
		return NULL;
	}

	app = e_ms_oapxbc_new_sync (client_id, authority, cancellable, &local_error);
	if (!app) {
		if (local_error) {
			g_prefix_error (&local_error, "%s", _("Failed to create MS OAPXBC app: "));
			g_propagate_error (error, local_error);
		}
		return NULL;
	}
	accounts = e_ms_oapxbc_get_accounts_sync (app, cancellable, &local_error);
	if (!accounts) {
		if (local_error) {
			g_prefix_error (&local_error, "%s", _("Failed to get accounts: "));
			g_propagate_error (error, local_error);
		}
		g_object_unref (app);
		return NULL;
	}
	accounts_member = json_object_get_array_member (accounts, "accounts");
	if (accounts_member) {
		accounts_len = json_array_get_length (accounts_member);
	}
	if (!accounts_member || accounts_len == 0) {
		json_object_unref (accounts);
		g_object_unref (app);
		return NULL;
	}

	/* iterate accounts, find one with corresponding email */
	for (ii = 0; ii < accounts_len; ii++) {
		account = json_array_get_object_element (accounts_member, ii);
		account_user = json_object_get_string_member (account, "username");
		if (account_user && g_ascii_strcasecmp (account_user, auth_user) == 0) {
			break;
		}
		account = NULL;
	}
	if (!account) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     _("No account found for user '%s'"), auth_user);
		json_object_unref (accounts);
		g_object_unref (app);
		return NULL;
	}

	scopes = json_array_new ();
	json_array_add_string_element (scopes, "https://graph.microsoft.com/.default");
	prtsso_cookie = e_ms_oapxbc_acquire_prt_sso_cookie_sync (app, account, sso_uri, scopes,
								 redirect_uri, cancellable, &local_error);
	if (!prtsso_cookie && local_error) {
		g_propagate_error (error, local_error);
	}
	json_array_unref (scopes);
	json_object_unref (accounts);
	g_object_unref (app);

	return prtsso_cookie;
}
