/*
 * e-mail-config-ews-ooo-page.c
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
 */

#include "e-mail-config-ews-ooo-page.h"

#include <config.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <misc/e-dateedit.h>
#include <e-util/e-dialog-utils.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"

#define E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE, EMailConfigEwsOooPagePrivate))

typedef enum {
	EXTERNAL_AUDIENCE_NONE,
	EXTERNAL_AUDIENCE_KNOWN,
	EXTERNAL_AUDIENCE_ALL
} ExternalAudience;

struct _EMailConfigEwsOooPagePrivate {
	ESource *account_source;
	ESource *identity_source;

	gboolean state;

	/*to set duration or not*/
	gboolean set_range;
	GtkWidget *range_wt;

	/*duration for out of office*/
	time_t from_time;
	time_t to_time;
	EDateEdit *from_date;
	EDateEdit *to_date;

	/*External Audience type*/
	gchar *audience;
	gint audience_type;
	GtkWidget *aud_box;

	/*Internal and External messages*/
	gchar *external_message;
	gchar *internal_message;
	GtkWidget *external_view;
	GtkWidget *internal_view;

	/*Update box*/
	GtkWidget *stat_box;
};

enum {
	PROP_0,
	PROP_ACCOUNT_SOURCE,
	PROP_IDENTITY_SOURCE
};

/* Forward Declarations */
static void	e_mail_config_ews_ooo_page_interface_init
					(EMailConfigPageInterface *interface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailConfigEwsOooPage,
	e_mail_config_ews_ooo_page,
	GTK_TYPE_BOX,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_MAIL_CONFIG_PAGE,
		e_mail_config_ews_ooo_page_interface_init))

static void
update_audience_type (void)
{
#if 0  /* ACCOUNT_MGMT */
	if (!g_ascii_strcasecmp (oof_data->audience, "None"))
		oof_data->audience_type = EXTERNAL_AUDIENCE_NONE;
	else if (!g_ascii_strcasecmp (oof_data->audience, "Known"))
		oof_data->audience_type = EXTERNAL_AUDIENCE_KNOWN;
	else
		oof_data->audience_type = EXTERNAL_AUDIENCE_ALL;
#endif /* ACCOUNT_MGMT */
}

static void
update_audience (void)
{
#if 0  /* ACCOUNT_MGMT */
	g_free (oof_data->audience);
	oof_data->audience = NULL;

	if (oof_data->audience_type == EXTERNAL_AUDIENCE_NONE)
		oof_data->audience = g_strdup ("None");
	else if (oof_data->audience_type == EXTERNAL_AUDIENCE_KNOWN)
		oof_data->audience = g_strdup ("Known");
	else
		oof_data->audience = g_strdup ("All");
#endif /* ACCOUNT_MGMT */
}

static void
update_audience_cb (GtkComboBoxText *combo,
                    gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	gint active;

	active = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
	if (active == oof_data->audience_type)
		return;
	else
		oof_data->audience_type = active;

	update_audience ();
#endif /* ACCOUNT_MGMT */
}

static void
update_int_msg_cb (GtkTextBuffer *buffer,
                   gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	if (gtk_text_buffer_get_modified (buffer)) {
		GtkTextIter start, end;
		if (oof_data->internal_message)
			g_free (oof_data->internal_message);
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		oof_data->internal_message =  gtk_text_buffer_get_text (buffer, &start,
							       &end, FALSE);
		gtk_text_buffer_set_modified (buffer, FALSE);
	}
#endif /* ACCOUNT_MGMT */
}

static void
update_ext_msg_cb (GtkTextBuffer *buffer,
                   gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	if (gtk_text_buffer_get_modified (buffer)) {
		GtkTextIter start, end;
		if (oof_data->external_message)
			g_free (oof_data->external_message);
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		oof_data->external_message =  gtk_text_buffer_get_text (buffer, &start,
							       &end, FALSE);
		gtk_text_buffer_set_modified (buffer, FALSE);
	}
#endif /* ACCOUNT_MGMT */
}

static void
toggled_state_cb (GtkToggleButton *button,
                  gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	gboolean current_oof_state;

	current_oof_state = gtk_toggle_button_get_active (button);
	if (current_oof_state == oof_data->state)
		return;
	oof_data->state = current_oof_state;
	gtk_widget_set_sensitive (oof_data->range_wt, current_oof_state);
	gtk_widget_set_sensitive (oof_data->internal_view, current_oof_state);
	gtk_widget_set_sensitive (oof_data->external_view, current_oof_state);
	gtk_widget_set_sensitive ((GtkWidget *) oof_data->from_date, current_oof_state && oof_data->set_range);
	gtk_widget_set_sensitive ((GtkWidget *) oof_data->to_date, current_oof_state && oof_data->set_range);
	gtk_widget_set_sensitive (oof_data->aud_box, current_oof_state);
#endif /* ACCOUNT_MGMT */
}

static void
toggled_set_date_cb (GtkToggleButton *button,
                     gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	gboolean current_state;

	current_state = gtk_toggle_button_get_active (button);
	if (current_state == oof_data->set_range)
		return;

	oof_data->set_range = current_state;
	gtk_widget_set_sensitive ((GtkWidget *) oof_data->from_date, current_state);
	gtk_widget_set_sensitive ((GtkWidget *) oof_data->to_date, current_state);
#endif /* ACCOUNT_MGMT */
}

static void
from_time_changed_cb (EDateEdit *date_tm,
                      gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	if (e_date_edit_get_time (date_tm) < time (NULL)) {
		e_notice (NULL, GTK_MESSAGE_WARNING, _("Cannot set Date-Time in Past"));
		e_date_edit_set_time (date_tm, time (NULL) + 60);
	} else if (e_date_edit_date_is_valid (date_tm) && e_date_edit_time_is_valid (date_tm)) {
		oof_data->from_time = e_date_edit_get_time (date_tm);
	}
#endif /* ACCOUNT_MGMT */
}

static void
to_time_changed_cb (EDateEdit *date_tm,
                    gpointer data)
{
#if 0  /* ACCOUNT_MGMT */
	if (e_date_edit_get_time (date_tm) < time (NULL)) {
		e_notice (NULL, GTK_MESSAGE_WARNING, _("Cannot set Date-Time in Past"));
		e_date_edit_set_time (date_tm, time (NULL) + 60);
		return;
	} else if (e_date_edit_date_is_valid (date_tm) && e_date_edit_time_is_valid (date_tm)) {
		oof_data->to_time = e_date_edit_get_time (date_tm);
	}
	if (oof_data->from_time > oof_data->to_time)
		e_notice (NULL, GTK_MESSAGE_WARNING, _("Select a valid time range"));
#endif /* ACCOUNT_MGMT */
}

static void
oof_data_new (void)
{
#if 0  /* ACCOUNT_MGMT */
	oof_data = g_new0 (OOFData, 1);
	oof_data->state = FALSE;
	oof_data->set_range = FALSE;
	oof_data->range_wt = NULL;
	oof_data->audience_type = EXTERNAL_AUDIENCE_ALL;
	oof_data->audience = NULL;
	oof_data->external_message = NULL;
	oof_data->internal_message = NULL;
	oof_data->internal_view = NULL;
	oof_data->external_view = NULL;
	oof_data->from_time = 0;
	oof_data->to_time = 0;
	oof_data->from_date = NULL;
	oof_data->to_date = NULL;
	oof_data->stat_box = NULL;
#endif /* ACCOUNT_MGMT */
}

static void
destroy_oof_data (void)
{
#if 0  /* ACCOUNT_MGMT */
	if (oof_data->audience) {
		g_free (oof_data->audience);
		oof_data->audience = NULL;
	}

	if (oof_data->external_message) {
		g_free (oof_data->external_message);
		oof_data->external_message = NULL;
	}

	if (oof_data->internal_message) {
		g_free (oof_data->internal_message);
		oof_data->internal_message = NULL;
	}

	if (oof_data) {
		g_free (oof_data);
		oof_data = NULL;
	}
#endif /* ACCOUNT_MGMT */
}

#if 0  /* ACCOUNT_MGMT */
static gchar *
get_password (CamelSettings *settings)
{
	gchar *key, *password = NULL;
	CamelURL *url;

	url = g_new0 (CamelURL, 1);
	camel_settings_save_to_url (settings, url);
	key = camel_url_to_string (url, CAMEL_URL_HIDE_PARAMS);
	camel_url_free (url);

	password = e_passwords_get_password ("Exchange Web Services", key);

	g_free (key);
	return password;
}
#endif /* ACCOUNT_MGMT */

#if 0  /* ACCOUNT_MGMT */
static EEwsConnection *
get_connection (EMConfigTargetSettings *target)
{
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	EEwsConnection *cnc;
	const gchar *host_url;
	const gchar *user;
	gchar *email, *password;
	GError *error = NULL;

	ews_settings = CAMEL_EWS_SETTINGS (target->storage_settings);
	network_settings = CAMEL_NETWORK_SETTINGS (target->storage_settings);

	/* Create a new connection */
	host_url = camel_ews_settings_get_hosturl (ews_settings);
	user = camel_network_settings_get_user (network_settings);
	password = get_password (target->storage_settings);
	email = target->email_address;

	cnc = e_ews_connection_new (host_url, user, password, NULL, NULL, &error);

	if (!cnc) {
		g_warning ("Error in connection: %s\n", error->message);
		g_clear_error (&error);
		return NULL;
	}

	e_ews_connection_set_mailbox (cnc, email);

	g_free (password);
	return cnc;
}
#endif /* ACCOUNT_MGMT */

static void
set_oof_error_to_frame (GtkWidget *oof_frame,
                        GError *error)
{
#if 0  /* ACCOUNT_MGMT */
	GtkHBox *error_box;
	GtkLabel *error_msg;
	GtkWidget *error_img;
	gchar *message;

	gtk_widget_destroy (oof_data->stat_box);

	error_box = (GtkHBox*) g_object_new (GTK_TYPE_HBOX, NULL, "homogeneous", FALSE, "spacing", 6, NULL);
	error_img = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_SMALL_TOOLBAR);
	message = g_strdup_printf (_("Unable to fetch out of office settings: \n%s"), error->message);
	error_msg = (GtkLabel *) gtk_label_new (message);
	gtk_label_set_use_markup (error_msg, TRUE);
	gtk_box_pack_start (GTK_BOX (error_box), GTK_WIDGET (error_img), FALSE, FALSE, 12);
	gtk_box_pack_start (GTK_BOX (error_box), GTK_WIDGET (error_msg), FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (oof_frame), GTK_WIDGET (error_box));
	gtk_widget_show_all (GTK_WIDGET (error_box));

	g_free (message);
#endif /* ACCOUNT_MGMT */
}

static void
set_oof_settings_to_frame (GtkWidget *oof_frame)
{
#if 0  /* ACCOUNT_MGMT */
	gtk_widget_destroy (oof_data->stat_box);
#endif /* ACCOUNT_MGMT */
}

static void
get_oof_settings_cb (GObject *object,
                     GAsyncResult *res,
                     gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GtkWidget *oof_frame = GTK_WIDGET (user_data);
	OOFSettings *oof_settings = NULL;
	GError *error = NULL;

	e_ews_connection_get_oof_settings_finish (cnc, res, &oof_settings, &error);

	if (error) {
		g_warning ("Error Unable to get out of office settings: %s\n", error->message);
		set_oof_error_to_frame (oof_frame, error);
		g_object_unref (cnc);
		g_clear_error (&error);
		return;
	}

#if 0  /* ACCOUNT_MGMT */
	if (!g_ascii_strcasecmp (oof_settings->state, "Disabled"))
		oof_data->state = FALSE;
	else
		oof_data->state = TRUE;

	oof_data->audience = g_strdup (oof_settings->ext_aud);
	update_audience_type ();
	if (!g_ascii_strcasecmp (oof_settings->state, "Scheduled")) {
		oof_data->from_time = oof_settings->start_tm;
		oof_data->to_time = oof_settings->end_tm;
		oof_data->set_range = TRUE;
	}
	oof_data->internal_message = g_strdup (oof_settings->int_reply);
	oof_data->external_message = g_strdup (oof_settings->ext_reply);
#endif /* ACCOUNT_MGMT */

	set_oof_settings_to_frame (oof_frame);
	e_ews_connection_free_oof_settings (oof_settings);
	g_object_unref (cnc);
}

#if 0  /* ACCOUNT_MGMT */
static void
set_oof_data_from_settings (EMConfigTargetSettings *target,
                            GtkWidget *oof_frame)
{
	GCancellable *cancellable = NULL;
	EEwsConnection *cnc = NULL;

	cnc = get_connection (target);
	if (!cnc)
		return;

	cancellable = g_cancellable_new ();

	e_ews_connection_get_oof_settings (
		cnc, EWS_PRIORITY_MEDIUM, cancellable,
		get_oof_settings_cb, oof_frame);
}
#endif /* ACCOUNT_MGMT */

static OOFSettings *
get_settings_from_data (void)
{
	OOFSettings *oof_settings = NULL;

	oof_settings = g_new0 (OOFSettings, 1);

#if 0  /* ACCOUNT_MGMT */
	if (oof_data->from_time >= oof_data->to_time || !oof_data->set_range) {
		d (printf ("not a valid time range or set duration is not available"));
		oof_data->from_time = 0;
		oof_data->to_time = 0;
	}

	if (oof_data->state) {
		if (oof_data->from_time && oof_data->to_time)
			oof_settings->state = g_strdup ("Scheduled");
		else
			oof_settings->state = g_strdup ("Enabled");
	} else
		oof_settings->state = g_strdup ("Disabled");

	oof_settings->ext_aud = g_strdup (oof_data->audience);
	oof_settings->start_tm = oof_data->from_time;
	oof_settings->end_tm = oof_data->to_time;
	oof_settings->int_reply = g_strdup (oof_data->internal_message);
	oof_settings->ext_reply = g_strdup (oof_data->external_message);
#endif /* ACCOUNT_MGMT */

	return oof_settings;
}

#if 0  /* ACCOUNT_MGMT */
gboolean
ews_set_oof_settings (EMConfigTargetSettings *target)
{
	GCancellable *cancellable = NULL;
	OOFSettings *oof_settings = NULL;
	EEwsConnection *cnc = NULL;
	GError *error = NULL;
	gboolean ret_val;

	cnc = get_connection (target);
	if (!cnc) {
		destroy_oof_data ();
		return FALSE;
	}

	cancellable = g_cancellable_new ();

	oof_settings = get_settings_from_data ();

	e_ews_connection_set_oof_settings_sync (
		cnc, EWS_PRIORITY_MEDIUM,
		oof_settings, cancellable, &error);

	if (error) {
		g_warning ("Error While setting out of office: %s\n", error->message);
		g_clear_error (&error);
		ret_val = FALSE;
	} else
		ret_val = TRUE;

	destroy_oof_data ();
	e_ews_connection_free_oof_settings (oof_settings);
	g_object_unref (cnc);

	return ret_val;
}
#endif /* ACCOUNT_MGMT */

#if 0  /* ACCOUNT_MGMT */
GtkWidget *
ews_get_outo_office_widget (EMConfigTargetSettings *target_account)
{
	GtkFrame *frm_oof;
	GtkHBox *stat_box;
	GtkLabel *stat_msg;
	GtkWidget *spinner, *label;
	gchar *txt;

	txt = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", _("Out of Office"));
	label = gtk_label_new (NULL);
	gtk_label_set_markup ((GtkLabel *) label, txt);
	g_free (txt);
	frm_oof = (GtkFrame*) g_object_new (GTK_TYPE_FRAME, "label-widget", label, NULL);

	oof_data_new ();

	stat_box = (GtkHBox*) g_object_new (GTK_TYPE_HBOX, NULL, "homogeneous", FALSE, "spacing", 6, NULL);
	spinner = gtk_spinner_new ();
	stat_msg = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("Fetching out of office settings..."), "use-markup", TRUE, NULL);

	oof_data->stat_box = GTK_WIDGET (stat_box);

	gtk_box_pack_start (GTK_BOX (stat_box), GTK_WIDGET (spinner), FALSE, FALSE, 12);
	gtk_box_pack_start (GTK_BOX (stat_box), GTK_WIDGET (stat_msg), FALSE, FALSE, 0);
	gtk_spinner_start ((GtkSpinner *) spinner);
	gtk_container_add (GTK_CONTAINER (frm_oof), GTK_WIDGET (stat_box));

	set_oof_data_from_settings (target_account, (GtkWidget *) frm_oof);

	return (GtkWidget *) frm_oof;
}
#endif /* ACCOUNT_MGMT */

static void
mail_config_ews_ooo_page_set_account_source (EMailConfigEwsOooPage *page,
                                             ESource *account_source)
{
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (page->priv->account_source == NULL);

	page->priv->account_source = g_object_ref (account_source);
}

static void
mail_config_ews_ooo_page_set_identity_source (EMailConfigEwsOooPage *page,
                                              ESource *identity_source)
{
	g_return_if_fail (E_IS_SOURCE (identity_source));
	g_return_if_fail (page->priv->identity_source == NULL);

	page->priv->identity_source = g_object_ref (identity_source);
}

static void
mail_config_ews_ooo_page_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			mail_config_ews_ooo_page_set_account_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_IDENTITY_SOURCE:
			mail_config_ews_ooo_page_set_identity_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_ooo_page_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_ooo_page_get_account_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object)));
			return;

		case PROP_IDENTITY_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_ooo_page_get_identity_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_ooo_page_dispose (GObject *object)
{
	EMailConfigEwsOooPagePrivate *priv;

	priv = E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE (object);

	if (priv->account_source != NULL) {
		g_object_unref (priv->account_source);
		priv->account_source = NULL;
	}

	if (priv->identity_source != NULL) {
		g_object_unref (priv->identity_source);
		priv->identity_source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_ooo_page_parent_class)->
		dispose (object);
}

static void
mail_config_ews_ooo_page_constructed (GObject *object)
{
	EMailConfigEwsOooPage *page;
	GtkWidget *widget;
	GtkWidget *container;
	const gchar *text;
	gchar *markup;

	GtkHBox *hbox_ext, *hbox_state;
	GtkLabel *lbl_oof_desc, *from_label, *to_label;
	GtkTable *tbl_oof_status;
	GtkLabel *lbl_status, *lbl_external, *lbl_internal;
	GtkRadioButton *radio_iof, *radio_oof;
	GtkScrolledWindow *scrwnd_oof_int, *scrwnd_oof_ext;
	GtkTextView *txtview_oof_int, *txtview_oof_ext;
	GtkTextBuffer *buffer_int, *buffer_ext;
	GtkWidget *from_date, *to_date, *aud_box, *set_range;

	page = E_MAIL_CONFIG_EWS_OOO_PAGE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_ooo_page_parent_class)->
		constructed (object);

	text = _("Out of Office");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (page), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (page), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	text = _("The messages specified below will be automatically sent "
		 "to each internal and external personal who sends a mail "
		 "to you.");
	widget = gtk_label_new (text);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 0, 1, 1);
	gtk_widget_show (widget);

	/* XXX Crap starts here */

	tbl_oof_status = (GtkTable*) g_object_new (GTK_TYPE_TABLE, "n-rows", 7, "n-columns", 2, "homogeneous", FALSE, "row-spacing", 6, "column-spacing", 6, NULL);
	gtk_grid_attach (GTK_GRID (container), GTK_WIDGET (tbl_oof_status), 0, 1, 1, 1);
	gtk_widget_show (GTK_WIDGET (tbl_oof_status));

	lbl_status = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("Status:"), "use-markup", TRUE, NULL);
	gtk_misc_set_alignment (GTK_MISC (lbl_status), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (lbl_status), 0, 0);

#if 0  /* ACCOUNT_MGMT */
	if (oof_data->state) {
#endif /* ACCOUNT_MGMT */
		radio_oof = (GtkRadioButton*) g_object_new (GTK_TYPE_RADIO_BUTTON, "label", _("I am _out of the office"), "use-underline", TRUE, NULL);
		radio_iof = (GtkRadioButton*) g_object_new (GTK_TYPE_RADIO_BUTTON, "label", _("I am _in the office"), "use-underline", TRUE, "group", radio_oof, NULL);
#if 0  /* ACCOUNT_MGMT */
	} else {
		radio_iof = (GtkRadioButton*) g_object_new (GTK_TYPE_RADIO_BUTTON, "label", _("I am _in the office"), "use-underline", TRUE, NULL);
		radio_oof = (GtkRadioButton*) g_object_new (GTK_TYPE_RADIO_BUTTON, "label", _("I am _out of the office"), "use-underline", TRUE, "group", radio_iof, NULL);
	}
#endif /* ACCOUNT_MGMT */
	g_signal_connect (radio_oof, "toggled", G_CALLBACK (toggled_state_cb), NULL);

	hbox_state = g_object_new (GTK_TYPE_HBOX, NULL, "homogeneous", FALSE, "spacing", 6, NULL);
	gtk_box_pack_start (GTK_BOX (hbox_state), GTK_WIDGET (radio_iof), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox_state), GTK_WIDGET (radio_oof), FALSE, FALSE, 12);

	/*Check box for setting date*/
	set_range = gtk_check_button_new_with_mnemonic (_("_Send only during this time period"));
#if 0  /* ACCOUNT_MGMT */
	oof_data->range_wt = set_range;
	gtk_toggle_button_set_active ((GtkToggleButton *) set_range, oof_data->set_range);
#endif /* ACCOUNT_MGMT */
	g_signal_connect ((GtkToggleButton*) set_range, "toggled", G_CALLBACK (toggled_set_date_cb), NULL);

	/*Selectable Dates*/
	from_date = e_date_edit_new ();
	to_date = e_date_edit_new ();

#if 0  /* ACCOUNT_MGMT */
	e_date_edit_set_time ((EDateEdit *) from_date, oof_data->from_time);
	e_date_edit_set_time ((EDateEdit *) to_date, oof_data->to_time);

	oof_data->from_date = (EDateEdit *) from_date;
	oof_data->to_date = (EDateEdit *) to_date;
#endif /* ACCOUNT_MGMT */

	g_signal_connect (from_date, "changed", G_CALLBACK (from_time_changed_cb), NULL);
	g_signal_connect (to_date, "changed", G_CALLBACK (to_time_changed_cb), NULL);

	from_label = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("_From:"), "use-underline", TRUE, "use-markup", TRUE, NULL);
	gtk_label_set_mnemonic_widget (from_label, GTK_WIDGET (from_date));
	gtk_misc_set_alignment (GTK_MISC (from_label), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (from_label), 0, 0);

	to_label = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("_To:"), "use-markup", TRUE, "use-underline", TRUE, NULL);
	gtk_label_set_mnemonic_widget (to_label, GTK_WIDGET (to_date));
	gtk_misc_set_alignment (GTK_MISC (to_label), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (to_label), 0, 0);

	scrwnd_oof_int = (GtkScrolledWindow*) g_object_new (GTK_TYPE_SCROLLED_WINDOW, "hscrollbar-policy", GTK_POLICY_AUTOMATIC, "vscrollbar-policy", GTK_POLICY_AUTOMATIC, "shadow-type", GTK_SHADOW_IN, NULL);
	txtview_oof_int = (GtkTextView*) g_object_new (GTK_TYPE_TEXT_VIEW, "justification", GTK_JUSTIFY_LEFT, "wrap-mode", GTK_WRAP_WORD, "editable", TRUE, NULL);

	buffer_int = gtk_text_view_get_buffer (txtview_oof_int);
#if 0  /* ACCOUNT_MGMT */
	if (oof_data->internal_message) {
		/* previuosly set message */
		gtk_text_buffer_set_text (buffer_int, oof_data->internal_message, -1);
		gtk_text_view_set_buffer (txtview_oof_int, buffer_int);
	}
#endif /* ACCOUNT_MGMT */
	gtk_text_buffer_set_modified (buffer_int, FALSE);
	gtk_container_add (GTK_CONTAINER (scrwnd_oof_int), GTK_WIDGET (txtview_oof_int));

	lbl_internal = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("I_nternal:"), "use-underline", TRUE, "use-markup", TRUE, NULL);
	gtk_label_set_mnemonic_widget (lbl_internal, GTK_WIDGET (txtview_oof_int));
	gtk_widget_set_tooltip_text (GTK_WIDGET (lbl_internal), _("Message to be sent inside organization"));
	gtk_misc_set_alignment (GTK_MISC (lbl_internal), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (lbl_internal), 0, 0);

	/*Select External Audience*/
	hbox_ext = g_object_new (GTK_TYPE_HBOX, NULL, "homogeneous", FALSE, "spacing", 6, NULL);	
	aud_box = gtk_combo_box_text_new ();
	gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT (aud_box), EXTERNAL_AUDIENCE_NONE, _("None"));
	gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT (aud_box), EXTERNAL_AUDIENCE_KNOWN, _("Known"));
	gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT (aud_box), EXTERNAL_AUDIENCE_ALL, _("All"));
#if 0  /* ACCOUNT_MGMT */
	gtk_combo_box_set_active (GTK_COMBO_BOX (aud_box), oof_data->audience_type);
#endif /* ACCOUNT_MGMT */
	gtk_widget_set_tooltip_text (GTK_WIDGET (aud_box), _("Send Message to"));

#if 0  /* ACCOUNT_MGMT */
	oof_data->aud_box = aud_box;
#endif /* ACCOUNT_MGMT */
	gtk_box_pack_start (GTK_BOX (hbox_ext), GTK_WIDGET (aud_box), FALSE, FALSE, 0);
	g_signal_connect (GTK_COMBO_BOX (aud_box), "changed", G_CALLBACK (update_audience_cb), NULL);

	scrwnd_oof_ext = (GtkScrolledWindow*) g_object_new (GTK_TYPE_SCROLLED_WINDOW, "hscrollbar-policy", GTK_POLICY_AUTOMATIC, "vscrollbar-policy", GTK_POLICY_AUTOMATIC, "shadow-type", GTK_SHADOW_IN, NULL);
	gtk_box_pack_start (GTK_BOX (hbox_ext), GTK_WIDGET (scrwnd_oof_ext), TRUE, TRUE, 0);
	txtview_oof_ext = (GtkTextView*) g_object_new (GTK_TYPE_TEXT_VIEW, "justification", GTK_JUSTIFY_LEFT, "wrap-mode", GTK_WRAP_WORD, "editable", TRUE, NULL);

	buffer_ext = gtk_text_view_get_buffer (txtview_oof_ext);
#if 0  /* ACCOUNT_MGMT */
	if (oof_data->external_message) {
		/* previuosly set message */
		gtk_text_buffer_set_text (buffer_ext, oof_data->external_message, -1);
		gtk_text_view_set_buffer (txtview_oof_ext, buffer_ext);

	}
#endif /* ACCOUNT_MGMT */
	gtk_text_buffer_set_modified (buffer_ext, FALSE);
	gtk_container_add (GTK_CONTAINER (scrwnd_oof_ext), GTK_WIDGET (txtview_oof_ext));

	lbl_external = (GtkLabel*) g_object_new (GTK_TYPE_LABEL, "label", _("E_xternal:"), "use-underline", TRUE, "use-markup", TRUE, NULL);
	gtk_label_set_mnemonic_widget (lbl_external, GTK_WIDGET (txtview_oof_ext));
	gtk_widget_set_tooltip_text (GTK_WIDGET (lbl_external), _("Message to be sent outside organization"));
	gtk_misc_set_alignment (GTK_MISC (lbl_external), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (lbl_external), 0, 0);

	g_signal_connect (buffer_int, "changed", G_CALLBACK (update_int_msg_cb), NULL);
	g_signal_connect (buffer_ext, "changed", G_CALLBACK (update_ext_msg_cb), NULL);

	gtk_table_attach (tbl_oof_status, GTK_WIDGET (lbl_status), 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (hbox_state), 1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (set_range), 1, 2, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (from_label), 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (from_date), 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (to_label), 0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (to_date), 1, 2, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (lbl_internal), 0, 1, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (scrwnd_oof_int), 1, 2, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (lbl_external), 0, 1, 5, 6, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach (tbl_oof_status, GTK_WIDGET (hbox_ext), 1, 2, 5, 6, GTK_FILL, GTK_FILL, 0, 0);

#if 0  /* ACCOUNT_MGMT */
	if (!oof_data->state) {
		gtk_widget_set_sensitive ((GtkWidget *) set_range, FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (txtview_oof_int), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (txtview_oof_ext), FALSE);
		gtk_widget_set_sensitive ((GtkWidget *) from_date, FALSE);
		gtk_widget_set_sensitive ((GtkWidget *) to_date, FALSE);
		gtk_widget_set_sensitive (aud_box, FALSE);
	}
	if (!oof_data->set_range) {
		gtk_widget_set_sensitive ((GtkWidget *) from_date, FALSE);
		gtk_widget_set_sensitive ((GtkWidget *) to_date, FALSE);
	}

	oof_data->internal_view = GTK_WIDGET (txtview_oof_int);
	oof_data->external_view = GTK_WIDGET (txtview_oof_ext);
#endif /* ACCOUNT_MGMT */
}

static void
e_mail_config_ews_ooo_page_class_init (EMailConfigEwsOooPageClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EMailConfigEwsOooPagePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_config_ews_ooo_page_set_property;
	object_class->get_property = mail_config_ews_ooo_page_get_property;
	object_class->dispose = mail_config_ews_ooo_page_dispose;
	object_class->constructed = mail_config_ews_ooo_page_constructed;

	g_object_class_install_property (
		object_class,
		PROP_ACCOUNT_SOURCE,
		g_param_spec_object (
			"account-source",
			"Account Source",
			"Mail account source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY_SOURCE,
		g_param_spec_object (
			"identity-source",
			"Identity Source",
			"Mail identity source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_mail_config_ews_ooo_page_interface_init (EMailConfigPageInterface *interface)
{
	interface->title = _("EWS Settings");
	interface->sort_order = E_MAIL_CONFIG_EWS_OOO_PAGE_SORT_ORDER;
}

static void
e_mail_config_ews_ooo_page_class_finalize (EMailConfigEwsOooPageClass *class)
{
}

static void
e_mail_config_ews_ooo_page_init (EMailConfigEwsOooPage *page)
{
	page->priv = E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE (page);
}

void
e_mail_config_ews_ooo_page_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_ooo_page_register_type (type_module);
}

EMailConfigPage *
e_mail_config_ews_ooo_page_new (ESource *account_source,
                                ESource *identity_source)
{
	g_return_val_if_fail (E_IS_SOURCE (account_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (identity_source), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE,
		"account-source", account_source,
		"identity-source", identity_source, NULL);
}

ESource *
e_mail_config_ews_ooo_page_get_account_source (EMailConfigEwsOooPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_OOO_PAGE (page), NULL);

	return page->priv->account_source;
}

ESource *
e_mail_config_ews_ooo_page_get_identity_source (EMailConfigEwsOooPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_OOO_PAGE (page), NULL);

	return page->priv->identity_source;
}

