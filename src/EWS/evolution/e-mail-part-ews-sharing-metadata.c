/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <e-util/e-util.h>
#include <mail/e-mail-display.h>
#include <shell/e-shell-window.h>

#include "camel/camel-ews-folder.h"
#include "camel/camel-ews-store.h"

#include "e-ews-subscribe-foreign-folder.h"

#include "e-mail-part-ews-sharing-metadata.h"

G_DEFINE_DYNAMIC_TYPE (EMailPartEwsSharingMetadata, e_mail_part_ews_sharing_metadata, E_TYPE_MAIL_PART)

typedef struct _SubscribeData {
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	ENamedParameters *params;
} SubscribeData;

static void
subscribe_data_free (gpointer ptr)
{
	SubscribeData *sd = ptr;

	if (sd) {
		g_clear_object (&sd->ews_store);
		g_clear_object (&sd->cnc);
		e_named_parameters_free (sd->params);
		g_slice_free (SubscribeData, sd);
	}
}

static void
ews_sharing_metadata_subscribe_thread (EAlertSinkThreadJobData *job_data,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	SubscribeData *sd = user_data;
	EwsFolderId fid;
	EEwsFolder *folder = NULL;
	const gchar *email;
	gchar *folder_id = NULL;
	gchar *display_name = NULL;
	GError *local_error = NULL;

	g_return_if_fail (sd != NULL);

	if (!sd->cnc) {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot subscribe EWS folders in offline mode"));
		return;
	}

	email = e_named_parameters_get (sd->params, "email");

	if (!e_ews_connection_convert_id_sync (sd->cnc, G_PRIORITY_DEFAULT, email,
		e_named_parameters_get (sd->params, "folder_id"),
		"HexEntryId", "EwsId", &folder_id, cancellable, error)) {
		return;
	}

	fid.id = folder_id;
	fid.change_key = NULL;
	fid.is_distinguished_id = FALSE;

	if (e_ews_connection_get_folder_info_sync (sd->cnc, G_PRIORITY_DEFAULT, email, &fid, &folder, cancellable, &local_error)) {
		if (e_ews_folder_get_folder_type (folder) == E_EWS_FOLDER_TYPE_UNKNOWN) {
			local_error = g_error_new_literal (EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND,
				_("Cannot add folder, cannot determine folder’s type"));
		} else {
			e_ews_folder_set_foreign (folder, TRUE);

			if (!e_ews_subscribe_foreign_folder_resolve_name_sync (sd->cnc, email, &display_name, NULL, cancellable, NULL))
				display_name = NULL;

			e_ews_subscrive_foreign_folder_subscribe_sync (sd->ews_store, folder,
				display_name, email, _("Folder"), FALSE, cancellable, &local_error);
		}
	} else if (!local_error ||
		   g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND) ||
		   g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND)) {
		g_clear_error (&local_error);
		local_error = g_error_new (
			EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND,
			_("Folder “%s” not found. Either it does not exist or you do not have permission to access it."),
			e_named_parameters_get (sd->params, "folder_id"));
	}

	if (local_error) {
		g_propagate_error (error, local_error);
	} else {
		e_alert_sink_thread_job_set_alert_ident (job_data, "ews:folder-subscribe-info");
		e_alert_sink_thread_job_set_alert_arg_0 (job_data, display_name ? display_name : email);

		/* Create a fake error, to give a feedback about successful subscribe of the folder */
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "");
	}

	g_clear_object (&folder);
	g_free (display_name);
	g_free (folder_id);
}

static void
ews_sharing_metadata_btn_clicked_cb (EWebView *web_view,
				     const gchar *iframe_id,
				     const gchar *element_id,
				     const gchar *element_class,
				     const gchar *element_value,
				     const GtkAllocation *element_position,
				     gpointer user_data)
{
	EMailPartList *part_list;
	EAlertSink *alert_sink = NULL;
	EActivity *activity;
	CamelFolder *folder;
	CamelStore *store;
	GtkWidget *widget;
	SubscribeData *sd;

	if (!element_value || !*element_value || !E_IS_MAIL_DISPLAY (web_view))
		return;

	part_list = e_mail_display_get_part_list (E_MAIL_DISPLAY (web_view));
	folder = part_list ? e_mail_part_list_get_folder (part_list) : NULL;

	if (!CAMEL_IS_EWS_FOLDER (folder))
		return;

	store = camel_folder_get_parent_store (folder);

	if (!CAMEL_IS_EWS_STORE (store))
		return;

	widget = gtk_widget_get_toplevel (GTK_WIDGET (web_view));

	if (E_IS_SHELL_WINDOW (widget))
		alert_sink = E_ALERT_SINK (widget);

	if (!alert_sink)
		alert_sink = E_ALERT_SINK (web_view);

	sd = g_slice_new (SubscribeData);
	sd->ews_store = CAMEL_EWS_STORE (g_object_ref (store));
	sd->cnc = camel_ews_store_ref_connection (CAMEL_EWS_STORE (store));
	sd->params = e_named_parameters_new_string (element_value);

	activity = e_alert_sink_submit_thread_job (alert_sink,
		_("Subscribing EWS folder…"), "ews:folder-subscribe-error", NULL,
		ews_sharing_metadata_subscribe_thread, sd, subscribe_data_free);

	g_clear_object (&activity);
}

static void
mail_part_ews_sharing_metadata_content_loaded (EMailPart *part,
					       EWebView *web_view,
					       const gchar *iframe_id)
{
	g_return_if_fail (E_IS_MAIL_PART_EWS_SHARING_METADATA (part));
	g_return_if_fail (E_IS_WEB_VIEW (web_view));

	if (g_strcmp0 ((iframe_id && *iframe_id) ? iframe_id : NULL, e_mail_part_get_id (part)) != 0)
		return;

	e_web_view_register_element_clicked (web_view, "ews-sharing-metadata-btn",
		ews_sharing_metadata_btn_clicked_cb, NULL);
}

static void
mail_part_ews_sharing_metadata_finalize (GObject *object)
{
	EMailPartEwsSharingMetadata *part = E_MAIL_PART_EWS_SHARING_METADATA (object);

	g_clear_pointer (&part->xml, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_mail_part_ews_sharing_metadata_parent_class)->finalize (object);
}

static void
e_mail_part_ews_sharing_metadata_class_init (EMailPartEwsSharingMetadataClass *klass)
{
	GObjectClass *object_class;
	EMailPartClass *mail_part_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = mail_part_ews_sharing_metadata_finalize;

	mail_part_class = E_MAIL_PART_CLASS (klass);
	mail_part_class->content_loaded = mail_part_ews_sharing_metadata_content_loaded;
}

static void
e_mail_part_ews_sharing_metadata_class_finalize (EMailPartEwsSharingMetadataClass *klass)
{
}

static void
e_mail_part_ews_sharing_metadata_init (EMailPartEwsSharingMetadata *part)
{
}

void
e_mail_part_ews_sharing_metadata_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_part_ews_sharing_metadata_register_type (type_module);
}

EMailPart *
e_mail_part_ews_sharing_metadata_new (CamelMimePart *mime_part,
                      const gchar *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	return g_object_new (
		E_TYPE_MAIL_PART_EWS_SHARING_METADATA,
		"id", id, "mime-part", mime_part, NULL);
}
