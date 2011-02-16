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

#include "e-ews-compat.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <shell/e-shell.h>
#if EDS_CHECK_VERSION(2,33,0)
#include <mail/e-mail-session.h>
#else
#include <mail/mail-session.h>
#endif
#include <mail/e-mail-backend.h>

#include <camel/camel.h>
#include <libedataserver/e-account.h>
#include <libedataserver/e-account-list.h>
#include <camel-ews-utils.h>

#include "exchange-ews-account-listener.h"
#include "exchange-ews-account-setup.h"
#include "camel-ews-store-summary.h"
#include "ews-esource-utils.h"

#define d(x) x

G_DEFINE_TYPE (ExchangeEWSAccountListener, exchange_ews_account_listener, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;

struct _ExchangeEWSAccountListenerPrivate {
	GConfClient *gconf_client;
	EAccountList *account_list;
};

typedef struct _EwsAccountInfo EwsAccountInfo;

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

static EwsAccountInfo*
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
ews_account_removed (EAccountList *account_listener, EAccount *account)
{
	EShell *shell;
	EShellBackend *shell_backend;
	EVO3(EMailSession *session;)
	EwsAccountInfo *info = NULL;
	CamelStore *store;

	if (!is_ews_account (account))
		return;

	info = lookup_account_info (account->uid);
	if (!info)
		return;

	ews_esource_utils_remove_groups (account->id->address);
	ews_accounts = g_list_remove (ews_accounts, info);

	shell = e_shell_get_default ();
	shell_backend = e_shell_get_backend_by_name (shell, "mail");
	EVO3(session = e_mail_backend_get_session (E_MAIL_BACKEND (shell_backend)));
	store = (CamelStore *) camel_session_get_service (CAMEL_SESSION (session), 
						account->source->url, CAMEL_PROVIDER_STORE, NULL);

	/* FIXME This has to go through the CamelStore instead of accessing through derived class.
	    Ideally Evo should delete the cache when the email account is removed */
	camel_ews_store_summary_remove (((CamelEwsStore *)store)->summary);

	ews_account_info_free (info);
	g_object_unref (store);
}

static void
ews_account_changed (EAccountList *account_listener, EAccount *account)
{
	gboolean ews_account = FALSE;
	EwsAccountInfo *existing_account_info = NULL;

	ews_account = is_ews_account (account);

	if (ews_account)
		existing_account_info = lookup_account_info (account->uid);

	if (existing_account_info == NULL && ews_account && account->enabled) {
		EwsAccountInfo *info = ews_account_info_from_eaccount (account);
		ews_accounts = g_list_append (ews_accounts, info);
	} else if (existing_account_info != NULL && !ews_account)
		ews_account_removed (account_listener, account);
	else if (existing_account_info != NULL && ews_account) {
		if (!account->enabled)
			ews_account_removed (account_listener, account);
		else {
			/* TODO update props like refresh timeout */
		}
	}
}

static void
ews_account_added (EAccountList *account_listener, EAccount *account)
{
	gboolean ews_account = FALSE;
	
	ews_account = is_ews_account (account);

	if (ews_account) {
		EwsAccountInfo *info = ews_account_info_from_eaccount (account);
		ews_accounts = g_list_append (ews_accounts, info);
	}
}

static void
exchange_ews_account_listener_construct (ExchangeEWSAccountListener *config_listener)
{
	EIterator *iter;

	d(g_print ("\n Construct the listener"));
	
	config_listener->priv->account_list = e_account_list_new (config_listener->priv->gconf_client);

	for (iter = e_list_get_iterator (E_LIST(config_listener->priv->account_list)); e_iterator_is_valid (iter); e_iterator_next (iter)) {
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
	config_listener->priv->gconf_client = gconf_client_get_default();

	exchange_ews_account_listener_construct (config_listener);

	return config_listener;
}
