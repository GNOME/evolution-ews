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

#include <libedataserverui/libedataserverui.h>
#include <shell/e-shell.h>
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
	ESource *source;
	CamelEwsSettings *ews_settings;
	gchar *email_address;
};

enum {
	PROP_0,
	PROP_BACKEND
};

G_DEFINE_DYNAMIC_TYPE (EMailConfigEwsAutodiscover, e_mail_config_ews_autodiscover, GTK_TYPE_BUTTON)

static void
async_context_free (gpointer ptr)
{
	AsyncContext *async_context = ptr;

	if (!async_context)
		return;

	g_clear_object (&async_context->autodiscover);
	g_clear_object (&async_context->activity);
	g_clear_object (&async_context->source);
	g_clear_object (&async_context->ews_settings);
	g_free (async_context->email_address);

	g_slice_free (AsyncContext, async_context);
}

static gboolean
mail_config_ews_autodiscover_finish (EMailConfigEwsAutodiscover *autodiscover,
				     GAsyncResult *result,
				     GError **error)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_AUTODISCOVER (autodiscover), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, autodiscover), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, mail_config_ews_autodiscover_finish), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
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

	mail_config_ews_autodiscover_finish (E_MAIL_CONFIG_EWS_AUTODISCOVER (source_object), result, &error);

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
}

static gboolean
mail_config_ews_autodiscover_sync (ECredentialsPrompter *prompter,
				   ESource *source,
				   const ENamedParameters *credentials,
				   gboolean *out_authenticated,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error)
{
	AsyncContext *async_context = user_data;
	GError *local_error = NULL;
	gboolean res = TRUE;

	e_ews_autodiscover_ws_url_sync (
		async_context->ews_settings, async_context->email_address,
		credentials && e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD) ?
		e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD) : "",
		cancellable, &local_error);

	if (local_error == NULL) {
		*out_authenticated = TRUE;
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		*out_authenticated = FALSE;
		g_error_free (local_error);
	} else {
		res = FALSE;
		g_propagate_error (error, local_error);
	}

	return res;
}

static void
mail_config_ews_autodiscover_run_thread (GTask *task,
					 gpointer source_object,
					 gpointer task_data,
					 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	gboolean success = FALSE;

	if (!g_cancellable_set_error_if_cancelled (cancellable, &local_error) && !local_error) {
		gboolean without_password;

		without_password = e_ews_connection_utils_get_without_password (async_context->ews_settings);
		if (without_password) {
			success = e_ews_autodiscover_ws_url_sync (
				async_context->ews_settings, async_context->email_address, "",
				cancellable, &local_error);
		}

		if (!without_password || g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			EShell *shell;

			e_ews_connection_utils_force_off_ntlm_auth_check ();
			g_clear_error (&local_error);

			shell = e_shell_get_default ();

			success = e_credentials_prompter_loop_prompt_sync (e_shell_get_credentials_prompter (shell),
				async_context->source, E_CREDENTIALS_PROMPTER_PROMPT_FLAG_ALLOW_SOURCE_SAVE,
				mail_config_ews_autodiscover_sync, async_context, cancellable, &local_error);
		}
	}

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

static void
mail_config_ews_autodiscover_run (EMailConfigEwsAutodiscover *autodiscover)
{
	EActivity *activity;
	EMailConfigServicePage *page;
	EMailConfigServiceBackend *backend;
	CamelSettings *settings;
	ESource *source;
	GCancellable *cancellable;
	AsyncContext *async_context;
	GTask *task;

	backend = e_mail_config_ews_autodiscover_get_backend (autodiscover);
	page = e_mail_config_service_backend_get_page (backend);
	source = e_mail_config_service_backend_get_source (backend);
	settings = e_mail_config_service_backend_get_settings (backend);

	activity = e_mail_config_activity_page_new_activity (E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	cancellable = e_activity_get_cancellable (activity);

	e_activity_set_text (activity, _("Querying Autodiscover service"));

	gtk_widget_set_sensitive (GTK_WIDGET (autodiscover), FALSE);

	async_context = g_slice_new0 (AsyncContext);
	async_context->autodiscover = g_object_ref (autodiscover);
	async_context->activity = activity;  /* takes ownership */
	async_context->source = g_object_ref (source);
	async_context->ews_settings = g_object_ref (settings);
	async_context->email_address = g_strdup (e_mail_config_service_page_get_email_address (page));

	/*
	 * The GTask will be run in a new thread, which will invoke
	 * camel_ews_settings_set_{oaburl,hosturl}(), emiting signals that
	 * are bound to GTK+ UI signals, causing GTK+ calls in this
	 * secondary thread and consequently a crash. To avoid this, let's stop
	 * the property changes notifications while we are not in the main thread.
	 */
	g_object_freeze_notify (G_OBJECT (settings));

	task = g_task_new (autodiscover, cancellable, mail_config_ews_autodiscover_run_cb, async_context);
	g_task_set_source_tag (task, mail_config_ews_autodiscover_finish);
	g_task_set_task_data (task, async_context, async_context_free);

	g_task_run_in_thread (task, mail_config_ews_autodiscover_run_thread);

	g_object_unref (task);
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

