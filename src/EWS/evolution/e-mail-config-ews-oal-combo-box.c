/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <mail/e-mail-config-service-page.h>

#include "common/e-ews-connection.h"
#include "common/e-ews-connection-utils.h"

#include "e-ews-config-utils.h"
#include "e-mail-config-ews-oal-combo-box.h"

struct _EMailConfigEwsOalComboBoxPrivate {
	EMailConfigServiceBackend *backend;

	/* The try_password() method deposits results here, and the
	 * update_finish() function uses the results to re-populate
	 * the combo box.  This avoids calling GTK+ functions from
	 * multiple threads. */
	GSList *oal_items;
	GMutex oal_items_lock;
};

enum {
	PROP_0,
	PROP_BACKEND
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigEwsOalComboBox, e_mail_config_ews_oal_combo_box, GTK_TYPE_COMBO_BOX_TEXT, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailConfigEwsOalComboBox))

typedef struct _AsyncContext {
	EMailConfigEwsOalComboBox *combo_box;
	GSimpleAsyncResult *simple;
	ESource *source;
	GObject *settings;
} AsyncContext;

static void
async_context_free (gpointer ptr)
{
	AsyncContext *async_context = ptr;

	if (!async_context)
		return;

	if (async_context->settings)
		g_object_thaw_notify (async_context->settings);

	g_clear_object (&async_context->combo_box);
	g_clear_object (&async_context->simple);
	g_clear_object (&async_context->source);
	g_clear_object (&async_context->settings);

	g_slice_free (AsyncContext, async_context);
}

static void
mail_config_ews_oal_combo_box_set_backend (EMailConfigEwsOalComboBox *combo_box,
                                           EMailConfigServiceBackend *backend)
{
	g_return_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend));
	g_return_if_fail (combo_box->priv->backend == NULL);

	combo_box->priv->backend = g_object_ref (backend);
}

static void
mail_config_ews_oal_combo_box_set_property (GObject *object,
                                            guint property_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			mail_config_ews_oal_combo_box_set_backend (
				E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_oal_combo_box_get_property (GObject *object,
                                            guint property_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_set_object (
				value,
				e_mail_config_ews_oal_combo_box_get_backend (
				E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_oal_combo_box_dispose (GObject *object)
{
	EMailConfigEwsOalComboBox *ews_combo = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (object);

	g_clear_object (&ews_combo->priv->backend);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_oal_combo_box_parent_class)->dispose (object);
}

static void
mail_config_ews_oal_combo_box_finalize (GObject *object)
{
	EMailConfigEwsOalComboBox *ews_combo = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (object);

	g_mutex_clear (&ews_combo->priv->oal_items_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_mail_config_ews_oal_combo_box_parent_class)->finalize (object);
}

static void
e_mail_config_ews_oal_combo_box_class_init (EMailConfigEwsOalComboBoxClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_config_ews_oal_combo_box_set_property;
	object_class->get_property = mail_config_ews_oal_combo_box_get_property;
	object_class->dispose = mail_config_ews_oal_combo_box_dispose;
	object_class->finalize = mail_config_ews_oal_combo_box_finalize;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"Service backend",
			E_TYPE_MAIL_CONFIG_SERVICE_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_mail_config_ews_oal_combo_box_class_finalize (EMailConfigEwsOalComboBoxClass *class)
{
}

static void
e_mail_config_ews_oal_combo_box_init (EMailConfigEwsOalComboBox *combo_box)
{
	combo_box->priv = e_mail_config_ews_oal_combo_box_get_instance_private (combo_box);

	g_mutex_init (&combo_box->priv->oal_items_lock);
}

void
e_mail_config_ews_oal_combo_box_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_oal_combo_box_register_type (type_module);
}

GtkWidget *
e_mail_config_ews_oal_combo_box_new (EMailConfigServiceBackend *backend)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX,
		"backend", backend, NULL);
}

EMailConfigServiceBackend *
e_mail_config_ews_oal_combo_box_get_backend (EMailConfigEwsOalComboBox *combo_box)
{
	g_return_val_if_fail (
		E_IS_MAIL_CONFIG_EWS_OAL_COMBO_BOX (combo_box), NULL);

	return combo_box->priv->backend;
}

static ESourceAuthenticationResult
mail_config_ews_aol_combo_box_update_try_credentials_sync (EEwsConnection *connection,
							   const ENamedParameters *credentials,
							   gpointer user_data,
							   GCancellable *cancellable,
							   GError **error)
{
	AsyncContext *async_context = user_data;
	EMailConfigEwsOalComboBox *combo_box;
	ESourceAuthenticationResult result;
	GSList *oal_items = NULL;
	GError *local_error = NULL;

	combo_box = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (async_context->combo_box);

	e_ews_connection_get_oal_list_sync (connection, &oal_items, cancellable, &local_error);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		/* Deposit results in the private struct for
		 * the update_finish() function to pick up. */
		g_mutex_lock (&combo_box->priv->oal_items_lock);
		g_slist_free_full (
			combo_box->priv->oal_items,
			(GDestroyNotify) ews_oal_free);
		combo_box->priv->oal_items = oal_items;
		g_mutex_unlock (&combo_box->priv->oal_items_lock);

	} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_error_free (local_error);

	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	return result;
}

static void
mail_config_ews_aol_combo_box_update_thread_cb (GObject *with_object,
						gpointer user_data,
						GCancellable *cancellable,
						GError **perror)
{
	AsyncContext *async_context = user_data;
	CamelEwsSettings *ews_settings;
	const gchar *oab_url;
	EEwsConnection *connection;

	if (g_cancellable_set_error_if_cancelled (cancellable, perror))
		return;

	ews_settings = CAMEL_EWS_SETTINGS (async_context->settings);
	oab_url = camel_ews_settings_get_oaburl (ews_settings);

	connection = e_ews_config_utils_open_connection_for (async_context->source, ews_settings, oab_url,
		mail_config_ews_aol_combo_box_update_try_credentials_sync, async_context, cancellable, perror);

	g_clear_object (&connection);
}

static void
mail_config_ews_aol_combo_box_update_idle_cb (GObject *with_object,
					      gpointer user_data,
					      GCancellable *cancellable,
					      GError **perror)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = (AsyncContext *) user_data;

	if (perror) {
		error = *perror;
		*perror = NULL;
	}

	if (error != NULL)
		g_simple_async_result_take_error (async_context->simple, error);

	g_simple_async_result_complete (async_context->simple);
}

void
e_mail_config_ews_oal_combo_box_update (EMailConfigEwsOalComboBox *combo_box,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	EMailConfigServiceBackend *backend;
	AsyncContext *async_context;
	CamelSettings *settings;
	ESource *source;
	ESource *collection;

	g_return_if_fail (E_IS_MAIL_CONFIG_EWS_OAL_COMBO_BOX (combo_box));

	backend = e_mail_config_ews_oal_combo_box_get_backend (combo_box);
	settings = e_mail_config_service_backend_get_settings (backend);
	source = e_mail_config_service_backend_get_source (backend);
	collection = e_mail_config_service_backend_get_collection (backend);

	if (collection && e_source_has_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION))
		source = collection;

	simple = g_simple_async_result_new (
		G_OBJECT (combo_box), callback, user_data,
		e_mail_config_ews_oal_combo_box_update);

	async_context = g_slice_new0 (AsyncContext);
	async_context->combo_box = g_object_ref (combo_box);
	async_context->simple = simple;  /* takes ownership */
	async_context->source = g_object_ref (source);
	async_context->settings = G_OBJECT (g_object_ref (settings));

	/* Property changes can cause update of the UI, but this runs in a thread,
	   thus freeze the notify till be back in UI thread */
	g_object_freeze_notify (async_context->settings);

	e_ews_config_utils_run_in_thread (G_OBJECT (combo_box),
		mail_config_ews_aol_combo_box_update_thread_cb,
		mail_config_ews_aol_combo_box_update_idle_cb,
		async_context, async_context_free, cancellable);
}

gboolean
e_mail_config_ews_oal_combo_box_update_finish (EMailConfigEwsOalComboBox *combo_box,
                                               GAsyncResult *result,
                                               GError **error)
{
	GSimpleAsyncResult *simple;
	GtkComboBoxText *combo_box_text;
	GSList *list, *link;
	gchar *active_id;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (combo_box),
		e_mail_config_ews_oal_combo_box_update), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	/* Re-populate the combo box using the cached results. */

	g_mutex_lock (&combo_box->priv->oal_items_lock);
	list = combo_box->priv->oal_items;
	combo_box->priv->oal_items = NULL;
	g_mutex_unlock (&combo_box->priv->oal_items_lock);

	active_id = g_strdup (gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo_box)));
	combo_box_text = GTK_COMBO_BOX_TEXT (combo_box);
	gtk_combo_box_text_remove_all (combo_box_text);

	for (link = list; link != NULL; link = g_slist_next (link)) {
		EwsOAL *oal = link->data;
		const gchar *name = oal->name;

		while (name && *name == '\\')
			name++;

		gtk_combo_box_text_append (
			combo_box_text, oal->id, name);
	}

	g_slist_free_full (list, (GDestroyNotify) ews_oal_free);

	if (active_id && *active_id)
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo_box), active_id);
	else
		gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	g_free (active_id);

	return TRUE;
}

