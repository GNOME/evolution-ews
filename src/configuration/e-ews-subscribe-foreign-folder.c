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
 * Authors:
 *    Milan Crha <mcrha@redhat.com>
 *
 * Copyright (C) 2012 Red Hat, Inc. (www.redhat.com)
 *
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libemail-engine/libemail-engine.h>

#include "camel/camel-ews-store.h"
#include "camel/camel-ews-store-summary.h"
#include "camel/camel-ews-utils.h"

#include "server/e-ews-calendar-utils.h"

#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"
#include "e-ews-subscribe-foreign-folder.h"

#define STR_USER_NAME_SELECTOR_ENTRY	"e-ews-name-selector-entry"
#define STR_FOLDER_NAME_COMBO		"e-ews-folder-name-combo"
#define STR_SUBFOLDERS_CHECK		"e-ews-subfolders-check"
#define STR_EWS_CAMEL_SESSION		"e-ews-camel-session"
#define STR_EWS_CAMEL_STORE		"e-ews-camel-store"
#define STR_EWS_DIRECT_EMAIL		"e-ews-direct-email"

static void
announce_new_folder (CamelEwsStore *ews_store,
                     const gchar *fid)
{
	CamelFolderInfo *fi;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));
	g_return_if_fail (fid != NULL);
	g_return_if_fail (camel_ews_store_summary_has_folder (ews_store->summary, fid));

	fi = camel_ews_utils_build_folder_info (ews_store, fid);
	camel_store_folder_created (CAMEL_STORE (ews_store), fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
	camel_folder_info_free (fi);
}

static gboolean
add_foreign_folder_to_camel (CamelEwsStore *ews_store,
                             const gchar *foreign_email,
                             EEwsFolder *folder,
			     gboolean include_subfolders,
                             const gchar *display_username,
                             const gchar *display_foldername,
                             GError **perror)
{
	gchar *foreign_mailbox_id;
	gchar *mailbox, *fullname;
	const EwsFolderId *fid, *parent_fid;

	g_return_val_if_fail (ews_store != NULL, FALSE);
	g_return_val_if_fail (ews_store->summary != NULL, FALSE);
	g_return_val_if_fail (foreign_email != NULL, FALSE);
	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (display_username != NULL, FALSE);
	g_return_val_if_fail (display_foldername != NULL, FALSE);

	fid = e_ews_folder_get_id (folder);
	parent_fid = e_ews_folder_get_parent_id (folder);

	g_return_val_if_fail (fid != NULL, FALSE);
	g_return_val_if_fail (parent_fid != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (fid->id, parent_fid->id) != 0, FALSE);

	if (camel_ews_store_summary_has_folder (ews_store->summary, fid->id)) {
		gchar *full_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, fid->id, NULL);

		g_propagate_error (
			perror,
			g_error_new (EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDEREXISTS,
			_("Cannot add folder, folder already exists as “%s”"), full_name));

		g_free (full_name);

		return FALSE;
	}

	/* Translators: The '%s' is replaced with user name, to whom the foreign mailbox belongs.
	 * Example result: "Mailbox — John Smith"
	*/
	mailbox = g_strdup_printf (C_("ForeignFolder", "Mailbox — %s"), display_username);

	foreign_mailbox_id = g_strdup_printf ("ForeignMailbox::%s", foreign_email);
	if (!camel_ews_store_summary_has_folder (ews_store->summary, foreign_mailbox_id)) {
		camel_ews_store_summary_new_folder (
			ews_store->summary,
			foreign_mailbox_id, EWS_FOREIGN_FOLDER_ROOT_ID, NULL,
			mailbox, E_EWS_FOLDER_TYPE_MAILBOX,
			CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOSELECT,
			0, FALSE, FALSE);
	}

	if (camel_ews_store_summary_has_folder (ews_store->summary, parent_fid->id)) {
		camel_ews_store_summary_new_folder (
			ews_store->summary,
			fid->id, parent_fid->id, fid->change_key,
			display_foldername, E_EWS_FOLDER_TYPE_MAILBOX,
			CAMEL_FOLDER_SUBSCRIBED, e_ews_folder_get_total_count (folder), TRUE, FALSE);
	} else {
		const gchar *displayname;
		gchar *escaped_name;

		escaped_name = e_ews_folder_utils_escape_name (display_foldername);
		fullname = g_strdup_printf ("%s/%s/%s", EWS_FOREIGN_FOLDER_ROOT_DISPLAY_NAME, mailbox, escaped_name);
		g_free (escaped_name);

		/* make sure the path is unique */
		camel_ews_store_ensure_unique_path (ews_store, &fullname);

		displayname = strrchr (fullname, '/');
		displayname++;

		camel_ews_store_summary_new_folder (
			ews_store->summary,
			fid->id, foreign_mailbox_id, fid->change_key,
			displayname, E_EWS_FOLDER_TYPE_MAILBOX,
			CAMEL_FOLDER_SUBSCRIBED, e_ews_folder_get_total_count (folder), TRUE, FALSE);

		g_free (fullname);
	}

	camel_ews_store_ensure_virtual_folders (ews_store);
	camel_ews_store_summary_set_foreign_subfolders (ews_store->summary, fid->id, include_subfolders);
	camel_ews_store_summary_save (ews_store->summary, perror);

	announce_new_folder (ews_store, EWS_FOREIGN_FOLDER_ROOT_ID);
	announce_new_folder (ews_store, foreign_mailbox_id);
	announce_new_folder (ews_store, fid->id);

	g_free (foreign_mailbox_id);
	g_free (mailbox);

	if (include_subfolders)
		camel_ews_store_update_foreign_subfolders (ews_store, fid->id);

	return TRUE;
}

static void
enable_ok_button_by_data (GObject *dialog)
{
	GtkEntry *entry;
	GtkComboBoxText *combo;
	const gchar *entry_text;
	gchar *combo_text;

	g_return_if_fail (dialog != NULL);

	entry = g_object_get_data (dialog, STR_USER_NAME_SELECTOR_ENTRY);
	g_return_if_fail (entry != NULL);

	combo = g_object_get_data (dialog, STR_FOLDER_NAME_COMBO);
	g_return_if_fail (combo != NULL);

	entry_text = gtk_entry_get_text (entry);
	combo_text = gtk_combo_box_text_get_active_text (combo);

	gtk_dialog_set_response_sensitive (
		GTK_DIALOG (dialog), GTK_RESPONSE_OK,
		entry_text && *entry_text && *entry_text != ' ' && *entry_text != ',' &&
		combo_text && *combo_text);

	g_free (combo_text);
}

static void
name_entry_changed_cb (GObject *dialog)
{
	GtkEntry *entry;

	g_return_if_fail (dialog != NULL);

	entry = g_object_get_data (dialog, STR_USER_NAME_SELECTOR_ENTRY);
	g_return_if_fail (entry != NULL);

	g_object_set_data (G_OBJECT (entry), STR_EWS_DIRECT_EMAIL, NULL);

	enable_ok_button_by_data (dialog);
}

static void
folder_name_combo_changed_cb (GObject *dialog,
			      GtkComboBox *combo)
{
	enable_ok_button_by_data (dialog);
}

struct EEwsCheckForeignFolderData
{
	GtkWidget *dialog;
	gboolean include_subfolders;
	gchar *email;
	gchar *direct_email;
	gchar *user_displayname;
	gchar *orig_foldername;
	gchar *use_foldername;
	EEwsFolder *folder;
};

static void
e_ews_check_foreign_folder_data_free (gpointer ptr)
{
	struct EEwsCheckForeignFolderData *cffd = ptr;

	if (!cffd)
		return;

	g_free (cffd->email);
	g_free (cffd->direct_email);
	g_free (cffd->user_displayname);
	g_free (cffd->orig_foldername);
	g_free (cffd->use_foldername);

	/* folder tells whether successfully finished,
	 * then the dialog can be destroyed */
	if (cffd->folder && cffd->dialog)
		gtk_widget_destroy (cffd->dialog);

	if (cffd->folder)
		g_object_unref (cffd->folder);

	g_free (cffd);
}

static void
check_foreign_folder_thread (GObject *with_object,
                             gpointer user_data,
                             GCancellable *cancellable,
                             GError **perror)
{
	struct EEwsCheckForeignFolderData *cffd = user_data;
	GError *local_error = NULL;
	EEwsConnection *conn;
	EwsFolderId fid;
	EEwsFolder *folder = NULL;

	g_return_if_fail (with_object != NULL);
	g_return_if_fail (CAMEL_IS_EWS_STORE (with_object));
	g_return_if_fail (user_data != NULL);
	g_return_if_fail (cffd->email != NULL);

	if (g_cancellable_set_error_if_cancelled (cancellable, perror))
		return;

	conn = camel_ews_store_ref_connection (CAMEL_EWS_STORE (with_object));
	if (!conn) {
		g_set_error_literal (
			perror, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NORESPONSE,
			_("Cannot test foreign folder availability while in offline mode"));
		return;
	}

	if (cffd->direct_email && *cffd->direct_email) {
		g_return_if_fail (cffd->user_displayname == NULL);

		cffd->user_displayname = cffd->email;
		cffd->email = g_strdup (cffd->direct_email);
	} else {
		GSList *mailboxes = NULL;
		EwsMailbox *mailbox = NULL;
		gboolean includes_last_item = FALSE;

		if (!e_ews_connection_resolve_names_sync (conn, G_PRIORITY_DEFAULT,
			cffd->email, EWS_SEARCH_AD, NULL, FALSE,
			&mailboxes, NULL, &includes_last_item,
			cancellable, perror)) {
			g_object_unref (conn);
			return;
		}

		if (!mailboxes) {
			g_set_error (
				perror, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_MAILRECIPIENTNOTFOUND,
				_("User “%s” was not found on the server"), cffd->email);
			g_object_unref (conn);
			return;
		}

		/* is there only one result? */
		if (mailboxes->next == NULL) {
			mailbox = mailboxes->data;
		} else {
			GSList *iter;

			for (iter = mailboxes; iter; iter = iter->next) {
				EwsMailbox *mb = iter->data;

				if (!mb)
					continue;

				if (mb->name && g_utf8_collate (mb->name, cffd->email) == 0) {
					mailbox = mb;
					break;
				}
			}
		}

		if (mailbox) {
			g_free (cffd->user_displayname);
			cffd->user_displayname = g_strdup (mailbox->name);
			g_free (cffd->email);
			cffd->email = g_strdup (mailbox->email);
		}

		g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);

		if (!mailbox) {
			g_set_error (
				perror, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND,
				_("User name “%s” is ambiguous, specify it more precisely, please"), cffd->email);
			g_object_unref (conn);
			return;
		}
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, perror)) {
		g_object_unref (conn);
		return;
	}

	if (g_strcmp0 (cffd->use_foldername, "freebusy-calendar") == 0) {
		EEWSFreeBusyData fbdata;
		GSList *free_busy = NULL;
		gchar *tmp;
		gboolean success;

		fbdata.period_start = time (NULL);
		fbdata.period_end = fbdata.period_start + (60 * 60);
		fbdata.user_mails = g_slist_prepend (NULL, cffd->email);

		success = e_ews_connection_get_free_busy_sync (conn, G_PRIORITY_DEFAULT,
			e_ews_cal_utils_prepare_free_busy_request, &fbdata,
			&free_busy, cancellable, perror);

		g_slist_free_full (free_busy, (GDestroyNotify) icalcomponent_free);
		g_slist_free (fbdata.user_mails);

		if (!success) {
			g_object_unref (conn);
			return;
		}

		tmp = g_strconcat (cffd->use_foldername, "::", cffd->email, NULL);

		folder = g_object_new (E_TYPE_EWS_FOLDER, NULL);
		e_ews_folder_set_id (folder, e_ews_folder_id_new (tmp, NULL, FALSE));
		/* Translators: This is used as a calendar name; it constructs "User Name - Availability" string shown in UI */
		e_ews_folder_set_name (folder, _("Availability"));
		e_ews_folder_set_folder_type (folder, E_EWS_FOLDER_TYPE_CALENDAR);
		e_ews_folder_set_foreign_mail (folder, cffd->email);

		g_free (tmp);
	} else {
		fid.id = (gchar *) (cffd->use_foldername ? cffd->use_foldername : cffd->orig_foldername);
		fid.change_key = NULL;
		fid.is_distinguished_id = cffd->use_foldername != NULL;

		if (!e_ews_connection_get_folder_info_sync (conn, G_PRIORITY_DEFAULT,
			cffd->email, &fid, &folder, cancellable, &local_error)) {
			if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND) ||
			    g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND)) {
				g_clear_error (&local_error);
				local_error = g_error_new (
					EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND,
					_("Folder “%s” not found. Either it does not exist or you do not have permission to access it."),
					cffd->orig_foldername);
			}

			g_propagate_error (perror, local_error);
			g_object_unref (conn);
			return;
		}
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, perror)) {
		g_object_unref (folder);
		g_object_unref (conn);
		return;
	}

	if (e_ews_folder_get_folder_type (folder) == E_EWS_FOLDER_TYPE_UNKNOWN) {
		g_propagate_error (
			perror, g_error_new_literal (EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_FOLDERNOTFOUND, _("Cannot add folder, cannot determine folder’s type")));
		g_object_unref (folder);
		g_object_unref (conn);
		return;
	}

	e_ews_folder_set_foreign (folder, TRUE);

	cffd->folder = folder;
	g_object_unref (conn);
}

static void
check_foreign_folder_idle (GObject *with_object,
                           gpointer user_data,
                           GCancellable *cancellable,
                           GError **perror)
{
	struct EEwsCheckForeignFolderData *cffd = user_data;
	gchar *folder_name;
	const gchar *base_username, *base_foldername;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	CamelEwsStore *ews_store;
	ESourceRegistry *registry = NULL;
	CamelSession *session;
	EEwsFolderType folder_type;

	g_return_if_fail (with_object != NULL);
	g_return_if_fail (CAMEL_IS_EWS_STORE (with_object));
	g_return_if_fail (user_data != NULL);
	g_return_if_fail (cffd->email != NULL);

	if (!cffd->folder)
		return;

	folder_type = e_ews_folder_get_folder_type (cffd->folder);
	base_username = cffd->user_displayname ? cffd->user_displayname : cffd->email;
	base_foldername = e_ews_folder_get_name (cffd->folder) ? e_ews_folder_get_name (cffd->folder) : cffd->orig_foldername;

	/* Translators: This is used to name foreign folder.
	 * The first '%s' is replaced with user name to whom the folder belongs,
	 * the second '%s' is replaced with folder name.
	 * Example result: "John Smith — Calendar"
	*/
	folder_name = g_strdup_printf (C_("ForeignFolder", "%s — %s"), base_username, base_foldername);
	if (folder_type != E_EWS_FOLDER_TYPE_MAILBOX)
		e_ews_folder_set_name (cffd->folder, folder_name);

	ews_store = CAMEL_EWS_STORE (with_object);
	settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));
	ews_settings = CAMEL_EWS_SETTINGS (settings);
	session = camel_service_ref_session (CAMEL_SERVICE (ews_store));
	if (E_IS_MAIL_SESSION (session))
		registry = e_mail_session_get_registry (E_MAIL_SESSION (session));

	if ((folder_type == E_EWS_FOLDER_TYPE_MAILBOX &&
	     !add_foreign_folder_to_camel (ews_store,
		cffd->email,
		cffd->folder,
		cffd->include_subfolders,
		base_username,
		base_foldername,
		perror)) ||
	    (folder_type != E_EWS_FOLDER_TYPE_MAILBOX && !e_ews_folder_utils_add_as_esource (registry,
		camel_ews_settings_get_hosturl (ews_settings),
		camel_network_settings_get_user (CAMEL_NETWORK_SETTINGS (ews_settings)),
		cffd->folder,
		(cffd->include_subfolders ? E_EWS_ESOURCE_FLAG_INCLUDE_SUBFOLDERS : 0) | E_EWS_ESOURCE_FLAG_OFFLINE_SYNC,
		0,
		cancellable,
		perror))
	) {
		/* to not destroy the dialog on error */
		g_object_unref (cffd->folder);
		cffd->folder = NULL;
	}

	g_free (folder_name);
	g_object_unref (session);
	g_object_unref (settings);
}

static void
subscribe_foreign_response_cb (GObject *dialog,
                               gint response_id)
{
	struct EEwsCheckForeignFolderData *cffd;
	ENameSelectorEntry *entry;
	GtkComboBoxText *combo_text;
	GtkToggleButton *subfolders_check;
	EDestinationStore *dest_store;
	CamelStore *cstore;
	gchar *description;
	const gchar *username;
	gchar *orig_foldername, *use_foldername = NULL, *show_foldername = NULL;

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	g_return_if_fail (dialog != NULL);

	entry = g_object_get_data (dialog, STR_USER_NAME_SELECTOR_ENTRY);
	combo_text = g_object_get_data (dialog, STR_FOLDER_NAME_COMBO);
	subfolders_check = g_object_get_data (dialog, STR_SUBFOLDERS_CHECK);
	cstore = g_object_get_data (dialog, STR_EWS_CAMEL_STORE);

	g_return_if_fail (entry != NULL);
	g_return_if_fail (cstore != NULL);

	username = NULL;
	dest_store = e_name_selector_entry_peek_destination_store (entry);
	if (dest_store && e_destination_store_get_destination_count (dest_store) > 0) {
		EDestination *dest;
		GList *dests = e_destination_store_list_destinations (dest_store);

		g_return_if_fail (dests != NULL);

		/* pick the first, there is no option to limit to only one destination */
		dest = dests->data;
		if (dest) {
			username = e_destination_get_email (dest);
			if (!username || !*username)
				username = e_destination_get_name (dest);
		}

		g_list_free (dests);
	}

	if (!username || !*username)
		username = gtk_entry_get_text (GTK_ENTRY (entry));

	orig_foldername = gtk_combo_box_text_get_active_text (combo_text);
	if (!orig_foldername)
		orig_foldername = g_strdup ("");

	/* convert well-known names to their non-localized form */
	if (g_strcmp0 (orig_foldername, _("Inbox")) == 0) {
		use_foldername = g_strdup ("inbox");
	} else if (g_strcmp0 (orig_foldername, _("Contacts")) == 0) {
		use_foldername = g_strdup ("contacts");
	} else if (g_strcmp0 (orig_foldername, _("Calendar")) == 0) {
		use_foldername = g_strdup ("calendar");
	} else if (g_strcmp0 (orig_foldername, _("Free/Busy as Calendar")) == 0) {
		use_foldername = g_strdup ("freebusy-calendar");
	} else if (g_strcmp0 (orig_foldername, _("Memos")) == 0) {
		use_foldername = g_strdup ("notes");
	} else if (g_strcmp0 (orig_foldername, _("Tasks")) == 0) {
		use_foldername = g_strdup ("tasks");
	} else if (strlen (orig_foldername) > 13) {
		/* if its a folder ID, then show only first 10 letters of it */
		show_foldername = g_strdup_printf ("%.10s...", orig_foldername);
	}

	cffd = g_new0 (struct EEwsCheckForeignFolderData, 1);
	cffd->dialog = GTK_WIDGET (dialog);
	cffd->email = g_strdup (username ? username : "");
	cffd->direct_email = g_strdup (g_object_get_data (G_OBJECT (entry), STR_EWS_DIRECT_EMAIL));
	cffd->orig_foldername = orig_foldername;
	cffd->use_foldername = use_foldername;
	cffd->include_subfolders = gtk_toggle_button_get_active (subfolders_check);
	cffd->folder = NULL;

	description = g_strdup_printf (
		_("Testing availability of folder “%s” of user “%s”, please wait..."),
		show_foldername ? show_foldername : cffd->orig_foldername, cffd->email);

	e_ews_config_utils_run_in_thread_with_feedback (
		GTK_WINDOW (dialog),
		G_OBJECT (cstore),
		description,
		check_foreign_folder_thread,
		check_foreign_folder_idle,
		cffd,
		e_ews_check_foreign_folder_data_free);

	g_free (description);
	g_free (show_foldername);
}

static void
pick_gal_user_clicked_cb (GtkButton *button,
                          GObject *dialog)
{
	GtkEntry *entry;
	CamelEwsStore *ews_store;
	EEwsConnection *conn;
	gchar *text, *display_name = NULL, *email = NULL;

	g_return_if_fail (dialog != NULL);

	entry = g_object_get_data (dialog, STR_USER_NAME_SELECTOR_ENTRY);
	ews_store = g_object_get_data (dialog, STR_EWS_CAMEL_STORE);

	g_return_if_fail (entry != NULL);
	g_return_if_fail (ews_store != NULL);

	text = g_strstrip (g_strdup (gtk_entry_get_text (entry)));
	conn = camel_ews_store_ref_connection (ews_store);

	if (e_ews_search_user_modal (GTK_WINDOW (dialog),
				     conn,
				     text,
				     &display_name,
				     &email)) {
		if (display_name && email && *email) {
			gtk_entry_set_text (entry, display_name);
			g_object_set_data_full (G_OBJECT (entry), STR_EWS_DIRECT_EMAIL, g_strdup (email), g_free);
		}
	}

	g_free (text);
	g_free (display_name);
	g_free (email);
	g_object_unref (conn);
}

/* Opens dialog to subscribe to folders of other
 * users in the given store */
void
e_ews_subscribe_foreign_folder (GtkWindow *parent,
                                CamelSession *session,
                                CamelStore *store,
                                EClientCache *client_cache)
{
	PangoAttrList *attrs;
	ENameSelector *name_selector;
	ENameSelectorModel *name_selector_model;
	ENameSelectorDialog *name_selector_dialog;
	GObject *dialog;
	GtkWidget *content;
	GtkWidget *label, *widget, *entry, *check;
	GtkGrid *grid;
	GtkComboBoxText *combo_text;
	gint row;

	g_return_if_fail (session != NULL);
	g_return_if_fail (store != NULL);
	g_return_if_fail (CAMEL_IS_EWS_STORE (store));

	dialog = G_OBJECT (gtk_dialog_new_with_buttons (
		_("Subscribe to folder of other EWS user..."),
		parent,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL));

	g_signal_connect (dialog, "response", G_CALLBACK (subscribe_foreign_response_cb), NULL);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_row_homogeneous (grid, FALSE);
	gtk_grid_set_row_spacing (grid, 6);
	gtk_grid_set_column_homogeneous (grid, FALSE);
	gtk_grid_set_column_spacing (grid, 6);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	gtk_container_add (GTK_CONTAINER (content), GTK_WIDGET (grid));

	row = 0;

	label = gtk_label_new (_("Account:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		NULL);

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	widget = gtk_label_new (camel_service_get_display_name (CAMEL_SERVICE (store)));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"use-underline", FALSE,
		"attributes", attrs,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		NULL);
	pango_attr_list_unref (attrs);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 2, 1);

	row++;

	name_selector = e_name_selector_new (client_cache);
	name_selector_model = e_name_selector_peek_model (name_selector);
	e_name_selector_model_add_section (name_selector_model, "User", _("User"), NULL);
	name_selector_dialog = e_name_selector_peek_dialog (name_selector);
	g_signal_connect (name_selector_dialog, "response", G_CALLBACK (gtk_widget_hide), name_selector);
	e_name_selector_load_books (name_selector);

	g_object_set_data_full (dialog, "e-ews-name-selector", name_selector, g_object_unref);

	label = gtk_label_new_with_mnemonic (_("_User:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	entry = GTK_WIDGET (e_name_selector_peek_section_entry (name_selector, "User"));
	g_object_set (
		G_OBJECT (entry),
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL);

	widget = gtk_button_new_with_mnemonic (_("C_hoose..."));
	g_object_set (
		G_OBJECT (entry),
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
	g_signal_connect (widget, "clicked", G_CALLBACK (pick_gal_user_clicked_cb), dialog);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, entry, 1, row, 1, 1);
	gtk_grid_attach (grid, widget, 2, row, 1, 1);

	row++;

	label = gtk_label_new_with_mnemonic (_("_Folder name:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	widget = GTK_WIDGET (
		g_object_new (gtk_combo_box_text_get_type (),
		"has-entry", TRUE,
		"entry-text-column", 0,
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL));

	combo_text = GTK_COMBO_BOX_TEXT (widget);
	gtk_combo_box_text_append_text (combo_text, _("Inbox"));
	gtk_combo_box_text_append_text (combo_text, _("Contacts"));
	gtk_combo_box_text_append_text (combo_text, _("Calendar"));
	gtk_combo_box_text_append_text (combo_text, _("Free/Busy as Calendar"));
	gtk_combo_box_text_append_text (combo_text, _("Memos"));
	gtk_combo_box_text_append_text (combo_text, _("Tasks"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_text), 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 2, 1);

	row++;

	check = gtk_check_button_new_with_mnemonic (_("Include _subfolders"));
	gtk_grid_attach (grid, check, 1, row, 2, 1);

	/* remember widgets for later use */
	g_object_set_data (dialog, STR_USER_NAME_SELECTOR_ENTRY, entry);
	g_object_set_data (dialog, STR_FOLDER_NAME_COMBO, widget);
	g_object_set_data (dialog, STR_SUBFOLDERS_CHECK, check);

	g_object_set_data_full (dialog, STR_EWS_CAMEL_SESSION, g_object_ref (session), g_object_unref);
	g_object_set_data_full (dialog, STR_EWS_CAMEL_STORE, g_object_ref (store), g_object_unref);

	g_signal_connect_swapped (entry, "changed", G_CALLBACK (name_entry_changed_cb), dialog);
	g_signal_connect_swapped (combo_text, "changed", G_CALLBACK (folder_name_combo_changed_cb), dialog);

	name_entry_changed_cb (dialog);

	gtk_widget_show_all (content);
	gtk_widget_show (GTK_WIDGET (dialog));
}
