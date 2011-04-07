/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libedataserverui/e-passwords.h>
#include <libedataserver/e-account.h>
#include <e-util/e-dialog-utils.h>
#include "mail/em-config.h"
#include "exchange-ews-account-setup.h"
#include <addressbook/gui/widgets/eab-config.h>
#include <calendar/gui/e-cal-config.h>

#include <camel-ews-folder.h>
#include <e-ews-connection.h>
#include <camel-ews-utils.h>

#define d(x) x

gint e_plugin_lib_enable (EPlugin *ep, gint enable);

/* Account Setup */
GtkWidget *org_gnome_exchange_ews_account_setup (EPlugin *epl, EConfigHookItemFactoryData *data);
gboolean org_gnome_exchange_ews_check_options(EPlugin *epl, EConfigHookPageCheckData *data);

/* New Addressbook/CAL */
GtkWidget *exchange_ews_create_addressbook (EPlugin *epl, EConfigHookItemFactoryData *data);
GtkWidget *exchange_ews_create_calendar (EPlugin *epl, EConfigHookItemFactoryData *data);

/* New Addressbook */
gboolean exchange_ews_book_check (EPlugin *epl, EConfigHookPageCheckData *data);
void exchange_ews_book_commit (EPlugin *epl, EConfigTarget *target);

/* New calendar/task list/memo list */
gboolean exchange_ews_cal_check (EPlugin *epl, EConfigHookPageCheckData *data);
void exchange_ews_cal_commit (EPlugin *epl, EConfigTarget *target);

static ExchangeEWSAccountListener *config_listener = NULL;

static void
free_ews_listener ( void )
{
	g_object_unref (config_listener);
}

gint
e_plugin_lib_enable (EPlugin *ep, gint enable)
{
	g_debug ("Loading Exchange EWS Plugin \n");

	if (!config_listener) {
		config_listener = exchange_ews_account_listener_new ();
		g_atexit ( free_ews_listener );
	}

	return 0;
}

ExchangeEWSAccountListener *
exchange_ews_accounts_peek_config_listener ()
{
	return config_listener;
}

struct _AutoDiscCallBackData {
	EConfig *config;
	GtkWidget *entry;
};

static void autodiscover_callback (char *url, gpointer user_data, GError *error)
{
	if (error) {
		g_warning ("Autodiscover failed: %s", error->message);
		g_clear_error (&error);
	}
	if (url) {
		g_message("Got ASURL %s", url);
		gtk_entry_set_text (GTK_ENTRY (user_data), url);
		g_free (url);
	}
}
 

static void
validate_credentials (GtkWidget *widget, struct _AutoDiscCallBackData *cbdata)
{
	EConfig *config = cbdata->config;
	EMConfigTargetAccount *target_account = (EMConfigTargetAccount *)(config->target);
	CamelURL *url = NULL;
	gchar *key, *password;
	
	url = camel_url_new (e_account_get_string (target_account->account, E_ACCOUNT_SOURCE_URL), NULL);

	key = camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD | CAMEL_URL_HIDE_PARAMS);
	password = e_passwords_get_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
	if (!password || !*password) {
		gboolean remember = e_account_get_bool (target_account->account, E_ACCOUNT_SOURCE_SAVE_PASSWD);
		gchar *title;

		g_free (password);
		title = g_strdup_printf (_("Enter Password for %s"), target_account->account->id->address);
		password = e_passwords_ask_password (title, EXCHANGE_EWS_PASSWORD_COMPONENT, key, title,
						     E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET,
						     &remember, NULL);
		g_free (title);
	}

	/*Can there be a account without password ?*/
	if (password && *password) {
		e_ews_autodiscover_ws_url (autodiscover_callback, cbdata->entry,
					   g_strdup(target_account->account->id->address),
					   password);
	} else {
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		e_notice (NULL, GTK_MESSAGE_ERROR, "%s", _("Authentication failed."));
		g_free (password);
	}

	g_free (key);
	camel_url_free (url);
}

static void
host_url_changed (GtkWidget *entry, EConfig *config)
{
	EMConfigTargetAccount *target = (EMConfigTargetAccount *)(config->target);
	CamelURL *url = NULL;
	const gchar *domain = NULL;
	gchar *url_string = NULL;

	url = camel_url_new (e_account_get_string(target->account, E_ACCOUNT_SOURCE_URL), NULL);
	domain = gtk_entry_get_text (GTK_ENTRY(entry));

	if (domain && domain[0]) {
		CamelURL *hosturl;
		camel_url_set_param (url, "hosturl", domain);
		hosturl = camel_url_new (domain, NULL);
		if (hosturl) {
			camel_url_set_host (url, hosturl->host);
			camel_url_free (hosturl);
		}
	} else
		camel_url_set_param (url, "hosturl", NULL);

	url_string = camel_url_to_string (url, 0);
	e_account_set_string (target->account, E_ACCOUNT_SOURCE_URL, url_string);
	e_account_set_string (target->account, E_ACCOUNT_TRANSPORT_URL, url_string);
	g_free (url_string);

	camel_url_free (url);
}

GtkWidget *
org_gnome_exchange_ews_account_setup (EPlugin *epl, EConfigHookItemFactoryData *data)
{
	EMConfigTargetAccount *target_account;
	CamelURL *url;
	GtkWidget *hbox = NULL;
	gint row;

	target_account = (EMConfigTargetAccount *)data->config->target;
	url = camel_url_new(e_account_get_string(target_account->account, E_ACCOUNT_SOURCE_URL), NULL);

	/* is NULL on new account creation */
	if (url == NULL)
		return NULL;

	if (!g_ascii_strcasecmp (url->protocol, "ews")) {
		GtkWidget *label;
		GtkWidget *host_url;
		GtkWidget *auto_discover;
		const gchar *host_url_val = camel_url_get_param (url, "hosturl");
		const gchar *temp, *email_id;
		gchar *url_string;
		struct _AutoDiscCallBackData *cbdata = g_new0 (struct _AutoDiscCallBackData, 1);

		g_object_get (data->parent, "n-rows", &row, NULL);
	
		/* Set email_id */
		email_id = target_account->account->id->address;
		camel_url_set_param (url, "email", email_id);
		temp = g_strstr_len (email_id, -1, "@");
		/* Don't overwrite the URL if it's already been set */
		if (!url->host || !url->host[0])
			camel_url_set_host (url, g_strdup (temp + 1));
		
		url_string = camel_url_to_string (url, 0);
		e_account_set_string (target_account->account, E_ACCOUNT_SOURCE_URL, url_string);
		e_account_set_string (target_account->account, E_ACCOUNT_TRANSPORT_URL, url_string);
		g_free (url_string);

		/* Host url and Autodiscover button */
		hbox = gtk_hbox_new (FALSE, 6);
		label = gtk_label_new_with_mnemonic (_("_Host Url:"));
		gtk_widget_show (label);

		host_url = gtk_entry_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), host_url);
		if (host_url_val && *host_url_val)
			gtk_entry_set_text (GTK_ENTRY (host_url), host_url_val);
		gtk_box_pack_start (GTK_BOX (hbox), host_url, FALSE, FALSE, 0);
		g_signal_connect (host_url, "changed", G_CALLBACK(host_url_changed), data->config);

		cbdata->config = data->config;
		cbdata->entry = host_url;
		auto_discover = gtk_button_new_with_mnemonic (_("_Fetch Url"));
		gtk_box_pack_start (GTK_BOX (hbox), auto_discover, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT(auto_discover), "clicked",  G_CALLBACK(validate_credentials), cbdata);

		gtk_table_attach (GTK_TABLE (data->parent), label, 0, 1, row, row+1, 0, 0, 0, 0);
		gtk_widget_show_all (GTK_WIDGET (hbox));
		gtk_table_attach (GTK_TABLE (data->parent), GTK_WIDGET (hbox), 1, 2, row, row+1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
		row++;
	}

	camel_url_free (url);
	return hbox;
}

gboolean
org_gnome_exchange_ews_check_options(EPlugin *epl, EConfigHookPageCheckData *data)
{
	EMConfigTargetAccount *target = (EMConfigTargetAccount *)(data->config->target);
	gboolean status = TRUE;

	if (data->pageid == NULL || data->pageid[0] == 0 ||
	    g_ascii_strcasecmp (data->pageid, "10.receive") == 0) {
		CamelURL *url = camel_url_new (e_account_get_string(target->account,
								    E_ACCOUNT_SOURCE_URL), NULL);

		if (url && url->protocol && g_ascii_strcasecmp (url->protocol, "ews") == 0) {
			const gchar *url_str = NULL;
			CamelURL *hurl;

			url_str = camel_url_get_param (url, "hosturl");
			hurl = camel_url_new (url_str, NULL);

			/*Host url not set. Do not proceed with account creation.*/
			if (!hurl)
				status = FALSE;
			else
				camel_url_free (hurl);
		}



		if (url)
			camel_url_free(url);
	}
	
	return status;
}
