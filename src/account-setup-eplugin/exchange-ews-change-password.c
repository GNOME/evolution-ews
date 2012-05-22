/*
 *
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
 * Authors : Vibha Yadav <yvibha@suse.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

/* exchange-ews-change-password: Change Password code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "server/e-ews-connection.h"
#include "server/ews-kerberos.h"
#include "utils/camel-ews-settings.h"
#include "mail/em-config.h"
#include <libedataserverui/e-passwords.h>
#include <exchange-ews-account-setup.h>
#include <e-util/e-dialog-utils.h>
#include <stdlib.h>

#include "exchange-ews-change-password.h"
#include <gtk/gtk.h>


#ifdef HAVE_KRB5
static void
entry_changed (GtkEntry *entry,
               gpointer user_data)
{
	GtkEntry *new_entry, *confirm_entry;
	GtkDialog *pass_dialog;
	const gchar *text;

	new_entry = GTK_ENTRY (entry);
	confirm_entry = GTK_ENTRY (user_data);
	pass_dialog = GTK_DIALOG (g_object_get_data (G_OBJECT (new_entry), "pass_dialog"));

	text = gtk_entry_get_text (new_entry);
	if (!text || !*text) {
		gtk_dialog_set_response_sensitive (pass_dialog, GTK_RESPONSE_OK, FALSE);
		return;
	}

	text = gtk_entry_get_text (confirm_entry);
	if (!text || !*text) {
		gtk_dialog_set_response_sensitive (pass_dialog, GTK_RESPONSE_OK, FALSE);
		return;
	}

	gtk_dialog_set_response_sensitive (pass_dialog, GTK_RESPONSE_OK, TRUE);
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
		title = g_strdup_printf ("Enter Password for %s", host);
		password = e_passwords_ask_password (title, EXCHANGE_EWS_PASSWORD_COMPONENT, key, title,
						    E_PASSWORDS_REMEMBER_FOREVER | E_PASSWORDS_SECRET,
						     &remember, NULL);
		g_free (title);
	}

	if (!password || !*password) {
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		e_notice (NULL, GTK_MESSAGE_ERROR, "%s","Could not get password.");
	}

	g_free (key);

	return password;
}

static gboolean
e_ews_set_password (EMConfigTargetSettings *account,
		    const gchar *old_pwd,
		    const gchar *new_pwd)
{
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	CamelURL *url;
	EwsKerberosResult result;

	const gchar *user;
	const gchar *domain;
	gchar *key;

	/*add up code to call up kerberos set up*/
	/*Get domain name*/
	/*pass on username domain name, old and new password to kerberos libraries*/
	settings = account->storage_settings;

	if (!CAMEL_IS_EWS_SETTINGS (settings))
		return FALSE;

	/* Verify the storage and transport settings are shared. */
	g_warn_if_fail (
		account->storage_settings ==
		account->transport_settings);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);

	domain = camel_network_settings_get_host (network_settings);

	user = camel_network_settings_get_user (network_settings);

	result = ews_kerberos_change_password (user, domain, old_pwd, new_pwd);

	if(result != EWS_KERBEROS_OK)
		return FALSE;

	/*On Success add new password to the keyring*/

	url = g_malloc0 (sizeof (CamelURL));
	camel_settings_save_to_url (settings, url);
	key = camel_url_to_string (url, CAMEL_URL_HIDE_PARAMS);
	camel_url_free (url);

	e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
	e_passwords_add_password (key, new_pwd);
	return TRUE;
}
#endif

/**
 * exchange_get_new_password:
 * @existing_password: The user's current password
 * @voluntary: %TRUE if the user has chosen "Change Password",
 * %FALSE if their old password has expired.
 *
 * Prompt the user for a new password.
 */
gchar *
e_ews_get_new_password (const gchar *existing_password,
                           gboolean voluntary)
{
	gchar *new_pass = NULL;
#ifdef HAVE_KRB5
	GtkResponseType response;
	GtkWidget *pass_dialog;
	GtkWidget *dialog_vbox1;
	GtkWidget *pass_label;
	GtkWidget *table1;
	GtkWidget *current_pass_label;
	GtkWidget *new_pass_label;
	GtkWidget *confirm_pass_label;
	GtkWidget *current_pass_entry;
	GtkWidget *new_pass_entry;
	GtkWidget *confirm_pass_entry;

	pass_dialog = gtk_dialog_new_with_buttons (
		"Change Password",
		NULL,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	dialog_vbox1 = gtk_dialog_get_content_area (GTK_DIALOG (pass_dialog));
	gtk_widget_show (dialog_vbox1);

	pass_label = gtk_label_new ("Your current password has expired. Please change your password now.");
	gtk_widget_show (pass_label);
	gtk_box_pack_start (GTK_BOX (dialog_vbox1), pass_label, FALSE, FALSE, 0);
	gtk_label_set_justify (GTK_LABEL (pass_label), GTK_JUSTIFY_CENTER);
	gtk_label_set_line_wrap (GTK_LABEL (pass_label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (pass_label), 0.52, 0.5);
	gtk_misc_set_padding (GTK_MISC (pass_label), 0, 6);

	table1 = gtk_table_new (3, 2, FALSE);
	gtk_widget_show (table1);
	gtk_box_pack_start (GTK_BOX (dialog_vbox1), table1, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (table1), 6);
	gtk_table_set_row_spacings (GTK_TABLE (table1), 6);
	gtk_table_set_col_spacings (GTK_TABLE (table1), 6);

	current_pass_label = gtk_label_new_with_mnemonic ("Current _Password:");
	gtk_widget_show (current_pass_label);
	gtk_table_attach (GTK_TABLE (table1), current_pass_label, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (current_pass_label), 0, 0.5);

	new_pass_label = gtk_label_new_with_mnemonic ("_New Password:");
	gtk_widget_show (new_pass_label);
	gtk_table_attach (GTK_TABLE (table1), new_pass_label, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (new_pass_label), 0, 0.5);

	confirm_pass_label = gtk_label_new_with_mnemonic ("_Confirm Password:");
	gtk_widget_show (confirm_pass_label);
	gtk_table_attach (GTK_TABLE (table1), confirm_pass_label, 0, 1, 2, 3,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (confirm_pass_label), 0, 0.5);

	new_pass_entry = gtk_entry_new ();
	gtk_widget_show (new_pass_entry);
	gtk_table_attach (GTK_TABLE (table1), new_pass_entry, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_entry_set_visibility (GTK_ENTRY (new_pass_entry), FALSE);

	confirm_pass_entry = gtk_entry_new ();
	gtk_widget_show (confirm_pass_entry);
	gtk_table_attach (GTK_TABLE (table1), confirm_pass_entry, 1, 2, 2, 3,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_entry_set_visibility (GTK_ENTRY (confirm_pass_entry), FALSE);

	current_pass_entry = gtk_entry_new ();
	gtk_widget_show (current_pass_entry);
	gtk_table_attach (GTK_TABLE (table1), current_pass_entry, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 12);
	gtk_entry_set_visibility (GTK_ENTRY (current_pass_entry), FALSE);

	g_object_set_data (G_OBJECT (new_pass_entry), "pass_dialog", pass_dialog);
	g_object_set_data (G_OBJECT (confirm_pass_entry), "pass_dialog", pass_dialog);
	g_signal_connect (new_pass_entry, "changed", G_CALLBACK (entry_changed), confirm_pass_entry);
	g_signal_connect (confirm_pass_entry, "changed", G_CALLBACK (entry_changed), new_pass_entry);
	entry_changed (GTK_ENTRY (new_pass_entry), confirm_pass_entry);

	if (voluntary)
		gtk_widget_hide (GTK_WIDGET (pass_label));

run_dialog_again:
	response = gtk_dialog_run (GTK_DIALOG (pass_dialog));
	if (response == GTK_RESPONSE_OK) {
		const gchar *cur_pass, *new_pass1, *new_pass2;

		cur_pass = gtk_entry_get_text (GTK_ENTRY (current_pass_entry));
		new_pass1 = gtk_entry_get_text (GTK_ENTRY (new_pass_entry));
		new_pass2 = gtk_entry_get_text (GTK_ENTRY (confirm_pass_entry));

		if (existing_password) {
			if (g_strcmp0 (cur_pass, existing_password) != 0) {
				/* User entered a wrong existing
				 * password. Prompt him again.
				 */
				gtk_label_set_text (GTK_LABEL (pass_label), "The current password should not match the existing password for your account. Please enter the correct password");
				gtk_widget_show (pass_label);
				goto run_dialog_again;
			}
		}

		if (g_strcasecmp (new_pass1, new_pass2) != 0) {
			gtk_label_set_text (GTK_LABEL (pass_label), "The two passwords do not match. Please re-enter the passwords.");
			gtk_widget_show (pass_label);
			goto run_dialog_again;
		}

		if (g_strcasecmp (existing_password, new_pass1) == 0)
		{
			gtk_label_set_text (GTK_LABEL (pass_label), "The new password matches old password. Please re-enter the passwords.");
			gtk_widget_show (pass_label);
			goto run_dialog_again;
		}

		new_pass = g_strdup (new_pass1);
	} else
		new_pass = g_strdup(""); /*Don't use NULL it crashes on kerberos libraries*/

	gtk_widget_destroy (pass_dialog);
#endif

	return new_pass;
}


static void
btn_chpass_clicked (GtkButton *button,
                    gpointer data)
{
#ifdef HAVE_KRB5
	EMConfigTargetSettings *target_account = (EMConfigTargetSettings *) data;
	gchar *password, *new_password;
	password = get_password(target_account);
	new_password = e_ews_get_new_password ( password, TRUE);
	g_print("Password is %s \n New password is %s", password, new_password);

	if(e_ews_set_password(target_account, password, new_password))
		e_notice(button, GTK_MESSAGE_INFO, "Password changed successfully. Please remember the same.");
	else
#endif
		e_notice(button, GTK_MESSAGE_ERROR, "Could not reset password. Please try again.");
}

GtkWidget *
ews_get_change_pwd_widget (EMConfigTargetSettings *target_account)
{
	GtkFrame *frame;
	GtkWidget *label;
	GtkVBox *vbox_auth;
	GtkTable *tbl_auth;
	GtkLabel *lbl_chpass;
	GtkButton *btn_chpass;

	gchar *txt;

	txt = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", "Security Settings");
	label = gtk_label_new (NULL);
	gtk_label_set_markup ((GtkLabel *) label, txt);
	g_free (txt);
	frame = (GtkFrame*) g_object_new (GTK_TYPE_FRAME, "label-widget", label, NULL);
	
	/*Create a VBox*/
	vbox_auth = (GtkVBox*) g_object_new (GTK_TYPE_VBOX, "homogeneous", FALSE, "spacing", 6, NULL);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_auth), 6);
	gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox_auth));

	/*Table for authentication*/
	tbl_auth = (GtkTable*) g_object_new (GTK_TYPE_TABLE, "n-rows", 2, "n-columns", 2, "homogeneous", FALSE, "row-spacing", 6, "column-spacing", 6, NULL);

	/*Define label and button for change password*/
	lbl_chpass = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", "Change the password for Exchange EWS account", NULL);
	gtk_misc_set_alignment (GTK_MISC (lbl_chpass), 0, 0.5);
	btn_chpass = (GtkButton*) g_object_new (GTK_TYPE_BUTTON, "label", "Change Password", NULL);
	g_signal_connect (btn_chpass, "clicked", G_CALLBACK (btn_chpass_clicked), target_account);

	gtk_table_attach_defaults (tbl_auth, GTK_WIDGET (lbl_chpass), 0, 1, 0, 1);
	gtk_table_attach (tbl_auth, GTK_WIDGET (btn_chpass), 1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

	gtk_box_pack_start (GTK_BOX (vbox_auth), GTK_WIDGET (tbl_auth), FALSE, FALSE, 0);

	return (GtkWidget *) frame;
}
