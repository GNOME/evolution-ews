/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-ews-ooo-notificator.h"
#include "camel/camel-ews-store.h"
#include "common/e-ews-oof-settings.h"

#include <shell/e-shell.h>
#include <shell/e-shell-view.h>
#include <mail/e-mail-backend.h>
#include <mail/e-mail-ui-session.h>
#include <glib/gi18n-lib.h>

typedef gboolean (*EEwsOooNotificationDispatcherFunction) (gpointer data);

struct _EEwsOooNotificatorPrivate {
	EShell *shell;
	EMailAccountStore *account_store;
	GList *stores;
	GHashTable *alerts;
};

typedef struct _EEwsOooNotificatorDispatcherData {
	EEwsOooNotificator *extension;
	CamelEwsStore *ews_store;
	guint timeout_id;
} EEwsOooNotificatorDispatcherData;

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEwsOooNotificator, e_ews_ooo_notificator, E_TYPE_EXTENSION, 0,
	G_ADD_PRIVATE_DYNAMIC (EEwsOooNotificator))

/* Forward declarations */
static void e_ews_ooo_notificator_has_ooo_set_cb (EEwsOooNotificator *extension,
						  GParamSpec *pspec,
						  CamelEwsStore *ews_store);
static void e_ews_ooo_notificator_service_disabled_cb (EEwsOooNotificator *extension,
						       CamelService *service,
						       EMailAccountStore *account_store);
static void e_ews_ooo_notificator_service_removed_cb (EEwsOooNotificator *extension,
						      CamelService *service,
						      EMailAccountStore *session);
static void e_ews_ooo_notificator_service_added_cb (EEwsOooNotificator *extension,
						    CamelService *service,
						    EMailAccountStore *session);
static void e_ews_ooo_notificator_online_cb (EEwsOooNotificator* extension,
					     GParamSpec *pspec,
					     EShell *shell);

static EShellView *
e_ews_ooo_notificator_get_extensible (EEwsOooNotificator *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_SHELL_VIEW (extensible);
}

static void
e_ews_ooo_notificator_dispatcher_data_free (gpointer user_data)
{
	EEwsOooNotificatorDispatcherData *data = user_data;

	if (data->extension)
		g_object_unref (data->extension);
	if (data->ews_store)
		g_object_unref (data->ews_store);
	g_slice_free (EEwsOooNotificatorDispatcherData, data);
}

static void
e_ews_ooo_notificator_dismiss_cb (EEwsOooNotificatorDispatcherData *data,
				  gint response_id,
				  EAlert *alert)
{

	if (response_id == GTK_RESPONSE_ACCEPT)
		camel_ews_store_unset_oof_settings_state (data->ews_store);

	camel_ews_store_set_ooo_alert_state (data->ews_store, CAMEL_EWS_STORE_OOO_ALERT_STATE_CLOSED);
	g_hash_table_remove (data->extension->priv->alerts, data->ews_store);

	if (data->timeout_id) {
		guint id;

		/* Unset the data structure before calling remove(),
		   because the removal can free the `data`. */
		id = data->timeout_id;
		data->timeout_id = 0;

		g_source_remove (id);
	}
}

static void
e_ews_ooo_notificator_hide_notification (EEwsOooNotificator *extension,
					 CamelEwsStore *ews_store)
{
	EAlert *alert;

	alert = g_hash_table_lookup (extension->priv->alerts, ews_store);
	if (alert) {
		e_alert_response (alert, GTK_RESPONSE_NONE);
		g_hash_table_remove (extension->priv->alerts, ews_store);
	}
}

static gboolean
e_ews_ooo_notificator_hide_notification_by_timeout_cb (gpointer user_data)
{
	EEwsOooNotificatorDispatcherData *data = user_data;

	e_ews_ooo_notificator_hide_notification (data->extension, data->ews_store);

	return FALSE;
}

static void
e_ews_ooo_notificator_show_notification (EEwsOooNotificator *extension,
					 CamelEwsStore *ews_store)
{
	EAlert *alert;
	EShellView *view;
	EShellContent *shell_content;
	EEwsOooNotificatorDispatcherData *data;
	EUIAction *action;
	const gchar *account_name;

	data = g_slice_new0 (EEwsOooNotificatorDispatcherData);
	data->extension = g_object_ref (extension);
	data->ews_store = g_object_ref (ews_store);

	view = e_ews_ooo_notificator_get_extensible (extension);
	shell_content = e_shell_view_get_shell_content (view);

	account_name = camel_service_get_display_name (CAMEL_SERVICE (ews_store));
	alert = e_alert_new ("ews:has-ooo-set", account_name, NULL);

	g_signal_connect_swapped (
			alert,
			"response",
			G_CALLBACK (e_ews_ooo_notificator_dismiss_cb), data);

	action = e_ui_action_new ("ews-ooo-notificator", "ooo-unset-on-server", NULL);
	e_ui_action_set_label (action, _("Unset on Server"));
	e_ui_action_set_tooltip (action, _("Unset the “Out of Office” status"));
	e_alert_add_action (alert, action, GTK_RESPONSE_ACCEPT, FALSE);
	g_object_unref (action);

	g_hash_table_insert (extension->priv->alerts, g_object_ref (ews_store), g_object_ref_sink (alert));
	e_alert_sink_submit_alert (E_ALERT_SINK (shell_content), alert);

	/* If the user doesn't cancel the notify, it will be hide automatically in 5 minutes */
	data->timeout_id = e_named_timeout_add_seconds_full (
			G_PRIORITY_DEFAULT, 300,
			e_ews_ooo_notificator_hide_notification_by_timeout_cb,
			data, e_ews_ooo_notificator_dispatcher_data_free);
}

static gboolean
e_ews_ooo_notificator_has_ooo_set (gpointer user_data)
{
	EEwsOooNotificatorDispatcherData *data = user_data;
	CamelEwsStoreOooAlertState alert_state;
	gboolean has_ooo_set;

	alert_state = camel_ews_store_get_ooo_alert_state (data->ews_store);
	has_ooo_set = camel_ews_store_get_has_ooo_set (data->ews_store);

	if (has_ooo_set && alert_state == CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN) {
		e_ews_ooo_notificator_show_notification (data->extension, data->ews_store);
		camel_ews_store_set_ooo_alert_state (data->ews_store, CAMEL_EWS_STORE_OOO_ALERT_STATE_NOTIFIED);
	}

	return FALSE;
}

static gboolean
e_ews_ooo_notificator_service_disabled (gpointer user_data)
{
	EEwsOooNotificatorDispatcherData *data = user_data;
	CamelEwsStoreOooAlertState state;

	e_ews_ooo_notificator_hide_notification (data->extension, data->ews_store);
	state = camel_ews_store_get_ooo_alert_state (data->ews_store);
	if (state != CAMEL_EWS_STORE_OOO_ALERT_STATE_CLOSED)
		camel_ews_store_set_ooo_alert_state (data->ews_store, CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN);
	camel_ews_store_set_has_ooo_set (data->ews_store, FALSE);

	return FALSE;
}

static gboolean
e_ews_ooo_notificator_service_removed (gpointer user_data)
{
	EEwsOooNotificatorDispatcherData *data = user_data;

	e_ews_ooo_notificator_hide_notification (data->extension, data->ews_store);
	g_signal_handlers_disconnect_by_func (
			data->ews_store,
			e_ews_ooo_notificator_has_ooo_set_cb,
			data->extension);
	data->extension->priv->stores = g_list_remove (
			data->extension->priv->stores, data->ews_store);

	return FALSE;
}

/*
 * GTK+ UI calls cannot be done in a dedicated thread.
 * So, let's ensure that our functions (that do something in the UI) will run
 * in the main thread, calling them through e_named_timeout_add_full().
 */
static void
e_ews_ooo_notificator_dispatcher (EEwsOooNotificatorDispatcherData *data,
				  EEwsOooNotificationDispatcherFunction function,
				  GDestroyNotify destroy_data)
{
	e_named_timeout_add_full (G_PRIORITY_DEFAULT, 1, function, data, destroy_data);
}

static void
e_ews_ooo_notificator_has_ooo_set_cb (EEwsOooNotificator *extension,
				      GParamSpec *pspec,
				      CamelEwsStore *ews_store)
{
	EEwsOooNotificatorDispatcherData *data;

	data = g_slice_new0 (EEwsOooNotificatorDispatcherData);
	data->extension = g_object_ref (extension);
	data->ews_store = g_object_ref (ews_store);

	e_ews_ooo_notificator_dispatcher (
			data,
			e_ews_ooo_notificator_has_ooo_set,
			e_ews_ooo_notificator_dispatcher_data_free);
}

static void
e_ews_ooo_notificator_service_disabled_cb (EEwsOooNotificator *extension,
					   CamelService *service,
					   EMailAccountStore *account_store)
{

	EEwsOooNotificatorDispatcherData *data;

	if (!CAMEL_IS_EWS_STORE (service))
		return;

	data = g_slice_new0 (EEwsOooNotificatorDispatcherData);
	data->extension = g_object_ref (extension);
	data->ews_store = g_object_ref (CAMEL_EWS_STORE (service));

	e_ews_ooo_notificator_dispatcher (
			data,
			e_ews_ooo_notificator_service_disabled,
			e_ews_ooo_notificator_dispatcher_data_free);
}

static void
e_ews_ooo_notificator_service_removed_cb (EEwsOooNotificator *extension,
					  CamelService *service,
					  EMailAccountStore *session)
{
	EEwsOooNotificatorDispatcherData *data;

	if (!CAMEL_IS_EWS_STORE (service))
		return;

	data = g_slice_new0 (EEwsOooNotificatorDispatcherData);
	data->extension = g_object_ref (extension);
	data->ews_store = g_object_ref (CAMEL_EWS_STORE (service));

	e_ews_ooo_notificator_dispatcher (
			data,
			e_ews_ooo_notificator_service_removed,
			e_ews_ooo_notificator_dispatcher_data_free);
}

static void
e_ews_ooo_notificator_service_added_cb (EEwsOooNotificator *extension,
					CamelService *service,
					EMailAccountStore *session)
{
	CamelEwsStore *ews_store;

	if (!CAMEL_IS_EWS_STORE (service))
		return;

	ews_store = CAMEL_EWS_STORE (service);
	g_signal_connect_swapped (
			ews_store, "notify::has-ooo-set",
			G_CALLBACK (e_ews_ooo_notificator_has_ooo_set_cb), extension);
	extension->priv->stores = g_list_append (extension->priv->stores, g_object_ref (ews_store));
}

static void
e_ews_ooo_notificator_online_cb (EEwsOooNotificator* extension,
				 GParamSpec *pspec,
				 EShell *shell)
{
	GList *l;

	if (e_shell_get_online (shell))
		return;

	for (l = extension->priv->stores; l; l = l->next) {
		EEwsOooNotificatorDispatcherData *data;

		data = g_slice_new0 (EEwsOooNotificatorDispatcherData);
		data->extension = g_object_ref (extension);
		data->ews_store = g_object_ref (CAMEL_EWS_STORE (l->data));

		e_ews_ooo_notificator_dispatcher (
				data,
				e_ews_ooo_notificator_service_disabled,
				e_ews_ooo_notificator_dispatcher_data_free);
	}
}

static void
e_ews_ooo_notificator_init (EEwsOooNotificator *extension)
{
	extension->priv = e_ews_ooo_notificator_get_instance_private (extension);

	extension->priv->alerts = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);
}

static void
e_ews_ooo_notificator_constructed (GObject *object)
{
	EEwsOooNotificator *extension;
	EShell *shell;
	EShellView *view;
	EShellBackend *backend;
	EMailAccountStore *account_store;
	EMailSession *session;
	GList *stores, *l;
	const gchar *view_name;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_ews_ooo_notificator_parent_class)->constructed (object);

	extension = E_EWS_OOO_NOTIFICATOR (object);
	view = e_ews_ooo_notificator_get_extensible (extension);
	view_name = e_shell_view_get_name (view);

	if (g_strcmp0 (view_name, "mail") != 0)
		return;

	backend = e_shell_view_get_shell_backend (view);
	shell = e_shell_backend_get_shell (backend);
	session = e_mail_backend_get_session (E_MAIL_BACKEND (backend));
	account_store = e_mail_ui_session_get_account_store (E_MAIL_UI_SESSION (session));
	stores = camel_session_list_services (CAMEL_SESSION (session));

	extension->priv->shell = shell;
	extension->priv->account_store = g_object_ref (account_store);

	for (l = stores; l; l = l->next) {
		CamelService *service = l->data;
		CamelEwsStore *ews_store;
		gboolean has_ooo_set;

		if (!CAMEL_IS_EWS_STORE (service))
			continue;

		ews_store = CAMEL_EWS_STORE (service);
		has_ooo_set = camel_ews_store_get_has_ooo_set (ews_store);

		if (has_ooo_set) {
			e_ews_ooo_notificator_show_notification (extension, ews_store);
			camel_ews_store_set_ooo_alert_state (ews_store, CAMEL_EWS_STORE_OOO_ALERT_STATE_NOTIFIED);
		}

		g_signal_connect_swapped (
				ews_store, "notify::has-ooo-set",
				G_CALLBACK (e_ews_ooo_notificator_has_ooo_set_cb), extension);
		extension->priv->stores = g_list_append (extension->priv->stores, g_object_ref (ews_store));
	}

	g_signal_connect_swapped (
			account_store, "service-disabled",
			G_CALLBACK (e_ews_ooo_notificator_service_disabled_cb), extension);

	g_signal_connect_swapped (
			account_store, "service-removed",
			G_CALLBACK (e_ews_ooo_notificator_service_removed_cb), extension);

	g_signal_connect_swapped (
			account_store, "service-added",
			G_CALLBACK (e_ews_ooo_notificator_service_added_cb), extension);

	g_signal_connect_swapped (
			shell, "notify::online",
			G_CALLBACK (e_ews_ooo_notificator_online_cb), extension);

	g_list_free_full (stores, g_object_unref);
}

static void
e_ews_ooo_notificator_dispose (GObject *object)
{
	EEwsOooNotificator *extension;
	GList *l;

	extension = E_EWS_OOO_NOTIFICATOR (object);

	if (extension->priv->shell) {
		g_signal_handlers_disconnect_by_data (extension->priv->shell, extension);
		extension->priv->shell = NULL;
	}

	if (extension->priv->account_store) {
		g_signal_handlers_disconnect_by_data (extension->priv->account_store, extension);
		g_object_unref (extension->priv->account_store);
		extension->priv->account_store = NULL;
	}

	for (l = extension->priv->stores; l; l = l->next) {
		CamelService *service = l->data;

		if (service) {
			g_signal_handlers_disconnect_by_data (service, extension);
			g_object_unref (service);
		}
	}
	g_list_free (extension->priv->stores);
	extension->priv->stores = NULL;

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_ooo_notificator_parent_class)->dispose (object);
}

static void
e_ews_ooo_notificator_finalize (GObject *object)
{
	EEwsOooNotificator *extension;

	extension = E_EWS_OOO_NOTIFICATOR (object);

	if (extension->priv->alerts) {
		g_hash_table_destroy (extension->priv->alerts);
		extension->priv->alerts = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_ooo_notificator_parent_class)->finalize (object);
}

static void
e_ews_ooo_notificator_class_init (EEwsOooNotificatorClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_ews_ooo_notificator_constructed;
	object_class->dispose = e_ews_ooo_notificator_dispose;
	object_class->finalize = e_ews_ooo_notificator_finalize;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SHELL_VIEW;
}

static void
e_ews_ooo_notificator_class_finalize (EEwsOooNotificatorClass *class)
{
}

void
e_ews_ooo_notificator_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_ooo_notificator_register_type (type_module);
}
