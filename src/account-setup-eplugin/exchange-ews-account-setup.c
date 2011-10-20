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
#include <libedataserver/eds-version.h>
#include <e-util/e-dialog-utils.h>
#include "mail/em-config.h"
#include "exchange-ews-account-setup.h"
#include <addressbook/gui/widgets/eab-config.h>
#include <calendar/gui/e-cal-config.h>

#include <camel-ews-folder.h>
#include <e-ews-connection.h>
#include <camel-ews-utils.h>
#include <shell/e-shell.h>

#define d(x) x

#if GTK_CHECK_VERSION(2,24,0)
#else
#define GTK_COMBO_BOX_TEXT GTK_COMBO_BOX
#define GTK_IS_COMBO_BOX_TEXT GTK_IS_COMBO_BOX
#define gtk_combo_box_text_append_text gtk_combo_box_append_text
#define gtk_combo_box_text_new gtk_combo_box_new_text
#define GtkComboBoxText GtkComboBox
#endif

gint e_plugin_lib_enable (EPlugin *ep, gint enable);

/* Account Setup */
GtkWidget *org_gnome_exchange_ews_account_setup (EPlugin *epl, EConfigHookItemFactoryData *data);
gboolean org_gnome_exchange_ews_check_options(EPlugin *epl, EConfigHookPageCheckData *data);

/* OAB receiving options */
GtkWidget * org_gnome_ews_oab_settings (EPlugin *epl, EConfigHookItemFactoryData *data);

static ExchangeEWSAccountListener *config_listener = NULL;

static void
free_ews_listener ( void )
{
	g_object_unref (config_listener);
}

gint
e_plugin_lib_enable (EPlugin *ep, gint enable)
{
	if (!config_listener) {
		config_listener = exchange_ews_account_listener_new ();
		g_atexit ( free_ews_listener );
	}

	return 0;
}

static EAccount *
get_modified_account (EMConfigTargetAccount *target)
{
	return target->modified_account;
}

ExchangeEWSAccountListener *
exchange_ews_accounts_peek_config_listener ()
{
	return config_listener;
}

struct _AutoDiscCallBackData {
	EConfig *config;
	GtkWidget *host_entry;
	GtkWidget *oab_entry;
};

static void autodiscover_callback (EwsUrls *urls, gpointer user_data, GError *error)
{
	struct _AutoDiscCallBackData *cbdata = (struct _AutoDiscCallBackData *) user_data;
	
	if (error) {
		g_warning ("Autodiscover failed: %s", error->message);
		e_notice (NULL, GTK_MESSAGE_ERROR, _("Autodiscover failed: %s"), error->message);
		g_clear_error (&error);
	}
	if (urls) {
		char *oab_url;
		
		gtk_entry_set_text (GTK_ENTRY (cbdata->host_entry), urls->as_url);

		oab_url = g_strconcat (urls->oab_url, "oab.xml", NULL);
		gtk_entry_set_text (GTK_ENTRY (cbdata->oab_entry), oab_url);
		g_free (oab_url);
		
		g_free (urls->as_url);
		g_free (urls->oab_url);
		g_free (urls);
	}
}

static char *
get_password (EMConfigTargetAccount *target_account)
{
	gchar *key, *password = NULL;
	CamelURL *url;
	EAccount *account;

	account = get_modified_account (target_account);
	url = camel_url_new (e_account_get_string (account, E_ACCOUNT_SOURCE_URL), NULL);

	key = camel_url_to_string (url, CAMEL_URL_HIDE_PARAMS);
	password = e_passwords_get_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
	if (!password || !*password) {
		gboolean remember = e_account_get_bool (account, E_ACCOUNT_SOURCE_SAVE_PASSWD);
		gchar *title;

		g_free (password);
		title = g_strdup_printf (_("Enter Password for %s"), account->id->address);
		password = e_passwords_ask_password (title, EXCHANGE_EWS_PASSWORD_COMPONENT, key, title,
                                                    E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET,
						     &remember, NULL);
		g_free (title);
	}

	if (!password || !*password) {
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		e_notice (NULL, GTK_MESSAGE_ERROR, "%s", _("Could not get password."));
	}

	g_free (key);
	camel_url_free (url);
	
	return password;
}

static void
validate_credentials (GtkWidget *widget, struct _AutoDiscCallBackData *cbdata)
{
	EConfig *config = cbdata->config;
	EMConfigTargetAccount *target_account = (EMConfigTargetAccount *)(config->target);
	gchar *password = NULL;

	password = get_password (target_account);
	/*Can there be a account without password ?*/
	if (password && *password) {
		e_ews_autodiscover_ws_url (autodiscover_callback, cbdata,
					   (get_modified_account (target_account))->id->address,
					   password);
	}
	g_free (password);
}

static void
url_changed (GtkWidget *entry, EConfig *config, const gchar *param)
{
	EMConfigTargetAccount *target = (EMConfigTargetAccount *)(config->target);
	CamelURL *url = NULL;
	const gchar *uri_str = NULL;
	gchar *url_string = NULL;
	EAccount *account;

	account = get_modified_account (target);
	url = camel_url_new (e_account_get_string(account, E_ACCOUNT_SOURCE_URL), NULL);
	uri_str = gtk_entry_get_text (GTK_ENTRY(entry));

	camel_url_set_param (url, param, uri_str);
	url_string = camel_url_to_string (url, 0);
	e_account_set_string (account, E_ACCOUNT_SOURCE_URL, url_string);
	e_account_set_string (account, E_ACCOUNT_TRANSPORT_URL, url_string);
	g_free (url_string);

	camel_url_free (url);
}

static void
host_url_changed (GtkWidget *entry, EConfig *config)
{
	url_changed (entry, config, "hosturl");
}

static void
oab_url_changed (GtkWidget *entry, EConfig *config)
{
	url_changed (entry, config, "oaburl");
}

GtkWidget *
org_gnome_exchange_ews_account_setup (EPlugin *epl, EConfigHookItemFactoryData *data)
{
	EMConfigTargetAccount *target_account;
	CamelURL *url;
	GtkWidget *hbox = NULL;
	gint row;

	target_account = (EMConfigTargetAccount *)data->config->target;
	url = camel_url_new(e_account_get_string(get_modified_account (target_account), E_ACCOUNT_SOURCE_URL), NULL);

	/* is NULL on new account creation */
	if (url == NULL)
		return NULL;

	if (!g_ascii_strcasecmp (url->protocol, "ews")) {
		GtkWidget *label, *oab_label;
		GtkWidget *host_url, *oab_url;
		GtkWidget *auto_discover;
		const gchar *host_url_val = camel_url_get_param (url, "hosturl");
		const gchar *oab_url_val = camel_url_get_param (url, "oaburl");
		const gchar *temp, *email_id;
		gchar *temp_host_url_val = NULL;
		gchar *url_string;
		struct _AutoDiscCallBackData *cbdata = g_new0 (struct _AutoDiscCallBackData, 1);
		EAccount *account;
		/* FIXME free cbdata */

		g_object_get (data->parent, "n-rows", &row, NULL);
		account = get_modified_account (target_account);

		/* Set email_id */
		email_id = account->id->address;
		camel_url_set_param (url, "email", email_id);
		temp = g_strstr_len (email_id, -1, "@");
		/* Don't overwrite the URL if it's already been set */
		if (temp && (!url->host || !url->host[0])) {
			camel_url_set_host (url, temp + 1);
		}

		if (temp && (!host_url_val || !*host_url_val)) {
			temp_host_url_val = g_strdup_printf ("https://exchange.%s/EWS/Exchange.asmx", temp + 1);
			host_url_val = temp_host_url_val;
			camel_url_set_param (url, "hosturl", host_url_val);
		}

		url_string = camel_url_to_string (url, 0);
		e_account_set_string (account, E_ACCOUNT_SOURCE_URL, url_string);
		e_account_set_string (account, E_ACCOUNT_TRANSPORT_URL, url_string);
		g_free (url_string);
		
		/* OAB url entry */
		oab_label = gtk_label_new_with_mnemonic (_("OAB U_RL:"));
		gtk_widget_show (oab_label);

		oab_url = gtk_entry_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL (oab_label), oab_url);
		if (oab_url_val && *oab_url_val)
			gtk_entry_set_text (GTK_ENTRY (oab_url), oab_url_val);
		g_signal_connect (oab_url, "changed", G_CALLBACK (oab_url_changed), data->config);
		gtk_widget_show (oab_url);

		/* Host url and Autodiscover button */
		hbox = gtk_hbox_new (FALSE, 6);
		label = gtk_label_new_with_mnemonic (_("_Host URL:"));
		gtk_widget_show (label);

		host_url = gtk_entry_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), host_url);
		if (host_url_val && *host_url_val)
			gtk_entry_set_text (GTK_ENTRY (host_url), host_url_val);
		else
			gtk_entry_set_text (GTK_ENTRY (host_url), "https://exchange.server.com/EWS/Exchange.asmx");
		gtk_box_pack_start (GTK_BOX (hbox), host_url, TRUE, TRUE, 0);
		g_signal_connect (host_url, "changed", G_CALLBACK(host_url_changed), data->config);

		cbdata->config = data->config;
		cbdata->host_entry = host_url;
		cbdata->oab_entry = oab_url;
		auto_discover = gtk_button_new_with_mnemonic (_("Fetch _URL"));
		gtk_box_pack_start (GTK_BOX (hbox), auto_discover, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT(auto_discover), "clicked",  G_CALLBACK(validate_credentials), cbdata);

		/* Add Host entry */
		gtk_table_attach (GTK_TABLE (data->parent), label, 0, 1, row, row+1, 0, 0, 0, 0);
		gtk_widget_show_all (GTK_WIDGET (hbox));
		gtk_table_attach (GTK_TABLE (data->parent), GTK_WIDGET (hbox), 1, 2, row, row+1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
		row++;

		/* Add OAB entry */
		gtk_table_attach (GTK_TABLE (data->parent), oab_label, 0, 1, row, row+1, 0, 0, 0, 0);
		gtk_table_attach (GTK_TABLE (data->parent), oab_url, 1, 2, row, row+1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
		row++;

		g_free (temp_host_url_val);
	}

	camel_url_free (url);
	return hbox;
}

gboolean
org_gnome_exchange_ews_check_options(EPlugin *epl, EConfigHookPageCheckData *data)
{
	EMConfigTargetAccount *target = (EMConfigTargetAccount *)(data->config->target);
	gboolean status = TRUE;
	CamelURL *url;

	url = camel_url_new (e_account_get_string(get_modified_account (target), E_ACCOUNT_SOURCE_URL), NULL);

	if (url && url->protocol && g_ascii_strcasecmp (url->protocol, "ews") != 0)
		goto exit;

	/* FIXME pageid is not set while editing an account */
	if (!data->pageid || !*data->pageid)
		goto exit;

	if (!g_ascii_strcasecmp (data->pageid, "10.receive")) {
		const gchar *url_str = NULL;
		CamelURL *hurl;

		url_str = camel_url_get_param (url, "hosturl");
		hurl = camel_url_new (url_str, NULL);

		/*Host url not set. Do not proceed with account creation.*/
		if (!hurl)
			status = FALSE;
		else
			camel_url_free (hurl);

	} else if (!g_ascii_strcasecmp (data->pageid, "20.receive_options")) {
		const gchar *marked_for_offline, *oal_selected;	

		/* If GAL is marked for caching, an OAL (offline address list) should be selected */
		marked_for_offline = camel_url_get_param (url, "oab_offline");
		if (marked_for_offline && !strcmp (marked_for_offline, "1")) {
			oal_selected = camel_url_get_param (url, "oal_selected");
			if (!oal_selected || !*oal_selected)
				status = FALSE;
		}
	}

exit:	
	if (url)
		camel_url_free(url);

	return status;
}

struct _oab_setting_data {
	EConfig *config;
	GtkWidget *combo_text;
	GtkWidget *hbox;
	GtkWidget *check;
	GtkWidget *fetch_button;
	GCancellable *cancellable;
	GSList *oals;
};

static void
clear_combo (GtkComboBoxText *combo_box)
{
	GtkListStore *store;

	g_return_if_fail (GTK_IS_COMBO_BOX_TEXT (combo_box));

	store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box)));
	gtk_list_store_clear (store);
}

static void
update_camel_url (struct _oab_setting_data *cbdata)
{
	EMConfigTargetAccount *target = (EMConfigTargetAccount *) cbdata->config->target;
	CamelURL *url;
	gchar *url_string;
	
	url = camel_url_new (e_account_get_string(get_modified_account (target), E_ACCOUNT_SOURCE_URL), NULL);
	
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cbdata->check))) {
		gint num;
		
		gtk_widget_set_sensitive (cbdata->hbox, TRUE);
		camel_url_set_param (url, "oab_offline", "1");
		num = gtk_combo_box_get_active (GTK_COMBO_BOX (cbdata->combo_text));
		
		/* Set the active oal */
		if (cbdata->oals && num != -1) {
			gchar *mangled_oal;
			EwsOAL *oal = g_slist_nth_data (cbdata->oals, num);

			mangled_oal = g_strconcat (oal->id, ":", oal->name, NULL);
			camel_url_set_param (url, "oal_selected", mangled_oal);
			g_free (mangled_oal);
		}
	} else {
		gtk_widget_set_sensitive (cbdata->hbox, FALSE);
		camel_url_set_param (url, "oab_offline", NULL);
		camel_url_set_param (url, "oal_selected", NULL);

		if (cbdata->oals == NULL)
			clear_combo (GTK_COMBO_BOX_TEXT (cbdata->combo_text));
	}

	url_string = camel_url_to_string (url, 0);
	e_account_set_string (get_modified_account (target), E_ACCOUNT_SOURCE_URL, url_string);
	g_free (url_string);
	camel_url_free (url);
}

static void
cache_setting_toggled (GtkToggleButton *check, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;

	update_camel_url (cbdata);
}

static void
combo_selection_changed (GtkComboBox *combo, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;

	update_camel_url (cbdata);
}

static void
ews_oal_list_ready (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;
	EEwsConnection *cnc = E_EWS_CONNECTION (obj);
	GError *error = NULL;
	GSList *oals = NULL, *l;
	gboolean cancelled;

	cancelled = g_cancellable_is_cancelled (cbdata->cancellable);
	g_object_unref (cbdata->cancellable);
	cbdata->cancellable = NULL;

	if (!e_ews_connection_get_oal_list_finish (E_EWS_CONNECTION (cnc), res, &oals, &error)) {
		g_object_unref (cnc);

		if (cancelled) {
			g_clear_error (&error);
			return;	
		}

		e_notice (NULL, GTK_MESSAGE_ERROR, "%s%s", _("Could not fetch oal list: "), error->message);
		g_clear_error (&error);
		/* Re-activate fetch button since we were not able to fetch the list */
		gtk_widget_set_sensitive (GTK_WIDGET (cbdata->fetch_button), TRUE);
		return;
	}
	cbdata->oals = oals;

	g_signal_handlers_block_by_func (cbdata->combo_text, combo_selection_changed, cbdata);
	clear_combo (GTK_COMBO_BOX_TEXT (cbdata->combo_text));
	g_signal_handlers_unblock_by_func (cbdata->combo_text, combo_selection_changed, cbdata);

	for (l = oals; l != NULL; l = g_slist_next (l)) {
		EwsOAL *oal = l->data;
		
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cbdata->combo_text), oal->name);
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (cbdata->combo_text), 0);

	g_object_unref (cnc);
}

static void
fetch_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;
	EMConfigTargetAccount *target = (EMConfigTargetAccount *) cbdata->config->target;
	GCancellable *cancellable;
	EEwsConnection *cnc;
	CamelURL *url;
	const gchar *oab_url;
	gchar *password;
	
	url = camel_url_new (e_account_get_string(get_modified_account (target), E_ACCOUNT_SOURCE_URL), NULL);

	cancellable = g_cancellable_new ();

	/* De-sensitize fetch_button and get the list from the server */
	g_signal_handlers_block_by_func (cbdata->combo_text, combo_selection_changed, cbdata);
	
	clear_combo (GTK_COMBO_BOX_TEXT (cbdata->combo_text));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cbdata->combo_text), _("Fetching..."));
	gtk_combo_box_set_active (GTK_COMBO_BOX (cbdata->combo_text), 0);
	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

	g_signal_handlers_unblock_by_func (cbdata->combo_text, combo_selection_changed, cbdata);
	

	/* Fetch the oab lists from server */
	oab_url = camel_url_get_param (url, "oaburl");
	password = get_password (target);

	/* pass user name while creating connection  to fetch oals */
	cnc = e_ews_connection_new (oab_url, url->user, password, NULL, NULL, NULL);
	cbdata->cancellable = cancellable;
	e_ews_connection_get_oal_list_start (cnc, ews_oal_list_ready, cancellable, cbdata);

	camel_url_free (url);
	g_free (password);
}

static void
ews_oal_free (gpointer data, gpointer user_data)
{
	EwsOAL *oal = (EwsOAL *) data;

	g_free (oal->id);
	g_free (oal->dn);
	g_free (oal->name);
	g_free (oal);
}

static gboolean
table_deleted_cb (GtkWidget *widget, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;
	
	if (cbdata->cancellable)
		g_cancellable_cancel (cbdata->cancellable);
	
	if (cbdata->oals) {
		g_slist_foreach (cbdata->oals, (GFunc) ews_oal_free, NULL);
		g_slist_free (cbdata->oals);
	}
	
	g_free (cbdata);
	return FALSE;
}

static void
init_widgets (struct _oab_setting_data *cbdata)
{
	const gchar *oab_url, *marked_for_offline;
	EMConfigTargetAccount *target_account;
	CamelURL *url;
	
	target_account = (EMConfigTargetAccount *) cbdata->config->target;
	url = camel_url_new(e_account_get_string(get_modified_account (target_account), E_ACCOUNT_SOURCE_URL), NULL);

	marked_for_offline = camel_url_get_param (url, "oab_offline");
	if (marked_for_offline && !strcmp (marked_for_offline, "1")) {
		const gchar *selected_list;

		g_signal_handlers_block_by_func (cbdata->check, cache_setting_toggled, cbdata);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cbdata->check), TRUE);
		g_signal_handlers_unblock_by_func (cbdata->check, cache_setting_toggled, cbdata);

		/* selected list will be of form "id:name" */
		selected_list = camel_url_get_param (url, "oal_selected");
		if (selected_list && gtk_combo_box_get_active (GTK_COMBO_BOX (cbdata->combo_text)) == -1) {
			const gchar *tmp;
			
			tmp = strrchr (selected_list, ':');
			gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cbdata->combo_text), tmp+1);
			
			g_signal_handlers_block_by_func (cbdata->combo_text, combo_selection_changed, cbdata);
			gtk_combo_box_set_active (GTK_COMBO_BOX (cbdata->combo_text), 0);
			g_signal_handlers_unblock_by_func (cbdata->combo_text, combo_selection_changed, cbdata);
		}
	} else
		gtk_widget_set_sensitive (cbdata->hbox, FALSE);

	/* If oab url is not set, dsensitize*/
	oab_url = camel_url_get_param (url, "oaburl");
	if (!oab_url) {
		gtk_widget_set_sensitive (cbdata->check, FALSE);
		gtk_widget_set_sensitive (cbdata->hbox, FALSE);
	} else
		gtk_widget_set_sensitive (cbdata->check, TRUE);

	camel_url_free (url);
}

static void
ews_prepare_receive_options_page (GtkWidget *page, gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;
	GtkWidget *receive_options;

	receive_options = e_config_page_get (cbdata->config, "20.receive_options");
	if (receive_options == page)
		init_widgets (cbdata);
}

static void
ews_assistant_page_changed_cb (GtkAssistant *assistant, GtkWidget *page, gpointer user_data)
{
	ews_prepare_receive_options_page (page, user_data);
}

static void
ews_page_switched_cb (GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data)
{
	ews_prepare_receive_options_page (page, user_data);
}

GtkWidget *
org_gnome_ews_oab_settings (EPlugin *epl, EConfigHookItemFactoryData *data)
{	
	EMConfigTargetAccount *target_account;
	CamelURL *url;
	
	target_account = (EMConfigTargetAccount *)data->config->target;
	url = camel_url_new(e_account_get_string(get_modified_account (target_account), E_ACCOUNT_SOURCE_URL), NULL);

	/* is NULL on new account creation */
	if (url == NULL)
		return NULL;

	if (!g_ascii_strcasecmp (url->protocol, "ews")) {
		GtkWidget *check = NULL;
		GtkWidget *label;
		GtkWidget *hbox, *oal_combo, *fetch_button;
		gint row = 0;
		EShell *shell;
		struct _oab_setting_data *cbdata;

		/* Add cache check box */
		check = gtk_check_button_new_with_mnemonic (_("Cache o_ffline address book"));
		gtk_widget_show (check);
		gtk_table_attach (GTK_TABLE (data->parent), check, 0, 1, row, row+1, 0, 0, 0, 0);
		row++;
	
		/* Add label */	
		label = gtk_label_new_with_mnemonic (_("Select Ad_dress list: "));
		gtk_widget_show (label);
		gtk_table_attach (GTK_TABLE (data->parent), label, 0, 1, row, row+1, 0, 0, 0, 0);
		
		/* OAL combo and fetch OAL button */	
		hbox = gtk_hbox_new (FALSE, 6);
		oal_combo = gtk_combo_box_text_new ();
		gtk_label_set_mnemonic_widget (GTK_LABEL(label), oal_combo);
		gtk_box_pack_start (GTK_BOX (hbox), oal_combo, TRUE, TRUE, 0);

		fetch_button = gtk_button_new_with_mnemonic (_("Fetch _list"));
		gtk_box_pack_start (GTK_BOX (hbox), fetch_button, FALSE, FALSE, 0);

		/* Add hbox to table */
		gtk_table_attach (GTK_TABLE (data->parent), hbox, 1, 2, row, row+1, GTK_FILL|GTK_EXPAND, GTK_FILL, 0, 0);
		gtk_widget_show_all (hbox);
		row++;

		/* If evolution is offline, dsensitize and return */
		shell = e_shell_get_default ();
		if (!e_shell_get_online (shell)) {
			gtk_widget_set_sensitive (check, FALSE);
			gtk_widget_set_sensitive (hbox, FALSE);
			camel_url_free (url);
			return check;
		}
		
		cbdata = g_new0 (struct _oab_setting_data, 1);
		cbdata->check = check;
		cbdata->combo_text = oal_combo;
		cbdata->hbox = hbox;
		cbdata->fetch_button = fetch_button;
		cbdata->config = data->config; 

		/* Connect the signals */
		g_signal_connect (check, "toggled", G_CALLBACK (cache_setting_toggled), cbdata);
		g_signal_connect (G_OBJECT (fetch_button), "clicked",  G_CALLBACK (fetch_button_clicked_cb), cbdata);
		g_signal_connect (GTK_COMBO_BOX (oal_combo), "changed", G_CALLBACK (combo_selection_changed), cbdata);

		/* Init widgets when the page is changed to receiving options page */
		if (GTK_IS_ASSISTANT (data->config->widget))
			g_signal_connect (GTK_ASSISTANT (data->config->widget), "prepare", G_CALLBACK (ews_assistant_page_changed_cb), cbdata);
		if (GTK_IS_NOTEBOOK (data->config->widget))
			g_signal_connect (GTK_NOTEBOOK (data->config->widget), "switch-page", G_CALLBACK (ews_page_switched_cb), cbdata);

		/* Free the call back data here */
		g_signal_connect (GTK_WIDGET (data->config->widget), "destroy", G_CALLBACK (table_deleted_cb), cbdata);

		camel_url_free (url);
		return check;
	}

	camel_url_free (url);
	return NULL;
}
