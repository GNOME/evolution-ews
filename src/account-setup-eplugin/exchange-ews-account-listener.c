/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <shell/e-shell.h>
#include <mail/e-mail-backend.h>

#include <camel/camel.h>
#include <libedataserver/e-account.h>
#include <libedataserver/e-account-list.h>
#include <libebook/e-book.h>

#include "camel/camel-ews-store-summary.h"
#include "camel/camel-ews-utils.h"

#include "utils/ews-esource-utils.h"

#include "exchange-ews-account-listener.h"
#include "exchange-ews-account-setup.h"

#define d(x)

G_DEFINE_TYPE (ExchangeEWSAccountListener, exchange_ews_account_listener, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;

struct _ExchangeEWSAccountListenerPrivate {
	GConfClient *gconf_client;
	EAccountList *account_list;
};

typedef struct _EwsAccountInfo EwsAccountInfo;

static void
ews_account_added (EAccountList *account_listener, EAccount *account);

struct _EwsAccountInfo {
	gchar *uid;
	gchar *name;
	gchar *source_url;
	gboolean enabled;
};

static	GList *ews_accounts = NULL;

static gboolean
is_ews_account (EAccount *account)
{
	return (account->source->url && (g_ascii_strncasecmp (account->source->url, EWS_URI_PREFIX, EWS_PREFIX_LENGTH) == 0));
}

static EwsAccountInfo *
lookup_account_info (const gchar *key)
{
	GList *list;

	g_return_val_if_fail (key != NULL, NULL);

	for (list = g_list_first (ews_accounts); list; list = g_list_next (list)) {
		EwsAccountInfo *info = (EwsAccountInfo *)(list->data);
		if (g_ascii_strcasecmp (info->uid, key) == 0)
			return info;
	}

	return NULL;
}

static EwsAccountInfo *
ews_account_info_from_eaccount (EAccount *account)
{
	EwsAccountInfo *info;

	info = g_new0 (EwsAccountInfo, 1);
	info->uid = g_strdup (account->uid);
	info->name = g_strdup (account->name);
	info->source_url = g_strdup (account->source->url);
	info->enabled = account->enabled;

	return info;
}

static void
ews_account_info_free (EwsAccountInfo *info)
{
	if (info) {
		g_free (info->uid);
		g_free (info->name);
		g_free (info->source_url);
		g_free (info);
	}
}

static void
ews_account_removed (EAccountList *account_listener,
                     EAccount *account)
{
	EShell *shell;
	EShellBackend *shell_backend;
	CamelSession *session;
	EwsAccountInfo *info = NULL;
	CamelService *service;
	CamelURL *url;
	gchar *summary_file, *storage_path;

	if (!is_ews_account (account))
		return;

	info = lookup_account_info (account->uid);
	if (!info)
		return;

	url = camel_url_new (account->source->url, NULL);

	ews_esource_utils_remove_groups (url);
	ews_accounts = g_list_remove (ews_accounts, info);

	shell = e_shell_get_default ();
	shell_backend = e_shell_get_backend_by_name (shell, "mail");
	session = (CamelSession *) e_mail_backend_get_session (E_MAIL_BACKEND (shell_backend));
	service = camel_session_get_service_by_url (session, url, CAMEL_PROVIDER_STORE);
	camel_url_free (url);

	/* FIXME  Use this until CamelStore gets camel_store_remove_storage API which would be available eds 3.2 onwards */
	storage_path = g_strdup (camel_service_get_user_cache_dir (service));
	summary_file = g_build_filename (storage_path, "folder-tree", NULL);
	g_unlink (summary_file);

	d(g_print ("Removed ews store summary: %s \n", summary_file);)

	g_free (storage_path);
	g_free (summary_file);
	ews_account_info_free (info);

	/* FIXME: This wasn't done for EDS 3.2+?  Is that right? 
	 * g_object_unref (service);
	*/
}

static gboolean
ews_is_str_equal (const gchar *str1,
                  const gchar *str2)
{
	if (str1 && str2 && !strcmp (str1, str2))
		return TRUE;
	else if (!str1 && !str2)
		return TRUE;
	else
		return FALSE;
}

static gboolean
remove_gal_esource (CamelURL *account_url)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	GConfClient *client;
	const gchar *conf_key;
	GSList *sources;
	gboolean ret = TRUE;
	EBook *book;
	GError *error = NULL;

	conf_key = CONTACT_SOURCES;
	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = ews_esource_utils_ensure_group (source_list, account_url);

	sources = e_source_group_peek_sources (group);
	if (!(source = ews_find_source_by_matched_prop (sources, "gal", "1"))) {
		ret = FALSE;
		goto exit;
	}

	book = e_book_new (source, &error);
	if (book) {
		e_book_remove (book, &error);
		g_object_unref (book);
	}

	e_source_group_remove_source (group, source);
	e_source_list_sync (source_list, NULL);

exit:
	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);

	if (error) {
		g_warning ("Unable to remove GAL cache : %s \n", error->message);
		g_clear_error (&error);
	}

	return ret;
}

/* add gal esource. If oal is not selected, gal will be just used for auto-completion */
static void
add_gal_esource (CamelURL *url)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	GConfClient *client;
	const gchar *conf_key, *email_id;
	const gchar *oal_sel, *tmp, *oal_name;
	gchar *oal_id = NULL;
	gchar *account_uri, *source_uri;

	conf_key = CONTACT_SOURCES;
	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	email_id = camel_url_get_param (url, "email");
	oal_sel = camel_url_get_param (url, "oal-selected");

	/* if oal is not selected, gal just performs auto-completion and does not cache GAL */	
	if (oal_sel) {
		tmp = strrchr (oal_sel, ':');
		oal_name = tmp + 1;
		oal_id = g_strndup (oal_sel, (tmp - oal_sel));
	} else
		oal_name = _("Global Address list");

	account_uri = camel_url_to_string (url, CAMEL_URL_HIDE_PARAMS);
	source_uri = g_strdup_printf ("%s;gal=1", account_uri + strlen (EWS_BASE_URI));
	source = e_source_new (oal_name, source_uri);

	/* set properties */
	e_source_set_property (source, "username", url->user);
	e_source_set_property (source, "auth-domain", "Exchange Web Services");
	e_source_set_property (source, "email", email_id);
	e_source_set_property (source, "gal", "1");
	e_source_set_property (source, "hosturl", camel_url_get_param (url, "hosturl"));
	e_source_set_property (source, "delete", "no");

	/* If oal_id is present it means the GAL is marked for offline usage, we do not check for offline_sync property */
	if (oal_sel) {
		e_source_set_property (source, "oal_id", oal_id);
		e_source_set_property (source, "oab_url", camel_url_get_param (url, "oaburl"));
	}

	e_source_set_property (source, "auth", "plain/password");
	e_source_set_property (source, "completion", "true");

	/* add the source to group and sync */
	group = ews_esource_utils_ensure_group (source_list, url);
	e_source_group_add_source (group, source, -1);
	e_source_list_sync (source_list, NULL);

	g_object_unref (source);
	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);
	g_free (oal_id);
	g_free (account_uri);
	g_free (source_uri);

	return;
}

static void
ews_account_changed (EAccountList *account_listener,
                     EAccount *account)
{
	gboolean ews_account = FALSE;
	EwsAccountInfo *existing_account_info = NULL;

	ews_account = is_ews_account (account);

	if (ews_account)
		existing_account_info = lookup_account_info (account->uid);

	if (existing_account_info == NULL && ews_account && account->enabled) {
		ews_account_added (account_listener, account);
	} else if (existing_account_info != NULL && !ews_account)
		ews_account_removed (account_listener, account);
	else if (existing_account_info != NULL && ews_account) {
		if (!account->enabled)
			ews_account_removed (account_listener, account);
		else {
			CamelURL *old_url, *new_url;
			const gchar *o_oal_sel, *n_oal_sel;

			/* TODO update props like refresh timeout */
			old_url = camel_url_new (existing_account_info->source_url, NULL);
			new_url = camel_url_new (account->source->url, NULL);

			o_oal_sel = camel_url_get_param (old_url, "oal-selected");
			n_oal_sel = camel_url_get_param (new_url, "oal-selected");

			if (!ews_is_str_equal (o_oal_sel, n_oal_sel)) {
				/* remove gal esource and cache associated with it */
				remove_gal_esource (new_url);

				/* add gal esource */
				add_gal_esource (new_url);
			}

			g_free (existing_account_info->name);
			g_free (existing_account_info->source_url);
			existing_account_info->name = g_strdup (account->name);
			existing_account_info->source_url = g_strdup (account->source->url);

			camel_url_free (old_url);
			camel_url_free (new_url);
		}
	}
}

static void
ews_account_added (EAccountList *account_listener,
                   EAccount *account)
{
	gboolean ews_account = FALSE;

	ews_account = is_ews_account (account);

	if (ews_account) {
		CamelURL *url;

		EwsAccountInfo *info = ews_account_info_from_eaccount (account);
		ews_accounts = g_list_append (ews_accounts, info);
		url = camel_url_new (account->source->url, NULL);

		/* add gal esource */
		add_gal_esource (url);

		camel_url_free (url);
	}
}

static void
exchange_ews_account_listener_construct (ExchangeEWSAccountListener *config_listener)
{
	EIterator *iter;

	config_listener->priv->account_list = e_account_list_new (config_listener->priv->gconf_client);

	for (iter = e_list_get_iterator (E_LIST (config_listener->priv->account_list)); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		EAccount *account = E_ACCOUNT (e_iterator_get (iter));
		if (is_ews_account (account) && account->enabled) {
			EwsAccountInfo *info;

			info = ews_account_info_from_eaccount (account);
			ews_accounts = g_list_append (ews_accounts, info);
		}
	}

	g_signal_connect (config_listener->priv->account_list, "account_added", G_CALLBACK (ews_account_added), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_changed", G_CALLBACK (ews_account_changed), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_removed", G_CALLBACK (ews_account_removed), NULL);
}

static void
dispose (GObject *object)
{
	ExchangeEWSAccountListener *config_listener = EXCHANGE_EWS_ACCOUNT_LISTENER (object);

	g_object_unref (config_listener->priv->gconf_client);
	g_object_unref (config_listener->priv->account_list);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeEWSAccountListener *config_listener = EXCHANGE_EWS_ACCOUNT_LISTENER (object);
	GList *list;

	if (config_listener->priv) {
		g_free (config_listener->priv);
	}

	for (list = g_list_first (ews_accounts); list; list = g_list_next (list)) {
		EwsAccountInfo *info = (EwsAccountInfo *)(list->data);
		ews_account_info_free (info);
	}

	g_list_free (ews_accounts);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
exchange_ews_account_listener_class_init (ExchangeEWSAccountListenerClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	object_class = G_OBJECT_CLASS (class);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
exchange_ews_account_listener_init (ExchangeEWSAccountListener *config_listener)
{
	config_listener->priv = g_new0 (ExchangeEWSAccountListenerPrivate, 1);
}

ExchangeEWSAccountListener *
exchange_ews_account_listener_new (void)
{
	ExchangeEWSAccountListener *config_listener;

	config_listener = g_object_new (EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE, NULL);
	config_listener->priv->gconf_client = gconf_client_get_default ();

	exchange_ews_account_listener_construct (config_listener);

	return config_listener;
}
