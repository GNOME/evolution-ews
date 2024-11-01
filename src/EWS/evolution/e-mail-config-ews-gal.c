/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-mail-config-ews-gal.h"

#include <glib/gi18n-lib.h>

#include <mail/e-mail-config-provider-page.h>

#include "common/camel-ews-settings.h"

#include "e-mail-config-ews-oal-combo-box.h"

typedef struct _AsyncContext AsyncContext;

struct _EMailConfigEwsGalPrivate {
	GtkWidget *toggle_button;	/* not referenced */
	GtkWidget *combo_box;		/* not referenced */
	GtkWidget *fetch_button;	/* not referenced */
};

struct _AsyncContext {
	EMailConfigEwsGal *extension;
	EActivity *activity;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigEwsGal, e_mail_config_ews_gal, E_TYPE_EXTENSION, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailConfigEwsGal))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->extension != NULL)
		g_object_unref (async_context->extension);

	if (async_context->activity != NULL)
		g_object_unref (async_context->activity);

	g_slice_free (AsyncContext, async_context);
}

static EMailConfigProviderPage *
mail_config_ews_gal_get_provider_page (EMailConfigEwsGal *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_MAIL_CONFIG_PROVIDER_PAGE (extensible);
}

static gboolean
mail_config_ews_gal_string_to_boolean (GBinding *binding,
                                       const GValue *source_value,
                                       GValue *target_value,
                                       gpointer unused)
{
	const gchar *v_string;
	gboolean v_boolean;

	v_string = g_value_get_string (source_value);
	v_boolean = (v_string != NULL && *v_string != '\0');
	g_value_set_boolean (target_value, v_boolean);

	return TRUE;
}

static gboolean
mail_config_ews_gal_oal_selected_to_active_id (GBinding *binding,
                                               const GValue *source_value,
                                               GValue *target_value,
                                               gpointer unused)
{
	GObject *target_object;
	GtkComboBox *combo_box;
	const gchar *selected;
	gchar *active_id;
	gchar *active_text;

	selected = g_value_get_string (source_value);

	if (selected == NULL)
		return FALSE;

	/* Selected value is of the form "id:name". */
	active_id = g_strdup (selected);
	active_text = strrchr (active_id, ':');

	if (active_text != NULL) {
		*active_text++ = '\0';
		while (*active_text == '\\')
			active_text++;
	} else {
		g_free (active_id);
		return FALSE;
	}

	target_object = g_binding_get_target (binding);
	combo_box = GTK_COMBO_BOX (target_object);

	if (g_strcmp0 (active_id, gtk_combo_box_get_active_id (combo_box)) != 0) {
		/* The combo box might already have the OAL ID, in which case
		 * we simply make it the active combo box row.  Otherwise we
		 * have to add a new row and make it the active row. */
		if (!gtk_combo_box_set_active_id (combo_box, active_id)) {
			gtk_combo_box_text_append (
				GTK_COMBO_BOX_TEXT (combo_box),
				active_id, active_text);
			gtk_combo_box_set_active_id (combo_box, active_id);
		}
	}

	g_value_set_string (target_value, active_id);
	g_free (active_id);

	return TRUE;
}

static gboolean
mail_config_ews_gal_active_id_to_oal_selected (GBinding *binding,
                                               const GValue *source_value,
                                               GValue *target_value,
                                               gpointer unused)
{
	GObject *target_object;
	GtkComboBoxText *combo_box;
	const gchar *active_id;
	gchar *active_text;
	gchar *selected = NULL;

	target_object = g_binding_get_target (binding);
	combo_box = GTK_COMBO_BOX_TEXT (target_object);

	active_id = g_value_get_string (source_value);
	active_text = gtk_combo_box_text_get_active_text (combo_box);

	if (active_id != NULL && active_text != NULL)
		selected = g_strdup_printf ("%s:%s", active_id, active_text);

	g_value_set_string (target_value, selected);

	g_free (active_text);
	g_free (selected);

	return TRUE;
}

static void
mail_config_ews_gal_fetch_list_cb (GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
	AsyncContext *async_context = user_data;
	EMailConfigEwsGal *extension;
	EAlertSink *alert_sink;
	GError *error = NULL;

	extension = async_context->extension;
	alert_sink = e_activity_get_alert_sink (async_context->activity);

	e_mail_config_ews_oal_combo_box_update_finish (
		E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (source_object),
		result, &error);

	if (e_activity_handle_cancellation (async_context->activity, error)) {
		g_error_free (error);

	} else if (error != NULL) {
		e_alert_submit (
			alert_sink,
			"ews:query-oal-error",
			error->message, NULL);
		g_error_free (error);
	}

	gtk_widget_set_sensitive (extension->priv->combo_box, TRUE);
	gtk_widget_set_sensitive (extension->priv->fetch_button, TRUE);

	async_context_free (async_context);
}

static void
mail_config_ews_gal_fetch_list (EMailConfigEwsGal *extension)
{
	EActivity *activity;
	EMailConfigProviderPage *page;
	GtkWidget *combo_box;
	GCancellable *cancellable;
	AsyncContext *async_context;

	combo_box = extension->priv->combo_box;

	page = mail_config_ews_gal_get_provider_page (extension);

	activity = e_mail_config_activity_page_new_activity (
		E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	cancellable = e_activity_get_cancellable (activity);

	e_activity_set_text (activity, _("Locating offline address books"));

	gtk_widget_set_sensitive (extension->priv->combo_box, FALSE);
	gtk_widget_set_sensitive (extension->priv->fetch_button, FALSE);

	async_context = g_slice_new0 (AsyncContext);
	async_context->extension = g_object_ref (extension);
	async_context->activity = activity;  /* takes ownership */

	e_mail_config_ews_oal_combo_box_update (
		E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (combo_box),
		cancellable, mail_config_ews_gal_fetch_list_cb,
		async_context);
}

static void
mail_config_ews_gal_fetch_button_clicked_cb (GtkButton *button,
                                             EMailConfigEwsGal *extension)
{
	mail_config_ews_gal_fetch_list (extension);
}

static void
mail_config_ews_gal_constructed (GObject *object)
{
	EMailConfigEwsGal *extension;
	EMailConfigProviderPage *page;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	GtkWidget *container;
	GtkWidget *widget;
	GtkLabel *label;
	const gchar *text;
	gchar *markup;

	extension = E_MAIL_CONFIG_EWS_GAL (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_gal_parent_class)->constructed (object);

	page = mail_config_ews_gal_get_provider_page (extension);
	backend = e_mail_config_provider_page_get_backend (page);
	settings = e_mail_config_service_backend_get_settings (backend);

	/* A provider page is created for every available CamelStore
	 * class.  We're only interested in the one for CamelEwsStore.
	 * We determine this by the CamelSettings type returned. */
	if (!CAMEL_IS_EWS_SETTINGS (settings))
		return;

	container = e_mail_config_activity_page_get_internal_box (E_MAIL_CONFIG_ACTIVITY_PAGE (page));

	text = _("Global Address List");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_widget_set_margin_top (widget, 6);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	e_binding_bind_property_full (
		settings, "oaburl",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE,
		mail_config_ews_gal_string_to_boolean,
		NULL,
		NULL, (GDestroyNotify) NULL);

	container = widget;

	text = _("Cache o_ffline address book");
	widget = gtk_check_button_new_with_mnemonic (text);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 0, 1, 1);
	extension->priv->toggle_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	e_binding_bind_property (
		settings, "oab-offline",
		widget, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, FALSE);
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_set_spacing (GTK_BOX (widget), 6);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 1, 1, 1);
	gtk_widget_show (widget);

	e_binding_bind_property (
		settings, "oab-offline",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	container = widget;

	text = _("Select ad_dress list:");
	widget = gtk_label_new_with_mnemonic (text);
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = e_mail_config_ews_oal_combo_box_new (backend);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	extension->priv->combo_box = widget;  /* do not reference */
	gtk_widget_show (widget);

	e_binding_bind_property_full (
		settings, "oal-selected",
		widget, "active-id",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		mail_config_ews_gal_oal_selected_to_active_id,
		mail_config_ews_gal_active_id_to_oal_selected,
		NULL, (GDestroyNotify) NULL);

	widget = gtk_button_new_with_label (_("Fetch List"));
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	extension->priv->fetch_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	g_signal_connect (
		widget, "clicked",
		G_CALLBACK (mail_config_ews_gal_fetch_button_clicked_cb),
		extension);
}

static void
e_mail_config_ews_gal_class_init (EMailConfigEwsGalClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = mail_config_ews_gal_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_MAIL_CONFIG_PROVIDER_PAGE;
}

static void
e_mail_config_ews_gal_class_finalize (EMailConfigEwsGalClass *class)
{
}

static void
e_mail_config_ews_gal_init (EMailConfigEwsGal *extension)
{
	extension->priv = e_mail_config_ews_gal_get_instance_private (extension);
}

void
e_mail_config_ews_gal_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_gal_register_type (type_module);
}

