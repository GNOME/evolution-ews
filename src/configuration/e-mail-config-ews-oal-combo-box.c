/*
 * e-mail-config-ews-oal-combo-box.c
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

#include "e-mail-config-ews-oal-combo-box.h"

#include <mail/e-mail-config-service-page.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-connection-utils.h"

#define E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX, EMailConfigEwsOalComboBoxPrivate))

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

/* Forward Declarations */
static void	e_mail_config_ews_oal_combo_box_authenticator_init
				(ESourceAuthenticatorInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailConfigEwsOalComboBox,
	e_mail_config_ews_oal_combo_box,
	GTK_TYPE_COMBO_BOX_TEXT,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_mail_config_ews_oal_combo_box_authenticator_init))

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
	EMailConfigEwsOalComboBoxPrivate *priv;

	priv = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_GET_PRIVATE (object);

	if (priv->backend != NULL) {
		g_object_ref (priv->backend);
		priv->backend = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_oal_combo_box_parent_class)->
		dispose (object);
}

static void
mail_config_ews_oal_combo_box_finalize (GObject *object)
{
	EMailConfigEwsOalComboBoxPrivate *priv;

	priv = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_GET_PRIVATE (object);

	g_mutex_clear (&priv->oal_items_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_mail_config_ews_oal_combo_box_parent_class)->
		finalize (object);
}

static gboolean
mail_config_ews_oal_combo_box_get_without_password (ESourceAuthenticator *auth)
{
	EMailConfigEwsOalComboBox *combo_box;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;

	combo_box = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (auth);
	backend = e_mail_config_ews_oal_combo_box_get_backend (combo_box);
	settings = e_mail_config_service_backend_get_settings (backend);

	ews_settings = CAMEL_EWS_SETTINGS (settings);

	return e_ews_connection_utils_get_without_password (ews_settings);
}

static ESourceAuthenticationResult
mail_config_ews_oal_combo_box_try_password_sync (ESourceAuthenticator *auth,
                                                 const GString *password,
                                                 GCancellable *cancellable,
                                                 GError **error)
{
	EMailConfigEwsOalComboBox *combo_box;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	ESourceAuthenticationResult result;
	EEwsConnection *cnc;
	GSList *oal_items = NULL;
	const gchar *oab_url;
	GError *local_error = NULL;

	combo_box = E_MAIL_CONFIG_EWS_OAL_COMBO_BOX (auth);
	backend = e_mail_config_ews_oal_combo_box_get_backend (combo_box);
	settings = e_mail_config_service_backend_get_settings (backend);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	oab_url = camel_ews_settings_get_oaburl (ews_settings);

	cnc = e_ews_connection_new (oab_url, ews_settings);
	e_ews_connection_set_password (cnc, password->str);

	e_ews_connection_get_oal_list_sync (
		cnc, &oal_items, cancellable, &local_error);

	g_object_unref (cnc);

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
e_mail_config_ews_oal_combo_box_class_init (EMailConfigEwsOalComboBoxClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EMailConfigEwsOalComboBoxPrivate));

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
e_mail_config_ews_oal_combo_box_authenticator_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password =
		mail_config_ews_oal_combo_box_get_without_password;
	iface->try_password_sync =
		mail_config_ews_oal_combo_box_try_password_sync;
}

static void
e_mail_config_ews_oal_combo_box_class_finalize (EMailConfigEwsOalComboBoxClass *class)
{
}

static void
e_mail_config_ews_oal_combo_box_init (EMailConfigEwsOalComboBox *combo_box)
{
	combo_box->priv =
		E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_GET_PRIVATE (combo_box);

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

/* Helper for e_mail_config_ews_oal_combo_box_update() */
static void
mail_config_ews_oal_combo_box_update_cb (GObject *source_object,
                                         GAsyncResult *result,
                                         gpointer user_data)
{
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	e_source_registry_authenticate_finish (
		E_SOURCE_REGISTRY (source_object), result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

void
e_mail_config_ews_oal_combo_box_update (EMailConfigEwsOalComboBox *combo_box,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	EMailConfigServicePage *page;
	EMailConfigServiceBackend *backend;
	ESourceAuthenticator *authenticator;
	ESourceRegistry *registry;
	ESource *source;

	g_return_if_fail (E_IS_MAIL_CONFIG_EWS_OAL_COMBO_BOX (combo_box));

	backend = e_mail_config_ews_oal_combo_box_get_backend (combo_box);
	page = e_mail_config_service_backend_get_page (backend);
	source = e_mail_config_service_backend_get_source (backend);
	registry = e_mail_config_service_page_get_registry (page);

	authenticator = E_SOURCE_AUTHENTICATOR (combo_box);

	simple = g_simple_async_result_new (
		G_OBJECT (combo_box), callback, user_data,
		e_mail_config_ews_oal_combo_box_update);

	e_source_registry_authenticate (
		registry, source, authenticator, cancellable,
		mail_config_ews_oal_combo_box_update_cb, simple);
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

