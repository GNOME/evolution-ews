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
#include <libedataserver/eds-version.h>

#include <e-util/e-dialog-utils.h>
#include <mail/em-config.h>
#include <addressbook/gui/widgets/eab-config.h>
#include <calendar/gui/e-cal-config.h>

#include <camel/camel.h>
#include <shell/e-shell.h>

#include "camel/camel-ews-folder.h"
#include "camel/camel-ews-utils.h"

#include "server/e-ews-connection.h"

#include "utils/camel-ews-settings.h"

#include "exchange-ews-account-out-of-office.h"
#include "exchange-ews-account-setup.h"
#include "exchange-ews-change-password.h"

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
gboolean org_gnome_exchange_ews_check_options (EPlugin *epl, EConfigHookPageCheckData *data);

/* OAB receiving options */
GtkWidget * org_gnome_ews_oab_settings (EPlugin *epl, EConfigHookItemFactoryData *data);

/*Ews Settings Page*/
GtkWidget * org_gnome_ews_settings (EPlugin *epl, EConfigHookItemFactoryData *data);
void	org_gnome_exchange_ews_commit ( EPlugin *epl, EMConfigTargetSettings *target_account);

static ExchangeEWSAccountListener *config_listener = NULL;

static void
free_ews_listener (void)
{
	g_object_unref (config_listener);
}

gint
e_plugin_lib_enable (EPlugin *ep,
                     gint enable)
{
	if (!config_listener)
		config_listener = exchange_ews_account_listener_new ();

	if (!enable)
		free_ews_listener ();

	return 0;
}

ExchangeEWSAccountListener *
exchange_ews_accounts_peek_config_listener ()
{
	return config_listener;
}

static gchar *
get_password (EMConfigTargetSettings *target_account)
{
	gchar *key, *password = NULL;
	CamelSettings *settings;
	CamelURL *url;

	settings = target_account->storage_settings;

	url = g_malloc0 (sizeof (CamelURL));
	camel_settings_save_to_url (settings, url);
	key = camel_url_to_string (url, CAMEL_URL_HIDE_PARAMS);
	camel_url_free (url);

	password = e_passwords_get_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
	if (!password || !*password) {
		CamelNetworkSettings *network_settings;
		const gchar *host;
		gboolean remember = TRUE;
		gchar *title;

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		host = camel_network_settings_get_host (network_settings);

		g_free (password);
		title = g_strdup_printf (_("Enter Password for %s"), host);
		password = e_passwords_ask_password (title, EXCHANGE_EWS_PASSWORD_COMPONENT, key, title,
						    E_PASSWORDS_REMEMBER_FOREVER | E_PASSWORDS_SECRET,
						     &remember, NULL);
		g_free (title);
	}

	if (!password || !*password) {
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		e_notice (NULL, GTK_MESSAGE_ERROR, "%s", _("Could not get password."));
	}

	g_free (key);

	return password;
}

gboolean
org_gnome_exchange_ews_check_options (EPlugin *epl,
                                      EConfigHookPageCheckData *data)
{
	EMConfigTargetSettings *target = (EMConfigTargetSettings *)(data->config->target);
	CamelEwsSettings *ews_settings;
	gboolean status = TRUE;

	if (!CAMEL_IS_EWS_SETTINGS (target->storage_settings))
		return TRUE;

	ews_settings = CAMEL_EWS_SETTINGS (target->storage_settings);

	/* FIXME pageid is not set while editing an account */
	if (!data->pageid || !*data->pageid)
		return TRUE;

	if (!g_ascii_strcasecmp (data->pageid, "10.receive")) {
		const gchar *url_str = NULL;
		CamelURL *hurl;

		url_str = camel_ews_settings_get_hosturl (ews_settings);
		hurl = camel_url_new (url_str, NULL);

		/*Host url not set. Do not proceed with account creation.*/
		if (!hurl)
			status = FALSE;
		else
			camel_url_free (hurl);

	} else if (!g_ascii_strcasecmp (data->pageid, "20.receive_options")) {
		const gchar *oal_selected;
		gboolean marked_for_offline;

		/* If GAL is marked for caching, an OAL (offline address list) should be selected */
		marked_for_offline = camel_ews_settings_get_oab_offline (ews_settings);
		if (marked_for_offline) {
			oal_selected = camel_ews_settings_get_oal_selected (ews_settings);
			if (!oal_selected || !*oal_selected)
				status = FALSE;
		}
	}

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
ews_oal_list_ready (GObject *obj,
                    GAsyncResult *res,
                    gpointer user_data)
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

	clear_combo (GTK_COMBO_BOX_TEXT (cbdata->combo_text));

	for (l = oals; l != NULL; l = g_slist_next (l)) {
		EwsOAL *oal = l->data;

		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cbdata->combo_text), oal->name);
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (cbdata->combo_text), 0);

	g_object_unref (cnc);
}

static void
fetch_button_clicked_cb (GtkButton *button,
                         gpointer user_data)
{
	struct _oab_setting_data *cbdata = (struct _oab_setting_data *) user_data;
	EMConfigTargetSettings *target = (EMConfigTargetSettings *) cbdata->config->target;
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	GCancellable *cancellable;
	EEwsConnection *cnc;
	const gchar *oab_url;
	const gchar *user;
	gchar *password;

	cancellable = g_cancellable_new ();

	ews_settings = CAMEL_EWS_SETTINGS (target->storage_settings);
	network_settings = CAMEL_NETWORK_SETTINGS (target->storage_settings);

	/* De-sensitize fetch_button and get the list from the server */

	clear_combo (GTK_COMBO_BOX_TEXT (cbdata->combo_text));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cbdata->combo_text), _("Fetching..."));
	gtk_combo_box_set_active (GTK_COMBO_BOX (cbdata->combo_text), 0);
	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

	/* Fetch the oab lists from server */
	oab_url = camel_ews_settings_get_oaburl (ews_settings);
	user = camel_network_settings_get_user (network_settings);
	password = get_password (target);

	/* pass user name while creating connection  to fetch oals */
	cnc = e_ews_connection_new (oab_url, user, password, NULL, NULL, NULL);
	cbdata->cancellable = cancellable;
	e_ews_connection_get_oal_list (
		cnc, cancellable, ews_oal_list_ready, cbdata);

	g_free (password);
}

static gboolean
table_deleted_cb (GtkWidget *widget,
                  gpointer user_data)
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

GtkWidget *
org_gnome_ews_oab_settings (EPlugin *epl,
                            EConfigHookItemFactoryData *data)
{
	EMConfigTargetSettings *target_account;
	GtkWidget *check = NULL;
	GtkWidget *label;
	GtkWidget *hbox, *oal_combo, *fetch_button;
	gint row = 0;
	EShell *shell;
	struct _oab_setting_data *cbdata;

	target_account = (EMConfigTargetSettings *) data->config->target;

	if (!CAMEL_IS_EWS_SETTINGS (target_account->storage_settings))
		return NULL;

	/* Add cache check box */
	check = gtk_check_button_new_with_mnemonic (_("Cache o_ffline address book"));
	gtk_widget_show (check);
	gtk_table_attach (GTK_TABLE (data->parent), check, 0, 1, row, row + 1, 0, 0, 0, 0);
	row++;

	/* Add label */	
	label = gtk_label_new_with_mnemonic (_("Select Ad_dress list: "));
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (data->parent), label, 0, 1, row, row + 1, 0, 0, 0, 0);

	/* OAL combo and fetch OAL button */
	hbox = gtk_hbox_new (FALSE, 6);
	oal_combo = gtk_combo_box_text_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), oal_combo);
	gtk_box_pack_start (GTK_BOX (hbox), oal_combo, TRUE, TRUE, 0);

	fetch_button = gtk_button_new_with_mnemonic (_("Fetch _list"));
	gtk_box_pack_start (GTK_BOX (hbox), fetch_button, FALSE, FALSE, 0);

	/* Add hbox to table */
	gtk_table_attach (GTK_TABLE (data->parent), hbox, 1, 2, row, row + 1, GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
	gtk_widget_show_all (hbox);
	row++;

	/* If evolution is offline, dsensitize and return */
	shell = e_shell_get_default ();
	if (!e_shell_get_online (shell)) {
		gtk_widget_set_sensitive (check, FALSE);
		gtk_widget_set_sensitive (hbox, FALSE);
		return check;
	}

	cbdata = g_new0 (struct _oab_setting_data, 1);
	cbdata->check = check;
	cbdata->combo_text = oal_combo;
	cbdata->hbox = hbox;
	cbdata->fetch_button = fetch_button;
	cbdata->config = data->config;

	/* Connect the signals */
	g_signal_connect (G_OBJECT (fetch_button), "clicked",  G_CALLBACK (fetch_button_clicked_cb), cbdata);

	/* Init widgets when the page is changed to receiving options page */
	if (GTK_IS_ASSISTANT (data->config->widget))
		g_signal_connect (GTK_ASSISTANT (data->config->widget), "prepare", G_CALLBACK (ews_assistant_page_changed_cb), cbdata);
	if (GTK_IS_NOTEBOOK (data->config->widget))
		g_signal_connect (GTK_NOTEBOOK (data->config->widget), "switch-page", G_CALLBACK (ews_page_switched_cb), cbdata);

	/* Free the call back data here */
	g_signal_connect (GTK_WIDGET (data->config->widget), "destroy", G_CALLBACK (table_deleted_cb), cbdata);

		return check;
}

void
org_gnome_exchange_ews_commit (EPlugin *epl,
                               EMConfigTargetSettings *target_account)
{
	/*return if it is not a ews account*/
	if (!CAMEL_IS_EWS_SETTINGS (target_account->storage_settings))
		return;

	/* Verify the storage and transport settings are shared. */
	g_warn_if_fail (
		target_account->storage_settings ==
		target_account->transport_settings);

	/* Set oof data in exchange account */
	ews_set_oof_settings (target_account);
}

GtkWidget *
org_gnome_ews_settings (EPlugin *epl,
                        EConfigHookItemFactoryData *data)
{
	EMConfigTargetSettings *target_account;
	GtkVBox *vbox_settings;
	GtkWidget *oof, *chgpwd;

	target_account = (EMConfigTargetSettings *) data->config->target;

	/*return if it is not a ews account*/
	if (!CAMEL_IS_EWS_SETTINGS (target_account->storage_settings))
		return NULL;

	if (data->old)
		return data->old;

	/* Verify the storage and transport settings are shared. */
	g_warn_if_fail (
		target_account->storage_settings ==
		target_account->transport_settings);

	vbox_settings = (GtkVBox*) g_object_new (GTK_TYPE_VBOX, "homogeneous", FALSE, "spacing", 6, NULL);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_settings), 12);

	/*Get Out of office widget*/
	oof = ews_get_outo_office_widget (target_account);
	gtk_box_pack_start (GTK_BOX (vbox_settings), oof, FALSE, FALSE, 0);

	/*Get Change Password widget*/
	chgpwd = ews_get_change_pwd_widget (target_account);
	gtk_box_pack_start (GTK_BOX (vbox_settings), chgpwd, FALSE, FALSE, 0);

	gtk_widget_show_all (GTK_WIDGET (vbox_settings));
	gtk_notebook_insert_page (GTK_NOTEBOOK (data->parent), GTK_WIDGET (vbox_settings), gtk_label_new(_("EWS Settings")), 4);
	return GTK_WIDGET (vbox_settings);
}
