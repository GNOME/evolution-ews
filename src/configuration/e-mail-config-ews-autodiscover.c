/*
 * e-mail-config-ews-autodiscover.c
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

#include "e-mail-config-ews-autodiscover.h"

#include <glib/gi18n-lib.h>

#include <mail/e-mail-config-service-page.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-connection-utils.h"

#define E_MAIL_CONFIG_EWS_AUTODISCOVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER, EMailConfigEwsAutodiscoverPrivate))

typedef struct _AsyncContext AsyncContext;

struct _EMailConfigEwsAutodiscoverPrivate {
	EMailConfigServiceBackend *backend;
};

struct _AsyncContext {
	EMailConfigEwsAutodiscover *autodiscover;
	EActivity *activity;
};

enum {
	PROP_0,
	PROP_BACKEND
};

/* Forward Declarations */
static void	e_mail_config_ews_autodiscover_authenticator_init
				(ESourceAuthenticatorInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailConfigEwsAutodiscover,
	e_mail_config_ews_autodiscover,
	GTK_TYPE_BUTTON,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_mail_config_ews_autodiscover_authenticator_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->autodiscover != NULL)
		g_object_unref (async_context->autodiscover);

	if (async_context->activity != NULL)
		g_object_unref (async_context->activity);

	g_slice_free (AsyncContext, async_context);
}

static void
mail_config_ews_autodiscover_run_cb (GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data)
{
	AsyncContext *async_context = user_data;
	EMailConfigEwsAutodiscover *autodiscover;
	EAlertSink *alert_sink;
	GError *error = NULL;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;

	autodiscover = async_context->autodiscover;
	alert_sink = e_activity_get_alert_sink (async_context->activity);

	e_source_registry_authenticate_finish (
		E_SOURCE_REGISTRY (source_object), result, &error);

	backend = e_mail_config_ews_autodiscover_get_backend (autodiscover);
	settings = e_mail_config_service_backend_get_settings (backend);
	/*
	 * And unstop since we are back to the main thread.
	 */
	g_object_thaw_notify (G_OBJECT (settings));

	if (e_activity_handle_cancellation (async_context->activity, error)) {
		g_error_free (error);

	} else if (error != NULL) {
		e_alert_submit (
			alert_sink,
			"ews:autodiscovery-error",
			error->message, NULL);
		g_error_free (error);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (autodiscover), TRUE);

	async_context_free (async_context);
}

static void
mail_config_ews_autodiscover_run (EMailConfigEwsAutodiscover *autodiscover)
{
	EActivity *activity;
	EMailConfigServicePage *page;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	ESourceRegistry *registry;
	ESource *source;
	GCancellable *cancellable;
	AsyncContext *async_context;

	backend = e_mail_config_ews_autodiscover_get_backend (autodiscover);
	page = e_mail_config_service_backend_get_page (backend);
	source = e_mail_config_service_backend_get_source (backend);
	settings = e_mail_config_service_backend_get_settings (backend);

	registry = e_mail_config_service_page_get_registry (page);

	activity = e_mail_config_activity_page_new_activity (
		E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	cancellable = e_activity_get_cancellable (activity);

	e_activity_set_text (activity, _("Querying Autodiscover service"));

	gtk_widget_set_sensitive (GTK_WIDGET (autodiscover), FALSE);

	async_context = g_slice_new0 (AsyncContext);
	async_context->autodiscover = g_object_ref (autodiscover);
	async_context->activity = activity;  /* takes ownership */

	/*
	 * e_source_registry_authenticate() will be run in a new a thread, which
	 * one will invoke camel_ews_settings_set_{oaburl,hosturl}(), emiting
	 * signals that are bound to GTK+ UI signals, causing GTK+ calls in this
	 * secondary thread and consequently a crash. To avoid this, let's stop
	 * the property changes notifications while we are not in the main thread.
	 */
	g_object_freeze_notify (G_OBJECT (settings));
	e_source_registry_authenticate (
		registry, source,
		E_SOURCE_AUTHENTICATOR (autodiscover),
		cancellable, mail_config_ews_autodiscover_run_cb,
		async_context);
}

static void
mail_config_ews_autodiscover_set_backend (EMailConfigEwsAutodiscover *autodiscover,
                                          EMailConfigServiceBackend *backend)
{
	g_return_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend));
	g_return_if_fail (autodiscover->priv->backend == NULL);

	autodiscover->priv->backend = g_object_ref (backend);
}

static void
mail_config_ews_autodiscover_set_property (GObject *object,
                                           guint property_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			mail_config_ews_autodiscover_set_backend (
				E_MAIL_CONFIG_EWS_AUTODISCOVER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_autodiscover_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_set_object (
				value,
				e_mail_config_ews_autodiscover_get_backend (
				E_MAIL_CONFIG_EWS_AUTODISCOVER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_autodiscover_dispose (GObject *object)
{
	EMailConfigEwsAutodiscoverPrivate *priv;

	priv = E_MAIL_CONFIG_EWS_AUTODISCOVER_GET_PRIVATE (object);

	if (priv->backend != NULL) {
		g_object_unref (priv->backend);
		priv->backend = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_autodiscover_parent_class)->
		dispose (object);
}

static void
mail_config_ews_autodiscover_constructed (GObject *object)
{
	GtkButton *button;

	button = GTK_BUTTON (object);
	gtk_button_set_label (button, _("Fetch _URL"));
	gtk_button_set_use_underline (button, TRUE);

	/* Chain up tp parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_autodiscover_parent_class)->constructed (object);
}

static void
mail_config_ews_autodiscover_clicked (GtkButton *button)
{
	EMailConfigEwsAutodiscover *autodiscover;

	autodiscover = E_MAIL_CONFIG_EWS_AUTODISCOVER (button);

	mail_config_ews_autodiscover_run (autodiscover);
}

static gboolean
mail_config_ews_autodiscover_get_without_password (ESourceAuthenticator *auth)
{
	EMailConfigEwsAutodiscover *autodiscover;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;

	autodiscover = E_MAIL_CONFIG_EWS_AUTODISCOVER (auth);
	backend = e_mail_config_ews_autodiscover_get_backend (autodiscover);
	settings = e_mail_config_service_backend_get_settings (backend);
	ews_settings = CAMEL_EWS_SETTINGS (settings);

	return e_ews_connection_utils_get_without_password (ews_settings);
}

static ESourceAuthenticationResult
mail_config_ews_autodiscover_try_password_sync (ESourceAuthenticator *auth,
                                                const GString *password,
                                                GCancellable *cancellable,
                                                GError **error)
{
	EMailConfigEwsAutodiscover *autodiscover;
	EMailConfigServiceBackend *backend;
	EMailConfigServicePage *page;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	ESourceAuthenticationResult result;
	const gchar *email_address;
	GError *local_error = NULL;

	autodiscover = E_MAIL_CONFIG_EWS_AUTODISCOVER (auth);
	backend = e_mail_config_ews_autodiscover_get_backend (autodiscover);
	page = e_mail_config_service_backend_get_page (backend);
	settings = e_mail_config_service_backend_get_settings (backend);

	email_address = e_mail_config_service_page_get_email_address (page);

	ews_settings = CAMEL_EWS_SETTINGS (settings);

	e_ews_autodiscover_ws_url_sync (
		ews_settings, email_address, password->str,
		cancellable, &local_error);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
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
e_mail_config_ews_autodiscover_class_init (EMailConfigEwsAutodiscoverClass *class)
{
	GObjectClass *object_class;
	GtkButtonClass *button_class;

	g_type_class_add_private (
		class, sizeof (EMailConfigEwsAutodiscoverPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_config_ews_autodiscover_set_property;
	object_class->get_property = mail_config_ews_autodiscover_get_property;
	object_class->dispose = mail_config_ews_autodiscover_dispose;
	object_class->constructed = mail_config_ews_autodiscover_constructed;

	button_class = GTK_BUTTON_CLASS (class);
	button_class->clicked = mail_config_ews_autodiscover_clicked;

	g_object_class_install_property (
		object_class,
		PROP_BACKEND,
		g_param_spec_object (
			"backend",
			"Backend",
			"Mail configuration backend",
			E_TYPE_MAIL_CONFIG_SERVICE_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_mail_config_ews_autodiscover_authenticator_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password =
		mail_config_ews_autodiscover_get_without_password;
	iface->try_password_sync =
		mail_config_ews_autodiscover_try_password_sync;
}

static void
e_mail_config_ews_autodiscover_class_finalize (EMailConfigEwsAutodiscoverClass *class)
{
}

static void
e_mail_config_ews_autodiscover_init (EMailConfigEwsAutodiscover *autodiscover)
{
	autodiscover->priv =
		E_MAIL_CONFIG_EWS_AUTODISCOVER_GET_PRIVATE (autodiscover);
}

void
e_mail_config_ews_autodiscover_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_autodiscover_register_type (type_module);
}

GtkWidget *
e_mail_config_ews_autodiscover_new (EMailConfigServiceBackend *backend)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER,
		"backend", backend, NULL);
}

EMailConfigServiceBackend *
e_mail_config_ews_autodiscover_get_backend (EMailConfigEwsAutodiscover *autodiscover)
{
	g_return_val_if_fail (
		E_IS_MAIL_CONFIG_EWS_AUTODISCOVER (autodiscover), NULL);

	return autodiscover->priv->backend;
}

