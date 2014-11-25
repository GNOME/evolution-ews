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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-config-ews-ooo-page.h"

#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <e-util/e-util.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"
#include "server/e-ews-connection-utils.h"
#include "server/e-ews-oof-settings.h"

#define E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE, EMailConfigEwsOooPagePrivate))

typedef struct _AsyncContext AsyncContext;

struct _EMailConfigEwsOooPagePrivate {
	ESourceRegistry *registry;
	ESource *account_source;
	ESource *identity_source;
	ESource *collection_source;

	/* The try_password() method deposits results here.
	 * This avoids calling GTK+ functions from multiple threads. */
	EEwsOofSettings *oof_settings;
	GMutex oof_settings_lock;

	GCancellable *refresh_cancellable;

	/* to not save unchanged state */
	gboolean changed;

	GtkWidget *enabled_radio_button;	/* not referenced */
	GtkWidget *disabled_radio_button;	/* not referenced */
	GtkWidget *scheduled_radio_button;	/* not referenced */
	GtkWidget *start_time;			/* not referenced */
	GtkWidget *end_time;			/* not referenced */
	GtkWidget *external_audience;		/* not referenced */
	GtkTextBuffer *internal_reply;		/* not referenced */
	GtkTextBuffer *external_reply;		/* not referenced */
};

struct _AsyncContext {
	EMailConfigEwsOooPage *page;
	EActivity *activity;
};

enum {
	PROP_0,
	PROP_ACCOUNT_SOURCE,
	PROP_COLLECTION_SOURCE,
	PROP_IDENTITY_SOURCE,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_mail_config_ews_ooo_page_interface_init
				(EMailConfigPageInterface *iface);
static void	e_mail_config_ews_ooo_page_authenticator_init
				(ESourceAuthenticatorInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailConfigEwsOooPage,
	e_mail_config_ews_ooo_page,
	E_TYPE_MAIL_CONFIG_ACTIVITY_PAGE,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_MAIL_CONFIG_PAGE,
		e_mail_config_ews_ooo_page_interface_init)
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_mail_config_ews_ooo_page_authenticator_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->page != NULL)
		g_object_unref (async_context->page);

	if (async_context->activity != NULL)
		g_object_unref (async_context->activity);

	g_slice_free (AsyncContext, async_context);
}

static CamelSettings *
mail_config_ews_ooo_page_get_settings (EMailConfigEwsOooPage *page)
{
	ESource *source;
	ESourceCamel *extension;
	const gchar *extension_name;

	source = e_mail_config_ews_ooo_page_get_collection_source (page);

	extension_name = e_source_camel_get_extension_name ("ews");
	extension = e_source_get_extension (source, extension_name);

	return e_source_camel_get_settings (extension);
}

static const gchar *
mail_config_ews_ooo_page_get_mailbox (EMailConfigEwsOooPage *page)
{
	ESourceRegistry *registry;
	ESource *collection_source;
	GList *list, *link;
	const gchar *collection_uid;
	const gchar *extension_name;
	const gchar *mailbox = NULL;

	/* Find the mail identity source that belongs to
	 * our collection and return its email address. */

	collection_source =
		e_mail_config_ews_ooo_page_get_collection_source (page);
	collection_uid = e_source_get_uid (collection_source);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	registry = e_mail_config_ews_ooo_page_get_registry (page);
	list = e_source_registry_list_sources (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);
		ESourceMailIdentity *extension;
		const gchar *parent_uid;

		parent_uid = e_source_get_parent (source);
		if (g_strcmp0 (parent_uid, collection_uid) != 0)
			continue;

		extension = e_source_get_extension (source, extension_name);
		mailbox = e_source_mail_identity_get_address (extension);
		break;
	}

	g_list_free_full (list, g_object_unref);

	return mailbox;
}

static void
mail_config_ews_ooo_page_display_settings (EMailConfigEwsOooPage *page,
                                           EEwsOofSettings *oof_settings)
{
	GtkWidget *button;
	GDateTime *date_time;

	switch (e_ews_oof_settings_get_state (oof_settings)) {
		default:
			/* fall through */
		case E_EWS_OOF_STATE_DISABLED:
			button = page->priv->disabled_radio_button;
			break;
		case E_EWS_OOF_STATE_ENABLED:
			button = page->priv->enabled_radio_button;
			break;
		case E_EWS_OOF_STATE_SCHEDULED:
			button = page->priv->scheduled_radio_button;
			break;
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

	gtk_combo_box_set_active (
		GTK_COMBO_BOX (page->priv->external_audience),
		e_ews_oof_settings_get_external_audience (oof_settings));

	date_time = e_ews_oof_settings_ref_start_time (oof_settings);
	e_date_edit_set_time (
		E_DATE_EDIT (page->priv->start_time),
		(time_t) g_date_time_to_unix (date_time));
	g_date_time_unref (date_time);

	date_time = e_ews_oof_settings_ref_end_time (oof_settings);
	e_date_edit_set_time (
		E_DATE_EDIT (page->priv->end_time),
		(time_t) g_date_time_to_unix (date_time));
	g_date_time_unref (date_time);

	gtk_text_buffer_set_text (
		page->priv->internal_reply,
		e_ews_oof_settings_get_internal_reply (oof_settings), -1);

	gtk_text_buffer_set_text (
		page->priv->external_reply,
		e_ews_oof_settings_get_external_reply (oof_settings), -1);
}

static void
mail_config_ews_ooo_page_refresh_cb (GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data)
{
	ESourceRegistry *registry;
	AsyncContext *async_context;
	EAlertSink *alert_sink;
	GError *error = NULL;

	registry = E_SOURCE_REGISTRY (source_object);
	async_context = (AsyncContext *) user_data;

	alert_sink = e_activity_get_alert_sink (async_context->activity);

	e_source_registry_authenticate_finish (registry, result, &error);

	if (e_activity_handle_cancellation (async_context->activity, error)) {
		g_error_free (error);

	} else if (error != NULL) {
		e_alert_submit (
			alert_sink,
			"ews:query-ooo-error",
			error->message, NULL);
		g_error_free (error);

	} else {
		EMailConfigEwsOooPage *page = async_context->page;

		g_mutex_lock (&page->priv->oof_settings_lock);

		if (page->priv->oof_settings != NULL)
			mail_config_ews_ooo_page_display_settings (
				page, page->priv->oof_settings);

		g_mutex_unlock (&page->priv->oof_settings_lock);
	}

	async_context_free (async_context);
}

static void
mail_config_ews_ooo_page_set_account_source (EMailConfigEwsOooPage *page,
                                             ESource *account_source)
{
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (page->priv->account_source == NULL);

	page->priv->account_source = g_object_ref (account_source);
}

static void
mail_config_ews_ooo_page_set_collection_source (EMailConfigEwsOooPage *page,
                                                ESource *collection_source)
{
	g_return_if_fail (E_IS_SOURCE (collection_source));
	g_return_if_fail (page->priv->collection_source == NULL);

	page->priv->collection_source = g_object_ref (collection_source);
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
mail_config_ews_ooo_page_set_registry (EMailConfigEwsOooPage *page,
                                       ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (page->priv->registry == NULL);

	page->priv->registry = g_object_ref (registry);
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

		case PROP_COLLECTION_SOURCE:
			mail_config_ews_ooo_page_set_collection_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_IDENTITY_SOURCE:
			mail_config_ews_ooo_page_set_identity_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_REGISTRY:
			mail_config_ews_ooo_page_set_registry (
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

		case PROP_COLLECTION_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_ooo_page_get_collection_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object)));
			return;

		case PROP_IDENTITY_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_ooo_page_get_identity_source (
				E_MAIL_CONFIG_EWS_OOO_PAGE (object)));
			return;

		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_mail_config_ews_ooo_page_get_registry (
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

	if (priv->refresh_cancellable) {
		g_cancellable_cancel (priv->refresh_cancellable);
		g_clear_object (&priv->refresh_cancellable);
	}

	g_clear_object (&priv->registry);
	g_clear_object (&priv->account_source);
	g_clear_object (&priv->collection_source);
	g_clear_object (&priv->identity_source);
	g_clear_object (&priv->oof_settings);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_ooo_page_parent_class)->
		dispose (object);
}

static void
mail_config_ews_ooo_page_finalize (GObject *object)
{
	EMailConfigEwsOooPagePrivate *priv;

	priv = E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE (object);

	g_mutex_clear (&priv->oof_settings_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_mail_config_ews_ooo_page_parent_class)->
		finalize (object);
}

static void
mail_config_ews_ooo_page_constructed (GObject *object)
{
	EMailConfigEwsOooPage *page;
	GtkLabel *label;
	GtkWidget *grid;
	GtkWidget *widget;
	GtkWidget *container;
	GtkSizeGroup *size_group;
	GtkTextBuffer *text_buffer;
	GSList *group = NULL;
	const gchar *text;
	gchar *markup;

	page = E_MAIL_CONFIG_EWS_OOO_PAGE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_ooo_page_parent_class)->constructed (object);

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_box_set_spacing (GTK_BOX (page), 12);

	text = _("Out of Office");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (page), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (page), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	text = _("The messages specified below will be automatically sent to "
		 "each internal and external person who sends a mail to you.");
	widget = gtk_label_new (text);
	gtk_label_set_max_width_chars (GTK_LABEL (widget), 80);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	text = _("Do _not send Out of Office replies");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	page->priv->disabled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));

	text = _("_Send Out of Office replies");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	page->priv->enabled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));

	text = _("Send Out of Office replies only _during this time period:");
	widget = gtk_radio_button_new_with_mnemonic (group, text);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	page->priv->scheduled_radio_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (page), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);

	g_object_bind_property (
		page->priv->disabled_radio_button, "active",
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

	g_object_bind_property (
		page->priv->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	label = GTK_LABEL (widget);

	widget = e_date_edit_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 1, 1, 1);
	page->priv->start_time = widget;  /* do not reference */
	gtk_widget_show (widget);

	g_object_bind_property (
		page->priv->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_label_new_with_mnemonic (_("_To:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (grid), widget, 0, 2, 1, 1);
	gtk_size_group_add_widget (size_group, widget);
	gtk_widget_show (widget);

	g_object_bind_property (
		page->priv->scheduled_radio_button, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	label = GTK_LABEL (widget);

	widget = e_date_edit_new ();
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (grid), widget, 1, 2, 1, 1);
	page->priv->end_time = widget;  /* do not reference */
	gtk_widget_show (widget);

	g_object_bind_property (
		page->priv->scheduled_radio_button, "active",
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
	page->priv->internal_reply = text_buffer;  /* do not reference */
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

	/* The order of the combo box items needs to stay
	 * syncrhonized with the EEwsExternalAudience enum. */
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
	page->priv->external_audience = widget;  /* do not reference */
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
	page->priv->external_reply = text_buffer;  /* do not reference */
	e_spell_text_view_attach (GTK_TEXT_VIEW (widget));
	gtk_widget_show (widget);

	/* XXX Bit of a hack.  Since the enum value for "none" is zero,
	 *     sensitize the text view if the combo box has a non-zero
	 *     "active" value (in other words, anything but "none"). */
	g_object_bind_property (
		page->priv->external_audience, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	g_object_unref (size_group);

	e_mail_config_ews_ooo_page_refresh (page);
}

/* Helper for mail_config_ews_ooo_page_submit() */
static void
mail_config_ews_ooo_page_submit_cb (GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	e_ews_oof_settings_submit_finish (
		E_EWS_OOF_SETTINGS (source_object), result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

static void
ews_oof_settings_changed (gboolean *pchanged)
{
	g_return_if_fail (pchanged != NULL);

	*pchanged = TRUE;
}

static void
mail_config_ews_ooo_page_submit (EMailConfigPage *page,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	EMailConfigEwsOooPagePrivate *priv;
	GSimpleAsyncResult *simple;
	GtkToggleButton *toggle_button;
	GtkTextBuffer *text_buffer;
	GtkTextIter start, end;
	GDateTime *date_time;
	gchar *text;
	time_t tm;
	gulong signal_id;

	priv = E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE (page);

	g_mutex_lock (&priv->oof_settings_lock);

	/* It may be that the Out of Office settings are still
	 * loading or have failed to load, in which case there
	 * are obviously no changes to submit. */
	if (priv->oof_settings == NULL) {
		g_mutex_unlock (&priv->oof_settings_lock);

		simple = g_simple_async_result_new (
			G_OBJECT (page), callback, user_data,
			mail_config_ews_ooo_page_submit);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);

		return;
	}

	signal_id = g_signal_connect_swapped (
		priv->oof_settings, "notify",
		G_CALLBACK (ews_oof_settings_changed), &priv->changed);

	toggle_button = GTK_TOGGLE_BUTTON (priv->enabled_radio_button);
	if (gtk_toggle_button_get_active (toggle_button))
		e_ews_oof_settings_set_state (
			priv->oof_settings,
			E_EWS_OOF_STATE_ENABLED);

	toggle_button = GTK_TOGGLE_BUTTON (priv->disabled_radio_button);
	if (gtk_toggle_button_get_active (toggle_button))
		e_ews_oof_settings_set_state (
			priv->oof_settings,
			E_EWS_OOF_STATE_DISABLED);

	toggle_button = GTK_TOGGLE_BUTTON (priv->scheduled_radio_button);
	if (gtk_toggle_button_get_active (toggle_button))
		e_ews_oof_settings_set_state (
			priv->oof_settings,
			E_EWS_OOF_STATE_SCHEDULED);

	tm = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	date_time = g_date_time_new_from_unix_utc ((gint64) tm);
	e_ews_oof_settings_set_start_time (priv->oof_settings, date_time);
	g_date_time_unref (date_time);

	tm = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));
	date_time = g_date_time_new_from_unix_utc ((gint64) tm);
	e_ews_oof_settings_set_end_time (priv->oof_settings, date_time);
	g_date_time_unref (date_time);

	text_buffer = priv->internal_reply;
	gtk_text_buffer_get_bounds (text_buffer, &start, &end);
	text = gtk_text_buffer_get_text (text_buffer, &start, &end, FALSE);
	e_ews_oof_settings_set_internal_reply (priv->oof_settings, text);
	g_free (text);

	e_ews_oof_settings_set_external_audience (priv->oof_settings,
		gtk_combo_box_get_active (GTK_COMBO_BOX (priv->external_audience)));

	text_buffer = priv->external_reply;
	gtk_text_buffer_get_bounds (text_buffer, &start, &end);
	text = gtk_text_buffer_get_text (text_buffer, &start, &end, FALSE);
	e_ews_oof_settings_set_external_reply (priv->oof_settings, text);
	g_free (text);

	simple = g_simple_async_result_new (
		G_OBJECT (page), callback, user_data,
		mail_config_ews_ooo_page_submit);

	g_signal_handler_disconnect (priv->oof_settings, signal_id);

	if (priv->changed) {
		g_simple_async_result_set_check_cancellable (simple, cancellable);

		e_ews_oof_settings_submit (
			priv->oof_settings, cancellable,
			mail_config_ews_ooo_page_submit_cb,
			g_object_ref (simple));
	} else {
		g_simple_async_result_complete (simple);
	}

	g_object_unref (simple);

	g_mutex_unlock (&priv->oof_settings_lock);
}

static gboolean
mail_config_ews_ooo_page_submit_finish (EMailConfigPage *page,
                                        GAsyncResult *result,
                                        GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (page),
		mail_config_ews_ooo_page_submit), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
mail_config_ews_ooo_page_get_without_password (ESourceAuthenticator *auth)
{
	EMailConfigEwsOooPage *page;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;

	page = E_MAIL_CONFIG_EWS_OOO_PAGE (auth);
	settings = mail_config_ews_ooo_page_get_settings (page);
	ews_settings = CAMEL_EWS_SETTINGS (settings);

	return e_ews_connection_utils_get_without_password (ews_settings);
}

static ESourceAuthenticationResult
mail_config_ews_ooo_page_try_password_sync (ESourceAuthenticator *auth,
                                            const GString *password,
                                            GCancellable *cancellable,
                                            GError **error)
{
	EMailConfigEwsOooPage *page;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	ESourceAuthenticationResult result;
	EEwsConnection *connection;
	EEwsOofSettings *oof_settings;
	const gchar *hosturl;
	const gchar *mailbox;
	GError *local_error = NULL;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return E_SOURCE_AUTHENTICATION_ERROR;

	page = E_MAIL_CONFIG_EWS_OOO_PAGE (auth);
	mailbox = mail_config_ews_ooo_page_get_mailbox (page);
	settings = mail_config_ews_ooo_page_get_settings (page);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_get_hosturl (ews_settings);

	connection = e_ews_connection_new (hosturl, ews_settings);
	e_ews_connection_set_password (connection, password->str);

	e_ews_connection_set_mailbox (connection, mailbox);

	oof_settings = e_ews_oof_settings_new_sync (
		connection, cancellable, &local_error);

	g_object_unref (connection);

	if (oof_settings != NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
		g_warn_if_fail (local_error == NULL);

		/* The page takes ownership of the settings. */
		g_mutex_lock (&page->priv->oof_settings_lock);
		if (page->priv->oof_settings != NULL)
			g_object_unref (oof_settings);
		page->priv->oof_settings = oof_settings;
		page->priv->changed = FALSE;
		g_mutex_unlock (&page->priv->oof_settings_lock);

	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_error_free (local_error);

	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	return result;
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
	object_class->finalize = mail_config_ews_ooo_page_finalize;
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
e_mail_config_ews_ooo_page_interface_init (EMailConfigPageInterface *iface)
{
	iface->title = _("Out of Office");
	iface->sort_order = E_MAIL_CONFIG_EWS_OOO_PAGE_SORT_ORDER;
	iface->submit = mail_config_ews_ooo_page_submit;
	iface->submit_finish = mail_config_ews_ooo_page_submit_finish;
}

static void
e_mail_config_ews_ooo_page_authenticator_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password =
		mail_config_ews_ooo_page_get_without_password;
	iface->try_password_sync =
		mail_config_ews_ooo_page_try_password_sync;
}

static void
e_mail_config_ews_ooo_page_class_finalize (EMailConfigEwsOooPageClass *class)
{
}

static void
e_mail_config_ews_ooo_page_init (EMailConfigEwsOooPage *page)
{
	page->priv = E_MAIL_CONFIG_EWS_OOO_PAGE_GET_PRIVATE (page);

	g_mutex_init (&page->priv->oof_settings_lock);
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
e_mail_config_ews_ooo_page_new (ESourceRegistry *registry,
                                ESource *account_source,
                                ESource *identity_source,
                                ESource *collection_source)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (account_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (identity_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (collection_source), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE,
		"registry", registry,
		"account-source", account_source,
		"identity-source", identity_source,
		"collection-source", collection_source,
		NULL);
}

void
e_mail_config_ews_ooo_page_refresh (EMailConfigEwsOooPage *page)
{
	ESourceAuthenticator *authenticator;
	ESourceRegistry *registry;
	ESource *source;
	EActivity *activity;
	GCancellable *cancellable;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_MAIL_CONFIG_EWS_OOO_PAGE (page));

	registry = e_mail_config_ews_ooo_page_get_registry (page);
	source = e_mail_config_ews_ooo_page_get_collection_source (page);
	authenticator = E_SOURCE_AUTHENTICATOR (page);

	if (page->priv->refresh_cancellable) {
		g_cancellable_cancel (page->priv->refresh_cancellable);
		g_clear_object (&page->priv->refresh_cancellable);
	}

	activity = e_mail_config_activity_page_new_activity (
		E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	cancellable = e_activity_get_cancellable (activity);
	page->priv->refresh_cancellable = g_object_ref (cancellable);

	e_activity_set_text (
		activity, _("Retrieving \"Out of Office\" settings"));

	async_context = g_slice_new0 (AsyncContext);
	async_context->page = g_object_ref (page);
	async_context->activity = activity;  /* takes ownership */

	e_source_registry_authenticate (
		registry, source, authenticator, cancellable,
		mail_config_ews_ooo_page_refresh_cb, async_context);
}

ESourceRegistry *
e_mail_config_ews_ooo_page_get_registry (EMailConfigEwsOooPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_OOO_PAGE (page), NULL);

	return page->priv->registry;
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

ESource *
e_mail_config_ews_ooo_page_get_collection_source (EMailConfigEwsOooPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_OOO_PAGE (page), NULL);

	return page->priv->collection_source;
}

