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
		GError *error = NULL;
		gchar *auri = NULL;

		auri = e_ews_autodiscover_ws_url (target_account->account->id->address, password, &error);
		if (error) {
			e_notice (NULL, GTK_MESSAGE_ERROR, "%s", error->message);
			g_clear_error (&error);
			goto exit;
		}

		gtk_entry_set_text ((GtkEntry *) cbdata->entry, auri);
		g_free (auri);
	} else {
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		e_notice (NULL, GTK_MESSAGE_ERROR, "%s", _("Authentication failed."));
	}

exit:	
	g_free (password);
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

	if (domain && domain[0])
		camel_url_set_param (url, "hosturl", domain);
	else
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
		camel_url_set_host (url, g_strdup (temp + 1));
		camel_url_set_param (url, "account-name", target_account->account->name);
		
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

	if (data->pageid != NULL && g_ascii_strcasecmp (data->pageid, "10.receive") == 0) {
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

#if 0 /* Folder creation... not yet */

enum {
	NAME_COL,
	FID_COL,
	FOLDER_COL,
	NUM_COLS
};

static gboolean
check_node (GtkTreeStore *ts, CamelEwsFolder *folder, GtkTreeIter iter)
{
	GtkTreeModel *ts_model;
	ews_id_t fid;

	ts_model = GTK_TREE_MODEL (ts);

	gtk_tree_model_get (ts_model, &iter, 1, &fid, -1);
	if (fid && folder->parent_folder_id == fid) {
		/* Do something */
		GtkTreeIter node;
		gtk_tree_store_append (ts, &node, &iter);
		gtk_tree_store_set (ts, &node, NAME_COL, folder->folder_name, FID_COL, folder->folder_id, FOLDER_COL, folder,-1);
		return TRUE;
	}

	if (gtk_tree_model_iter_has_child (ts_model, &iter)) {
		GtkTreeIter child;
		gtk_tree_model_iter_children (ts_model, &child, &iter);
		if (check_node (ts, folder, child))
		    return TRUE;
	}

	if (gtk_tree_model_iter_next (ts_model, &iter)) {
		return check_node (ts, folder, iter);
	}

	return FALSE;
}

static void
add_to_store (GtkTreeStore *ts, CamelEwsFolder *folder)
{
	GtkTreeModel *ts_model;
	GtkTreeIter iter;

	ts_model = GTK_TREE_MODEL (ts);

	gtk_tree_model_get_iter_first (ts_model, &iter);
	if (!check_node (ts, folder, iter)) {
		GtkTreeIter node;
		gtk_tree_store_append (ts, &node, &iter);
		gtk_tree_store_set (ts, &node, NAME_COL, folder->folder_name, FID_COL, folder->folder_id, FOLDER_COL, folder, -1);
	}
}

static void
traverse_tree (GtkTreeModel *model, GtkTreeIter iter, CamelEwsFolderType folder_type, gboolean *pany_sub_used)
{
	gboolean any_sub_used = FALSE;
	gboolean has_next = TRUE;

	do {
		gboolean sub_used = FALSE;
		GtkTreeIter next = iter;
		CamelEwsFolder *folder = NULL;

		has_next = gtk_tree_model_iter_next (model, &next);

		if (gtk_tree_model_iter_has_child (model, &iter)) {
			GtkTreeIter child;

			gtk_tree_model_iter_children (model, &child, &iter);
			traverse_tree (model, child, folder_type, &sub_used);
		}

		gtk_tree_model_get (model, &iter, FOLDER_COL, &folder, -1);
		if (folder && (camel_ews_folder_get_type (folder) == folder_type || (folder_type == CAMEL_FOLDER_TYPE_MEMO && camel_ews_folder_get_type (folder) == CAMEL_FOLDER_TYPE_JOURNAL))) {
			sub_used = TRUE;
		}

		if (sub_used)
			any_sub_used = TRUE;
		else if (pany_sub_used && folder)
			gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

		iter = next;
	} while (has_next);

	if (pany_sub_used && any_sub_used)
		*pany_sub_used = TRUE;
}

static void
add_folders (GSList *folders, GtkTreeStore *ts, CamelEwsFolderType folder_type)
{
	GSList *tmp = folders;
	GtkTreeIter iter;
	gchar *node = _("Personal Folders");

	/* add all... */
	gtk_tree_store_append (ts, &iter, NULL);
	gtk_tree_store_set (ts, &iter, NAME_COL, node, -1);
	while (tmp) {
		CamelEwsFolder *folder = tmp->data;
		add_to_store (ts, folder);
		tmp = tmp->next;
	}

	/* ... then remove those which don't belong to folder_type */
	if (gtk_tree_model_get_iter_first ((GtkTreeModel *)ts, &iter)) {
		traverse_tree ((GtkTreeModel *)ts, iter, folder_type, NULL);
	}
}

static void
select_folder (GtkTreeModel *model, ews_id_t fid, GtkWidget *tree_view)
{
	GtkTreeIter iter, next;
	gboolean found = FALSE, can = TRUE;

	g_return_if_fail (model != NULL);
	g_return_if_fail (tree_view != NULL);

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return;

	while (!found && can) {
		CamelEwsFolder *folder = NULL;

		gtk_tree_model_get (model, &iter, FOLDER_COL, &folder, -1);

		if (folder && camel_ews_folder_get_fid (folder) == fid) {
			gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view)), &iter);
			found = TRUE;
			break;
		}

		can = FALSE;
		if (gtk_tree_model_iter_children (model, &next, &iter)) {
			iter = next;
			can = TRUE;
		}

		next = iter;
		if (!can && gtk_tree_model_iter_next (model, &next)) {
			iter = next;
			can = TRUE;
		}

		if (!can && gtk_tree_model_iter_parent (model, &next, &iter)) {
			while (!can) {
				iter = next;

				if (gtk_tree_model_iter_next (model, &iter)) {
					can = TRUE;
					break;
				}

				iter = next;
				if (!gtk_tree_model_iter_parent (model, &next, &iter))
					break;
			}
		}
	}
}

static void
exchange_ews_cursor_change (GtkTreeView *treeview, ESource *source)
{
	GtkTreeSelection *selection;
	GtkTreeModel     *model;
	GtkTreeIter       iter;
	ews_id_t pfid;
	gchar *sfid=NULL;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	gtk_tree_selection_get_selected(selection, &model, &iter);

	gtk_tree_model_get (model, &iter, FID_COL, &pfid, -1);
	sfid = camel_ews_util_ews_id_to_string (pfid);
	e_source_set_property (source, "parent-fid", sfid);
	g_free (sfid);
}

static GtkWidget *
exchange_ews_create (GtkWidget *parent, ESource *source, CamelEwsFolderType folder_type)
{
	GtkWidget *vbox, *label, *scroll, *tv;
	gchar *uri_text, *profile = NULL;
	ESourceGroup *group;
	gint row;
	GtkCellRenderer *rcell;
	GtkTreeStore *ts;
	GtkTreeViewColumn *tvc;
	const gchar *acc;
	GSList *folders;
	EEwsConnection *conn;
	ews_id_t fid = 0;

	uri_text = e_source_get_uri (source);
	if (uri_text && g_ascii_strncasecmp (uri_text, EWS_URI_PREFIX, EWS_PREFIX_LENGTH)) {
		return NULL;
	}

	folders = NULL;
	group = e_source_peek_group (source);
	profile = g_strdup (e_source_get_property (source, "profile"));
	if (profile == NULL) {
		profile = e_source_group_get_property (group, "profile");
		e_source_set_property (source, "profile", profile);
	}
	conn = e_ews_connection_find (profile);
	g_free (profile);
	if (conn && e_ews_connection_connected (conn))
		folders = e_ews_connection_peek_folders_list (conn);
	acc = e_source_group_peek_name (group);
	ts = gtk_tree_store_new (NUM_COLS, G_TYPE_STRING, G_TYPE_INT64, G_TYPE_POINTER);

	add_folders (folders, ts, folder_type);

	if (conn)
		g_object_unref (conn);

	vbox = gtk_vbox_new (FALSE, 6);

	if (folder_type == CAMEL_FOLDER_TYPE_CONTACT) {
		gtk_container_add (GTK_CONTAINER (parent), vbox);
	} else {
		g_object_get (parent, "n-rows", &row, NULL);
		gtk_table_attach (GTK_TABLE (parent), vbox, 0, 2, row+1, row+2, GTK_FILL|GTK_EXPAND, 0, 0, 0);
	}

	label = gtk_label_new_with_mnemonic (_("_Location:"));
	gtk_widget_show (label);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	rcell = gtk_cell_renderer_text_new ();
	tvc = gtk_tree_view_column_new_with_attributes (acc, rcell, "text", NAME_COL, NULL);
	tv = gtk_tree_view_new_with_model (GTK_TREE_MODEL (ts));
	gtk_tree_view_append_column (GTK_TREE_VIEW (tv), tvc);
	g_object_set (tv,"expander-column", tvc, "headers-visible", TRUE, NULL);
	gtk_tree_view_expand_all (GTK_TREE_VIEW (tv));

	if (e_source_get_property (source, "folder-id")) {
		camel_ews_util_ews_id_from_string (e_source_get_property (source, "folder-id"), &fid);
		select_folder (GTK_TREE_MODEL (ts), fid, tv);
	}

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	g_object_set (scroll, "height-request", 150, NULL);
	gtk_container_add (GTK_CONTAINER (scroll), tv);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), tv);
	g_signal_connect (G_OBJECT (tv), "cursor-changed", G_CALLBACK (exchange_ews_cursor_change), source);
	gtk_widget_show_all (scroll);

	gtk_box_pack_start (GTK_BOX (vbox), scroll, FALSE, FALSE, 0);

	gtk_widget_show_all (vbox);
	return vbox;
}

GtkWidget *
exchange_ews_create_addressbook (EPlugin *epl, EConfigHookItemFactoryData *data)
{
	EABConfigTargetSource *t = (EABConfigTargetSource *) data->target;

	return exchange_ews_create (data->parent, t->source, CAMEL_FOLDER_TYPE_CONTACT);
}

GtkWidget *
exchange_ews_create_calendar (EPlugin *epl, EConfigHookItemFactoryData *data)
{
	ECalConfigTargetSource *t = (ECalConfigTargetSource *) data->target;
	CamelEwsFolderType folder_type;

	switch (t->source_type) {
	case E_CAL_SOURCE_TYPE_EVENT:
		folder_type = CAMEL_FOLDER_TYPE_APPOINTMENT;
		break;
	case E_CAL_SOURCE_TYPE_TODO:
		folder_type = CAMEL_FOLDER_TYPE_TASK;
		break;
	case E_CAL_SOURCE_TYPE_JOURNAL:
		folder_type = CAMEL_FOLDER_TYPE_MEMO;
		break;
	default:
		g_return_val_if_reached (NULL);
	}

	return exchange_ews_create (data->parent, t->source, folder_type);
}

gboolean
exchange_ews_book_check (EPlugin *epl, EConfigHookPageCheckData *data)
{
	EABConfigTargetSource *t = (EABConfigTargetSource *) data->target;
	ESource *source = t->source;
	gchar *uri_text = e_source_get_uri (source);

	if (!uri_text)
		return TRUE;

	/* FIXME: Offline handling */

	/* not a EWS account */
	if (g_ascii_strncasecmp (uri_text, EWS_URI_PREFIX, EWS_PREFIX_LENGTH)) {
		g_free (uri_text);
		return TRUE;
	}

	/* does not have a parent-fid which is needed for folder creation on server */
	if (!e_source_get_property (source, "parent-fid")) {
		g_free (uri_text);
		return FALSE;
	}

	g_free (uri_text);
	return TRUE;
}

void
exchange_ews_book_commit (EPlugin *epl, EConfigTarget *target)
{
	EABConfigTargetSource *t = (EABConfigTargetSource *) target;
	ESource *source = t->source;
	gchar *uri_text, *r_uri, *sfid;
	ESourceGroup *grp;
	EEwsConnection *conn;
	ews_id_t fid, pfid;
	GError *ews_error = NULL;

	uri_text = e_source_get_uri (source);
	if (uri_text && g_ascii_strncasecmp (uri_text, EWS_URI_PREFIX, EWS_PREFIX_LENGTH))
		return;
	
	camel_ews_util_ews_id_from_string (e_source_get_property (source, "parent-fid"), &pfid);

	/* the profile should be already connected */
	conn = e_ews_connection_find (e_source_get_property (source, "profile"));
	g_return_if_fail (conn != NULL);

	fid = e_ews_connection_create_folder (conn, olFolderContacts, pfid, 0, e_source_peek_name (source), &ews_error);
	g_object_unref (conn);

	if (!fid) {
		if (ews_error) {
			e_notice (NULL, GTK_MESSAGE_ERROR, _("Failed to create address book '%s': %s"), e_source_peek_name (source), ews_error->message);
			g_error_free (ews_error);
		} else {
			e_notice (NULL, GTK_MESSAGE_ERROR, _("Failed to create address book '%s'"), e_source_peek_name (source));
		}

		return;
	}

	sfid = camel_ews_util_ews_id_to_string (fid);
	r_uri = g_strconcat (";", sfid, NULL);
	e_source_set_relative_uri (source, r_uri);

	//FIXME: Offline handling
	grp = e_source_peek_group (source);
	e_source_set_property (source, "auth", "plain/password");
	e_source_set_property (source, "auth-domain", EXCHANGE_EWS_PASSWORD_COMPONENT);
	e_source_set_property(source, "user", e_source_group_get_property (grp, "user"));
	e_source_set_property(source, "host", e_source_group_get_property (grp, "host"));
	e_source_set_property(source, "profile", e_source_group_get_property (grp, "profile"));
	e_source_set_property(source, "domain", e_source_group_get_property (grp, "domain"));
	e_source_set_relative_uri (source, g_strconcat (";",e_source_peek_name (source), NULL));

	e_source_set_property (source, "completion", "true");
	e_source_set_property (source, "public", "no");
	// Update the folder list in the plugin and CamelEwsFolder
	g_free (r_uri);
	g_free (sfid);

	return;
}

/* New calendar/task list/memo list */
gboolean
exchange_ews_cal_check (EPlugin *epl, EConfigHookPageCheckData *data)
{
	ECalConfigTargetSource *t = (ECalConfigTargetSource *)(data->target);
	ESource *source = t->source;
	gchar *uri_text = e_source_get_uri (source);

	if (!uri_text)
		return TRUE;

	/* FIXME: Offline handling */

	/* not a EWS account */
	if (g_ascii_strncasecmp (uri_text, EWS_URI_PREFIX, EWS_PREFIX_LENGTH)) {
		g_free (uri_text);
		return TRUE;
	}

	g_free (uri_text);

	/* FIXME: Offline handling */

	/* does not have a parent-fid which is needed for folder creation on server */
	if (!e_source_get_property (source, "parent-fid"))
		return FALSE;

	return TRUE;
}

void
exchange_ews_cal_commit (EPlugin *epl, EConfigTarget *target)
{
	EEwsConnection *conn;
	ECalConfigTargetSource *t = (ECalConfigTargetSource *) target;
	ESourceGroup *group;
	ESource *source = t->source;
	gchar *tmp, *sfid;
	ews_id_t fid, pfid;
	unsigned int type;
	gchar *uri_text = e_source_get_uri (source);
	GError *ews_error = NULL;

	if (!uri_text || g_ascii_strncasecmp (uri_text, EWS_URI_PREFIX, EWS_PREFIX_LENGTH))
		return;
	g_free (uri_text);

	switch (t->source_type) {
		case E_CAL_SOURCE_TYPE_EVENT:
			type = olFolderCalendar;
			break;
		case E_CAL_SOURCE_TYPE_TODO:
			type = olFolderTasks;
			break;
		case E_CAL_SOURCE_TYPE_JOURNAL:
			type = olFolderNotes;
			break;
		default:
			g_warning ("%s: %s: Unknown CamelEwsFolderType\n", G_STRLOC, G_STRFUNC);
			return;
	}

	/* FIXME: Offline handling */

	camel_ews_util_ews_id_from_string (e_source_get_property (source, "parent-fid"), &pfid);

	/* the profile should be already connected */
	conn = e_ews_connection_find (e_source_get_property (source, "profile"));
	g_return_if_fail (conn != NULL);

	fid = e_ews_connection_create_folder (conn, type, pfid, 0, e_source_peek_name (source), &ews_error);
	g_object_unref (conn);

	if (!fid) {
		if (ews_error) {
			e_notice (NULL, GTK_MESSAGE_ERROR, _("Failed to create calendar '%s': %s"), e_source_peek_name (source), ews_error->message);
			g_error_free (ews_error);
		} else {
			e_notice (NULL, GTK_MESSAGE_ERROR, _("Failed to create calendar '%s'"), e_source_peek_name (source));
		}

		return;
	}

	sfid = camel_ews_util_ews_id_to_string (fid);
	tmp = g_strconcat (";", sfid, NULL);
	e_source_set_relative_uri (source, tmp);
	g_free (tmp);
	g_free (sfid);

	e_source_set_property (source, "auth", "1");
	e_source_set_property (source, "auth-domain", EXCHANGE_EWS_PASSWORD_COMPONENT);
	e_source_set_property (source, "auth-type", "plain/password");
	e_source_set_property (source, "public", "no");

	group = e_source_peek_group (source);

	tmp = e_source_group_get_property (group, "username");
	e_source_set_property (source, "username", tmp);
	g_free (tmp);

	tmp = e_source_group_get_property (group, "host");
	e_source_set_property (source, "host", tmp);
	g_free (tmp);

	tmp = e_source_group_get_property (group, "profile");
	e_source_set_property (source, "profile", tmp);
	g_free (tmp);

	tmp = e_source_group_get_property (group, "domain");
	e_source_set_property (source, "domain", tmp);
	g_free (tmp);

	tmp = camel_ews_util_ews_id_to_string (fid);
	e_source_set_property (source, "folder-id", tmp);
	g_free (tmp);

	e_source_set_property (source, "offline_sync", "0");

	/* Delegatees can never create folders for delegators. So we can copy safely. */
	tmp = e_source_group_get_property (group, "acl-user-name");
	e_source_set_property (source, "acl-user-name", tmp);
	g_free (tmp);
	tmp = e_source_group_get_property (group, "acl-user-email");
	e_source_set_property (source, "acl-user-email", tmp);
	g_free (tmp);
	tmp = e_source_group_get_property (group, "acl-owner-name");
	e_source_set_property (source, "acl-owner-name", tmp);
	g_free (tmp);
	tmp = e_source_group_get_property (group, "acl-owner-email");
	e_source_set_property (source, "acl-owner-email", tmp);
	g_free (tmp);

	// Update the folder list in the plugin and CamelEwsFolder
	return;
}
#endif
