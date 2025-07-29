/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <shell/e-shell.h>
#include <shell/e-shell-view.h>
#include <mail/e-mail-backend.h>
#include <mail/e-mail-ui-session.h>
#include <glib/gi18n-lib.h>

#include "camel/camel-m365-store.h"

#include "e-m365-ooo-notificator.h"

typedef gboolean (*EM365OooNotificationDispatcherFunction) (gpointer data);

struct _EM365OooNotificator {
	EExtension parent;

	EShell *shell;
	EMailAccountStore *account_store;
	GPtrArray *stores;
	GHashTable *alerts;
};

typedef struct _EM365OooNotificatorDispatcherData {
	EM365OooNotificator *self;
	CamelM365Store *m365_store;
	guint timeout_id;
} EM365OooNotificatorDispatcherData;

G_DEFINE_DYNAMIC_TYPE (EM365OooNotificator, e_m365_ooo_notificator, E_TYPE_EXTENSION)

/* Forward declarations */
static void e_m365_ooo_notificator_has_ooo_set_cb (EM365OooNotificator *extension,
						   GParamSpec *pspec,
						   CamelM365Store *m365_store);

static EShellView *
e_m365_ooo_notificator_get_extensible (EM365OooNotificator *self)
{
	return E_SHELL_VIEW (e_extension_get_extensible (E_EXTENSION (self)));
}

static void
e_m365_ooo_notificator_dispatcher_data_free (gpointer user_data)
{
	EM365OooNotificatorDispatcherData *data = user_data;

	g_clear_object (&data->self);
	g_clear_object (&data->m365_store);
	g_free (data);
}

static void
e_m365_ooo_notificator_dismiss_cb (EM365OooNotificatorDispatcherData *data,
				   gint response_id,
				   EAlert *alert)
{

	if (response_id == GTK_RESPONSE_ACCEPT)
		camel_m365_store_unset_oof_settings_state (data->m365_store);

	camel_m365_store_set_ooo_alert_state (data->m365_store, CAMEL_M365_STORE_OOO_ALERT_STATE_CLOSED);
	g_hash_table_remove (data->self->alerts, data->m365_store);

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
e_m365_ooo_notificator_hide_notification (EM365OooNotificator *self,
					  CamelM365Store *m365_store)
{
	EAlert *alert;

	alert = g_hash_table_lookup (self->alerts, m365_store);
	if (alert) {
		e_alert_response (alert, GTK_RESPONSE_NONE);
		g_hash_table_remove (self->alerts, m365_store);
	}
}

static gboolean
e_m365_ooo_notificator_hide_notification_by_timeout_cb (gpointer user_data)
{
	EM365OooNotificatorDispatcherData *data = user_data;

	e_m365_ooo_notificator_hide_notification (data->self, data->m365_store);

	return FALSE;
}

static void
e_m365_ooo_notificator_show_notification (EM365OooNotificator *self,
					  CamelM365Store *m365_store)
{
	EAlert *alert;
	EShellView *view;
	EShellContent *shell_content;
	EM365OooNotificatorDispatcherData *data;
	EUIAction *action;
	const gchar *account_name;
	gchar *text;

	data = g_new0 (EM365OooNotificatorDispatcherData, 1);
	data->self = g_object_ref (self);
	data->m365_store = g_object_ref (m365_store);

	view = e_m365_ooo_notificator_get_extensible (self);
	shell_content = e_shell_view_get_shell_content (view);

	account_name = camel_service_get_display_name (CAMEL_SERVICE (m365_store));
	text = g_strdup_printf (_("Your Microsoft 365 account “%s” has the status set as “Out of Office”."), account_name);
	alert = e_alert_new ("system:simple-info", text, NULL);
	g_free (text);

	g_signal_connect_swapped (alert, "response",
		G_CALLBACK (e_m365_ooo_notificator_dismiss_cb), data);

	action = e_ui_action_new ("m365-ooo-notificator", "ooo-unset-on-server", NULL);
	e_ui_action_set_label (action, _("Unset on Server"));
	e_ui_action_set_tooltip (action, _("Unset the “Out of Office” status"));
	e_alert_add_action (alert, action, GTK_RESPONSE_ACCEPT, FALSE);
	g_object_unref (action);

	g_hash_table_insert (self->alerts, g_object_ref (m365_store), g_object_ref_sink (alert));
	e_alert_sink_submit_alert (E_ALERT_SINK (shell_content), alert);

	/* If the user doesn't cancel the notify, it will be hide automatically in 5 minutes */
	data->timeout_id = e_named_timeout_add_seconds_full (G_PRIORITY_DEFAULT, 300,
		e_m365_ooo_notificator_hide_notification_by_timeout_cb,
		data, e_m365_ooo_notificator_dispatcher_data_free);
}

static gboolean
e_m365_ooo_notificator_has_ooo_set (gpointer user_data)
{
	EM365OooNotificatorDispatcherData *data = user_data;
	CamelM365StoreOooAlertState alert_state;
	gboolean has_ooo_set;

	alert_state = camel_m365_store_get_ooo_alert_state (data->m365_store);
	has_ooo_set = camel_m365_store_get_has_ooo_set (data->m365_store);

	if (has_ooo_set && alert_state == CAMEL_M365_STORE_OOO_ALERT_STATE_UNKNOWN) {
		e_m365_ooo_notificator_show_notification (data->self, data->m365_store);
		camel_m365_store_set_ooo_alert_state (data->m365_store, CAMEL_M365_STORE_OOO_ALERT_STATE_NOTIFIED);
	}

	return FALSE;
}

static gboolean
e_m365_ooo_notificator_service_disabled (gpointer user_data)
{
	EM365OooNotificatorDispatcherData *data = user_data;
	CamelM365StoreOooAlertState state;

	e_m365_ooo_notificator_hide_notification (data->self, data->m365_store);
	state = camel_m365_store_get_ooo_alert_state (data->m365_store);
	if (state != CAMEL_M365_STORE_OOO_ALERT_STATE_CLOSED)
		camel_m365_store_set_ooo_alert_state (data->m365_store, CAMEL_M365_STORE_OOO_ALERT_STATE_UNKNOWN);
	camel_m365_store_set_has_ooo_set (data->m365_store, FALSE);

	return FALSE;
}

static gboolean
e_m365_ooo_notificator_service_removed (gpointer user_data)
{
	EM365OooNotificatorDispatcherData *data = user_data;

	e_m365_ooo_notificator_hide_notification (data->self, data->m365_store);
	g_signal_handlers_disconnect_by_func (data->m365_store,
		e_m365_ooo_notificator_has_ooo_set_cb, data->self);
	g_ptr_array_remove (data->self->stores, data->m365_store);

	return FALSE;
}

/*
 * GTK+ UI calls cannot be done in a dedicated thread.
 * So, let's ensure that our functions (that do something in the UI) will run
 * in the main thread, calling them through e_named_timeout_add_full().
 */
static void
e_m365_ooo_notificator_dispatcher (EM365OooNotificatorDispatcherData *data,
				   EM365OooNotificationDispatcherFunction function,
				   GDestroyNotify destroy_data)
{
	e_named_timeout_add_full (G_PRIORITY_DEFAULT, 1, function, data, destroy_data);
}

static void
e_m365_ooo_notificator_has_ooo_set_cb (EM365OooNotificator *self,
				       GParamSpec *pspec,
				       CamelM365Store *m365_store)
{
	EM365OooNotificatorDispatcherData *data;

	data = g_new0 (EM365OooNotificatorDispatcherData, 1);
	data->self = g_object_ref (self);
	data->m365_store = g_object_ref (m365_store);

	e_m365_ooo_notificator_dispatcher (data,
		e_m365_ooo_notificator_has_ooo_set,
		e_m365_ooo_notificator_dispatcher_data_free);
}

static void
e_m365_ooo_notificator_service_disabled_cb (EM365OooNotificator *self,
					    CamelService *service,
					    EMailAccountStore *account_store)
{

	EM365OooNotificatorDispatcherData *data;

	if (!CAMEL_IS_M365_STORE (service))
		return;

	data = g_new0 (EM365OooNotificatorDispatcherData, 1);
	data->self = g_object_ref (self);
	data->m365_store = g_object_ref (CAMEL_M365_STORE (service));

	e_m365_ooo_notificator_dispatcher (data,
		e_m365_ooo_notificator_service_disabled,
		e_m365_ooo_notificator_dispatcher_data_free);
}

static void
e_m365_ooo_notificator_service_removed_cb (EM365OooNotificator *self,
					   CamelService *service,
					   EMailAccountStore *session)
{
	EM365OooNotificatorDispatcherData *data;

	if (!CAMEL_IS_M365_STORE (service))
		return;

	data = g_new0 (EM365OooNotificatorDispatcherData, 1);
	data->self = g_object_ref (self);
	data->m365_store = g_object_ref (CAMEL_M365_STORE (service));

	e_m365_ooo_notificator_dispatcher (data,
		e_m365_ooo_notificator_service_removed,
		e_m365_ooo_notificator_dispatcher_data_free);
}

static void
e_m365_ooo_notificator_service_added_cb (EM365OooNotificator *self,
					 CamelService *service,
					 EMailAccountStore *session)
{
	CamelM365Store *m365_store;

	if (!CAMEL_IS_M365_STORE (service))
		return;

	m365_store = CAMEL_M365_STORE (service);
	g_signal_connect_swapped (m365_store, "notify::has-ooo-set",
		G_CALLBACK (e_m365_ooo_notificator_has_ooo_set_cb), self);
	g_ptr_array_add (self->stores, g_object_ref (m365_store));
}

static void
e_m365_ooo_notificator_online_cb (EM365OooNotificator *self,
				  GParamSpec *pspec,
				  EShell *shell)
{
	guint ii;

	if (e_shell_get_online (shell))
		return;

	for (ii = 0; ii < self->stores->len; ii++) {
		CamelM365Store *store = g_ptr_array_index (self->stores, ii);
		EM365OooNotificatorDispatcherData *data;

		data = g_new0 (EM365OooNotificatorDispatcherData, 1);
		data->self = g_object_ref (self);
		data->m365_store = g_object_ref (store);

		e_m365_ooo_notificator_dispatcher (data,
			e_m365_ooo_notificator_service_disabled,
			e_m365_ooo_notificator_dispatcher_data_free);
	}
}

static void
e_m365_ooo_notificator_init (EM365OooNotificator *self)
{
	self->alerts = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, g_object_unref);
	self->stores = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
e_m365_ooo_notificator_constructed (GObject *object)
{
	EM365OooNotificator *self = E_M365_OOO_NOTIFICATOR (object);
	EShell *shell;
	EShellView *view;
	EShellBackend *backend;
	EMailAccountStore *account_store;
	EMailSession *session;
	GList *stores, *link;
	const gchar *view_name;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_ooo_notificator_parent_class)->constructed (object);

	view = e_m365_ooo_notificator_get_extensible (self);
	view_name = e_shell_view_get_name (view);

	if (g_strcmp0 (view_name, "mail") != 0)
		return;

	backend = e_shell_view_get_shell_backend (view);
	shell = e_shell_backend_get_shell (backend);
	session = e_mail_backend_get_session (E_MAIL_BACKEND (backend));
	account_store = e_mail_ui_session_get_account_store (E_MAIL_UI_SESSION (session));
	stores = camel_session_list_services (CAMEL_SESSION (session));

	self->shell = shell;
	self->account_store = g_object_ref (account_store);

	for (link = stores; link; link = link->next) {
		CamelService *service = link->data;
		CamelM365Store *m365_store;
		gboolean has_ooo_set;

		if (!CAMEL_IS_M365_STORE (service))
			continue;

		m365_store = CAMEL_M365_STORE (service);
		has_ooo_set = camel_m365_store_get_has_ooo_set (m365_store);

		if (has_ooo_set) {
			e_m365_ooo_notificator_show_notification (self, m365_store);
			camel_m365_store_set_ooo_alert_state (m365_store, CAMEL_M365_STORE_OOO_ALERT_STATE_NOTIFIED);
		}

		g_signal_connect_swapped (m365_store, "notify::has-ooo-set",
			G_CALLBACK (e_m365_ooo_notificator_has_ooo_set_cb), self);
		g_ptr_array_add (self->stores, g_object_ref (m365_store));
	}

	g_signal_connect_swapped (account_store, "service-disabled",
		G_CALLBACK (e_m365_ooo_notificator_service_disabled_cb), self);

	g_signal_connect_swapped (account_store, "service-removed",
		G_CALLBACK (e_m365_ooo_notificator_service_removed_cb), self);

	g_signal_connect_swapped (account_store, "service-added",
		G_CALLBACK (e_m365_ooo_notificator_service_added_cb), self);

	g_signal_connect_swapped (shell, "notify::online",
		G_CALLBACK (e_m365_ooo_notificator_online_cb), self);

	g_list_free_full (stores, g_object_unref);
}

static void
e_m365_ooo_notificator_dispose (GObject *object)
{
	EM365OooNotificator *self = E_M365_OOO_NOTIFICATOR (object);
	guint ii;

	if (self->shell) {
		g_signal_handlers_disconnect_by_data (self->shell, self);
		self->shell = NULL;
	}

	if (self->account_store) {
		g_signal_handlers_disconnect_by_data (self->account_store, self);
		g_object_unref (self->account_store);
		self->account_store = NULL;
	}

	for (ii = 0; ii < self->stores->len; ii++) {
		CamelService *service = g_ptr_array_index (self->stores, ii);

		if (service)
			g_signal_handlers_disconnect_by_data (service, self);
	}
	g_ptr_array_remove_range (self->stores, 0, self->stores->len);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_ooo_notificator_parent_class)->dispose (object);
}

static void
e_m365_ooo_notificator_finalize (GObject *object)
{
	EM365OooNotificator *self;

	self = E_M365_OOO_NOTIFICATOR (object);

	g_clear_pointer (&self->alerts, g_hash_table_destroy);
	g_clear_pointer (&self->stores, g_ptr_array_unref);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_ooo_notificator_parent_class)->finalize (object);
}

static void
e_m365_ooo_notificator_class_init (EM365OooNotificatorClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_m365_ooo_notificator_constructed;
	object_class->dispose = e_m365_ooo_notificator_dispose;
	object_class->finalize = e_m365_ooo_notificator_finalize;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_SHELL_VIEW;
}

static void
e_m365_ooo_notificator_class_finalize (EM365OooNotificatorClass *klass)
{
}

void
e_m365_ooo_notificator_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_m365_ooo_notificator_register_type (type_module);
}
