/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "evolution-ews-config.h"

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <e-util/e-util.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"

#include "e-mail-config-m365-ooo-page.h"

typedef struct _CurrentSettings {
	EM365AutomaticRepliesStatusType status;
	EM365ExternalAudienceScopeType external_audience;
	time_t scheduled_start_date_time;
	time_t scheduled_end_date_time;
	gchar *external_reply_message;
	gchar *internal_reply_message;
} CurrentSettings;

struct _EMailConfigM365OooPage {
	EMailConfigActivityPage parent;

	ESourceRegistry *registry;
	ESource *account_source;
	ESource *identity_source;
	ESource *collection_source;

	EM365Connection *cnc;
	CurrentSettings current_settings;

	GCancellable *refresh_cancellable;

	GtkWidget *enabled_radio_button;	/* not referenced */
	GtkWidget *disabled_radio_button;	/* not referenced */
	GtkWidget *scheduled_radio_button;	/* not referenced */
	GtkWidget *start_time;			/* not referenced */
	GtkWidget *end_time;			/* not referenced */
	GtkWidget *external_audience;		/* not referenced */
	GtkTextBuffer *internal_reply;		/* not referenced */
	GtkTextBuffer *external_reply;		/* not referenced */
};

static void
current_settings_clear (CurrentSettings *self)
{
	g_clear_pointer (&self->external_reply_message, g_free);
	g_clear_pointer (&self->internal_reply_message, g_free);
	memset (self, 0, sizeof (CurrentSettings));
}

typedef struct _AsyncContext {
	EActivity *activity;
	EM365Connection *cnc;
	CurrentSettings settings;
	EM365AutomaticRepliesSetting *display_setting;
} AsyncContext;

enum {
	PROP_0,
	PROP_ACCOUNT_SOURCE,
	PROP_COLLECTION_SOURCE,
	PROP_IDENTITY_SOURCE,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_mail_config_m365_ooo_page_interface_init
				(EMailConfigPageInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigM365OooPage, e_mail_config_m365_ooo_page, E_TYPE_MAIL_CONFIG_ACTIVITY_PAGE, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_MAIL_CONFIG_PAGE, e_mail_config_m365_ooo_page_interface_init))

static void
async_context_free (gpointer ptr)
{
	AsyncContext *async_context = ptr;

	if (!async_context)
		return;

	g_clear_object (&async_context->activity);
	g_clear_object (&async_context->cnc);
	g_clear_pointer (&async_context->display_setting, json_object_unref);
	current_settings_clear (&async_context->settings);

	g_free (async_context);
}

static CamelSettings *
mail_config_m365_ooo_page_get_settings (EMailConfigM365OooPage *self)
{
	ESource *source;
	ESourceCamel *extension;
	const gchar *extension_name;

	source = self->collection_source;

	extension_name = e_source_camel_get_extension_name ("Microsoft365");
	extension = e_source_get_extension (source, extension_name);

	return e_source_camel_get_settings (extension);
}

static void
mail_config_m365_ooo_page_read_current_settings (EMailConfigM365OooPage *self,
						 CurrentSettings *inout_settings)
{
	GtkTextIter start, end;

	current_settings_clear (inout_settings);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->disabled_radio_button)))
		inout_settings->status = E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->enabled_radio_button)))
		inout_settings->status = E_M365_AUTOMATIC_REPLIES_STATUS_ALWAYS_ENABLED;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->scheduled_radio_button)))
		inout_settings->status = E_M365_AUTOMATIC_REPLIES_STATUS_SCHEDULED;
	else
		inout_settings->status = E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED;

	inout_settings->scheduled_start_date_time = e_date_edit_get_time (E_DATE_EDIT (self->start_time));
	inout_settings->scheduled_end_date_time = e_date_edit_get_time (E_DATE_EDIT (self->end_time));

	gtk_text_buffer_get_bounds (self->internal_reply, &start, &end);
	inout_settings->internal_reply_message = gtk_text_buffer_get_text (self->internal_reply, &start, &end, FALSE);

	gtk_text_buffer_get_bounds (self->external_reply, &start, &end);
	inout_settings->external_reply_message = gtk_text_buffer_get_text (self->external_reply, &start, &end, FALSE);

	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (self->external_audience))) {
	default:
	case 0:
		inout_settings->external_audience = E_M365_EXTERNAL_AUDIENCE_SCOPE_NONE;
		break;
	case 1:
		inout_settings->external_audience = E_M365_EXTERNAL_AUDIENCE_SCOPE_CONTACTS_ONLY;
		break;
	case 2:
		inout_settings->external_audience = E_M365_EXTERNAL_AUDIENCE_SCOPE_ALL;
		break;
	}
}

static void
mail_config_m365_ooo_page_set_datetime (EDateEdit *date_edit,
					EM365DateTimeWithZone *dtz)
{
	GDateTime *dt;
	const gchar *zone;

	if (!dtz) {
		e_date_edit_set_time (date_edit, (time_t) -1);
		return;
	}

	zone = e_m365_date_time_get_time_zone (dtz);
	if (zone && *zone) {
		GTimeZone *tz;
		GDateTime *tmp;

		tz = g_time_zone_new_identifier (zone);

		tmp = g_date_time_new_from_unix_utc (e_m365_date_time_get_date_time (dtz));

		if (tmp) {
			dt = g_date_time_to_timezone (tmp, tz);
			g_date_time_unref (tmp);
		} else {
			dt = NULL;
		}

		g_time_zone_unref (tz);
	} else {
		dt = g_date_time_new_from_unix_local (e_m365_date_time_get_date_time (dtz));
	}

	if (!dt) {
		e_date_edit_set_time (date_edit, (time_t) -1);
		return;
	}

	e_date_edit_set_time (date_edit, (time_t) g_date_time_to_unix (dt));

	g_date_time_unref (dt);
}

static void
mail_config_m365_ooo_page_display_settings (EMailConfigM365OooPage *self,
					    EM365AutomaticRepliesSetting *setting)
{
	GtkWidget *button;

	switch (e_m365_automatic_replies_setting_get_status (setting)) {
	default:
	case E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED:
		button = self->disabled_radio_button;
		break;
	case E_M365_AUTOMATIC_REPLIES_STATUS_ALWAYS_ENABLED:
		button = self->enabled_radio_button;
		break;
	case E_M365_AUTOMATIC_REPLIES_STATUS_SCHEDULED:
		button = self->scheduled_radio_button;
		break;
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

	gtk_combo_box_set_active (
		GTK_COMBO_BOX (self->external_audience),
		e_m365_automatic_replies_setting_get_external_audience (setting) - E_M365_EXTERNAL_AUDIENCE_SCOPE_NONE);

	mail_config_m365_ooo_page_set_datetime (E_DATE_EDIT (self->start_time),
		e_m365_automatic_replies_setting_get_scheduled_start_date_time (setting));

	mail_config_m365_ooo_page_set_datetime (E_DATE_EDIT (self->end_time),
		e_m365_automatic_replies_setting_get_scheduled_end_date_time (setting));

	gtk_text_buffer_set_text (
		self->internal_reply,
		e_m365_automatic_replies_setting_get_internal_reply_message (setting), -1);

	gtk_text_buffer_set_text (
		self->external_reply,
		e_m365_automatic_replies_setting_get_external_reply_message (setting), -1);

	mail_config_m365_ooo_page_read_current_settings (self, &self->current_settings);
}

static void
mail_config_m365_ooo_page_set_account_source (EMailConfigM365OooPage *self,
					      ESource *account_source)
{
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (self->account_source == NULL);

	self->account_source = g_object_ref (account_source);
}

static void
mail_config_m365_ooo_page_set_collection_source (EMailConfigM365OooPage *self,
						 ESource *collection_source)
{
	g_return_if_fail (E_IS_SOURCE (collection_source));
	g_return_if_fail (self->collection_source == NULL);

	self->collection_source = g_object_ref (collection_source);
}

static void
mail_config_m365_ooo_page_set_identity_source (EMailConfigM365OooPage *self,
					       ESource *identity_source)
{
	g_return_if_fail (E_IS_SOURCE (identity_source));
	g_return_if_fail (self->identity_source == NULL);

	self->identity_source = g_object_ref (identity_source);
}

static void
mail_config_m365_ooo_page_set_registry (EMailConfigM365OooPage *self,
					ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (self->registry == NULL);

	self->registry = g_object_ref (registry);
}

static void
mail_config_m365_ooo_page_set_property (GObject *object,
					guint property_id,
					const GValue *value,
					GParamSpec *pspec)
{
	switch (property_id) {
	case PROP_ACCOUNT_SOURCE:
		mail_config_m365_ooo_page_set_account_source (
			E_MAIL_CONFIG_M365_OOO_PAGE (object),
			g_value_get_object (value));
		return;

	case PROP_COLLECTION_SOURCE:
		mail_config_m365_ooo_page_set_collection_source (
			E_MAIL_CONFIG_M365_OOO_PAGE (object),
			g_value_get_object (value));
		return;

	case PROP_IDENTITY_SOURCE:
		mail_config_m365_ooo_page_set_identity_source (
			E_MAIL_CONFIG_M365_OOO_PAGE (object),
			g_value_get_object (value));
		return;

	case PROP_REGISTRY:
		mail_config_m365_ooo_page_set_registry (
			E_MAIL_CONFIG_M365_OOO_PAGE (object),
			g_value_get_object (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_m365_ooo_page_get_property (GObject *object,
					guint property_id,
					GValue *value,
					GParamSpec *pspec)
{
	switch (property_id) {
	case PROP_ACCOUNT_SOURCE:
		g_value_set_object (value, E_MAIL_CONFIG_M365_OOO_PAGE (object)->account_source);
		return;

	case PROP_COLLECTION_SOURCE:
		g_value_set_object (value, E_MAIL_CONFIG_M365_OOO_PAGE (object)->collection_source);
		return;

	case PROP_IDENTITY_SOURCE:
		g_value_set_object (value, E_MAIL_CONFIG_M365_OOO_PAGE (object)->identity_source);
		return;

	case PROP_REGISTRY:
		g_value_set_object (value, E_MAIL_CONFIG_M365_OOO_PAGE (object)->registry);
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_m365_ooo_page_dispose (GObject *object)
{
	EMailConfigM365OooPage *self = E_MAIL_CONFIG_M365_OOO_PAGE (object);

	if (self->refresh_cancellable) {
		g_cancellable_cancel (self->refresh_cancellable);
		g_clear_object (&self->refresh_cancellable);
	}

	g_clear_object (&self->registry);
	g_clear_object (&self->account_source);
	g_clear_object (&self->collection_source);
	g_clear_object (&self->identity_source);
	g_clear_object (&self->cnc);

	current_settings_clear (&self->current_settings);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_mail_config_m365_ooo_page_parent_class)->dispose (object);
}

static void
mail_config_m365_ooo_page_constructed (GObject *object)
{
	EMailConfigM365OooPage *self;
	GtkLabel *label;
	GtkWidget *grid;
	GtkWidget *widget;
	GtkWidget *container;
	GtkWidget *main_box;
	GtkSizeGroup *size_group;
	GtkTextBuffer *text_buffer;
	GSList *group = NULL;
	const gchar *text;
	gchar *markup;

	self = E_MAIL_CONFIG_M365_OOO_PAGE (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_mail_config_m365_ooo_page_parent_class)->constructed (object);

	main_box = e_mail_config_activity_page_get_internal_box (E_MAIL_CONFIG_ACTIVITY_PAGE (self));
	gtk_box_set_spacing (GTK_BOX (main_box), 12);

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	text = _("Out of Office");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (main_box), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (main_box), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	text = _("The messages specified below will be automatically sent to "
		 "each internal and external person who sends a mail to you.");
	widget = gtk_label_new (text);
	gtk_label_set_width_chars (GTK_LABEL (widget), 20);
	gtk_label_set_max_width_chars (GTK_LABEL (widget), 80);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	text = _("Do _not send Out of Office replies");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	self->disabled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));

	text = _("_Send Out of Office replies");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	self->enabled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));

	text = _("Send Out of Office replies only _during this time period:");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	self->scheduled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (main_box), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->disabled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE |
		G_BINDING_INVERT_BOOLEAN);

	grid = widget;

	widget = gtk_label_new_with_mnemonic (_("_From:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (grid), widget, 0, 1, 1, 1);
	gtk_size_group_add_widget (size_group, widget);
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	label = GTK_LABEL (widget);

	widget = e_date_edit_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 1, 1, 1);
	self->start_time = widget;  /* do not reference */
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_label_new_with_mnemonic (_("_To:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (grid), widget, 0, 2, 1, 1);
	gtk_size_group_add_widget (size_group, widget);
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	label = GTK_LABEL (widget);

	widget = e_date_edit_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 2, 1, 1);
	self->end_time = widget;  /* do not reference */
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_label_new_with_mnemonic (_("I_nternal:"));
	gtk_widget_set_margin_top (widget, 12);
	gtk_widget_set_margin_left (widget, 12);
	gtk_widget_set_valign (widget, GTK_ALIGN_START);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (grid), widget, 0, 3, 1, 1);
	gtk_size_group_add_widget (size_group, widget);
	gtk_widget_show (widget);

	text = _("Message to be sent within the organization");
	gtk_widget_set_tooltip_text (widget, text);

	label = GTK_LABEL (widget);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_widget_set_margin_top (widget, 12);
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_widget_set_vexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 3, 1, 1);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_text_view_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (widget), GTK_WRAP_WORD);
	gtk_container_add (GTK_CONTAINER (container), widget);
	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
	self->internal_reply = text_buffer;  /* do not reference */
	e_spell_text_view_attach (GTK_TEXT_VIEW (widget));
	gtk_widget_show (widget);

	widget = gtk_label_new_with_mnemonic (_("E_xternal:"));
	gtk_widget_set_margin_top (widget, 12);
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (grid), widget, 0, 4, 1, 1);
	gtk_size_group_add_widget (size_group, widget);
	gtk_widget_show (widget);

	text = _("Message to be sent outside the organization");
	gtk_widget_set_tooltip_text (widget, text);

	label = GTK_LABEL (widget);

	widget = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (
		GTK_COMBO_BOX_TEXT (widget),
		_("Do not reply to senders outside the organization"));
	gtk_combo_box_text_append_text (
		GTK_COMBO_BOX_TEXT (widget),
		_("Reply only to known senders outside the organization"));
	gtk_combo_box_text_append_text (
		GTK_COMBO_BOX_TEXT (widget),
		_("Reply to any sender outside the organization"));
	gtk_widget_set_margin_top (widget, 12);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 4, 1, 1);
	self->external_audience = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (
		GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_widget_set_vexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 5, 1, 1);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_text_view_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (widget), GTK_WRAP_WORD);
	gtk_container_add (GTK_CONTAINER (container), widget);
	text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
	self->external_reply = text_buffer;  /* do not reference */
	e_spell_text_view_attach (GTK_TEXT_VIEW (widget));
	gtk_widget_show (widget);

	e_binding_bind_property (
		self->external_audience, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	g_object_unref (size_group);

	e_mail_config_page_set_content (E_MAIL_CONFIG_PAGE (self), main_box);

	e_mail_config_m365_ooo_page_refresh (self);
}

static void
mail_config_m365_ooo_page_submit_thread (GTask *task,
					 gpointer source_object,
					 gpointer task_data,
					 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	JsonBuilder *builder;
	GError *local_error = NULL;

	builder = json_builder_new_immutable ();

	e_m365_begin_mailbox_settings (builder);
	e_m365_begin_automatic_replies_setting (builder);
	e_m365_automatic_replies_setting_add_status (builder, async_context->settings.status);
	e_m365_automatic_replies_setting_add_external_audience (builder, async_context->settings.external_audience);
	e_m365_automatic_replies_setting_add_external_reply_message (builder, async_context->settings.external_reply_message);
	e_m365_automatic_replies_setting_add_internal_reply_message (builder, async_context->settings.internal_reply_message);
	if (async_context->settings.scheduled_start_date_time != (time_t) -1)
		e_m365_automatic_replies_setting_add_scheduled_start_date_time (builder, async_context->settings.scheduled_start_date_time, "UTC");
	if (async_context->settings.scheduled_end_date_time != (time_t) -1)
		e_m365_automatic_replies_setting_add_scheduled_end_date_time (builder, async_context->settings.scheduled_end_date_time, "UTC");
	e_m365_end_automatic_replies_setting (builder);
	e_m365_end_mailbox_settings (builder);

	if (e_m365_connection_update_mailbox_settings_sync (async_context->cnc, NULL, builder, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, local_error);

	g_clear_object (&builder);
}

static void
mail_config_m365_ooo_page_submit (EMailConfigPage *page,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	EMailConfigM365OooPage *self;
	AsyncContext *async_context;
	GTask *task;

	self = E_MAIL_CONFIG_M365_OOO_PAGE (page);
	async_context = g_new0 (AsyncContext, 1);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, mail_config_m365_ooo_page_submit);
	g_task_set_task_data (task, async_context, async_context_free);

	mail_config_m365_ooo_page_read_current_settings (self, &async_context->settings);
	async_context->cnc = self->cnc ? g_object_ref (self->cnc) : NULL;

	/* do nothing when user did not change anything */
	if (self->current_settings.status == async_context->settings.status &&
	    self->current_settings.external_audience == async_context->settings.external_audience &&
	    self->current_settings.scheduled_start_date_time == async_context->settings.scheduled_start_date_time &&
	    self->current_settings.scheduled_end_date_time == async_context->settings.scheduled_end_date_time &&
	    g_strcmp0 (self->current_settings.external_reply_message, async_context->settings.external_reply_message) == 0 &&
	    g_strcmp0 (self->current_settings.internal_reply_message, async_context->settings.internal_reply_message) == 0) {
		g_task_return_boolean (task, TRUE);
	} else {
		/* cannot save without connection; as the connection is set in refresh(),
		   the refresh possibly failed or is not done yet, then the dialog shows
		   incomplete information, thus nothing to save to the server */
		if (!async_context->cnc)
			g_task_return_boolean (task, TRUE);
		else
			g_task_run_in_thread (task, mail_config_m365_ooo_page_submit_thread);
	}

	g_object_unref (task);
}

static gboolean
mail_config_m365_ooo_page_submit_finish (EMailConfigPage *page,
					 GAsyncResult *result,
					 GError **error)
{
	EMailConfigM365OooPage *self;

	if (!g_task_propagate_boolean (G_TASK (result), error))
		return FALSE;

	self = E_MAIL_CONFIG_M365_OOO_PAGE (page);

	/* read back what had been just saved to the server */
	mail_config_m365_ooo_page_read_current_settings (self, &self->current_settings);

	return TRUE;
}

static void
mail_config_m365_ooo_page_refresh_thread (GTask *task,
					  gpointer source_object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (e_m365_connection_get_automatic_replies_setting_sync (async_context->cnc, NULL, &async_context->display_setting, cancellable, &local_error)) {
		g_task_return_boolean (task, TRUE);
	} else if (local_error) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, FALSE);
	}
}

static void
mail_config_m365_ooo_page_refresh_done (GObject *source_object,
					GAsyncResult *result,
					gpointer user_data)
{
	AsyncContext *async_context;
	GTask *task;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_MAIL_CONFIG_M365_OOO_PAGE (source_object));
	g_return_if_fail (G_IS_TASK (result));

	task = G_TASK (result);
	async_context = g_task_get_task_data (task);

	if (g_task_propagate_boolean (task, &local_error)) {
		EMailConfigM365OooPage *self = E_MAIL_CONFIG_M365_OOO_PAGE (source_object);

		mail_config_m365_ooo_page_display_settings (self, async_context->display_setting);

		/* remember the connection, as a sign the information could be read */
		g_clear_object (&self->cnc);
		self->cnc = g_steal_pointer (&async_context->cnc);
	} else if (!e_activity_handle_cancellation (async_context->activity, local_error) && local_error) {
		EAlertSink *alert_sink;
		gchar *secondary;

		secondary = g_strdup_printf (_("The reported error was “%s”."), local_error->message);

		alert_sink = e_activity_get_alert_sink (async_context->activity);
		e_alert_submit (alert_sink, "system:generic-warning",
			_("Failed to retrieve “Out of Office” settings."),
			secondary, NULL);

		g_free (secondary);
	}

	g_clear_error (&local_error);
}

static void
e_mail_config_m365_ooo_page_class_init (EMailConfigM365OooPageClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = mail_config_m365_ooo_page_set_property;
	object_class->get_property = mail_config_m365_ooo_page_get_property;
	object_class->dispose = mail_config_m365_ooo_page_dispose;
	object_class->constructed = mail_config_m365_ooo_page_constructed;

	g_object_class_install_property (
		object_class,
		PROP_ACCOUNT_SOURCE,
		g_param_spec_object (
			"account-source",
			"Account Source",
			"Mail account source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_COLLECTION_SOURCE,
		g_param_spec_object (
			"collection-source",
			"Collection Source",
			"Collection source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY_SOURCE,
		g_param_spec_object (
			"identity-source",
			"Identity Source",
			"Mail identity source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_mail_config_m365_ooo_page_interface_init (EMailConfigPageInterface *iface)
{
	iface->title = _("Out of Office");
	iface->sort_order = E_MAIL_CONFIG_M365_OOO_PAGE_SORT_ORDER;
	iface->submit = mail_config_m365_ooo_page_submit;
	iface->submit_finish = mail_config_m365_ooo_page_submit_finish;
}

static void
e_mail_config_m365_ooo_page_class_finalize (EMailConfigM365OooPageClass *klass)
{
}

static void
e_mail_config_m365_ooo_page_init (EMailConfigM365OooPage *page)
{
}

void
e_mail_config_m365_ooo_page_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_m365_ooo_page_register_type (type_module);
}

EMailConfigPage *
e_mail_config_m365_ooo_page_new (ESourceRegistry *registry,
                                ESource *account_source,
                                ESource *identity_source,
                                ESource *collection_source)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (account_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (identity_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (collection_source), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_M365_OOO_PAGE,
		"registry", registry,
		"account-source", account_source,
		"identity-source", identity_source,
		"collection-source", collection_source,
		NULL);
}

void
e_mail_config_m365_ooo_page_refresh (EMailConfigM365OooPage *self)
{
	GTask *task;
	ESource *source;
	EActivity *activity;
	GCancellable *cancellable;
	CamelSettings *settings;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_MAIL_CONFIG_M365_OOO_PAGE (self));

	source = self->collection_source;

	if (self->refresh_cancellable) {
		g_cancellable_cancel (self->refresh_cancellable);
		g_clear_object (&self->refresh_cancellable);
	}

	activity = e_mail_config_activity_page_new_activity (E_MAIL_CONFIG_ACTIVITY_PAGE (self));
	cancellable = e_activity_get_cancellable (activity);
	self->refresh_cancellable = g_object_ref (cancellable);

	e_activity_set_text (activity, _("Retrieving “Out of Office” settings"));

	settings = mail_config_m365_ooo_page_get_settings (self);

	async_context = g_new0 (AsyncContext, 1);
	async_context->activity = activity;  /* takes ownership */
	async_context->cnc = e_m365_connection_new_full (source, CAMEL_M365_SETTINGS (settings), TRUE);

	task = g_task_new (self, cancellable, mail_config_m365_ooo_page_refresh_done, NULL);
	g_task_set_source_tag (task, e_mail_config_m365_ooo_page_refresh);
	g_task_set_task_data (task, async_context, async_context_free);

	g_task_run_in_thread (task, mail_config_m365_ooo_page_refresh_thread);

	g_object_unref (task);
}
