/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <libedataserver/libedataserver.h>
#include <libecal/libecal.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>

#include "e-ews-connection.h"
#include "e-ews-connection-utils.h"
#include "e-ews-request.h"
#include "e-ews-item-change.h"
#include "e-ews-debug.h"
#include "e-ews-notification.h"
#include "e-ews-oof-settings.h"

/* A chunk size limit when moving items in chunks. */
#define EWS_MOVE_ITEMS_CHUNK_SIZE 500

#define EWS_RETRY_IO_ERROR_SECONDS 3
#define EWS_RETRY_AUTH_ERROR_SECONDS 0.1

#define QUEUE_LOCK(x) (g_rec_mutex_lock(&(x)->priv->queue_lock))
#define QUEUE_UNLOCK(x) (g_rec_mutex_unlock(&(x)->priv->queue_lock))

#define NOTIFICATION_LOCK(x) (g_mutex_lock(&(x)->priv->notification_lock))
#define NOTIFICATION_UNLOCK(x) (g_mutex_unlock(&(x)->priv->notification_lock))

static GMutex connecting;
static GHashTable *loaded_connections_permissions = NULL;

struct _EEwsSoupThreadData {
	GMutex mutex;
	GCond cond;
	GMainContext *main_context;
	GMainLoop *main_loop;
	SoupSession *session;
	GThread *thread;
};

/* Connection APIS */

struct _EEwsConnectionPrivate {
	ESource *source;
	struct _EEwsSoupThreadData soup;

	GProxyResolver *proxy_resolver;
	EEwsNotification *notification;
	guint notification_delay_id;

	CamelEwsSettings *settings;
	guint concurrent_connections;
	GMutex property_lock;
	GMutex try_credentials_lock;

	/* Hash key for the loaded_connections_permissions table. */
	gchar *hash_key;

	gchar *uri;
	gchar *email;
	gchar *impersonate_user;

	GSList *jobs;
	GSList *active_job_queue;
	GRecMutex queue_lock;
	GMutex notification_lock;

	GHashTable *subscriptions;
	GSList *subscribed_folders;
	/* The subscription ID is not tight to the actual connection, it survives
	   disconnects, thus remember it and unsubscribe from it, before adding
	   a new subscription. */
	gchar *last_subscription_id;

	EEwsServerVersion version;
	gboolean backoff_enabled;

	/* Set to TRUE when this connection had been disconnected and cannot be used anymore */
	gboolean disconnected_flag;
	gboolean testing_sources;

	gboolean ssl_info_set;
	gchar *ssl_certificate_pem;
	GTlsCertificateFlags ssl_certificate_errors;

	ENamedParameters *credentials;
	gboolean credentials_changed;
};

enum {
	PROP_0,
	PROP_PASSWORD,
	PROP_PROXY_RESOLVER,
	PROP_SETTINGS,
	PROP_SOURCE,
	PROP_CONCURRENT_CONNECTIONS
};

enum {
	SERVER_NOTIFICATION,
	PASSWORD_WILL_EXPIRE,
	SUBSCRIPTION_ID_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static guint notification_key = 1;

typedef struct _EwsUrls EwsUrls;

struct _EwsUrls {
	xmlChar *as_url;
	xmlChar *oab_url;

	/* all the below variables are for future use */
	xmlChar *oof_url;
	gpointer future1;
	gpointer future2;
};

G_DEFINE_TYPE_WITH_PRIVATE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

/*
 * e_ews_debug_handler:
 *
 * GLib debug message handler, which is passed all messages from g_debug() calls,
 * and decides whether to print them.
 */
static void
e_ews_debug_handler (const gchar *log_domain,
		     GLogLevelFlags log_level,
		     const gchar *message,
		     gpointer user_data)
{
	if (e_ews_debug_get_log_level () >= 4)
		g_log_default_handler (log_domain, log_level, message, NULL);
}

/*
 * e_ews_soup_log_print:
 *
 * Log printer for the libsoup logging functionality, which just marshal all soup log
 * output to the standard GLib logging framework (and thus to debug_handler(), above).
 */
static void
e_ews_soup_log_printer (SoupLogger *logger,
			SoupLoggerLogLevel level,
			char direction,
			const gchar *data,
			gpointer user_data)
{
	g_debug ("%c %s", direction, e_ews_debug_redact_headers (direction, data));
}

static ESoupSession *
e_ews_connection_create_soup_session (EEwsConnection *cnc)
{
	ESoupSession *session;
	gint log_level;

	session = g_object_new (E_TYPE_SOUP_SESSION,
		"source", cnc->priv->source,
		"timeout", 15,
		"max-conns", cnc->priv->concurrent_connections,
		"max-conns-per-host", cnc->priv->concurrent_connections,
		NULL);

	e_binding_bind_property (
		cnc->priv->settings, "timeout",
		session, "timeout",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		cnc->priv->settings, "force-http1",
		session, "force-http1",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		cnc, "proxy-resolver",
		session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	log_level = e_ews_debug_get_log_level ();

	if (log_level >= 1) {
		SoupLogger *logger;

		logger = soup_logger_new (log_level == 1 ? SOUP_LOGGER_LOG_HEADERS : SOUP_LOGGER_LOG_BODY);

		if (log_level >= 4)
			soup_logger_set_printer (logger, e_ews_soup_log_printer, NULL, NULL);
		else if (log_level == 1 || log_level == 2)
			soup_logger_set_printer (logger, e_ews_debug_soup_log_printer_stdout, NULL, NULL);

		soup_session_add_feature (SOUP_SESSION (session), SOUP_SESSION_FEATURE (logger));

		g_object_unref (logger);
	}

	soup_session_add_feature_by_type (SOUP_SESSION (session), SOUP_TYPE_COOKIE_JAR);

	return session;
}

static gpointer
e_ews_soup_worker_thread (gpointer user_data)
{
	EEwsConnection *cnc = user_data;
	CamelEwsSettings *settings;

	g_warn_if_fail (cnc->priv->soup.session == NULL);

	settings = e_ews_connection_ref_settings (cnc);

	g_main_context_push_thread_default (cnc->priv->soup.main_context);
	g_mutex_lock (&cnc->priv->soup.mutex);
	cnc->priv->soup.session = SOUP_SESSION (e_ews_connection_create_soup_session (cnc));

	g_cond_broadcast (&cnc->priv->soup.cond);
	g_mutex_unlock (&cnc->priv->soup.mutex);

	g_clear_object (&settings);

	g_main_loop_run (cnc->priv->soup.main_loop);

	soup_session_abort (cnc->priv->soup.session);
	g_clear_object (&cnc->priv->soup.session);

	g_main_context_pop_thread_default (cnc->priv->soup.main_context);

	return NULL;
}

static gboolean
ews_connection_credentials_failed (EEwsConnection *connection,
				   SoupMessage *message,
				   gboolean can_emit,
				   GError **error)
{
	gint expire_in_days = 0;
	gboolean expired = FALSE;
	gchar *service_url = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (connection), FALSE);

	if (!message)
		return FALSE;

	if (!e_ews_connection_utils_check_x_ms_credential_headers (message, &expire_in_days, &expired, &service_url))
		return FALSE;

	if (can_emit) {
		if (expired) {
			e_ews_connection_utils_expired_password_to_error (service_url, error);
		} else if (expire_in_days > 0) {
			g_signal_emit (connection, signals[PASSWORD_WILL_EXPIRE], 0, expire_in_days, service_url);
		}
	}

	g_free (service_url);

	return expired;
}

static gboolean
e_ews_connection_accept_certificate_for_testing_sources_cb (SoupMessage *message,
							    GTlsCertificate *tls_peer_certificate,
							    GTlsCertificateFlags tls_peer_errors,
							    gpointer user_data)
{
	return TRUE;
}

static void
e_ews_connection_maybe_prepare_message_for_testing_sources (EEwsConnection *cnc,
							    SoupMessage *message)
{
	if (e_ews_connection_get_testing_sources (cnc)) {
		g_signal_connect (message, "accept-certificate",
			G_CALLBACK (e_ews_connection_accept_certificate_for_testing_sources_cb), NULL);
	}
}

typedef struct _ProcessData {
	GMutex mutex;
	GCond cond;
	gboolean done;
	gboolean repeat;
	EEwsConnection *cnc;
	ESoapRequest *request;
	ESoapResponse *response;
	SoupMessage *message;
	gpointer prepare_data;
	GCancellable *cancellable;
	GError *error;
	gchar **out_certificate_pem;
	GTlsCertificateFlags *out_certificate_errors;
} ProcessData;

static void
e_ews_connection_process_request_ready_cb (GObject *source_object,
					   GAsyncResult *result,
					   gpointer user_data)
{
	ProcessData *pd = user_data;
	GInputStream *input_stream;

	g_mutex_lock (&pd->mutex);

	input_stream = e_soup_session_send_message_finish (E_SOUP_SESSION (source_object), result, pd->out_certificate_pem, pd->out_certificate_errors, &pd->error);

	/* Need to process the 'input_stream' in this thread */
	if (!ews_connection_credentials_failed (pd->cnc, pd->message, FALSE, NULL) &&
	    soup_message_get_status (pd->message) != SOUP_STATUS_UNAUTHORIZED &&
	    input_stream) {
		ESoapRequestCustomProcessFn custom_process_fn = NULL;
		gpointer custom_process_data = NULL;

		e_soap_request_get_custom_process_fn (pd->request, &custom_process_fn, &custom_process_data);

		if (custom_process_fn) {
			custom_process_fn (pd->request, pd->message, input_stream, custom_process_data, &pd->repeat, pd->cancellable, &pd->error);
		} else {
			pd->response = e_soap_response_new ();

			e_soap_request_setup_response (pd->request, pd->response);

			if (!e_soap_response_from_message_sync (pd->response, pd->message, input_stream, pd->cancellable, &pd->error)) {
				g_clear_object (&pd->response);

				if (!pd->error) {
					g_set_error (&pd->error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NORESPONSE,
						_("No response: %s"), soup_message_get_reason_phrase (pd->message));
				}
			}
		}
	}

	pd->done = TRUE;
	g_cond_signal (&pd->cond);
	g_mutex_unlock (&pd->mutex);

	g_clear_object (&input_stream);
}

static gboolean
e_ews_connection_process_request_run_cb (gpointer user_data)
{
	ProcessData *pd = user_data;

	if (pd->cnc->priv->soup.session) {
		e_soup_session_send_message (E_SOUP_SESSION (pd->cnc->priv->soup.session), pd->message, G_PRIORITY_DEFAULT,
			pd->prepare_data, pd->cancellable, e_ews_connection_process_request_ready_cb, pd);
	} else {
		g_mutex_lock (&pd->mutex);
		g_set_error_literal (&pd->error, G_IO_ERROR, G_IO_ERROR_FAILED, "SoupSession missing");
		pd->done = TRUE;
		g_cond_signal (&pd->cond);
		g_mutex_unlock (&pd->mutex);
	}

	return FALSE;
}

static ESoapResponse *
e_ews_connection_process_request_sync (EEwsConnection *cnc,
				       ESoapRequest *request,
				       SoupMessage **out_message,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors,
				       gboolean *out_repeat,
				       GCancellable *cancellable,
				       GError **error)
{
	ProcessData pd;
	const gchar *persistent_auth;
	CamelEwsSettings *settings;
	GSource *source;

	g_return_val_if_fail (E_IS_SOAP_REQUEST (request), NULL);
	g_return_val_if_fail (out_message != NULL, NULL);
	g_return_val_if_fail (out_repeat != NULL, NULL);

	*out_message = NULL;
	*out_repeat = FALSE;

	settings = e_ews_connection_ref_settings (cnc);

	g_mutex_lock (&cnc->priv->soup.mutex);
	if (!cnc->priv->soup.thread) {
		g_mutex_lock (&cnc->priv->property_lock);
		cnc->priv->credentials_changed = TRUE;
		g_mutex_unlock (&cnc->priv->property_lock);
		cnc->priv->soup.thread = g_thread_new ("e_ews_soup_worker_thread", e_ews_soup_worker_thread, cnc);
	}
	while (!cnc->priv->soup.session) {
		g_cond_wait (&cnc->priv->soup.cond, &cnc->priv->soup.mutex);
	}
	g_mutex_unlock (&cnc->priv->soup.mutex);

	pd.request = request;
	pd.response = NULL;
	pd.message = e_soap_request_persist (request, E_SOUP_SESSION (cnc->priv->soup.session), settings, error);

	g_clear_object (&settings);

	if (!pd.message)
		return NULL;

	e_ews_connection_maybe_prepare_message_for_testing_sources (cnc, pd.message);

	g_mutex_lock (&cnc->priv->property_lock);
	if (cnc->priv->credentials_changed) {
		cnc->priv->credentials_changed = FALSE;

		e_soup_session_set_credentials (E_SOUP_SESSION (cnc->priv->soup.session), cnc->priv->credentials);
	}
	g_mutex_unlock (&cnc->priv->property_lock);

	pd.prepare_data = e_soup_session_prepare_message_send_sync (E_SOUP_SESSION (cnc->priv->soup.session), pd.message, cancellable, error);

	if (!pd.prepare_data) {
		if (error && (g_error_matches (*error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
			      /* Returned by the OAuth2 service when the token cannot be refreshed */
			      g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED))) {
			(*error)->domain = EWS_CONNECTION_ERROR;
			(*error)->code = EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED;
		}

		g_clear_object (&pd.message);
		return NULL;
	}

	g_mutex_init (&pd.mutex);
	g_cond_init (&pd.cond);
	pd.done = FALSE;
	pd.repeat = FALSE;
	pd.cnc = cnc;
	pd.cancellable = cancellable;
	pd.error = NULL;
	pd.out_certificate_pem = out_certificate_pem;
	pd.out_certificate_errors = out_certificate_errors;

	g_mutex_lock (&pd.mutex);

	source = g_idle_source_new ();
	g_source_set_priority (source, G_PRIORITY_DEFAULT);
	g_source_set_callback (source, e_ews_connection_process_request_run_cb, &pd, NULL);
	g_source_attach (source, cnc->priv->soup.main_context);
	g_source_unref (source);

	while (!pd.done) {
		g_cond_wait (&pd.cond, &pd.mutex);
	}

	g_mutex_unlock (&pd.mutex);

	persistent_auth = soup_message_headers_get_one (soup_message_get_response_headers (pd.message), "Persistent-Auth");
	if (persistent_auth && g_ascii_strcasecmp (persistent_auth, "false") == 0) {
		SoupSessionFeature *feature;

		feature = soup_session_get_feature (cnc->priv->soup.session, SOUP_TYPE_AUTH_MANAGER);
		if (feature) {
			soup_auth_manager_clear_cached_credentials (SOUP_AUTH_MANAGER (feature));
		}
	}

	*out_message = g_steal_pointer (&pd.message);
	*out_repeat = pd.repeat;

	g_clear_object (&pd.message);
	g_mutex_clear (&pd.mutex);
	g_cond_clear (&pd.cond);

	if (pd.error)
		g_propagate_error (error, pd.error);

	return pd.response;
}

static void
e_ews_connection_wait_ms (gint wait_ms,
			  GCancellable *cancellable)
{
	if (wait_ms > 0) {
		EFlag *flag;

		flag = e_flag_new ();

		while (wait_ms > 0 && !g_cancellable_is_cancelled (cancellable)) {
			gint64 now = g_get_monotonic_time ();
			gint left_minutes, left_seconds;

			left_minutes = wait_ms / 60000;
			left_seconds = (wait_ms / 1000) % 60;

			if (left_minutes > 0) {
				camel_operation_push_message (cancellable,
					g_dngettext (GETTEXT_PACKAGE,
						"Exchange server is busy, waiting to retry (%d:%02d minute)",
						"Exchange server is busy, waiting to retry (%d:%02d minutes)", left_minutes),
					left_minutes, left_seconds);
			} else {
				camel_operation_push_message (cancellable,
					g_dngettext (GETTEXT_PACKAGE,
						"Exchange server is busy, waiting to retry (%d second)",
						"Exchange server is busy, waiting to retry (%d seconds)", left_seconds),
					left_seconds);
			}

			e_flag_wait_until (flag, now + (G_TIME_SPAN_MILLISECOND * (wait_ms > 1000 ? 1000 : wait_ms)));

			now = g_get_monotonic_time () - now;
			now = now / G_TIME_SPAN_MILLISECOND;

			if (now >= wait_ms)
				wait_ms = 0;
			wait_ms -= now;

			camel_operation_pop_message (cancellable);
		}

		e_flag_free (flag);
	}
}

static gboolean
e_ews_connection_handle_backoff_policy (EEwsConnection *cnc,
					ESoapResponse *response,
					GCancellable *cancellable,
					GError **error)
{
	ESoapParameter *param;
	gint wait_ms = 0;
	gboolean repeat = FALSE;

	if (e_ews_connection_get_backoff_enabled (cnc))
		param = e_soap_response_get_first_parameter_by_name (response, "detail", NULL);
	else
		param = NULL;
	if (param)
		param = e_soap_parameter_get_first_child_by_name (param, "ResponseCode");
	if (param) {
		gchar *value;

		value = e_soap_parameter_get_string_value (param);
		if (value && ews_get_error_code (value) == EWS_CONNECTION_ERROR_SERVERBUSY) {
			param = e_soap_response_get_first_parameter_by_name (response, "detail", NULL);
			if (param)
				param = e_soap_parameter_get_first_child_by_name (param, "MessageXml");
			if (param) {
				param = e_soap_parameter_get_first_child_by_name (param, "Value");
				if (param) {
					g_free (value);

					value = e_soap_parameter_get_property (param, "Name");
					if (g_strcmp0 (value, "BackOffMilliseconds") == 0) {
						wait_ms = e_soap_parameter_get_int_value (param);
					}
				}
			}
		}

		g_free (value);
	}

	if (wait_ms > 0) {
		e_ews_connection_wait_ms (wait_ms, cancellable);

		repeat = !g_cancellable_set_error_if_cancelled (cancellable, error);
	}

	return repeat;
}

static ESoapResponse *
e_ews_connection_send_request_sync (EEwsConnection *cnc,
				    ESoapRequest *request,
				    GCancellable *cancellable,
				    GError **error)
{
	ESoapResponse *response = NULL;
	SoupMessage *message = NULL;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	gboolean retrying_after_io_error = FALSE;
	gboolean repeat = TRUE;
	GError *local_error = NULL;

	while (repeat) {
		GError *local_error2 = NULL;

		repeat = FALSE;

		g_clear_error (&local_error);

		response = e_ews_connection_process_request_sync (cnc, request, &message, &certificate_pem, &certificate_errors, &repeat, cancellable, &local_error);

		g_mutex_lock (&cnc->priv->property_lock);
		g_clear_pointer (&cnc->priv->ssl_certificate_pem, g_free);
		cnc->priv->ssl_info_set = certificate_pem != NULL;
		cnc->priv->ssl_certificate_pem = g_strdup (certificate_pem);
		cnc->priv->ssl_certificate_errors = certificate_errors;
		g_mutex_unlock (&cnc->priv->property_lock);

		e_soap_request_take_tls_error_details (request, certificate_pem, certificate_errors);

		if (message && !ews_connection_credentials_failed (cnc, message, TRUE, &local_error2)) {
			if (soup_message_get_status (message) == SOUP_STATUS_UNAUTHORIZED) {
				/* This can happen when the server terminated the connection before the request started */
				if (!retrying_after_io_error &&
				    soup_message_get_request_headers (message) &&
				    !soup_message_headers_get_one (soup_message_get_request_headers (message), "Authorization")) {
					g_clear_error (&local_error2);

					e_ews_connection_wait_ms (EWS_RETRY_AUTH_ERROR_SECONDS * 1000, cancellable);

					retrying_after_io_error = TRUE;
					repeat = !g_cancellable_set_error_if_cancelled (cancellable, &local_error2);
				} else {
					if (soup_message_get_response_headers (message)) {
						const gchar *diagnostics;

						diagnostics = soup_message_headers_get_list (soup_message_get_response_headers (message), "X-MS-DIAGNOSTICS");
						if (diagnostics && strstr (diagnostics, "invalid_grant")) {
							g_clear_error (&local_error2);
							g_set_error_literal (&local_error2, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ACCESSDENIED, diagnostics);
						} else if (diagnostics && *diagnostics) {
							g_clear_error (&local_error2);
							g_set_error_literal (&local_error2, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED, diagnostics);
						}
					}

					if (!local_error2)
						g_set_error_literal (&local_error2, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED, _("Authentication failed"));
				}
			}
		}

		if (local_error2) {
			g_clear_error (&local_error);
			local_error = local_error2;
		}

		if (!local_error && response && !repeat) {
			repeat = e_ews_connection_handle_backoff_policy (cnc, response, cancellable, &local_error);

			if (repeat || local_error)
				g_clear_object (&response);
		}

		if (!repeat && !retrying_after_io_error && local_error &&
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT)) {
			g_clear_object (&response);
			g_clear_error (&local_error);

			e_ews_connection_wait_ms (EWS_RETRY_IO_ERROR_SECONDS * 1000, cancellable);

			retrying_after_io_error = TRUE;
			repeat = !g_cancellable_set_error_if_cancelled (cancellable, &local_error);
		}

		if (repeat)
			g_clear_object (&response);
		g_clear_object (&message);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (response && cnc->priv->version == E_EWS_EXCHANGE_UNKNOWN) {
		ESoapParameter *param;

		param = e_soap_response_get_first_parameter_by_name (response, "ServerVersionInfo", NULL);
		if (param) {
			gchar *version;

			version = e_soap_parameter_get_property (param, "Version");

			e_ews_connection_set_server_version_from_string (cnc, version);

			g_free (version);
		}
	}

	return response;
}

static guint
ews_connection_get_concurrent_connections (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), 1);

	return cnc->priv->concurrent_connections;
}

static void
ews_connection_set_concurrent_connections (EEwsConnection *cnc,
					   guint concurrent_connections)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	concurrent_connections = CLAMP (
		concurrent_connections,
		MIN_CONCURRENT_CONNECTIONS,
		MAX_CONCURRENT_CONNECTIONS);

	if (cnc->priv->concurrent_connections == concurrent_connections)
		return;

	/* Will be updated in the priv->soup.session the next time it's created,
	   because "max-conns" is a construct-only property */
	cnc->priv->concurrent_connections = concurrent_connections;

	g_object_notify (G_OBJECT (cnc), "concurrent-connections");
}

GQuark
ews_connection_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "ews-connection-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

EEwsNotificationEvent *
e_ews_notification_event_new (void)
{
	return g_new0 (EEwsNotificationEvent, 1);
}

void
e_ews_notification_event_free (EEwsNotificationEvent *event)
{
	if (event != NULL) {
		g_free (event->folder_id);
		g_free (event->old_folder_id);
		g_free (event);
	}
}

EEwsCalendarTo *
e_ews_calendar_to_new (void)
{
	return g_new0 (EEwsCalendarTo, 1);
}

void
e_ews_calendar_to_free (EEwsCalendarTo *to) {
	if (to != NULL) {
		g_free (to->kind);
		g_free (to->value);
		g_free (to);
	}
}

EEwsCalendarAbsoluteDateTransition *
e_ews_calendar_absolute_date_transition_new (void)
{
	return g_new0 (EEwsCalendarAbsoluteDateTransition, 1);
}

void
e_ews_calendar_absolute_date_transition_free (EEwsCalendarAbsoluteDateTransition *adt)
{
	if (adt != NULL) {
		e_ews_calendar_to_free (adt->to);
		g_free (adt->date_time);
		g_free (adt);
	}
}

EEwsCalendarRecurringDayTransition *
e_ews_calendar_recurring_day_transition_new (void)
{
	return g_new0 (EEwsCalendarRecurringDayTransition, 1);
}

void
e_ews_calendar_recurring_day_transition_free (EEwsCalendarRecurringDayTransition *rdayt)
{
	if (rdayt != NULL) {
		e_ews_calendar_to_free (rdayt->to);
		g_free (rdayt->time_offset);
		g_free (rdayt->month);
		g_free (rdayt->day_of_week);
		g_free (rdayt->occurrence);
		g_free (rdayt);
	}
}

EEwsCalendarRecurringDateTransition *
e_ews_calendar_recurring_date_transition_new (void)
{
	return g_new0 (EEwsCalendarRecurringDateTransition, 1);
}

void
e_ews_calendar_recurring_date_transition_free (EEwsCalendarRecurringDateTransition *rdatet)
{
	if (rdatet != NULL) {
		e_ews_calendar_to_free (rdatet->to);
		g_free (rdatet->time_offset);
		g_free (rdatet->month);
		g_free (rdatet->day);
		g_free (rdatet);
	}
}

EEwsCalendarPeriod *
e_ews_calendar_period_new (void)
{
	return g_new0 (EEwsCalendarPeriod, 1);
}

void
e_ews_calendar_period_free (EEwsCalendarPeriod *period)
{
	if (period != NULL) {
		g_free (period->bias);
		g_free (period->name);
		g_free (period->id);
		g_free (period);
	}
}

EEwsCalendarTransitionsGroup *
e_ews_calendar_transitions_group_new (void)
{
	return g_new0 (EEwsCalendarTransitionsGroup, 1);
}

void
e_ews_calendar_transitions_group_free (EEwsCalendarTransitionsGroup *tg)
{
	if (tg != NULL) {
		g_free (tg->id);
		e_ews_calendar_to_free (tg->transition);
		g_slist_free_full (
			tg->absolute_date_transitions,
			(GDestroyNotify) e_ews_calendar_absolute_date_transition_free);
		g_slist_free_full (
			tg->recurring_day_transitions,
			(GDestroyNotify) e_ews_calendar_recurring_day_transition_free);
		g_slist_free_full (
			tg->recurring_date_transitions,
			(GDestroyNotify) e_ews_calendar_recurring_date_transition_free);
		g_free (tg);
	}
}

EEwsCalendarTransitions *
e_ews_calendar_transitions_new (void)
{
	return g_new0 (EEwsCalendarTransitions, 1);
}

void
e_ews_calendar_transitions_free (EEwsCalendarTransitions *transitions)
{
	if (transitions != NULL) {
		e_ews_calendar_to_free (transitions->transition);
		g_slist_free_full (
			transitions->absolute_date_transitions,
			(GDestroyNotify) e_ews_calendar_absolute_date_transition_free);
		g_slist_free_full (
			transitions->recurring_day_transitions,
			(GDestroyNotify) e_ews_calendar_recurring_day_transition_free);
		g_slist_free_full (
			transitions->recurring_date_transitions,
			(GDestroyNotify) e_ews_calendar_recurring_date_transition_free);
		g_free (transitions);
	}
}

EEwsCalendarTimeZoneDefinition *
e_ews_calendar_time_zone_definition_new (void)
{
	return g_new0 (EEwsCalendarTimeZoneDefinition, 1);
}

void
e_ews_calendar_time_zone_definition_free (EEwsCalendarTimeZoneDefinition *tzd)
{
	if (tzd != NULL) {
		g_free (tzd->name);
		g_free (tzd->id);
		g_slist_free_full (tzd->periods, (GDestroyNotify) e_ews_calendar_period_free);
		g_slist_free_full (tzd->transitions_groups, (GDestroyNotify) e_ews_calendar_transitions_group_free);
		e_ews_calendar_transitions_free (tzd->transitions);
		g_free (tzd);
	}
}

EEwsExtendedFieldURI *
e_ews_extended_field_uri_new (void)
{
	return g_new0 (EEwsExtendedFieldURI, 1);
}

void
e_ews_extended_field_uri_free (EEwsExtendedFieldURI *ex_field_uri)
{
	if (ex_field_uri != NULL) {
		g_free (ex_field_uri->distinguished_prop_set_id);
		g_free (ex_field_uri->prop_set_id);
		g_free (ex_field_uri->prop_tag);
		g_free (ex_field_uri->prop_name);
		g_free (ex_field_uri->prop_id);
		g_free (ex_field_uri->prop_type);
		g_free (ex_field_uri);
	}
}

EEwsIndexedFieldURI *
e_ews_indexed_field_uri_new (const gchar *uri,
			     const gchar *index)
{
	EEwsIndexedFieldURI *furi;

	furi = g_new0 (EEwsIndexedFieldURI, 1);
	furi->field_uri = g_strdup (uri);
	furi->field_index = g_strdup (index);

	return furi;
}

void
e_ews_indexed_field_uri_free (EEwsIndexedFieldURI *id_field_uri)
{
	if (id_field_uri != NULL) {
		g_free (id_field_uri->field_uri);
		g_free (id_field_uri->field_index);
		g_free (id_field_uri);
	}
}

EEwsAdditionalProps *
e_ews_additional_props_new (void)
{
	return g_new0 (EEwsAdditionalProps, 1);
}

void
e_ews_additional_props_free (EEwsAdditionalProps *add_props)
{
	if (add_props != NULL) {
		g_free (add_props->field_uri);
		g_slist_free_full (add_props->extended_furis, (GDestroyNotify) e_ews_extended_field_uri_free);
		g_slist_free_full (add_props->indexed_furis, (GDestroyNotify) e_ews_indexed_field_uri_free);
		g_free (add_props);
	}
}

static void
autodiscover_parse_protocol (xmlNode *node,
                             EwsUrls *urls)
{
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "ASUrl")) {
			if (urls->as_url != NULL)
				xmlFree (urls->as_url);

			urls->as_url = xmlNodeGetContent (node);
		} else if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "OABUrl")) {
			if (urls->oab_url != NULL)
				xmlFree (urls->oab_url);

			urls->oab_url = xmlNodeGetContent (node);
		}

		/* Once we find both, we can stop looking for the URLs */
		if (urls->as_url && urls->oab_url)
			return;
	}
}

static xmlChar *
autodiscover_get_protocol_type (xmlNode *node)
{
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "Type")) {
			return xmlNodeGetContent (node);
		}
	}

	return NULL;
}

static gchar *
autodiscover_dup_element_value (xmlNode *node,
				const gchar *element_name)
{
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !g_strcmp0 ((gchar *) node->name, element_name)) {
			xmlChar *str = xmlNodeGetContent (node);
			gchar *res;

			res = g_strdup ((const gchar *) str);
			xmlFree (str);
			return res;
		}
	}

	return NULL;
}

static void
ews_connection_set_settings (EEwsConnection *connection,
                             CamelEwsSettings *settings)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));
	g_return_if_fail (connection->priv->settings == NULL);

	connection->priv->settings = g_object_ref (settings);

	e_binding_bind_property (connection->priv->settings, "concurrent-connections",
		connection, "concurrent-connections",
		G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
}

static void
ews_connection_set_source (EEwsConnection *connection,
			   ESource *source)
{
	if (source)
		g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (connection->priv->source == NULL);

	connection->priv->source = source ? g_object_ref (source) : NULL;
}

static void
ews_connection_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PASSWORD:
			e_ews_connection_set_password (
				E_EWS_CONNECTION (object),
				g_value_get_string (value));
			return;

		case PROP_PROXY_RESOLVER:
			e_ews_connection_set_proxy_resolver (
				E_EWS_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SETTINGS:
			ews_connection_set_settings (
				E_EWS_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE:
			ews_connection_set_source (
				E_EWS_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			ews_connection_set_concurrent_connections (
				E_EWS_CONNECTION (object),
				g_value_get_uint (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_connection_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PASSWORD:
			g_value_take_string (
				value,
				e_ews_connection_dup_password (
				E_EWS_CONNECTION (object)));
			return;

		case PROP_PROXY_RESOLVER:
			g_value_take_object (
				value,
				e_ews_connection_ref_proxy_resolver (
				E_EWS_CONNECTION (object)));
			return;

		case PROP_SETTINGS:
			g_value_take_object (
				value,
				e_ews_connection_ref_settings (
				E_EWS_CONNECTION (object)));
			return;

		case PROP_SOURCE:
			g_value_set_object (
				value,
				e_ews_connection_get_source (
				E_EWS_CONNECTION (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				ews_connection_get_concurrent_connections (
				E_EWS_CONNECTION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_connection_constructed (GObject *object)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_connection_parent_class)->constructed (object);

	if (cnc->priv->source && cnc->priv->settings &&
	    e_source_has_extension (cnc->priv->source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;
		gchar *auth_method_source;
		const gchar *auth_method_settings;

		auth_extension = e_source_get_extension (cnc->priv->source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_method_source = e_source_authentication_dup_method (auth_extension);
		auth_method_settings = camel_ews_settings_get_auth_mechanism_string (cnc->priv->settings);

		/* Make sure the ESource and the CamelEwsSettings authentication methods correspond
		   to each other, because the NTLM is a default for any unknown value, which should
		   be propagated into the ESoupSession (through the ESource) as well. */
		if (!auth_method_source ||
		    (g_ascii_strcasecmp (auth_method_source, "Microsoft365") != 0 &&
		    auth_method_settings && g_ascii_strcasecmp (auth_method_source, auth_method_settings) != 0)) {
			e_source_authentication_set_method (auth_extension, auth_method_settings);
		}

		g_free (auth_method_source);
	}

	if (cnc->priv->source && cnc->priv->settings) {
		ESourceExtension *extension;

		/* the ESoupSession can read timeout from the WebDAV Backend extension,
		   thus make sure the options are in sync */
		extension = e_source_get_extension (cnc->priv->source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		e_binding_bind_property (
			cnc->priv->settings, "timeout",
			extension, "timeout",
			G_BINDING_SYNC_CREATE);
	}
}

static void
ews_connection_dispose (GObject *object)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);

	g_mutex_lock (&connecting);

	/* remove the connection from the hash table */
	if (loaded_connections_permissions != NULL &&
	    g_hash_table_lookup (loaded_connections_permissions, cnc->priv->hash_key) == (gpointer) object) {
		g_hash_table_remove (loaded_connections_permissions, cnc->priv->hash_key);
		if (g_hash_table_size (loaded_connections_permissions) == 0) {
			g_hash_table_destroy (loaded_connections_permissions);
			loaded_connections_permissions = NULL;
		}
	}

	g_mutex_unlock (&connecting);

	NOTIFICATION_LOCK (cnc);
	if (cnc->priv->notification_delay_id) {
		g_source_remove (cnc->priv->notification_delay_id);
		cnc->priv->notification_delay_id = 0;
	}

	if (cnc->priv->notification) {
		e_ews_notification_stop_listening_sync (cnc->priv->notification);
		g_clear_object (&cnc->priv->notification);
	}
	NOTIFICATION_UNLOCK (cnc);

	g_mutex_lock (&cnc->priv->soup.mutex);

	if (cnc->priv->soup.main_loop) {
		g_main_loop_quit (cnc->priv->soup.main_loop);

		if (cnc->priv->soup.thread) {
			g_thread_join (cnc->priv->soup.thread);
			cnc->priv->soup.thread = NULL;
		}

		g_main_loop_unref (cnc->priv->soup.main_loop);
		g_main_context_unref (cnc->priv->soup.main_context);

		cnc->priv->soup.main_loop = NULL;
		cnc->priv->soup.main_context = NULL;

		/* The soup thread creates and frees the session */
		g_warn_if_fail (cnc->priv->soup.session == NULL);
	}

	g_mutex_unlock (&cnc->priv->soup.mutex);

	g_clear_object (&cnc->priv->proxy_resolver);
	g_clear_object (&cnc->priv->source);
	g_clear_object (&cnc->priv->settings);

	e_ews_connection_set_password (cnc, NULL);

	QUEUE_LOCK (cnc);
	g_slist_free (cnc->priv->jobs);
	cnc->priv->jobs = NULL;

	g_slist_free (cnc->priv->active_job_queue);
	cnc->priv->active_job_queue = NULL;
	QUEUE_UNLOCK (cnc);

	g_slist_free_full (cnc->priv->subscribed_folders, g_free);
	cnc->priv->subscribed_folders = NULL;

	if (cnc->priv->subscriptions != NULL) {
		g_hash_table_destroy (cnc->priv->subscriptions);
		cnc->priv->subscriptions = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_connection_parent_class)->dispose (object);
}

static void
ews_connection_finalize (GObject *object)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);

	e_named_parameters_free (cnc->priv->credentials);
	g_free (cnc->priv->uri);
	g_free (cnc->priv->email);
	g_free (cnc->priv->hash_key);
	g_free (cnc->priv->impersonate_user);
	g_free (cnc->priv->ssl_certificate_pem);
	g_free (cnc->priv->last_subscription_id);

	g_mutex_clear (&cnc->priv->property_lock);
	g_mutex_clear (&cnc->priv->try_credentials_lock);
	g_rec_mutex_clear (&cnc->priv->queue_lock);
	g_mutex_clear (&cnc->priv->notification_lock);

	g_mutex_clear (&cnc->priv->soup.mutex);
	g_cond_clear (&cnc->priv->soup.cond);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_connection_parent_class)->finalize (object);
}

static void
e_ews_connection_class_init (EEwsConnectionClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_connection_set_property;
	object_class->get_property = ews_connection_get_property;
	object_class->constructed = ews_connection_constructed;
	object_class->dispose = ews_connection_dispose;
	object_class->finalize = ews_connection_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PASSWORD,
		g_param_spec_string (
			"password",
			"Password",
			"Authentication password",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PROXY_RESOLVER,
		g_param_spec_object (
			"proxy-resolver",
			"Proxy Resolver",
			"The proxy resolver for this backend",
			G_TYPE_PROXY_RESOLVER,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SETTINGS,
		g_param_spec_object (
			"settings",
			"Settings",
			"Connection settings",
			CAMEL_TYPE_EWS_SETTINGS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"Corresponding ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CONCURRENT_CONNECTIONS,
		g_param_spec_uint (
			"concurrent-connections",
			"Concurrent Connections",
			"Number of concurrent connections to use",
			MIN_CONCURRENT_CONNECTIONS,
			MAX_CONCURRENT_CONNECTIONS,
			1,
			/* Do not construct it, otherwise it overrides the value derived from CamelEwsSettings */
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	signals[SERVER_NOTIFICATION] = g_signal_new (
		"server-notification",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED | G_SIGNAL_ACTION,
		0, NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[PASSWORD_WILL_EXPIRE] = g_signal_new (
		"password-will-expire",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (EEwsConnectionClass, password_will_expire),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_INT,
		G_TYPE_STRING);

	signals[SUBSCRIPTION_ID_CHANGED] = g_signal_new (
		"subscription-id-changed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);

	if (e_ews_debug_get_log_level () >= 4) {
		g_log_set_handler (
			G_LOG_DOMAIN,
			G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING |
			G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO,
			e_ews_debug_handler, NULL);
	}
}

static void
e_ews_connection_folders_list_free (gpointer data)
{
	g_slist_free_full ((GSList *) data, g_free);
}

static void
e_ews_connection_init (EEwsConnection *cnc)
{
	cnc->priv = e_ews_connection_get_instance_private (cnc);

	g_mutex_init (&cnc->priv->soup.mutex);
	g_cond_init (&cnc->priv->soup.cond);
	cnc->priv->soup.main_context = g_main_context_new ();
	cnc->priv->soup.main_loop = g_main_loop_new (cnc->priv->soup.main_context, FALSE);

	cnc->priv->backoff_enabled = TRUE;
	cnc->priv->disconnected_flag = FALSE;
	cnc->priv->concurrent_connections = 1;
	cnc->priv->version = E_EWS_EXCHANGE_UNKNOWN;

	cnc->priv->subscriptions = g_hash_table_new_full (
			g_direct_hash, g_direct_equal,
			NULL, e_ews_connection_folders_list_free);

	g_mutex_init (&cnc->priv->property_lock);
	g_mutex_init (&cnc->priv->try_credentials_lock);
	g_rec_mutex_init (&cnc->priv->queue_lock);
	g_mutex_init (&cnc->priv->notification_lock);
}

void
ews_oal_free (EwsOAL *oal)
{
	if (oal != NULL) {
		g_free (oal->id);
		g_free (oal->dn);
		g_free (oal->name);
		g_free (oal);
	}
}

void
ews_oal_details_free (EwsOALDetails *details)
{
	if (details != NULL) {
		g_free (details->type);
		g_free (details->sha);
		g_free (details->filename);
		g_free (details);
	}
}

void
ews_user_id_free (EwsUserId *id)
{
	if (id) {
		g_free (id->sid);
		g_free (id->primary_smtp);
		g_free (id->display_name);
		g_free (id->distinguished_user);
		g_free (id->external_user);
		g_free (id);
	}
}

void
ews_delegate_info_free (EwsDelegateInfo *info)
{
	if (!info)
		return;

	ews_user_id_free (info->user_id);
	g_free (info);
}

EEwsAttachmentInfo *
e_ews_attachment_info_new (EEwsAttachmentInfoType type)
{
	EEwsAttachmentInfo *info;
	info = g_new0 (EEwsAttachmentInfo, 1);

	info->type = type;
	return info;
}

void
e_ews_attachment_info_free (EEwsAttachmentInfo *info)
{
	if (!info)
		return;

	switch (info->type) {
	case E_EWS_ATTACHMENT_INFO_TYPE_INLINED:
		g_free (info->data.inlined.filename);
		g_free (info->data.inlined.mime_type);
		g_free (info->data.inlined.data);
		break;
	case E_EWS_ATTACHMENT_INFO_TYPE_URI:
		g_free (info->data.uri);
		break;
	default:
		g_warning ("Unknown EEwsAttachmentInfoType %d", info->type);
		break;
	}

	g_free (info->prefer_filename);
	g_free (info->id);
	g_free (info);
}

EEwsAttachmentInfoType
e_ews_attachment_info_get_type (EEwsAttachmentInfo *info)
{
	return info->type;
}

const gchar *
e_ews_attachment_info_get_prefer_filename (EEwsAttachmentInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->prefer_filename;
}

void
e_ews_attachment_info_set_prefer_filename (EEwsAttachmentInfo *info,
					   const gchar *prefer_filename)
{
	g_return_if_fail (info != NULL);

	if (info->prefer_filename == prefer_filename)
		return;

	g_free (info->prefer_filename);
	info->prefer_filename = g_strdup (prefer_filename);
}

const gchar *
e_ews_attachment_info_get_inlined_data (EEwsAttachmentInfo *info,
					gsize *len)
{
	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED, NULL);

	*len = info->data.inlined.length;
	return info->data.inlined.data;
}

void
e_ews_attachment_info_set_inlined_data (EEwsAttachmentInfo *info,
					const guchar *data,
					gsize len)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED);

	info->data.inlined.data = g_malloc (len);
	memcpy (info->data.inlined.data, data, len);
	info->data.inlined.length = len;
}

const gchar *
e_ews_attachment_info_get_mime_type (EEwsAttachmentInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED, NULL);

	return info->data.inlined.mime_type;
}

void
e_ews_attachment_info_set_mime_type (EEwsAttachmentInfo *info,
				     const gchar *mime_type)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED);

	g_free (info->data.inlined.mime_type);
	info->data.inlined.mime_type = g_strdup (mime_type);
}

const gchar *
e_ews_attachment_info_get_filename (EEwsAttachmentInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED, NULL);

	return info->data.inlined.filename;
}

void
e_ews_attachment_info_set_filename (EEwsAttachmentInfo *info,
				    const gchar *filename)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_INLINED);

	g_free (info->data.inlined.filename);
	info->data.inlined.filename = g_strdup (filename);
}

const gchar *
e_ews_attachment_info_get_uri (EEwsAttachmentInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_URI, NULL);

	return info->data.uri;
}

void
e_ews_attachment_info_set_uri (EEwsAttachmentInfo *info,
			       const gchar *uri)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->type == E_EWS_ATTACHMENT_INFO_TYPE_URI);

	g_free (info->data.uri);
	info->data.uri = g_strdup (uri);
}

const gchar *
e_ews_attachment_info_get_id (EEwsAttachmentInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->id;
}

void
e_ews_attachment_info_set_id (EEwsAttachmentInfo *info,
			      const gchar *id)
{
	g_return_if_fail (info != NULL);

	if (info->id != id) {
		g_free (info->id);
		info->id = g_strdup (id);
	}
}

/* Connection APIS */

static gchar *
e_ews_connection_construct_hash_key (const gchar *uri,
				     CamelEwsSettings *ews_settings)
{
	gchar *impersonate_user = NULL, *user, *hash_key;

	user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (ews_settings));

	if (camel_ews_settings_get_use_impersonation (ews_settings))
		impersonate_user = camel_ews_settings_dup_impersonate_user (ews_settings);

	if (impersonate_user && *impersonate_user)
		hash_key = g_strdup_printf ("%s#%s@%s", impersonate_user, user, uri);
	else
		hash_key = g_strdup_printf ("%s@%s", user, uri);

	g_free (impersonate_user);
	g_free (user);

	return hash_key;
}

/**
 * e_ews_connection_find
 * @uri: Exchange server uri
 * @ews_settings: a #CamelEwsSettings
 *
 * Find an existing connection for this user/uri, if it exists.
 *
 * Returns: EEwsConnection
 **/
EEwsConnection *
e_ews_connection_find (const gchar *uri,
                       CamelEwsSettings *ews_settings)
{
	EEwsConnection *cnc;
	gchar *hash_key;

	g_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = e_ews_connection_construct_hash_key (uri, ews_settings);
		cnc = g_hash_table_lookup (loaded_connections_permissions, hash_key);
		g_free (hash_key);

		if (E_IS_EWS_CONNECTION (cnc) &&
		    !e_ews_connection_get_disconnected_flag (cnc)) {
			g_object_ref (cnc);
			g_mutex_unlock (&connecting);
			return cnc;
		}
	}

	g_mutex_unlock (&connecting);

	return NULL;
}

/**
 * e_ews_connection_list_existing:
 *
 * Returns: (transfer full) (element-type EEwsConnection): a new #GSList of all currently
 *    opened connections to all servers. Free the returned #GSList with
 *    g_slist_free_full (connections, g_object_unref);
 *    when no longer needed.
 **/
GSList * /* EEwsConnection * */
e_ews_connection_list_existing (void)
{
	GSList *connections = NULL;

	g_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, loaded_connections_permissions);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			if (value && !e_ews_connection_get_disconnected_flag (value))
				connections = g_slist_prepend (connections, g_object_ref (value));
		}
	}

	g_mutex_unlock (&connecting);

	return connections;
}

/**
 * e_ews_connection_new_full
 * @source: corresponding #ESource
 * @uri: Exchange server uri
 * @settings: a #CamelEwsSettings
 * @allow_connection_reuse: whether can return already created connection
 *
 * This does not authenticate to the server. It merely stores the username and password.
 * Authentication happens when a request is made to the server.
 *
 * Returns: EEwsConnection
 **/
EEwsConnection *
e_ews_connection_new_full (ESource *source,
			   const gchar *uri,
			   CamelEwsSettings *settings,
			   gboolean allow_connection_reuse)
{
	EEwsConnection *cnc;
	gchar *hash_key;

	if (source)
		g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	hash_key = e_ews_connection_construct_hash_key (uri, settings);

	g_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (allow_connection_reuse && loaded_connections_permissions != NULL) {
		cnc = g_hash_table_lookup (
			loaded_connections_permissions, hash_key);

		if (E_IS_EWS_CONNECTION (cnc) &&
		    !e_ews_connection_get_disconnected_flag (cnc)) {
			g_object_ref (cnc);

			g_free (hash_key);

			g_mutex_unlock (&connecting);
			return cnc;
		}
	}

	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_EWS_CONNECTION,
		"settings", settings,
		"source", source,
		NULL);

	cnc->priv->uri = g_strdup (uri);
	cnc->priv->hash_key = hash_key;  /* takes ownership */

	g_free (cnc->priv->impersonate_user);
	if (camel_ews_settings_get_use_impersonation (settings)) {
		cnc->priv->impersonate_user = camel_ews_settings_dup_impersonate_user (settings);
		if (cnc->priv->impersonate_user && !*cnc->priv->impersonate_user) {
			g_free (cnc->priv->impersonate_user);
			cnc->priv->impersonate_user = NULL;
		}
	} else {
		cnc->priv->impersonate_user = NULL;
	}

	if (allow_connection_reuse) {
		/* add the connection to the loaded_connections_permissions hash table */
		if (loaded_connections_permissions == NULL)
			loaded_connections_permissions = g_hash_table_new_full (
				g_str_hash, g_str_equal,
				g_free, NULL);
		g_hash_table_insert (
			loaded_connections_permissions,
			g_strdup (cnc->priv->hash_key), cnc);
	}

	/* free memory */
	g_mutex_unlock (&connecting);
	return cnc;

}

EEwsConnection *
e_ews_connection_new (ESource *source,
		      const gchar *uri,
		      CamelEwsSettings *settings)
{
	return e_ews_connection_new_full (source, uri, settings, TRUE);
}

EEwsConnection *
e_ews_connection_new_for_backend (EBackend *backend,
				  ESourceRegistry *registry,
				  const gchar *uri,
				  CamelEwsSettings *settings)
{
	ESource *source;
	EEwsConnection *cnc;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	source = e_backend_get_source (backend);
	if (!source)
		return e_ews_connection_new (source, uri, settings);

	g_object_ref (source);

	while (source && !e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION) &&
	       e_source_get_parent (source)) {
		ESource *parent;

		parent = e_source_registry_ref_source (registry, e_source_get_parent (source));
		if (!parent) {
			g_clear_object (&source);
			break;
		}

		g_object_unref (source);
		source = parent;
	}

	if (source)
		cnc = e_ews_connection_new (source, uri, settings);
	else
		cnc = e_ews_connection_new (e_backend_get_source (backend), uri, settings);

	g_clear_object (&source);

	return cnc;
}

void
e_ews_connection_set_testing_sources (EEwsConnection *cnc,
				      gboolean testing_sources)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	cnc->priv->testing_sources = testing_sources;
}

gboolean
e_ews_connection_get_testing_sources (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	return cnc->priv->testing_sources;
}

void
e_ews_connection_update_credentials (EEwsConnection *cnc,
				     const ENamedParameters *credentials)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	if (credentials) {
		if (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME)) {
			CamelNetworkSettings *network_settings;

			network_settings = CAMEL_NETWORK_SETTINGS (cnc->priv->settings);
			camel_network_settings_set_user (network_settings, e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME));
		}
	} else {
		e_ews_connection_set_password (cnc, NULL);
	}

	g_mutex_lock (&cnc->priv->property_lock);
	cnc->priv->credentials_changed = TRUE;
	e_named_parameters_free (cnc->priv->credentials);
	cnc->priv->credentials = credentials ? e_named_parameters_new_clone (credentials) : NULL;
	g_mutex_unlock (&cnc->priv->property_lock);
}

ESourceAuthenticationResult
e_ews_connection_try_credentials_sync (EEwsConnection *cnc,
				       const ENamedParameters *credentials,
				       ESource *use_source,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors,
				       GCancellable *cancellable,
				       GError **error)
{
	ESourceAuthenticationResult result;
	ESource *source;
	gboolean de_set_source;
	EwsFolderId *fid = NULL;
	GSList *ids = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), E_SOURCE_AUTHENTICATION_ERROR);

	/* when sharing the connection between multiple sources (like in the book/calendar factory),
	   make sure only one try_credentials() is running at a single time */
	g_mutex_lock (&cnc->priv->try_credentials_lock);

	e_ews_connection_update_credentials (cnc, credentials);

	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup ("inbox");
	fid->is_distinguished_id = TRUE;
	ids = g_slist_append (ids, fid);

	source = e_ews_connection_get_source (cnc);
	if (use_source && use_source != source) {
		cnc->priv->source = g_object_ref (use_source);
		de_set_source = TRUE;
	} else {
		source = NULL;
		de_set_source = FALSE;
	}

	e_ews_connection_get_folder_sync (
		cnc, EWS_PRIORITY_MEDIUM, "Default",
		NULL, ids, NULL, cancellable, &local_error);

	if (de_set_source) {
		g_clear_object (&cnc->priv->source);
		cnc->priv->source = source;
	}

	g_slist_free_full (ids, (GDestroyNotify) e_ews_folder_id_free);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE) &&
		   e_ews_connection_get_ssl_error_details (cnc, out_certificate_pem, out_certificate_errors)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
	} else {
		gboolean auth_failed;

		auth_failed = g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED);
		if (!auth_failed && g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN) &&
		    (!credentials || !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)))
			auth_failed = TRUE;

		if (auth_failed) {
			g_clear_error (&local_error);

			if (camel_ews_settings_get_auth_mechanism (cnc->priv->settings) != EWS_AUTH_TYPE_GSSAPI &&
			    camel_ews_settings_get_auth_mechanism (cnc->priv->settings) != EWS_AUTH_TYPE_OAUTH2 &&
			    (!credentials || !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD))) {
				result = E_SOURCE_AUTHENTICATION_REQUIRED;
			} else {
				result = E_SOURCE_AUTHENTICATION_REJECTED;
			}
		} else {
			g_propagate_error (error, local_error);
			result = E_SOURCE_AUTHENTICATION_ERROR;
		}

		e_ews_connection_set_password (cnc, NULL);
	}

	g_mutex_unlock (&cnc->priv->try_credentials_lock);

	return result;
}

ESource *
e_ews_connection_get_source (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	return cnc->priv->source;
}

gboolean
e_ews_connection_get_ssl_error_details (EEwsConnection *cnc,
					gchar **out_certificate_pem,
					GTlsCertificateFlags *out_certificate_errors)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_certificate_pem != NULL, FALSE);
	g_return_val_if_fail (out_certificate_errors != NULL, FALSE);

	g_mutex_lock (&cnc->priv->property_lock);
	if (!cnc->priv->ssl_info_set) {
		g_mutex_unlock (&cnc->priv->property_lock);
		return FALSE;
	}

	*out_certificate_pem = g_strdup (cnc->priv->ssl_certificate_pem);
	*out_certificate_errors = cnc->priv->ssl_certificate_errors;

	g_mutex_unlock (&cnc->priv->property_lock);

	return TRUE;
}

const gchar *
e_ews_connection_get_uri (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	return cnc->priv->uri;
}

gchar *
e_ews_connection_dup_password (EEwsConnection *cnc)
{
	const gchar *password;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	g_mutex_lock (&cnc->priv->property_lock);

	password = cnc->priv->credentials ? e_named_parameters_get (cnc->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD) : NULL;
	duplicate = g_strdup (password && *password ? password : NULL);

	g_mutex_unlock (&cnc->priv->property_lock);

	return duplicate;
}

void
e_ews_connection_set_password (EEwsConnection *cnc,
                               const gchar *password)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	g_mutex_lock (&cnc->priv->property_lock);

	if (cnc->priv->credentials) {
		cnc->priv->credentials_changed = TRUE;

		if (password && *password)
			e_named_parameters_set (cnc->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, password);
		else
			e_named_parameters_set (cnc->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, NULL);
	} else if (password && *password) {
		cnc->priv->credentials_changed = TRUE;
		cnc->priv->credentials = e_named_parameters_new ();
		e_named_parameters_set (cnc->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD, password);
	}

	g_mutex_unlock (&cnc->priv->property_lock);

	g_object_notify (G_OBJECT (cnc), "password");
}

const gchar *
e_ews_connection_get_impersonate_user (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	return cnc->priv->impersonate_user;
}

GProxyResolver *
e_ews_connection_ref_proxy_resolver (EEwsConnection *cnc)
{
	GProxyResolver *proxy_resolver = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	g_mutex_lock (&cnc->priv->property_lock);

	if (cnc->priv->proxy_resolver != NULL)
		proxy_resolver = g_object_ref (cnc->priv->proxy_resolver);

	g_mutex_unlock (&cnc->priv->property_lock);

	return proxy_resolver;
}

void
e_ews_connection_set_proxy_resolver (EEwsConnection *cnc,
                                     GProxyResolver *proxy_resolver)
{
	gboolean notify = FALSE;

	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	g_mutex_lock (&cnc->priv->property_lock);

	/* Emitting a "notify" signal unnecessarily might have
	 * unwanted side effects like cancelling a SoupMessage.
	 * Only emit if we now have a different GProxyResolver. */

	if (proxy_resolver != cnc->priv->proxy_resolver) {
		g_clear_object (&cnc->priv->proxy_resolver);
		cnc->priv->proxy_resolver = proxy_resolver;

		if (proxy_resolver != NULL)
			g_object_ref (proxy_resolver);

		notify = TRUE;
	}

	g_mutex_unlock (&cnc->priv->property_lock);

	if (notify)
		g_object_notify (G_OBJECT (cnc), "proxy-resolver");
}

CamelEwsSettings *
e_ews_connection_ref_settings (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	return g_object_ref (cnc->priv->settings);
}

gboolean
e_ews_connection_get_backoff_enabled (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	return cnc->priv->backoff_enabled;
}

void
e_ews_connection_set_backoff_enabled (EEwsConnection *cnc,
				      gboolean enabled)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	cnc->priv->backoff_enabled = enabled;
}

gboolean
e_ews_connection_get_disconnected_flag (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	return cnc->priv->disconnected_flag;
}

void
e_ews_connection_set_disconnected_flag (EEwsConnection *cnc,
					gboolean disconnected_flag)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	cnc->priv->disconnected_flag = disconnected_flag;
}

gchar *
e_ews_connection_dup_last_subscription_id (EEwsConnection *cnc)
{
	gchar *res;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	g_mutex_lock (&cnc->priv->property_lock);

	res = g_strdup (cnc->priv->last_subscription_id);

	g_mutex_unlock (&cnc->priv->property_lock);

	return res;
}

void
e_ews_connection_set_last_subscription_id (EEwsConnection *cnc,
					   const gchar *subscription_id)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	g_mutex_lock (&cnc->priv->property_lock);

	if (g_strcmp0 (subscription_id, cnc->priv->last_subscription_id) != 0) {
		g_free (cnc->priv->last_subscription_id);
		cnc->priv->last_subscription_id = g_strdup (subscription_id);
	}

	g_mutex_unlock (&cnc->priv->property_lock);
}

static gboolean
e_ews_process_generic_response (EEwsConnection *cnc,
				ESoapResponse *response,
				GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		if (!ews_get_response_status (subparam, error))
			return FALSE;

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

typedef gpointer (*ItemParser) (ESoapParameter *param);

static void
e_ews_process_sync_xxx_response (ESoapParameter *subparam,
				 ItemParser parser,
				 const gchar *last_tag,
				 const gchar *delete_id_tag,
				 gchar **out_new_sync_state,
				 gboolean *out_includes_last_item,
				 GSList **out_items_created,
				 GSList **out_items_updated,
				 GSList **out_items_deleted)
{
	ESoapParameter *node;
	gchar *new_sync_state = NULL, *value, *last;
	GSList *items_created = NULL, *items_updated = NULL, *items_deleted = NULL;
	gboolean includes_last_item;

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, last_tag);
	last = e_soap_parameter_get_string_value (node);
	/*
	 * Set the includes_last_item to TRUE as default.
	 * It can avoid an infinite loop in caller, when, for some reason,
	 * we don't receive the last_tag property from the server.
	 */
	includes_last_item = g_strcmp0 (last, "false") != 0;
	g_free (last);

	node = e_soap_parameter_get_first_child_by_name (subparam, "Changes");

	if (node) {
		ESoapParameter *subparam1;
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Create");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Create")) {
			GObject *object;

			object = parser (subparam1);
			if (object && (!E_IS_EWS_FOLDER (object) || !e_ews_folder_get_is_hidden (E_EWS_FOLDER (object))))
				items_created = g_slist_prepend (items_created, object);
			else
				g_clear_object (&object);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Update");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Update")) {
			GObject *object;

			object = parser (subparam1);
			if (object && (!E_IS_EWS_FOLDER (object) || !e_ews_folder_get_is_hidden (E_EWS_FOLDER (object))))
				items_updated = g_slist_prepend (items_updated, object);
			else
				g_clear_object (&object);
		}
		  /* Exchange 2007SP1 introduced <ReadFlagChange> which is basically identical
		   * to <Update>; no idea why they thought it was a good idea. */
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "ReadFlagChange");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "ReadFlagChange")) {
			GObject *object;

			object = parser (subparam1);
			if (object && (!E_IS_EWS_FOLDER (object) || !e_ews_folder_get_is_hidden (E_EWS_FOLDER (object))))
				items_updated = g_slist_prepend (items_updated, object);
			else
				g_clear_object (&object);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Delete");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Delete")) {
			ESoapParameter *folder_param;

			folder_param = e_soap_parameter_get_first_child_by_name (subparam1, delete_id_tag);
			value = e_soap_parameter_get_property (folder_param, "Id");
			items_deleted = g_slist_prepend (items_deleted, value);
		}
	}

	if (out_new_sync_state)
		*out_new_sync_state = new_sync_state;
	else
		g_free (new_sync_state);

	if (out_includes_last_item)
		*out_includes_last_item = includes_last_item;

	if (out_items_created)
		*out_items_created = g_slist_reverse (items_created);
	else
		g_slist_free_full (items_created, g_object_unref);

	if (out_items_updated)
		*out_items_updated = g_slist_reverse (items_updated);
	else
		g_slist_free_full (items_updated, g_object_unref);

	if (out_items_deleted)
		*out_items_deleted = g_slist_reverse (items_deleted);
	else
		g_slist_free_full (items_deleted, g_free);
}

static xmlDoc *
e_ews_autodiscover_ws_xml (const gchar *email_address)
{
	xmlDoc *doc;
	xmlNode *node;
	xmlNs *ns;

	doc = xmlNewDoc ((xmlChar *) "1.0");
	node = xmlNewDocNode (doc, NULL, (xmlChar *)"Autodiscover", NULL);
	xmlDocSetRootElement (doc, node);
	ns = xmlNewNs (
		node,
		(xmlChar *)"http://schemas.microsoft.com/exchange/autodiscover/outlook/requestschema/2006", NULL);

	node = xmlNewChild (node, ns, (xmlChar *)"Request", NULL);
	xmlNewChild (
		node, ns, (xmlChar *)"EMailAddress",
		(xmlChar *) email_address);
	xmlNewChild (
		node, ns, (xmlChar *)"AcceptableResponseSchema",
		(xmlChar *)"http://schemas.microsoft.com/exchange/autodiscover/outlook/responseschema/2006a");

	return doc;
}

typedef struct _AutodiscoverData {
	guint n_pending;

	/* Borrowed */
	GMainLoop *main_loop;
	CamelEwsSettings *settings;
	ESoupSession *session;
	GCancellable *cancellable;

	/* Allocated */
	xmlOutputBuffer *buf;

	gchar *redirect_addr;
	gchar *redirect_url;
	gint n_redirects;

	/* Results */
	gboolean success;
	gchar *certificate_pem;
	GTlsCertificateFlags certificate_errors;
	GError *error;
	gchar *as_url;
	gchar *oab_url;
} AutodiscoverData;

static void
autodiscover_data_clear (AutodiscoverData *ad)
{
	g_clear_pointer (&ad->buf, xmlOutputBufferClose);
	g_clear_error (&ad->error);

	g_free (ad->redirect_addr);
	g_free (ad->redirect_url);
	g_free (ad->certificate_pem);
	g_free (ad->as_url);
	g_free (ad->oab_url);
}

/* Frees only the content, not the 'urls' structure itself */
static void
ews_urls_free_content (EwsUrls *urls)
{
	if (!urls)
		return;

	if (urls->as_url)
		xmlFree (urls->as_url);
	urls->as_url = NULL;

	if (urls->oab_url)
		xmlFree (urls->oab_url);
	urls->oab_url = NULL;
}

static gboolean
e_ews_autodiscover_prepare_requests_and_send_sync (AutodiscoverData *ad,
						   const gchar *email_address,
						   const gchar *override_url,
						   GCancellable *cancellable);

static void
ews_process_autodiscover_response (AutodiscoverData *ad,
				   GByteArray *bytes,
				   GError **error)

{
	EwsUrls exch_urls, expr_urls;
	xmlDoc *doc;
	xmlNode *node;
	gchar *str;

	g_return_if_fail (bytes != NULL);

	memset (&exch_urls, 0, sizeof (EwsUrls));
	memset (&expr_urls, 0, sizeof (EwsUrls));

	doc = xmlReadMemory ((const gchar *) bytes->data, bytes->len, "autodiscover.xml", NULL, 0);

	if (!doc) {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1,
			_("Failed to parse autodiscover response XML"));
		return;
	}
	node = xmlDocGetRootElement (doc);
	if (strcmp ((gchar *) node->name, "Autodiscover")) {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1,
			_("Failed to find <Autodiscover> element"));
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "Response"))
			break;
	}
	if (!node) {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1,
			_("Failed to find <Response> element"));
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "Account"))
			break;
	}
	if (!node) {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1,
			_("Failed to find <Account> element"));
		goto failed;
	}

	str = autodiscover_dup_element_value (node, "RedirectAddr");
	if (str) {
		g_free (ad->redirect_addr);
		ad->redirect_addr = str;
	}

	str = autodiscover_dup_element_value (node, "RedirectUrl");
	if (str) {
		g_free (ad->redirect_url);
		ad->redirect_url = str;
	}

	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp ((gchar *) node->name, "Protocol")) {
			xmlChar *protocol_type = autodiscover_get_protocol_type (node);

			if (g_strcmp0 ((const gchar *) protocol_type, "EXCH") == 0) {
				ews_urls_free_content (&exch_urls);
				autodiscover_parse_protocol (node, &exch_urls);
			} else if (g_strcmp0 ((const gchar *) protocol_type, "EXPR") == 0) {
				ews_urls_free_content (&expr_urls);
				autodiscover_parse_protocol (node, &expr_urls);

				/* EXPR has precedence, thus stop once found both there */
				if (expr_urls.as_url && expr_urls.oab_url) {
					xmlFree (protocol_type);
					break;
				}
			}

			if (protocol_type)
				xmlFree (protocol_type);
		}
	}

	/* Make the <OABUrl> optional */
	if (!exch_urls.as_url && !expr_urls.as_url) {
		ews_urls_free_content (&exch_urls);
		ews_urls_free_content (&expr_urls);
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1,
			_("Failed to find <ASUrl> in autodiscover response"));
		goto failed;
	}

	ad->success = TRUE;

	/* It's a good response, cancel all pending */
	g_cancellable_cancel (ad->cancellable);

	if (expr_urls.as_url) {
		g_free (ad->as_url);
		ad->as_url = g_strdup ((gchar *) expr_urls.as_url);
	} else if (exch_urls.as_url) {
		g_free (ad->as_url);
		ad->as_url = g_strdup ((gchar *) exch_urls.as_url);
	}

	if (expr_urls.as_url && expr_urls.oab_url) {
		g_free (ad->oab_url);
		ad->oab_url = g_strdup ((gchar *) expr_urls.oab_url);
	} else if (!expr_urls.as_url && exch_urls.oab_url) {
		g_free (ad->oab_url);
		ad->oab_url = g_strdup ((gchar *) exch_urls.oab_url);
	}

	ews_urls_free_content (&exch_urls);
	ews_urls_free_content (&expr_urls);

	goto exit;

 failed:
	if (!g_cancellable_is_cancelled (ad->cancellable) &&
	    (!ad->as_url || !ad->oab_url) && ad->n_redirects < 11 &&
	    (ad->redirect_url || ad->redirect_addr) &&
	    !g_error_matches (ad->error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) &&
	    (!ad->error || (ad->error->domain != G_IO_ERROR && ad->error->domain != G_TLS_ERROR))) {
		const gchar *host_url;
		gchar *redirect_addr, *redirect_url;
		GError *local_error;

		redirect_addr = ad->redirect_addr;
		redirect_url = ad->redirect_url;
		local_error = ad->error;

		/* To avoid infinite recursion */
		ad->redirect_addr = NULL;
		ad->redirect_url = NULL;
		ad->n_redirects++;
		ad->error = NULL;

		host_url = redirect_url;

		if (!host_url)
			host_url = camel_ews_settings_get_hosturl (ad->settings);

		e_ews_autodiscover_prepare_requests_and_send_sync (ad, redirect_addr, host_url, ad->cancellable);

		g_clear_error (&local_error);
		g_free (redirect_addr);
		g_free (redirect_url);
	}

 exit:
	xmlFreeDoc (doc);
}

static ESoapRequest *
e_ews_create_request_for_url (const gchar *url,
			      xmlOutputBuffer *buf,
			      GError **error)
{
	ESoapRequest *request;

	if (url == NULL) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("URL cannot be NULL"));
		return NULL;
	}

	request = e_soap_request_new (buf ? SOUP_METHOD_POST : SOUP_METHOD_GET, url, FALSE, NULL, NULL, NULL, error);
	if (!request)
		return NULL;

	if (buf) {
		e_soap_request_set_custom_body (request, "text/xml; charset=utf-8",
			#ifdef LIBXML2_NEW_BUFFER
			xmlOutputBufferGetContent (buf), xmlOutputBufferGetSize (buf)
			#else
			buf->buffer->content, buf->buffer->use
			#endif
			);
	} else {
		/* No body set for the GET request */
		e_soap_request_set_custom_body (request, "", NULL, 0);
	}

	return request;
}

static GSList * /* ESoapRequest * */
e_ews_autodiscover_prepare_requests (AutodiscoverData *ad,
				     const gchar *email_address,
				     const gchar *override_url,
				     gchar **out_srv_lookup_domain,
				     GError **error)
{
	GUri *uri = NULL;
	gboolean use_secure = TRUE;
	gboolean is_outlook = FALSE;
	gchar *url1, *url2, *url3, *url4;
	const gchar *url5, *domain = NULL;
	GSList *requests = NULL;
	xmlDoc *doc;
	xmlOutputBuffer *buf;
	GError *local_error = NULL;

	g_return_val_if_fail (email_address && *email_address, FALSE);

	domain = strchr (email_address, '@');
	if (domain)
		domain++;

	g_return_val_if_fail ((domain && *domain) || (override_url && *override_url), FALSE);

	doc = e_ews_autodiscover_ws_xml (email_address);
	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, xmlDocGetRootElement (doc), 0, 1, NULL);
	xmlOutputBufferFlush (buf);
	xmlFreeDoc (doc);

	url1 = NULL;
	url2 = NULL;
	url3 = NULL;
	url4 = NULL;
	url5 = NULL;

	if (override_url)
		uri = g_uri_parse (override_url, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);

	if (uri) {
		const gchar *host = g_uri_get_host (uri);
		const gchar *scheme = g_uri_get_scheme (uri);

		use_secure = g_strcmp0 (scheme, "https") == 0;

		url1 = g_strdup_printf ("http%s://%s/autodiscover/autodiscover.xml", use_secure ? "s" : "", host);
		url2 = g_strdup_printf ("http%s://autodiscover.%s/autodiscover/autodiscover.xml", use_secure ? "s" : "", host);

		is_outlook = host && g_ascii_strcasecmp (host, "outlook.office365.com") == 0;

		/* outlook.office365.com has its autodiscovery at outlook.com */
		if (is_outlook && domain && g_ascii_strcasecmp (domain, "outlook.com") != 0) {
			url5 = "https://outlook.com/autodiscover/autodiscover.xml";
		} else if (!is_outlook && domain) {
			#define ON_MICROSOFT_COM_TEXT "onmicrosoft.com"
			gint len = strlen (domain);
			gint onmslen = strlen (ON_MICROSOFT_COM_TEXT);

			if (len >= onmslen) {
				const gchar *test_domain;

				test_domain = domain + len - onmslen;

				/* onmicrosoft.com addresses might be handled on the outlook.com/office365.com as well */
				if (g_ascii_strcasecmp (test_domain, ON_MICROSOFT_COM_TEXT) == 0 &&
				    (len == onmslen || (len > onmslen && domain[len - onmslen - 1] == '.')))
					url5 = "https://outlook.com/autodiscover/autodiscover.xml";
			}
			#undef ON_MICROSOFT_COM_TEXT
		}

		g_uri_unref (uri);
	}

	is_outlook = is_outlook || (domain && g_ascii_strcasecmp (domain, "outlook.com") == 0);

	if (domain) {
		url3 = g_strdup_printf ("http%s://%s/autodiscover/autodiscover.xml", use_secure ? "s" : "", domain);
		url4 = g_strdup_printf ("http%s://autodiscover.%s/autodiscover/autodiscover.xml", use_secure ? "s" : "", domain);
	}

	#define process_url(_url) G_STMT_START { \
		if (_url) { \
			ESoapRequest *request = e_ews_create_request_for_url (_url, buf, local_error ? NULL : &local_error); \
			if (request) \
				requests = g_slist_prepend (requests, request); \
		} \
	} G_STMT_END

	process_url (url1);
	process_url (url2);
	process_url (url3);
	process_url (url4);
	process_url (url5);

	#undef process_url

	if (local_error && requests)
		g_clear_error (&local_error);

	if (!is_outlook && !local_error && domain && requests && out_srv_lookup_domain)
		*out_srv_lookup_domain = g_strdup (domain);
	else if (out_srv_lookup_domain)
		*out_srv_lookup_domain = NULL;

	g_free (url1);
	g_free (url2);
	g_free (url3);
	g_free (url4);

	if (local_error) {
		xmlOutputBufferClose (buf);
		g_propagate_error (error, local_error);
		return NULL;
	}

	g_clear_pointer (&ad->buf, xmlOutputBufferClose);
	ad->buf = buf;

	return requests;
}

typedef struct _AutodiscoverResponseData {
	AutodiscoverData *ad;
	SoupMessage *message;
} AutodiscoverResponseData;

static void
ews_autodiscover_response_ready_cb (GObject *source_object,
				    GAsyncResult *result,
				    gpointer user_data)
{
	AutodiscoverResponseData *ard = user_data;
	AutodiscoverData *ad;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	GInputStream *input_stream;
	GError *local_error = NULL;

	g_return_if_fail (ard != NULL);

	ad = ard->ad;

	input_stream = e_soup_session_send_message_finish (E_SOUP_SESSION (source_object), result,
		&certificate_pem, &certificate_errors, &local_error);

	if (input_stream) {
		GByteArray *bytes;
		gint expected_length;
		gpointer buffer;
		gsize nread = 0;
		gboolean success;

		expected_length = soup_message_headers_get_content_length (soup_message_get_response_headers (ard->message));
		if (expected_length > 0)
			bytes = g_byte_array_sized_new (expected_length > 1024 * 1024 * 10 ? 1024 * 1024 * 10 : expected_length);
		else
			bytes = g_byte_array_new ();

		buffer = g_malloc (EWS_BUFFER_SIZE);

		while (success = g_input_stream_read_all (input_stream, buffer, EWS_BUFFER_SIZE, &nread, ad->cancellable, &local_error),
		       success && nread > 0) {
			g_byte_array_append (bytes, buffer, nread);
		}

		if (success)
			ews_process_autodiscover_response (ad, bytes, &local_error);

		g_free (buffer);
		g_byte_array_free (bytes, TRUE);
		g_object_unref (input_stream);
	}

	if (local_error && (!ad->error ||
	    (!g_error_matches (ad->error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) &&
	    (!ad->error || (ad->error->domain != G_IO_ERROR && ad->error->domain != G_TLS_ERROR))))) {
		g_clear_pointer (&ad->certificate_pem, g_free);
		ad->certificate_pem = certificate_pem;
		ad->certificate_errors = certificate_errors;
		ad->error = local_error;
	} else {
		g_free (certificate_pem);
		g_clear_error (&local_error);
	}

	if (g_atomic_int_dec_and_test (&ad->n_pending))
		g_main_loop_quit (ad->main_loop);

	g_object_unref (ard->message);
	g_slice_free (AutodiscoverResponseData, ard);
}

static void
ews_autodiscover_send_request (AutodiscoverData *ad,
			       ESoapRequest *request,
			       GCancellable *cancellable,
			       GError **error)
{
	SoupMessage *message;

	message = e_soap_request_persist (request, ad->session, ad->settings, error);

	if (message) {
		gpointer prepare_data;

		prepare_data = e_soup_session_prepare_message_send_sync (ad->session, message, cancellable, error);

		if (prepare_data) {
			AutodiscoverResponseData *ard;

			g_atomic_int_inc (&ad->n_pending);

			ard = g_slice_new (AutodiscoverResponseData);
			ard->ad = ad;
			ard->message = g_object_ref (message);

			e_soup_session_send_message (ad->session, message, G_PRIORITY_DEFAULT, prepare_data,
				ad->cancellable, ews_autodiscover_response_ready_cb, ard);
		}

		g_object_unref (message);
	}
}

static void
autodiscover_srv_record_resolved_cb (GObject *source,
				     GAsyncResult *result,
				     gpointer user_data)
{
	GList *targets, *link;
	AutodiscoverData *ad = user_data;
	gchar *new_uri = NULL;
	gboolean success;

	g_return_if_fail (ad != NULL);

	targets = g_resolver_lookup_service_finish (G_RESOLVER (source), result, NULL);

	success = !g_cancellable_is_cancelled (ad->cancellable) && targets;

	for (link = targets; link && success; link = g_list_next (link)) {
		GSrvTarget *target = link->data;
		const gchar *hostname;

		hostname = g_srv_target_get_hostname (target);

		switch (g_srv_target_get_port (target)) {
		case 80:
			link = NULL;
			new_uri = g_strdup_printf ("http://%s/autodiscover/autodiscover.xml", hostname);
			break;
		case 443:
			link = NULL;
			new_uri = g_strdup_printf ("https://%s/autodiscover/autodiscover.xml", hostname);
			break;
		}
	}

	g_list_free_full (targets, (GDestroyNotify) g_srv_target_free);

	if (new_uri && success) {
		ESoapRequest *request = e_ews_create_request_for_url (new_uri, ad->buf, ad->error ? NULL : &ad->error);
		if (request) {
			ews_autodiscover_send_request (ad, request, ad->cancellable, ad->error ? NULL : &ad->error);
			g_object_unref (request);
		} else {
			success = FALSE;
		}
	} else {
		success = FALSE;
	}

	if (g_atomic_int_dec_and_test (&ad->n_pending))
		g_main_loop_quit (ad->main_loop);

	g_free (new_uri);
}

static gboolean
e_ews_autodiscover_prepare_requests_and_send_sync (AutodiscoverData *ad,
						   const gchar *email_address,
						   const gchar *override_url,
						   GCancellable *cancellable)
{
	GSList *requests, *link;
	gchar *srv_lookup_domain = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (ad != NULL, FALSE);

	/* to not have the main loop quit while doing this */
	g_atomic_int_inc (&ad->n_pending);

	/* This is starting a new round, the last error and others are obsolete */
	g_clear_error (&ad->error);
	g_clear_pointer (&ad->as_url, g_free);
	g_clear_pointer (&ad->oab_url, g_free);
	g_clear_pointer (&ad->certificate_pem, g_free);
	ad->certificate_errors = 0;

	requests = e_ews_autodiscover_prepare_requests (ad, email_address, override_url, &srv_lookup_domain, &local_error);

	if (!local_error && srv_lookup_domain && *srv_lookup_domain) {
		g_atomic_int_inc (&ad->n_pending);

		g_resolver_lookup_service_async (g_resolver_get_default (), "autodiscover", "tcp", srv_lookup_domain, cancellable,
			autodiscover_srv_record_resolved_cb, ad);
	}

	for (link = requests; link; link = g_slist_next (link)) {
		ESoapRequest *request = link->data;

		ews_autodiscover_send_request (ad, request, cancellable, local_error ? NULL : &local_error);
	}

	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
	    camel_ews_settings_get_auth_mechanism (ad->settings) == EWS_AUTH_TYPE_OAUTH2) {
		/* To ask for the credentials when being OAuth2 and the secret is not found in the keyring */
		local_error->domain = E_SOUP_SESSION_ERROR;
		local_error->code = SOUP_STATUS_UNAUTHORIZED;
	}

	if (local_error && (!ad->error ||
	    (!g_error_matches (ad->error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) &&
	    (!ad->error || (ad->error->domain != G_IO_ERROR && ad->error->domain != G_TLS_ERROR))))) {
		g_clear_error (&ad->error);
		g_propagate_error (&ad->error, local_error);
	} else {
		g_clear_error (&local_error);
	}

	/* pair decrement for the increment at the beginning of the function */
	if (g_atomic_int_dec_and_test (&ad->n_pending))
		g_main_loop_quit (ad->main_loop);

	g_slist_free_full (requests, g_object_unref);
	g_free (srv_lookup_domain);

	return requests != NULL;
}

static void
e_ews_cancel_cancellable_cb (GCancellable *cancellable,
			     gpointer user_data)
{
	GCancellable *op_cancellable = user_data;

	g_cancellable_cancel (op_cancellable);
}

static gboolean
has_suffix_icmp (const gchar *text,
                 const gchar *suffix)
{
	gint ii, tlen, slen;

	g_return_val_if_fail (text != NULL, FALSE);
	g_return_val_if_fail (suffix != NULL, FALSE);

	tlen = strlen (text);
	slen = strlen (suffix);

	if (!*text || !*suffix || tlen < slen)
		return FALSE;

	for (ii = 0; ii < slen; ii++) {
		if (g_ascii_tolower (text[tlen - ii - 1]) !=
		    g_ascii_tolower (suffix[slen - ii - 1]))
			break;
	}

	return ii == slen;
}

gboolean
e_ews_autodiscover_ws_url_sync (ESource *source,
				CamelEwsSettings *settings,
                                const gchar *email_address,
                                const gchar *password,
				gchar **out_certificate_pem,
				GTlsCertificateFlags *out_certificate_errors,
                                GCancellable *cancellable,
                                GError **error)
{
	AutodiscoverData ad;
	gulong cancelled_id = 0;
	EEwsConnection *tmp_cnc;
	GMainContext *main_context;
	const gchar *domain;
	const gchar *host_url;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);
	g_return_val_if_fail (email_address != NULL, FALSE);
	g_return_val_if_fail (password != NULL, FALSE);

	domain = strchr (email_address, '@');

	/* if it's non-NULL, then domain[0] == '@' */
	if (!domain || !domain[1]) {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1, _("Email address is missing a domain part"));
		return FALSE;
	}

	memset (&ad, 0, sizeof (AutodiscoverData));

	ad.settings = settings;
	ad.cancellable = g_cancellable_new ();

	if (G_IS_CANCELLABLE (cancellable)) {
		cancelled_id = g_cancellable_connect (cancellable, G_CALLBACK (e_ews_cancel_cancellable_cb),
			ad.cancellable, NULL);
	}

	main_context = g_main_context_new ();
	ad.main_loop = g_main_loop_new (main_context, FALSE);
	g_main_context_push_thread_default (main_context);

	/*
	 * http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
	 * supposed to try $domain and then autodiscover.$domain. But some
	 * people have broken firewalls on the former which drop packets
	 * instead of rejecting connections, and make the request take ages
	 * to time out. So run both queries in parallel and let the fastest
	 * (successful) one win.
	 */
	tmp_cnc = e_ews_connection_new_full (source, "https://autodiscover.domain", settings, FALSE);
	ad.session = e_ews_connection_create_soup_session (tmp_cnc);
	g_object_set (ad.session, "timeout", 15, NULL);
	e_ews_connection_set_password (tmp_cnc, password);
	e_soup_session_set_credentials (E_SOUP_SESSION (ad.session), tmp_cnc->priv->credentials);

	host_url = camel_ews_settings_get_hosturl (settings);

	if (e_ews_autodiscover_prepare_requests_and_send_sync (&ad, email_address, host_url, cancellable) &&
	    g_atomic_int_get (&ad.n_pending) > 0)
		g_main_loop_run (ad.main_loop);

	g_main_context_pop_thread_default (main_context);

	g_main_context_unref (main_context);
	g_main_loop_unref (ad.main_loop);
	g_clear_object (&tmp_cnc);
	g_clear_object (&ad.session);

	if (cancelled_id)
		g_cancellable_disconnect (cancellable, cancelled_id);

	if (ad.success) {
		camel_ews_settings_set_hosturl (settings, ad.as_url);

		if (ad.oab_url && !has_suffix_icmp (ad.oab_url, "oab.xml")) {
			gchar *tmp;

			if (g_str_has_suffix (ad.oab_url, "/"))
				tmp = g_strconcat (ad.oab_url, "oab.xml", NULL);
			else
				tmp = g_strconcat (ad.oab_url, "/", "oab.xml", NULL);

			camel_ews_settings_set_oaburl (settings, tmp);
			g_free (tmp);
		} else {
			camel_ews_settings_set_oaburl (settings, ad.oab_url);
		}

		if (out_certificate_pem)
			*out_certificate_pem = NULL;
		if (out_certificate_errors)
			*out_certificate_errors = 0;
	} else {
		if (ad.error) {
			g_propagate_error (error, ad.error);
			ad.error = NULL;
		}
		if (out_certificate_pem)
			*out_certificate_pem = g_steal_pointer (&ad.certificate_pem);
		if (out_certificate_errors)
			*out_certificate_errors = ad.certificate_errors;
	}

	autodiscover_data_clear (&ad);

	return ad.success;
}

typedef struct _OalRequestData {
	const gchar *oal_id;
	const gchar *oal_element;

	GSList *oals; /* EwsOAL * */
	GSList *elements; /* EwsOALDetails */
	gchar *etag;
} OalRequestData;

static void
oal_request_data_init (OalRequestData *req_data)
{
	memset (req_data, 0, sizeof (OalRequestData));
}

static void
oal_request_data_clear (OalRequestData *req_data)
{
	g_free (req_data->etag);

	g_slist_free_full (req_data->oals, (GDestroyNotify) ews_oal_free);
	g_slist_free_full (req_data->elements, (GDestroyNotify) ews_oal_details_free);
}

static gchar *
get_property (xmlNodePtr node_ptr,
              const gchar *name)
{
	xmlChar *xml_s;
	gchar *s;

	xml_s = xmlGetProp (node_ptr, (const xmlChar *) name);
	s = g_strdup ((gchar *) xml_s);
	xmlFree (xml_s);

	return s;
}

static guint32
get_property_as_uint32 (xmlNodePtr node_ptr,
                        const gchar *name)
{
	gchar *s;
	guint32 val = -1;

	s = get_property (node_ptr, name);
	if (s)
		sscanf (s,"%"G_GUINT32_FORMAT, &val);
	g_free (s);

	return val;
}

static gchar *
get_content (xmlNodePtr node_ptr)
{
	xmlChar *xml_s;
	gchar *s;

	xml_s = xmlNodeGetContent (node_ptr);
	s = g_strdup ((gchar *) xml_s);
	xmlFree (xml_s);

	return s;
}

static GSList *
parse_oal_full_details (xmlNode *node,
                        const gchar *element)
{
	GSList *elements = NULL;

	for (node = node->children; node; node = node->next) {
		EwsOALDetails *det;
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (element && strcmp ((gchar *) node->name, element))
			continue;
		if (!element && strcmp ((gchar *) node->name, "Full") &&
		    strcmp((gchar *) node->name, "Diff"))
			continue;

		det = g_new0 (EwsOALDetails, 1);
		det->type = g_strdup((gchar *) node->name);
		det->seq = get_property_as_uint32 (node, "seq");
		det->ver = get_property_as_uint32 (node, "ver");
		det->size = get_property_as_uint32 (node, "size");
		det->uncompressed_size = get_property_as_uint32 (node, "uncompressedsize");
		det->sha = get_property (node, "uncompressedsize");
		det->filename = g_strstrip (get_content (node));

		elements = g_slist_prepend (elements, det);
		if (element && !strcmp (element, "Full"))
			break;
	}

	return elements;
}

static void
e_ews_process_oal_data_response (ESoapRequest *request,
				 SoupMessage *message,
				 GInputStream *input_stream,
				 gpointer user_data,
				 gboolean *out_repeat,
				 GCancellable *cancellable,
				 GError **error)
{
	OalRequestData *req_data = user_data;
	ESoapResponse *response;
	const gchar *etag;
	xmlDoc *doc;
	xmlNode *node;

	response = e_soap_response_new ();

	e_soap_request_setup_response (request, response);

	doc = e_soap_response_xmldoc_from_message_sync (response, message, input_stream, cancellable, error);

	g_clear_object (&response);

	if (!doc) {
		if (error && !*error)
			g_set_error_literal (error, EWS_CONNECTION_ERROR, -1, _("Failed to parse oab XML"));
		return;
	}

	etag = soup_message_headers_get_one (soup_message_get_response_headers (message), "ETag");
	if (etag)
		req_data->etag = g_strdup (etag);

	node = xmlDocGetRootElement (doc);
	if (strcmp ((gchar *) node->name, "OAB") == 0) {
		for (node = node->children; node; node = node->next) {
			if (node->type == XML_ELEMENT_NODE && strcmp ((gchar *) node->name, "OAL") == 0) {
				if (req_data->oal_id == NULL) {
					EwsOAL *oal = g_new0 (EwsOAL, 1);

					oal->id = get_property (node, "id");
					oal->dn = get_property (node, "dn");
					oal->name = get_property (node, "name");

					req_data->oals = g_slist_prepend (req_data->oals, oal);
				} else {
					gchar *id = get_property (node, "id");

					if (strcmp (id, req_data->oal_id) == 0) {
						/* parse details of full_details file */
						req_data->elements = parse_oal_full_details (node, req_data->oal_element);

						g_free (id);
						break;
					}

					g_free (id);
				}
			}
		}

		req_data->oals = g_slist_reverse (req_data->oals);
	} else {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, -1, _("Failed to find <OAB> element\n"));
	}

	xmlFreeDoc (doc);
}

gboolean
e_ews_connection_get_oal_list_sync (EEwsConnection *cnc,
                                    GSList **out_oals,
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	OalRequestData req_data;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_oals != NULL, FALSE);

	*out_oals = NULL;

	request = e_ews_create_request_for_url (cnc->priv->uri, NULL, error);

	if (!request)
		return FALSE;

	oal_request_data_init (&req_data);

	e_soap_request_set_custom_process_fn (request, e_ews_process_oal_data_response, &req_data);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, &local_error);
	g_warn_if_fail (response == NULL);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!local_error)
		*out_oals = g_steal_pointer (&req_data.oals);

	oal_request_data_clear (&req_data);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

gboolean
e_ews_connection_get_oal_detail_sync (EEwsConnection *cnc,
				      const gchar *oal_uri,
                                      const gchar *oal_id,
                                      const gchar *oal_element,
				      const gchar *old_etag,
                                      GSList **out_elements,
				      gchar **out_etag,
                                      GCancellable *cancellable,
                                      GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	OalRequestData req_data;
	gchar *tmp_oal_id = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (oal_id, FALSE);

	if (out_elements)
		*out_elements = NULL;
	if (out_etag)
		*out_etag = NULL;

	request = e_ews_create_request_for_url (oal_uri, NULL, error);

	if (!request)
		return FALSE;

	/* oal_id can be of form "GUID:name", but here is compared only GUID */
	if (strchr (oal_id, ':')) {
		gchar *sep;

		tmp_oal_id = g_strdup (oal_id);
		sep = strchr (tmp_oal_id, ':');
		if (sep)
			*sep = '\0';
	}

	oal_request_data_init (&req_data);

	req_data.oal_id = tmp_oal_id ? tmp_oal_id : oal_id;
	req_data.oal_element = oal_element;

	e_soap_request_set_custom_process_fn (request, e_ews_process_oal_data_response, &req_data);
	e_soap_request_set_etag (request, old_etag);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, &local_error);
	g_warn_if_fail (response == NULL);

	g_clear_object (&request);
	g_clear_object (&response);
	g_free (tmp_oal_id);

	if (!local_error) {
		if (out_elements)
			*out_elements = g_steal_pointer (&req_data.elements);
		if (out_etag)
			*out_etag = g_steal_pointer (&req_data.etag);
	}

	oal_request_data_clear (&req_data);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

typedef struct _DownloadOalData {
	const gchar *cache_filename;
	gint fd;
} DownloadOalData;

static void
e_ews_process_download_oal_file_response (ESoapRequest *request,
					  SoupMessage *message,
					  GInputStream *input_stream,
					  gpointer user_data,
					  gboolean *out_repeat,
					  GCancellable *cancellable,
					  GError **error)
{
	DownloadOalData *dod = user_data;
	ESoapResponseProgressFn progress_fn = NULL;
	gpointer progress_data = NULL;
	const gchar *size;
	gpointer buffer;
	gsize response_size = 0;
	gsize response_received = 0;
	gsize progress_percent = 0;
	gsize nread = 0;
	gboolean success;

	g_return_if_fail (dod != NULL);
	g_return_if_fail (dod->fd != -1);

	e_soap_request_get_progress_fn (request, &progress_fn, &progress_data);

	size = soup_message_headers_get_one (soup_message_get_response_headers (message), "Content-Length");

	if (size)
		response_size = g_ascii_strtoll (size, NULL, 10);

	buffer = g_malloc (EWS_BUFFER_SIZE);

	while (success = g_input_stream_read_all (input_stream, buffer, EWS_BUFFER_SIZE, &nread, cancellable, error),
	       success && nread > 0) {
		response_received += nread;

		if (response_size && progress_fn) {
			gint pc = response_received * 100 / response_size;
			if (progress_percent != pc) {
				progress_percent = pc;
				progress_fn (progress_data, progress_percent);
			}
		}

		if (write (dod->fd, (const gchar *) buffer, nread) != nread) {
			g_set_error (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_UNKNOWN,
				"Failed to write streaming data to file '%s': %s", dod->cache_filename, g_strerror (errno));
			break;
		}
	}

	g_free (buffer);
}

gboolean
e_ews_connection_download_oal_file_sync (EEwsConnection *cnc,
					 const gchar *oal_uri,
                                         const gchar *cache_filename,
                                         ESoapResponseProgressFn progress_fn,
                                         gpointer progress_data,
                                         GCancellable *cancellable,
                                         GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	DownloadOalData dod;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	request = e_ews_create_request_for_url (oal_uri, NULL, error);

	if (!request)
		return FALSE;

	/* Prepare the file */
	g_unlink (cache_filename);

	dod.cache_filename = cache_filename;
	dod.fd = g_open (cache_filename, O_RDONLY | O_WRONLY | O_APPEND | O_CREAT, 0600);
	if (dod.fd == -1) {
		g_set_error (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_UNKNOWN,
			"Failed to open the cache file '%s': %s", cache_filename, g_strerror (errno));
		g_clear_object (&request);

		return FALSE;
	}

	e_soap_request_set_progress_fn (request, progress_fn, progress_data);

	e_soap_request_set_custom_process_fn (request, e_ews_process_download_oal_file_response, &dod);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, &local_error);
	g_warn_if_fail (response == NULL);

	g_clear_object (&request);
	g_clear_object (&response);
	close (dod.fd);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

const gchar *
e_ews_connection_get_mailbox (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);

	if (!cnc->priv->email || !*cnc->priv->email)
		return camel_ews_settings_get_email (cnc->priv->settings);

	return cnc->priv->email;
}

void
e_ews_connection_set_mailbox (EEwsConnection *cnc,
                              const gchar *email)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));
	g_return_if_fail (email != NULL);

	g_free (cnc->priv->email);
	cnc->priv->email = g_strdup (email);
}

static void
ews_append_additional_props_to_msg (ESoapRequest *request,
                                    const EEwsAdditionalProps *add_props)
{
	GSList *l;

	if (!add_props)
		return;

	e_soap_request_start_element (request, "AdditionalProperties", NULL, NULL);

	if (add_props->field_uri) {
		gchar **prop = g_strsplit (add_props->field_uri, " ", 0);
		gint i = 0;

		while (prop[i]) {
			e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", prop[i]);
			i++;
		}

		g_strfreev (prop);
	}

	if (add_props->extended_furis) {
		for (l = add_props->extended_furis; l != NULL; l = g_slist_next (l)) {
			EEwsExtendedFieldURI *ex_furi = l->data;

			e_soap_request_start_element (request, "ExtendedFieldURI", NULL, NULL);

			if (ex_furi->distinguished_prop_set_id)
				e_soap_request_add_attribute (request, "DistinguishedPropertySetId", ex_furi->distinguished_prop_set_id, NULL, NULL);

			if (ex_furi->prop_tag)
				e_soap_request_add_attribute (request, "PropertyTag", ex_furi->prop_tag, NULL, NULL);

			if (ex_furi->prop_set_id)
				e_soap_request_add_attribute (request, "PropertySetId", ex_furi->prop_set_id, NULL, NULL);

			if (ex_furi->prop_name)
				e_soap_request_add_attribute (request, "PropertyName", ex_furi->prop_name, NULL, NULL);

			if (ex_furi->prop_id)
				e_soap_request_add_attribute (request, "PropertyId", ex_furi->prop_id, NULL, NULL);

			if (ex_furi->prop_type)
				e_soap_request_add_attribute (request, "PropertyType", ex_furi->prop_type, NULL, NULL);

			e_soap_request_end_element (request);
		}
	}

	if (add_props->indexed_furis) {
		for (l = add_props->indexed_furis; l != NULL; l = g_slist_next (l)) {
			EEwsIndexedFieldURI *in_furi = l->data;

			e_soap_request_start_element (request, "IndexedFieldURI", NULL, NULL);

			e_soap_request_add_attribute (request, "FieldURI", in_furi->field_uri, NULL, NULL);
			e_soap_request_add_attribute (request, "FieldIndex", in_furi->field_index, NULL, NULL);

			e_soap_request_end_element (request);
		}
	}

	e_soap_request_end_element (request);
}

static void
ews_write_sort_order_to_msg (ESoapRequest *request,
                             EwsSortOrder *sort_order)
{
	if (!sort_order)
		return;

	e_soap_request_start_element (request, "SortOrder", NULL, NULL);
	e_soap_request_start_element (request, "FieldOrder", NULL, NULL);
	e_soap_request_add_attribute (request, "Order", sort_order->order, NULL, NULL);

	if (sort_order->uri_type == NORMAL_FIELD_URI)
		e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", (gchar *) sort_order->field_uri);
	else if (sort_order->uri_type == INDEXED_FIELD_URI) {
		EEwsIndexedFieldURI *in_furi = sort_order->field_uri;

		e_soap_request_start_element (request, "IndexedFieldURI", NULL, NULL);
		e_soap_request_add_attribute (request, "FieldURI", in_furi->field_uri, NULL, NULL);
		e_soap_request_add_attribute (request, "FieldIndex", in_furi->field_index, NULL, NULL);
		e_soap_request_end_element (request);
	} else if (sort_order->uri_type == EXTENDED_FIELD_URI) {
		EEwsExtendedFieldURI *ex_furi = sort_order->field_uri;

		e_soap_request_start_element (request, "ExtendedFieldURI", NULL, NULL);

		if (ex_furi->distinguished_prop_set_id)
			e_soap_request_add_attribute (request, "DistinguishedPropertySetId", ex_furi->distinguished_prop_set_id, NULL, NULL);
		if (ex_furi->prop_set_id)
			e_soap_request_add_attribute (request, "PropertySetId", ex_furi->prop_set_id, NULL, NULL);
		if (ex_furi->prop_name)
			e_soap_request_add_attribute (request, "PropertyName", ex_furi->prop_name, NULL, NULL);
		if (ex_furi->prop_id)
			e_soap_request_add_attribute (request, "PropertyId", ex_furi->prop_id, NULL, NULL);
		if (ex_furi->prop_type)
			e_soap_request_add_attribute (request, "PropertyType", ex_furi->prop_type, NULL, NULL);

		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request);
	e_soap_request_end_element (request);
}

static gboolean
e_ews_process_sync_folder_items_response (EEwsConnection *cnc,
					  ESoapResponse *response,
					  gchar **out_new_sync_state,
					  gboolean *out_includes_last_item,
					  GSList **out_items_created,
					  GSList **out_items_updated,
					  GSList **out_items_deleted,
					  GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "SyncFolderItemsResponseMessage")) {
			e_ews_process_sync_xxx_response (
				subparam,  (ItemParser) e_ews_item_new_from_soap_parameter,
				"IncludesLastItemInRange", "ItemId",
				out_new_sync_state, out_includes_last_item, out_items_created, out_items_updated, out_items_deleted);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_sync_folder_items_sync (EEwsConnection *cnc,
                                         gint pri,
                                         const gchar *old_sync_state,
                                         const gchar *fid,
                                         const gchar *default_props,
					 const EEwsAdditionalProps *add_props,
                                         guint max_entries,
                                         gchar **out_new_sync_state,
                                         gboolean *out_includes_last_item,
                                         GSList **out_items_created,
                                         GSList **out_items_updated,
                                         GSList **out_items_deleted,
                                         GCancellable *cancellable,
                                         GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"SyncFolderItems",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ItemShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, default_props);

	ews_append_additional_props_to_msg (request, add_props);

	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "SyncFolderId", "messages", NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", fid);
	e_soap_request_end_element (request);

	if (old_sync_state)
		e_ews_request_write_string_parameter (request, "SyncState", "messages", old_sync_state);

	/* Max changes requested */
	e_ews_request_write_int_parameter (request, "MaxChangesReturned", "messages", max_entries);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_sync_folder_items_response (cnc, response,
		out_new_sync_state, out_includes_last_item, out_items_created,
		out_items_updated, out_items_deleted, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static void
ews_append_folder_ids_to_msg (ESoapRequest *request,
                              const gchar *email,
                              GSList *folder_ids)
{
	GSList *l;

	for (l = folder_ids; l != NULL; l = g_slist_next (l)) {
		const EwsFolderId *fid = l->data;

		e_ews_folder_id_append_to_request (request, email, fid);
	}
}

static void
ews_connection_write_only_ids_restriction (ESoapRequest *request,
					   GPtrArray *only_ids)
{
	guint ii;

	g_return_if_fail (E_IS_SOAP_REQUEST (request));
	g_return_if_fail (only_ids && only_ids->len);

	for (ii = 0; ii < only_ids->len; ii++) {
		const gchar *itemid = g_ptr_array_index (only_ids, ii);

		e_soap_request_start_element (request, "IsEqualTo", NULL, NULL);
		e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "item:ItemId");
		e_soap_request_start_element (request, "FieldURIOrConstant", NULL, NULL);
		e_ews_request_write_string_parameter_with_attribute (request, "Constant", NULL, NULL, "Value", itemid);
		e_soap_request_end_element (request); /* FieldURIOrConstant */
		e_soap_request_end_element (request); /* IsEqualTo */
	}
}

static gboolean
e_ews_process_find_folder_items_response (EEwsConnection *cnc,
					  ESoapResponse *response,
					  gboolean *out_includes_last_item,
					  GSList **out_items,
					  GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "FindItemResponseMessage")) {
			ESoapParameter *node, *subparam1;
			gchar *last;
			EEwsItem *item;
			gboolean includes_last_item;

			node = e_soap_parameter_get_first_child_by_name (subparam, "RootFolder");
			last = e_soap_parameter_get_property (node, "IncludesLastItemInRange");
			/*
			 * Set the includes_last_item to TRUE as default.
			 * It can avoid an infinite loop in caller, when, for some reason,
			 * we don't receive the last_tag property from the server.
			 */
			includes_last_item = g_strcmp0 (last, "false") != 0;
			g_free (last);

			node = e_soap_parameter_get_first_child_by_name (node, "Items");
			for (subparam1 = e_soap_parameter_get_first_child (node);
			     subparam1; subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				item = e_ews_item_new_from_soap_parameter (subparam1);
				if (item)
					*out_items = g_slist_prepend (*out_items, item);
			}

			if (out_includes_last_item)
				*out_includes_last_item = includes_last_item;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_find_folder_items_sync (EEwsConnection *cnc,
                                         gint pri,
                                         EwsFolderId *fid,
                                         const gchar *default_props,
                                         const EEwsAdditionalProps *add_props,
                                         EwsSortOrder *sort_order,
                                         const gchar *query,
					 GPtrArray *only_ids, /* element-type utf8 */
                                         EEwsFolderType type,
                                         gboolean *out_includes_last_item,
                                         GSList **out_items,
                                         EwsConvertQueryCallback convert_query_cb,
                                         GCancellable *cancellable,
                                         GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_items != NULL, FALSE);

	*out_items = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"FindItem",
		"Traversal",
		"Shallow",
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ItemShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, default_props);

	ews_append_additional_props_to_msg (request, add_props);

	e_soap_request_end_element (request);

	/*write restriction message based on query*/
	if (convert_query_cb) {
		e_soap_request_start_element (request, "Restriction", "messages", NULL);

		if (only_ids && only_ids->len) {
			e_soap_request_start_element (request, "And", "messages", NULL);
			e_soap_request_start_element (request, "Or", "messages", NULL);
			ews_connection_write_only_ids_restriction (request, only_ids);
			e_soap_request_end_element (request); /* Or */
		}

		convert_query_cb (request, query, type);

		if (only_ids && only_ids->len)
			e_soap_request_end_element (request); /* And */

		e_soap_request_end_element (request); /* Restriction */
	} else if (only_ids && only_ids->len) {
		e_soap_request_start_element (request, "Restriction", "messages", NULL);
		ews_connection_write_only_ids_restriction (request, only_ids);
		e_soap_request_end_element (request);
	}

	if (sort_order)
		ews_write_sort_order_to_msg (request, sort_order);

	e_soap_request_start_element (request, "ParentFolderIds", "messages", NULL);

	if (fid->is_distinguished_id)
		e_ews_request_write_string_parameter_with_attribute (request, "DistinguishedFolderId", NULL, NULL, "Id", fid->id);
	else
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", fid->id);

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_find_folder_items_response (cnc, response, out_includes_last_item, out_items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success) {
		*out_items = g_slist_reverse (*out_items);
	} else {
		g_slist_free_full (*out_items, g_object_unref);
		*out_items = NULL;
	}

	return success;
}

static gboolean
e_ews_process_sync_hierarchy_response (EEwsConnection *cnc,
				       ESoapResponse *response,
				       gchar **out_new_sync_state,
				       gboolean *out_includes_last_item,
				       GSList **out_items_created,
				       GSList **out_items_updated,
				       GSList **out_items_deleted,
				       GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "SyncFolderHierarchyResponseMessage")) {
			e_ews_process_sync_xxx_response (subparam, (ItemParser) e_ews_folder_new_from_soap_parameter,
				"IncludesLastFolderInRange", "FolderId",
				out_new_sync_state, out_includes_last_item, out_items_created, out_items_updated, out_items_deleted);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_sync_folder_hierarchy_sync (EEwsConnection *cnc,
                                             gint pri,
					     const gchar *old_sync_state,
                                             gchar **out_new_sync_state,
                                             gboolean *out_includes_last_folder,
                                             GSList **out_folders_created,
                                             GSList **out_folders_updated,
                                             GSList **out_folders_deleted,
                                             GCancellable *cancellable,
                                             GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"SyncFolderHierarchy",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, "AllProperties");
	e_soap_request_start_element (request, "AdditionalProperties", NULL, NULL);
	e_soap_request_start_element (request, "ExtendedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (request, "PropertyTag", "4340", NULL, NULL); /* PidTagAttributeHidden */
	e_soap_request_add_attribute (request, "PropertyType", "Boolean", NULL, NULL);
	e_soap_request_end_element (request); /* ExtendedFieldURI */
	e_soap_request_end_element (request); /* AdditionalProperties */
	e_soap_request_end_element (request); /* FolderShape */

	if (old_sync_state)
		e_ews_request_write_string_parameter (request, "SyncState", "messages", old_sync_state);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_sync_hierarchy_response (cnc, response, out_new_sync_state, out_includes_last_folder,
		out_folders_created, out_folders_updated, out_folders_deleted, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

EEwsServerVersion
e_ews_connection_get_server_version (EEwsConnection *cnc)
{
	g_return_val_if_fail (cnc != NULL, E_EWS_EXCHANGE_UNKNOWN);
	g_return_val_if_fail (cnc->priv != NULL, E_EWS_EXCHANGE_UNKNOWN);

	return cnc->priv->version;
}

void
e_ews_connection_set_server_version (EEwsConnection *cnc,
				     EEwsServerVersion version)
{
	g_return_if_fail (cnc != NULL);
	g_return_if_fail (cnc->priv != NULL);

	if (cnc->priv->version != version)
		cnc->priv->version = version;
}

void
e_ews_connection_set_server_version_from_string (EEwsConnection *cnc,
						 const gchar *version)
{
	if (!version)
		cnc->priv->version = E_EWS_EXCHANGE_UNKNOWN;
	else if (g_strcmp0 (version, "Exchange2007") == 0)
		cnc->priv->version = E_EWS_EXCHANGE_2007;
	else if (g_strcmp0 (version, "Exchange2007_SP1") == 0 ||
		 g_str_has_prefix (version, "Exchange2007"))
		cnc->priv->version = E_EWS_EXCHANGE_2007_SP1;
	else if (g_strcmp0 (version, "Exchange2010") == 0)
		cnc->priv->version = E_EWS_EXCHANGE_2010;
	else if (g_strcmp0 (version, "Exchange2010_SP1") == 0)
		cnc->priv->version = E_EWS_EXCHANGE_2010_SP1;
	else if (g_strcmp0 (version, "Exchange2010_SP2") == 0 ||
		 g_str_has_prefix (version, "Exchange2010"))
		cnc->priv->version = E_EWS_EXCHANGE_2010_SP2;
	else
		cnc->priv->version = E_EWS_EXCHANGE_FUTURE;
}

gboolean
e_ews_connection_satisfies_server_version (EEwsConnection *cnc,
					  EEwsServerVersion version)
{
	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (cnc->priv != NULL, FALSE);

	/*
	 * This test always will fail if, for some reason, we were not able to get the server version.
	 * It occurrs intentionally because we don't want to call any function that expects an EWS
	 * Server version higher than 2007 SP1 without be sure we using an EWS Server with version
	 * 2007 SP1 or later.
	 */
	return cnc->priv->version >= version;
}

static gboolean
e_ews_process_get_items_response (EEwsConnection *cnc,
				  ESoapResponse *response,
				  GSList **out_items,
				  GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (g_str_has_suffix (name, "ResponseMessage")) {
			ESoapParameter *node;
			EEwsItem *item;

			if (ews_get_response_status (subparam, &local_error))
				local_error = NULL;

			for (node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
			     node;
			     node = e_soap_parameter_get_next_child_by_name (subparam, "Items")) {
				if (node->children)
					item = e_ews_item_new_from_soap_parameter (node);
				else
					item = NULL;
				if (!item && local_error != NULL)
					item = e_ews_item_new_from_error (local_error);
				if (item)
					*out_items = g_slist_prepend (*out_items, item);
			}
		} else {
			g_warning ("%s: Unexpected element <%s>", G_STRFUNC, name);
		}

		/* Do not stop on errors. */
		g_clear_error (&local_error);

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	/* if there is only one item, then check whether it's an error */
	if (*out_items && (*out_items)->data && !(*out_items)->next) {
		EEwsItem *item = (*out_items)->data;

		if (item && e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_propagate_error (error, g_error_copy (e_ews_item_get_error (item)));

			g_slist_free_full (*out_items, g_object_unref);
			*out_items = NULL;

			return FALSE;
		}
	}

	*out_items = g_slist_reverse (*out_items);

	return TRUE;
}

gboolean
e_ews_connection_get_items_sync (EEwsConnection *cnc,
                                 gint pri,
                                 const GSList *ids,
                                 const gchar *default_props,
				 const EEwsAdditionalProps *add_props,
                                 gboolean include_mime,
                                 const gchar *mime_directory,
				 EEwsBodyType body_type,
                                 GSList **out_items,
                                 ESoapResponseProgressFn progress_fn,
                                 gpointer progress_data,
                                 GCancellable *cancellable,
                                 GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_items != NULL, FALSE);

	*out_items = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetItem",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (progress_fn && progress_data)
		e_soap_request_set_progress_fn (request, progress_fn, progress_data);

	e_soap_request_start_element (request, "ItemShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, default_props);

	if (include_mime)
		e_ews_request_write_string_parameter (request, "IncludeMimeContent", NULL, "true");
	else
		e_ews_request_write_string_parameter (request, "IncludeMimeContent", NULL, "false");
	if (mime_directory)
		e_soap_request_set_store_node_data (request, "MimeContent", mime_directory, TRUE);

	switch (body_type) {
	case E_EWS_BODY_TYPE_BEST:
		e_ews_request_write_string_parameter (request, "BodyType", NULL, "Best");
		break;
	case E_EWS_BODY_TYPE_HTML:
		e_ews_request_write_string_parameter (request, "BodyType", NULL, "HTML");
		break;
	case E_EWS_BODY_TYPE_TEXT:
		e_ews_request_write_string_parameter (request, "BodyType", NULL, "Text");
		break;
	case E_EWS_BODY_TYPE_ANY:
		break;
	}

	ews_append_additional_props_to_msg (request, add_props);

	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "ItemIds", "messages", NULL);

	for (link = ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "ItemId", NULL, NULL, "Id", link->data);
	}

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_items_response (cnc, response, out_items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success) {
		g_slist_free_full (*out_items, g_object_unref);
		*out_items = NULL;
	}

	return success;
}

static const gchar *
ews_delete_type_to_str (EwsDeleteType delete_type)
{
	switch (delete_type) {
		case EWS_HARD_DELETE:
			return "HardDelete";
		case EWS_SOFT_DELETE:
			return "SoftDelete";
		case EWS_MOVE_TO_DELETED_ITEMS:
			return "MoveToDeletedItems";
	}
	return NULL;
}

static const gchar *
ews_send_cancels_to_str (EwsSendMeetingCancellationsType send_cancels)
{
	switch (send_cancels) {
		case EWS_SEND_TO_NONE:
			return "SendToNone";
		case EWS_SEND_ONLY_TO_ALL:
			return "SendOnlyToAll";
		case EWS_SEND_TO_ALL_AND_SAVE_COPY:
			return "SendToAllAndSaveCopy";
	}
	return NULL;
}

static const gchar *
ews_affected_tasks_to_str (EwsAffectedTaskOccurrencesType affected_tasks)
{
	switch (affected_tasks) {
		case EWS_NONE_OCCURRENCES:
			return NULL;
		case EWS_ALL_OCCURRENCES:
			return "AllOccurrences";
		case EWS_SPECIFIED_OCCURRENCE_ONLY:
			return "SpecifiedOccurrenceOnly";
	}
	return NULL;
}

gboolean
e_ews_connection_delete_items_sync (EEwsConnection *cnc,
                                    gint pri,
                                    const GSList *ids,
                                    EwsDeleteType delete_type,
                                    EwsSendMeetingCancellationsType send_cancels,
                                    EwsAffectedTaskOccurrencesType affected_tasks,
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"DeleteItem",
		"DeleteType",
		ews_delete_type_to_str (delete_type),
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (send_cancels)
		e_soap_request_add_attribute (request, "SendMeetingCancellations",
			ews_send_cancels_to_str (send_cancels), NULL, NULL);

	if (affected_tasks)
		e_soap_request_add_attribute (request, "AffectedTaskOccurrences",
			ews_affected_tasks_to_str (affected_tasks), NULL, NULL);

	e_soap_request_start_element (request, "ItemIds", "messages", NULL);

	for (link = ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "ItemId", NULL, NULL, "Id", link->data);
	}

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_delete_item_sync (EEwsConnection *cnc,
                                   gint pri,
                                   EwsId *id,
                                   guint index,
                                   EwsDeleteType delete_type,
                                   EwsSendMeetingCancellationsType send_cancels,
                                   EwsAffectedTaskOccurrencesType affected_tasks,
                                   GCancellable *cancellable,
                                   GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gchar buffer[32];
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"DeleteItem",
		"DeleteType",
		ews_delete_type_to_str (delete_type),
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (send_cancels)
		e_soap_request_add_attribute (request, "SendMeetingCancellations",
			ews_send_cancels_to_str (send_cancels), NULL, NULL);

	if (affected_tasks != EWS_NONE_OCCURRENCES)
		e_soap_request_add_attribute (request, "AffectedTaskOccurrences",
			ews_affected_tasks_to_str (affected_tasks), NULL, NULL);

	e_soap_request_start_element (request, "ItemIds", "messages", NULL);

	if (index) {
		e_soap_request_start_element (request, "OccurrenceItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "RecurringMasterId", id->id, NULL, NULL);
		if (id->change_key)
			e_soap_request_add_attribute (request, "ChangeKey", id->change_key, NULL, NULL);
		snprintf (buffer, 32, "%u", index);
		e_soap_request_add_attribute (request, "InstanceIndex", buffer, NULL, NULL);
		e_soap_request_end_element (request);
	} else {
		e_soap_request_start_element (request, "ItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "Id", id->id, NULL, NULL);
		if (id->change_key)
			e_soap_request_add_attribute (request, "ChangeKey", id->change_key, NULL, NULL);
		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_delete_items_in_chunks_sync (EEwsConnection *cnc,
					      gint pri,
					      const GSList *ids,
					      EwsDeleteType delete_type,
					      EwsSendMeetingCancellationsType send_cancels,
					      EwsAffectedTaskOccurrencesType affected_tasks,
					      GCancellable *cancellable,
					      GError **error)
{
	const GSList *iter;
	guint total_ids = 0, done_ids = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	g_object_ref (cnc);

	iter = ids;

	while (success && iter) {
		guint n_ids;
		const GSList *tmp_iter;

		for (tmp_iter = iter, n_ids = 0; tmp_iter && n_ids < EWS_MOVE_ITEMS_CHUNK_SIZE; tmp_iter = g_slist_next (tmp_iter), n_ids++) {
			/* Only check bounds first, to avoid unnecessary allocations */
		}

		if (tmp_iter) {
			GSList *shorter = NULL;

			if (total_ids == 0)
				total_ids = g_slist_length ((GSList *) ids);

			for (n_ids = 0; iter && n_ids < EWS_MOVE_ITEMS_CHUNK_SIZE; iter = g_slist_next (iter), n_ids++) {
				shorter = g_slist_prepend (shorter, iter->data);
			}

			shorter = g_slist_reverse (shorter);

			success = e_ews_connection_delete_items_sync (cnc, pri, shorter, delete_type, send_cancels,
				affected_tasks, cancellable, error);

			g_slist_free (shorter);

			done_ids += n_ids;
		} else {
			success = e_ews_connection_delete_items_sync (cnc, pri, iter, delete_type, send_cancels,
				affected_tasks, cancellable, error);

			iter = NULL;
			done_ids = total_ids;
		}

		if (total_ids > 0)
			camel_operation_progress (cancellable, 100 * (gdouble) done_ids / (gdouble) total_ids);
	}

	g_object_unref (cnc);

	return success;
}

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx,
	    const gchar *format,
	    ...)
{
	xmlXPathObjectPtr result;
	va_list args;
	gchar *expr;

	if (ctx == NULL)
		return NULL;

	va_start (args, format);
	expr = g_strdup_vprintf (format, args);
	va_end (args);

	result = xmlXPathEvalExpression ((xmlChar *) expr, ctx);
	g_free (expr);

	if (result == NULL)
		return NULL;

	if (result->type == XPATH_NODESET && xmlXPathNodeSetIsEmpty (result->nodesetval)) {
		xmlXPathFreeObject (result);
		return NULL;
	}

	return result;
}

static gboolean
element_has_child (ESoapRequest *request,
		   const gchar *path)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr result;
	xmlNodeSetPtr nodeset;
	xmlNodePtr node;
	gboolean ret = FALSE;

	doc = e_soap_request_get_xml_doc (request);
	xpctx = xmlXPathNewContext (doc);

	xmlXPathRegisterNs (
			xpctx,
			(xmlChar *) "s",
			(xmlChar *) "http://schemas.xmlsoap.org/soap/envelope/");

	xmlXPathRegisterNs (
			xpctx,
			(xmlChar *) "m",
			(xmlChar *) "http://schemas.microsoft.com/exchange/services/2006/messages");

	xmlXPathRegisterNs (
			xpctx,
			(xmlChar *) "t",
			(xmlChar *) "http://schemas.microsoft.com/exchange/services/2006/types");

	result = xpath_eval (xpctx, path);

	if (result == NULL)
		goto exit;

	if (!xmlXPathNodeSetGetLength (result->nodesetval))
		goto exit;

	nodeset = result->nodesetval;
	node = nodeset->nodeTab[0];
	if (!node->children)
		goto exit;

	ret = TRUE;

exit:
	xmlXPathFreeObject (result);
	xmlXPathFreeContext (xpctx);
	return ret;
}

gboolean
e_ews_connection_update_items_sync (EEwsConnection *cnc,
                                    gint pri,
                                    const gchar *conflict_res,
                                    const gchar *msg_disposition,
                                    const gchar *send_invites,
                                    const gchar *folder_id,
                                    EEwsRequestCreationCallback create_cb,
                                    gpointer create_user_data,
                                    GSList **out_items,
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	GSList *items = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"UpdateItem",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (conflict_res)
		e_soap_request_add_attribute (request, "ConflictResolution", conflict_res, NULL, NULL);
	if (msg_disposition)
		e_soap_request_add_attribute (request, "MessageDisposition", msg_disposition, NULL, NULL);
	if (send_invites)
		e_soap_request_add_attribute (request, "SendMeetingInvitationsOrCancellations", send_invites, NULL, NULL);

	if (folder_id) {
		e_soap_request_start_element (request, "SavedItemFolderId", "messages", NULL);
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", folder_id);
		e_soap_request_end_element (request);
	}

	e_soap_request_start_element (request, "ItemChanges", "messages", NULL);

	if (!create_cb (request, create_user_data, error)) {
		g_clear_object (&request);
		return FALSE;
	}

	e_soap_request_end_element (request); /* ItemChanges */

	e_ews_request_write_footer (request);

	/*
	 * We need to check for both namespaces, because, the message is being wrote without use the types
	 * namespace. Maybe it is wrong, but the server doesn't complain about that. But this is the reason
	 * for the first check. The second one, is related to "how it should be" accord with EWS specifications.
	 */
	if (!element_has_child (request, "/s:Envelope/s:Body/m:UpdateItem/m:ItemChanges/ItemChange/Updates") &&
	    !element_has_child (request, "/s:Envelope/s:Body/m:UpdateItem/m:ItemChanges/t:ItemChange/t:Updates")) {
		g_clear_object (&request);
		return TRUE;
	}

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_items_response (cnc, response, &items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_items)
		*out_items = items;
	else
		g_slist_free_full (items, g_object_unref);

	return success;
}

gboolean
e_ews_connection_create_items_sync (EEwsConnection *cnc,
                                    gint pri,
                                    const gchar *msg_disposition,
                                    const gchar *send_invites,
                                    const EwsFolderId *fid,
                                    EEwsRequestCreationCallback create_cb,
                                    gpointer create_user_data,
                                    GSList **out_items,
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	GSList *items = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"CreateItem",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (msg_disposition)
		e_soap_request_add_attribute (request, "MessageDisposition", msg_disposition, NULL, NULL);
	if (send_invites)
		e_soap_request_add_attribute (request, "SendMeetingInvitations", send_invites, NULL, NULL);

	if (fid) {
		e_soap_request_start_element (request, "SavedItemFolderId", "messages", NULL);
		e_ews_folder_id_append_to_request (request, cnc->priv->email, fid);
		e_soap_request_end_element (request);
	}

	e_soap_request_start_element (request, "Items", "messages", NULL);

	if (!create_cb (request, create_user_data, error)) {
		g_clear_object (&request);
		return FALSE;
	}

	e_soap_request_end_element (request); /* Items */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_items_response (cnc, response, &items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_items)
		*out_items = items;
	else
		g_slist_free_full (items, g_object_unref);

	return success;
}

static const gchar *
get_search_scope_str (EwsContactsSearchScope scope)
{
	switch (scope) {
		case EWS_SEARCH_AD:
			return "ActiveDirectory";
		case EWS_SEARCH_AD_CONTACTS:
			return "ActiveDirectoryContacts";
		case EWS_SEARCH_CONTACTS:
			return "Contacts";
		case EWS_SEARCH_CONTACTS_AD:
			return "ContactsActiveDirectory";
		default:
			g_warn_if_reached ();
			return NULL;

	}
}

static gboolean
e_ews_process_resolve_names_response (EEwsConnection *cnc,
				      ESoapResponse *response,
				      gboolean *out_includes_last_item,
				      GSList **out_mailboxes,
				      GSList **out_contact_items,
				      GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	*out_includes_last_item = TRUE;
	*out_contact_items = NULL;
	*out_mailboxes = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error)) {
			return FALSE;
		}

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "ResolveNamesResponseMessage")) {
			ESoapParameter *node;
			gchar *prop;

			subparam = e_soap_parameter_get_first_child_by_name (subparam, "ResolutionSet");
			prop = e_soap_parameter_get_property (subparam, "IncludesLastItemInRange");
			/*
			 * Set the includes_last_item to TRUE as default.
			 * It can avoid an infinite loop in caller, when, for some reason,
			 * we don't receive the last_tag property from the server.
			 */
			*out_includes_last_item = g_strcmp0 (prop, "false") != 0;
			g_free (prop);

			for (subparam = e_soap_parameter_get_first_child_by_name (subparam, "Resolution");
			     subparam != NULL;
			     subparam = e_soap_parameter_get_next_child_by_name (subparam, "Resolution")) {
				EwsMailbox *mb;

				node = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
				mb = e_ews_item_mailbox_from_soap_param (node);
				if (mb) {
					EEwsItem *contact_item;

					*out_mailboxes = g_slist_prepend (*out_mailboxes, mb);

					/* 'mailboxes' and 'contact_items' match 1:1, but if the contact information
					 * wasn't found, then NULL is stored in the corresponding position */
					node = e_soap_parameter_get_first_child_by_name (subparam, "Contact");
					if (node) {
						contact_item = e_ews_item_new_from_soap_parameter (node);
						*out_contact_items = g_slist_prepend (*out_contact_items, contact_item);
					} else {
						*out_contact_items = g_slist_prepend (*out_contact_items, NULL);
					}
				}
			}

			break;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_resolve_names_sync (EEwsConnection *cnc,
                                     gint pri,
                                     const gchar *resolve_name,
                                     EwsContactsSearchScope scope,
                                     GSList *parent_folder_ids,
                                     gboolean fetch_contact_data,
                                     gboolean *out_includes_last_item,
                                     GSList **out_mailboxes,
                                     GSList **out_contact_items,
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean includes_last_item = TRUE;
	GSList *mailboxes = NULL, *contact_items = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"ResolveNames",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_add_attribute (request, "SearchScope", get_search_scope_str (scope), NULL, NULL);

	if (fetch_contact_data) {
		e_soap_request_add_attribute (request, "ReturnFullContactData", "true", NULL, NULL);

		if (e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2010_SP2))
			e_soap_request_add_attribute (request, "ContactDataShape", "AllProperties", NULL, NULL);
	} else {
		e_soap_request_add_attribute (request, "ReturnFullContactData", "false", NULL, NULL);
	}

	if (parent_folder_ids) {
		e_soap_request_start_element (request, "ParentFolderIds", "messages", NULL);
		ews_append_folder_ids_to_msg (request, cnc->priv->email, parent_folder_ids);
		e_soap_request_end_element (request);
	}

	e_ews_request_write_string_parameter (request, "UnresolvedEntry", "messages", resolve_name);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_resolve_names_response (cnc, response, &includes_last_item, &mailboxes, &contact_items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_includes_last_item)
		*out_includes_last_item = includes_last_item;

	if (success && out_mailboxes)
		*out_mailboxes = g_slist_reverse (mailboxes);
	else
		g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);

	if (success && out_contact_items)
		*out_contact_items = g_slist_reverse (contact_items);
	else
		e_util_free_nullable_object_slist (contact_items);

	return success;
}

static void
ews_connection_resolve_by_name (EEwsConnection *cnc,
                                gint pri,
                                const gchar *usename,
                                gboolean is_user_name,
                                gchar **smtp_address,
                                GCancellable *cancellable)
{
	GSList *mailboxes = NULL;
	GSList *contacts = NULL;
	gboolean includes_last_item = FALSE;
	GSList *miter;
	gint len;

	g_return_if_fail (cnc != NULL);
	g_return_if_fail (usename != NULL);
	g_return_if_fail (smtp_address != NULL);

	if (!*usename)
		return;

	len = strlen (usename);
	mailboxes = NULL;
	contacts = NULL;

	/* use the first error, not the guess-part error */
	e_ews_connection_resolve_names_sync (
		cnc, pri, usename,
		EWS_SEARCH_AD_CONTACTS, NULL, TRUE, &includes_last_item, &mailboxes, &contacts,
		cancellable, NULL);

	for (miter = mailboxes; miter; miter = miter->next) {
		const EwsMailbox *mailbox = miter->data;
		if (mailbox->email && *mailbox->email && g_strcmp0 (mailbox->routing_type, "EX") != 0
		    && ((!is_user_name && g_str_has_prefix (mailbox->email, usename) && mailbox->email[len] == '@') ||
		    (is_user_name && g_str_equal (usename, mailbox->name)))) {
			*smtp_address = g_strdup (mailbox->email);
			break;
		} else if (contacts && !contacts->next && contacts->data &&
			   e_ews_item_get_item_type (contacts->data) == E_EWS_ITEM_TYPE_CONTACT) {
			EEwsItem *contact_item = contacts->data;
			GHashTable *addresses_hash = e_ews_item_get_email_addresses (contact_item);
			GList *emails = addresses_hash ? g_hash_table_get_values (addresses_hash) : NULL, *iter;
			const gchar *display_name;
			gboolean found = FALSE;

			display_name = e_ews_item_get_display_name (contact_item);
			if (!display_name || !*display_name)
				display_name = e_ews_item_get_fileas (contact_item);

			for (iter = emails; iter && !found; iter = iter->next) {
				const gchar *it_email = iter->data;

				if (it_email && g_str_has_prefix (it_email, "SMTP:")
				    && ((!is_user_name && g_str_has_prefix (it_email, usename) && it_email[len] == '@') ||
				    (is_user_name && display_name && g_str_equal (usename, display_name)))) {
					found = TRUE;
					break;
				}
			}

			g_list_free (emails);

			if (found) {
				gint ii;

				for (ii = 0; ii < g_hash_table_size (addresses_hash); ii++) {
					gchar *key, *value;

					key = g_strdup_printf ("EmailAddress%d", ii + 1);
					value = g_hash_table_lookup (addresses_hash, key);
					g_free (key);

					if (value && g_str_has_prefix (value, "SMTP:")) {
						/* pick the first available SMTP address */
						*smtp_address = g_strdup (value + 5);
						break;
					}
				}
				break;
			}
		}
	}

	g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);
	e_util_free_nullable_object_slist (contacts);
}

gboolean
e_ews_connection_ex_to_smtp_sync (EEwsConnection *cnc,
                                  gint pri,
                                  const gchar *name,
                                  const gchar *ex_address,
                                  gchar **smtp_address,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GSList *mailboxes = NULL;
	GSList *contacts = NULL;
	gboolean includes_last_item = FALSE;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (ex_address != NULL, FALSE);
	g_return_val_if_fail (smtp_address != NULL, FALSE);

	*smtp_address = NULL;

	e_ews_connection_resolve_names_sync (
		cnc, pri, ex_address,
		EWS_SEARCH_AD_CONTACTS, NULL, TRUE, &includes_last_item, &mailboxes, &contacts,
		cancellable, error);

	/* only one mailbox matches */
	if (mailboxes && !mailboxes->next && mailboxes->data) {
		const EwsMailbox *mailbox = mailboxes->data;
		if (mailbox->email && *mailbox->email && g_strcmp0 (mailbox->routing_type, "EX") != 0) {
			*smtp_address = g_strdup (mailbox->email);
		} else if (contacts && !contacts->next && contacts->data &&
			   e_ews_item_get_item_type (contacts->data) == E_EWS_ITEM_TYPE_CONTACT) {
			EEwsItem *contact_item = contacts->data;
			GHashTable *addresses = e_ews_item_get_email_addresses (contact_item);
			gint ii;

			for (ii = 0; ii < (addresses ? g_hash_table_size (addresses) : 0); ii++) {
				gchar *key, *value;

				key = g_strdup_printf ("EmailAddress%d", ii + 1);
				value = g_hash_table_lookup (addresses, key);
				g_free (key);

				if (value && g_str_has_prefix (value, "SMTP:")) {
					/* pick the first available SMTP address */
					*smtp_address = g_strdup (value + 5);
					break;
				}
			}
		}
	}

	g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);
	e_util_free_nullable_object_slist (contacts);

	if (!*smtp_address) {
		const gchar *usename;

		usename = strrchr (ex_address, '/');
		if (usename && g_ascii_strncasecmp (usename, "/cn=", 4) == 0) {
			usename += 4;

			/* try to guess from common name of the EX address */
			ews_connection_resolve_by_name (cnc, pri, usename, FALSE, smtp_address, cancellable);
		}

		if (!*smtp_address && name && *name) {
			/* try to guess from mailbox name */
			ews_connection_resolve_by_name (cnc, pri, name, TRUE, smtp_address, cancellable);
		}
	}

	if (*smtp_address)
		g_clear_error (error);

	return *smtp_address != NULL;
}

static gboolean
e_ews_process_expand_dl_response (EEwsConnection *cnc,
				  ESoapResponse *response,
				  gboolean *out_includes_last_item,
				  GSList **out_mailboxes,
				  GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	*out_includes_last_item = TRUE;
	*out_mailboxes = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "ExpandDLResponseMessage")) {
			gchar *prop;

			subparam = e_soap_parameter_get_first_child_by_name (subparam, "DLExpansion");
			prop = e_soap_parameter_get_property (subparam, "IncludesLastItemInRange");
			/*
			 * Set the includes_last_item to TRUE as default.
			 * It can avoid an infinite loop in caller, when, for some reason,
			 * we don't receive the last_tag property from the server.
			 */
			*out_includes_last_item = g_strcmp0 (prop, "false") != 0;
			g_free (prop);

			for (subparam = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
			     subparam != NULL;
			     subparam = e_soap_parameter_get_next_child_by_name (subparam, "Mailbox")) {
				EwsMailbox *mb;

				mb = e_ews_item_mailbox_from_soap_param (subparam);
				if (mb)
					*out_mailboxes = g_slist_prepend (*out_mailboxes, mb);
			}

			break;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_expand_dl_sync (EEwsConnection *cnc,
                                 gint pri,
                                 const EwsMailbox *mb,
                                 gboolean *out_includes_last_item,
                                 GSList **out_mailboxes,
                                 GCancellable *cancellable,
                                 GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean includes_last_item = TRUE;
	GSList *mailboxes = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"ExpandDL",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", "messages", NULL);

	if (mb->item_id) {
		e_soap_request_start_element (request, "ItemId", NULL, NULL);

		e_soap_request_add_attribute (request, "Id", mb->item_id->id, NULL, NULL);
		e_soap_request_add_attribute (request, "ChangeKey", mb->item_id->change_key, NULL, NULL);

		e_soap_request_end_element (request); /* Mailbox */

	} else if (mb->email)
		e_ews_request_write_string_parameter (request, "EmailAddress", NULL, mb->email);

	e_soap_request_end_element (request); /* Mailbox */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_expand_dl_response (cnc, response, &includes_last_item, &mailboxes, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_includes_last_item)
		*out_includes_last_item = includes_last_item;

	if (success && out_mailboxes)
		*out_mailboxes = g_slist_reverse (mailboxes);
	else
		g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);

	return success;
}

gboolean
e_ews_connection_update_folder_sync (EEwsConnection *cnc,
                                     gint pri,
                                     EEwsRequestCreationCallback create_cb,
                                     gpointer create_user_data,
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (create_cb != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"UpdateFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderChanges", "messages", NULL);

	if (!create_cb (request, create_user_data, error)) {
		g_clear_object (&request);
		return FALSE;
	}

	e_soap_request_end_element (request); /* FolderChanges */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_move_folder_sync (EEwsConnection *cnc,
                                   gint pri,
                                   const gchar *to_folder,
                                   const gchar *folder,
                                   GCancellable *cancellable,
                                   GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"MoveFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ToFolderId", "messages", NULL);
	if (to_folder)
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", to_folder);
	else
		e_ews_request_write_string_parameter_with_attribute (request, "DistinguishedFolderId", NULL, NULL, "Id", "msgfolderroot");

	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", folder);
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gboolean
e_ews_process_get_folder_response (EEwsConnection *cnc,
				   ESoapResponse *response,
				   GSList **out_folders, /* EEwsFolder * */
				   GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, &local_error)) {
			if (g_strcmp0 (name, "GetFolderResponseMessage") == 0) {
				if (out_folders)
					*out_folders = g_slist_prepend (*out_folders, e_ews_folder_new_from_error (local_error));
				g_clear_error (&local_error);
			} else {
				g_propagate_error (error, local_error);
				return FALSE;
			}
		} else if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetFolderResponseMessage") && out_folders) {
			ESoapParameter *node;
			EEwsFolder *folder;

			for (node = e_soap_parameter_get_first_child_by_name (subparam, "Folders");
			     node;
			     node = e_soap_parameter_get_next_child_by_name (subparam, "Folders")) {
				folder = e_ews_folder_new_from_soap_parameter (node);
				if (!folder)
					continue;
				*out_folders = g_slist_prepend (*out_folders, folder);
			}
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_get_folder_sync (EEwsConnection *cnc,
                                  gint pri,
                                  const gchar *folder_shape,
                                  const EEwsAdditionalProps *add_props,
                                  GSList *folder_ids,
                                  GSList **out_folders,
                                  GCancellable *cancellable,
                                  GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		TRUE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, folder_shape);

	ews_append_additional_props_to_msg (request, add_props);
	e_soap_request_end_element (request);

	if (folder_ids) {
		e_soap_request_start_element (request, "FolderIds", "messages", NULL);
		ews_append_folder_ids_to_msg (request, cnc->priv->email, folder_ids);
		e_soap_request_end_element (request);
	}

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	if (out_folders)
		*out_folders = NULL;

	success = e_ews_process_get_folder_response (cnc, response, out_folders, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_folders) {
		*out_folders = g_slist_reverse (*out_folders);
	} else if (out_folders) {
		g_slist_free_full (*out_folders, g_object_unref);
		*out_folders = NULL;
	}

	return success;
}

static gboolean
e_ews_process_create_folder_response (EEwsConnection *cnc,
				      ESoapResponse *response,
				      EEwsFolderType folder_type,
				      GSList **out_folder_ids, /* EwsFolderId * */
				      GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	*out_folder_ids = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "CreateFolderResponseMessage")) {
			ESoapParameter *node;
			EwsFolderId *fid = NULL;
			const gchar *folder_element;

			switch (folder_type) {
				case E_EWS_FOLDER_TYPE_MAILBOX:
				case E_EWS_FOLDER_TYPE_MEMOS:
					folder_element = "Folder";
					break;
				case E_EWS_FOLDER_TYPE_CALENDAR:
					folder_element = "CalendarFolder";
					break;
				case E_EWS_FOLDER_TYPE_CONTACTS:
					folder_element = "ContactsFolder";
					break;
				case E_EWS_FOLDER_TYPE_SEARCH:
					folder_element = "SearchFolder";
					break;
				case E_EWS_FOLDER_TYPE_TASKS:
					folder_element = "TasksFolder";
					break;
				default:
					g_warn_if_reached ();
					folder_element = "Folder";
					break;
			}

			node = e_soap_parameter_get_first_child_by_name (subparam, "Folders");
			node = e_soap_parameter_get_first_child_by_name (node, folder_element);
			param = e_soap_parameter_get_first_child_by_name (node, "FolderId");

			fid = g_new0 (EwsFolderId, 1);
			fid->id = e_soap_parameter_get_property (param, "Id");
			fid->change_key = e_soap_parameter_get_property (param, "ChangeKey");
			*out_folder_ids = g_slist_prepend (*out_folder_ids, fid);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return *out_folder_ids != NULL;
}

gboolean
e_ews_connection_create_folder_sync (EEwsConnection *cnc,
                                     gint pri,
                                     const gchar *parent_folder_id,
                                     gboolean is_distinguished_id,
                                     const gchar *folder_name,
                                     EEwsFolderType folder_type,
                                     EwsFolderId **out_folder_id,
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	GSList *folder_ids = NULL;
	const gchar *folder_element;
	const gchar *folder_class = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"CreateFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ParentFolderId", "messages", NULL);

	/* If NULL passed for parent_folder_id, use "msgfolderroot" */
	if (is_distinguished_id || !parent_folder_id) {
		e_soap_request_start_element (request, "DistinguishedFolderId", NULL, NULL);
		e_soap_request_add_attribute (request, "Id", parent_folder_id ? parent_folder_id : "msgfolderroot", NULL, NULL);
		if (is_distinguished_id && cnc->priv->email) {
			e_soap_request_start_element (request, "Mailbox", NULL, NULL);
			e_ews_request_write_string_parameter (request, "EmailAddress", NULL, cnc->priv->email);
			e_soap_request_end_element (request);
		}
		e_soap_request_end_element (request);
	} else {
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", parent_folder_id);
	}

	e_soap_request_end_element (request);

	switch (folder_type) {
		default:
			g_warn_if_reached ();
			/* fall through */
		case E_EWS_FOLDER_TYPE_MAILBOX:
			folder_element = "Folder";
			folder_class = "IPF.Note";
			break;
		case E_EWS_FOLDER_TYPE_CALENDAR:
			folder_element = "CalendarFolder";
			break;
		case E_EWS_FOLDER_TYPE_CONTACTS:
			folder_element = "ContactsFolder";
			break;
		case E_EWS_FOLDER_TYPE_SEARCH:
			folder_element = "SearchFolder";
			break;
		case E_EWS_FOLDER_TYPE_TASKS:
			folder_element = "TasksFolder";
			break;
		case E_EWS_FOLDER_TYPE_MEMOS:
			folder_element = "Folder";
			folder_class = "IPF.StickyNote";
			break;
	}

	e_soap_request_start_element (request, "Folders", "messages", NULL);
	e_soap_request_start_element (request, folder_element, NULL, NULL);
	if (folder_class)
		e_ews_request_write_string_parameter (request, "FolderClass", NULL, folder_class);
	e_ews_request_write_string_parameter (request, "DisplayName", NULL, folder_name);

	e_soap_request_end_element (request);
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_create_folder_response (cnc, response, folder_type, &folder_ids, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success) {
		folder_ids = g_slist_reverse (folder_ids);
		g_warn_if_fail (g_slist_length (folder_ids) <= 1);

		if (out_folder_id)
			*out_folder_id = folder_ids ? folder_ids->data : NULL;

		if (folder_ids && out_folder_id)
			folder_ids->data = NULL;
	}

	g_slist_free_full (folder_ids, (GDestroyNotify) e_ews_folder_id_free);

	return success;
}

gboolean
e_ews_connection_move_items_sync (EEwsConnection *cnc,
                                  gint pri,
                                  const gchar *folder_id,
                                  gboolean docopy,
                                  const GSList *ids,
                                  GSList **out_items,
                                  GCancellable *cancellable,
                                  GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	GSList *items = NULL;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	if (docopy)
		request = e_ews_request_new_with_header (
			cnc->priv->uri,
			cnc->priv->impersonate_user,
			"CopyItem",
			NULL,
			NULL,
			cnc->priv->version,
			E_EWS_EXCHANGE_2007_SP1,
			FALSE,
			error);
	else
		request = e_ews_request_new_with_header (
			cnc->priv->uri,
			cnc->priv->impersonate_user,
			"MoveItem",
			NULL,
			NULL,
			cnc->priv->version,
			E_EWS_EXCHANGE_2007_SP1,
			FALSE,
			error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ToFolderId", "messages", NULL);
	e_soap_request_start_element (request, "FolderId", NULL, NULL);
	e_soap_request_add_attribute (request, "Id", folder_id, NULL, NULL);
	e_soap_request_end_element (request); /* FolderId */
	e_soap_request_end_element (request); /* ToFolderId */

	e_soap_request_start_element (request, "ItemIds", "messages", NULL);
	for (link = ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "ItemId", NULL, NULL, "Id", link->data);
	}
	e_soap_request_end_element (request); /* ItemIds */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_items_response (cnc, response, &items, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_items)
		*out_items = items;
	else
		g_slist_free_full (items, g_object_unref);

	return success;
}

gboolean
e_ews_connection_move_items_in_chunks_sync (EEwsConnection *cnc,
					    gint pri,
					    const gchar *folder_id,
					    gboolean docopy,
					    const GSList *ids,
					    GSList **items,
					    GCancellable *cancellable,
					    GError **error)
{
	const GSList *iter;
	guint total_ids = 0, done_ids = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (items != NULL, FALSE);

	g_object_ref (cnc);

	*items = NULL;
	iter = ids;

	while (success && iter) {
		guint n_ids;
		const GSList *tmp_iter;
		GSList *processed_items = NULL;

		for (tmp_iter = iter, n_ids = 0; tmp_iter && n_ids < EWS_MOVE_ITEMS_CHUNK_SIZE; tmp_iter = g_slist_next (tmp_iter), n_ids++) {
			/* Only check bounds first, to avoid unnecessary allocations */
		}

		if (tmp_iter) {
			GSList *shorter = NULL;

			if (total_ids == 0)
				total_ids = g_slist_length ((GSList *) ids);

			for (n_ids = 0; iter && n_ids < EWS_MOVE_ITEMS_CHUNK_SIZE; iter = g_slist_next (iter), n_ids++) {
				shorter = g_slist_prepend (shorter, iter->data);
			}

			shorter = g_slist_reverse (shorter);

			success = e_ews_connection_move_items_sync (cnc, pri, folder_id, docopy,
				shorter, &processed_items, cancellable, error);

			g_slist_free (shorter);

			done_ids += n_ids;
		} else {
			success = e_ews_connection_move_items_sync (cnc, pri, folder_id, docopy,
				iter, &processed_items, cancellable, error);

			iter = NULL;
			done_ids = total_ids;
		}

		if (processed_items)
			*items = g_slist_concat (*items, processed_items);

		if (total_ids > 0)
			camel_operation_progress (cancellable, 100 * (gdouble) done_ids / (gdouble) total_ids);
	}

	g_object_unref (cnc);

	return success;
}

gboolean
e_ews_connection_delete_folder_sync (EEwsConnection *cnc,
                                     gint pri,
                                     const gchar *folder_id,
                                     gboolean is_distinguished_id,
                                     const gchar *delete_type,
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"DeleteFolder",
		"DeleteType",
		delete_type,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);

	e_soap_request_start_element (request,
			is_distinguished_id ? "DistinguishedFolderId" : "FolderId",
			NULL,
			NULL);
	e_soap_request_add_attribute (request, "Id", folder_id, NULL, NULL);

	/* This element is required for delegate access */
	if (is_distinguished_id && cnc->priv->email) {
		e_soap_request_start_element (request, "Mailbox", NULL, NULL);
		e_ews_request_write_string_parameter (request, "EmailAddress", NULL, cnc->priv->email);
		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request); /* </DistinguishedFolderId> || </FolderId> */

	e_soap_request_end_element (request); /* </FolderIds> */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_empty_folder_sync (EEwsConnection *cnc,
				    gint pri,
				    const gchar *folder_id,
				    gboolean is_distinguished_id,
				    const gchar *delete_type,
				    gboolean delete_subfolders,
				    GCancellable *cancellable,
				    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"EmptyFolder",
		"DeleteType",
		delete_type,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_add_attribute (request, "DeleteSubFolders", delete_subfolders ? "true" : "false", NULL, NULL);

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);

	e_soap_request_start_element (request,
			is_distinguished_id ? "DistinguishedFolderId" : "FolderId",
			NULL,
			NULL);
	e_soap_request_add_attribute (request, "Id", folder_id, NULL, NULL);

	/* This element is required for delegate access */
	if (is_distinguished_id && cnc->priv->email) {
		e_soap_request_start_element (request, "Mailbox", NULL, NULL);
		e_ews_request_write_string_parameter (request, "EmailAddress", NULL, cnc->priv->email);
		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request); /* </DistinguishedFolderId> || </FolderId> */

	e_soap_request_end_element (request); /* </FolderIds> */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gboolean
e_ews_process_create_attachments_response (EEwsConnection *cnc,
					   ESoapResponse *response,
					   gchar **out_change_key,
					   GSList **out_attachments_ids,
					   GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "CreateAttachmentResponseMessage")) {
			/* http://msdn.microsoft.com/en-us/library/aa565877%28v=EXCHG.80%29.aspx */
			ESoapParameter *attsubparam, *attspara, *last_relevant = NULL, *attparam;

			attspara = e_soap_parameter_get_first_child_by_name (param, "Attachments");

			for (attsubparam = e_soap_parameter_get_first_child (attspara); attsubparam != NULL; attsubparam = e_soap_parameter_get_next_child (attsubparam)) {
				if (!g_ascii_strcasecmp (e_soap_parameter_get_name (attsubparam), "FileAttachment")) {
					attparam = e_soap_parameter_get_first_child (attsubparam);
					last_relevant = attparam;

					if (out_attachments_ids)
						*out_attachments_ids = g_slist_prepend (*out_attachments_ids, e_soap_parameter_get_property (attparam, "Id"));
				}
			}

			if (last_relevant != NULL && out_change_key) {
				g_free (*out_change_key);
				*out_change_key = e_soap_parameter_get_property (last_relevant, "RootItemChangeKey");
			}
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

static gboolean
e_ews_connection_attach_file (ESoapRequest *request,
                              EEwsAttachmentInfo *info,
			      gboolean contact_photo,
			      GError **error)
{
	EEwsAttachmentInfoType type = e_ews_attachment_info_get_type (info);
	gchar *filename = NULL, *buffer = NULL;
	const gchar *content = NULL, *prefer_filename;
	gsize length;

	switch (type) {
		case E_EWS_ATTACHMENT_INFO_TYPE_URI: {
			/* TODO - handle a situation where the file isnt accessible/other problem with it */
			/* TODO - This is a naive implementation that just uploads the whole content into */
			/*        memory, ie very inefficient */
			const gchar *uri;
			gchar *filepath;
			GError *local_error = NULL;

			uri = e_ews_attachment_info_get_uri (info);

			/* convert uri to actual file path */
			filepath = g_filename_from_uri (uri, NULL, &local_error);
			if (local_error != NULL) {
				g_propagate_error (error, local_error);
				return FALSE;
			}

			g_file_get_contents (filepath, &buffer, &length, &local_error);
			if (local_error != NULL) {
				g_free (filepath);
				g_propagate_error (error, local_error);
				return FALSE;
			}

			content = buffer;

			filename = strrchr (filepath, G_DIR_SEPARATOR);
			filename = filename ? g_strdup (++filename) : g_strdup (filepath);

			g_free (filepath);
			break;
		}
		case E_EWS_ATTACHMENT_INFO_TYPE_INLINED:
			content = e_ews_attachment_info_get_inlined_data (info, &length);
			filename = g_strdup (e_ews_attachment_info_get_filename (info));
			break;
		default:
			g_warning ("Unknown EwsAttachmentInfoType %d", type);
			return FALSE;
	}

	e_soap_request_start_element (request, "FileAttachment", NULL, NULL);

	prefer_filename = e_ews_attachment_info_get_prefer_filename (info);
	e_ews_request_write_string_parameter (request, "Name", NULL, prefer_filename ? prefer_filename : filename);
	if (contact_photo)
		e_ews_request_write_string_parameter (request, "IsContactPhoto", NULL, "true");
	e_soap_request_start_element (request, "Content", NULL, NULL);
	e_soap_request_write_base64 (request, content, length);
	e_soap_request_end_element (request); /* "Content" */
	e_soap_request_end_element (request); /* "FileAttachment" */

	g_free (filename);
	g_free (buffer);

	return TRUE;
}

gboolean
e_ews_connection_create_attachments_sync (EEwsConnection *cnc,
					  gint pri,
					  const EwsId *parent,
					  const GSList *files,
					  gboolean is_contact_photo,
					  gchar **out_change_key,
					  GSList **out_attachments_ids,
					  GCancellable *cancellable,
					  GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (parent != NULL, FALSE);

	if (out_change_key)
		*out_change_key = NULL;

	if (out_attachments_ids)
		*out_attachments_ids = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"CreateAttachment",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "ParentItemId", "messages", NULL);
	e_soap_request_add_attribute (request, "Id", parent->id, NULL, NULL);
	if (parent->change_key)
		e_soap_request_add_attribute (request, "ChangeKey", parent->change_key, NULL, NULL);
	e_soap_request_end_element (request);

	/* start interation over all items to get the attachemnts */
	e_soap_request_start_element (request, "Attachments", "messages", NULL);

	for (link = files; link; link = g_slist_next (link)) {
		if (!e_ews_connection_attach_file (request, link->data, is_contact_photo, error)) {
			g_clear_object (&request);
			return FALSE;
		}
	}

	e_soap_request_end_element (request); /* "Attachments" */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_create_attachments_response (cnc, response, out_change_key, out_attachments_ids, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success) {
		if (out_attachments_ids)
			*out_attachments_ids = g_slist_reverse (*out_attachments_ids);
	} else {
		if (out_change_key)
			g_clear_pointer (out_change_key, g_free);

		if (out_attachments_ids) {
			g_slist_free_full (*out_attachments_ids, g_free);
			*out_attachments_ids = NULL;
		}
	}

	return success;
}

static gboolean
e_ews_process_delete_attachments_response (EEwsConnection *cnc,
					   ESoapResponse *response,
					   gchar **out_new_change_key,
					   GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	if (out_new_change_key)
		*out_new_change_key = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "DeleteAttachmentResponseMessage")) {
			/* http://msdn.microsoft.com/en-us/library/aa580782%28v=EXCHG.80%29.aspx */
			ESoapParameter *attspara;

			attspara = e_soap_parameter_get_first_child_by_name (subparam, "RootItemId");

			if (attspara != NULL && out_new_change_key)
				*out_new_change_key = e_soap_parameter_get_property (attspara, "RootItemChangeKey");
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_delete_attachments_sync (EEwsConnection *cnc,
                                          gint pri,
                                          const GSList *attachments_ids,
					  gchar **out_new_change_key,
                                          GCancellable *cancellable,
                                          GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"DeleteAttachment",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	/* start interation over all items to get the attachemnts */
	e_soap_request_start_element (request, "AttachmentIds", "messages", NULL);

	for (link = attachments_ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "AttachmentId", NULL, NULL, "Id", link->data);
	}

	e_soap_request_end_element (request); /* "AttachmentIds" */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_delete_attachments_response (cnc, response, out_new_change_key, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success && out_new_change_key)
		g_clear_pointer (out_new_change_key, g_free);

	return success;
}

static gboolean
e_ews_process_get_attachments_response (EEwsConnection *cnc,
					ESoapResponse *response,
					const gchar *uid,
					const gchar *cache_directory,
					GSList **out_attachments,
					GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	if (out_attachments)
		*out_attachments = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetAttachmentResponseMessage") && out_attachments) {
			ESoapParameter *subattsparam, *attspara;
			EEwsAttachmentInfo *info = NULL;
			EEwsItem *item;

			attspara = e_soap_parameter_get_first_child_by_name (subparam, "Attachments");

			for (subattsparam = e_soap_parameter_get_first_child (attspara);
			     subattsparam;
			     subattsparam = e_soap_parameter_get_next_child (subattsparam)) {
				name = e_soap_parameter_get_name (subattsparam);

				if (!g_ascii_strcasecmp (name, "ItemAttachment")) {
					item = e_ews_item_new_from_soap_parameter (subattsparam);
					info = e_ews_item_dump_mime_content (item, cache_directory);
					g_clear_object (&item);
				} else if (!g_ascii_strcasecmp (name, "FileAttachment")) {
					info = e_ews_dump_file_attachment_from_soap_parameter (subattsparam, cache_directory, uid);
				}

				if (info)
					*out_attachments = g_slist_prepend (*out_attachments, info);

				info = NULL;
			}
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_get_attachments_sync (EEwsConnection *cnc,
                                       gint pri,
                                       const gchar *uid,
                                       const GSList *ids,
                                       const gchar *cache_directory,
                                       gboolean include_mime,
                                       GSList **out_attachments,
                                       ESoapResponseProgressFn progress_fn,
                                       gpointer progress_data,
                                       GCancellable *cancellable,
                                       GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_attachments != NULL, FALSE);

	*out_attachments = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetAttachment",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	/* not sure why I need it, need to check */
	if (progress_fn && progress_data)
		e_soap_request_set_progress_fn (request, progress_fn, progress_data);

	if (cache_directory)
		e_soap_request_set_store_node_data (request, "MimeContent Content", cache_directory, TRUE);

	/* write empty attachments shape, need to discover maybe usefull in some cases */
	e_soap_request_start_element (request, "AttachmentShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "IncludeMimeContent", NULL, "true");
	e_soap_request_end_element (request);

	/* start interation over all items to get the attachemnts */
	e_soap_request_start_element (request, "AttachmentIds", "messages", NULL);

	for (link = ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "AttachmentId", NULL, NULL, "Id", link->data);
	}

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_attachments_response (cnc, response, uid, cache_directory, out_attachments, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (out_attachments && success) {
		*out_attachments = g_slist_reverse (*out_attachments);
	} else if (out_attachments) {
		g_slist_free_full (*out_attachments, (GDestroyNotify) e_ews_attachment_info_free);
		*out_attachments = NULL;
	}

	return success;
}

static void
ews_handle_free_busy_view (ESoapParameter *param,
			   GSList **out_components)
{
       /*parse the response to create a free_busy data
	http://msdn.microsoft.com / en - us / library / aa564001 % 28v = EXCHG.140 % 29.aspx */
	ICalComponent *vfb;
	ICalProperty *prop = NULL;
	ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
	ESoapParameter *viewparam, *eventarray, *event_param, *subparam;
	GTimeVal t_val;
	const gchar *name;
	gchar *value, *new_val = NULL, *summary = NULL, *location = NULL, *id = NULL;

	viewparam = e_soap_parameter_get_first_child_by_name (param, "FreeBusyView");
	if (!viewparam)
		return;

	vfb = i_cal_component_new_vfreebusy ();
	eventarray = e_soap_parameter_get_first_child_by_name (viewparam, "CalendarEventArray");
	for (event_param = eventarray ? e_soap_parameter_get_first_child (eventarray) : NULL;
	     event_param != NULL;
	     event_param = e_soap_parameter_get_next_child (event_param)) {
		ICalPeriod *ipt;
		gboolean is_recurring = FALSE;

		ipt = i_cal_period_new_null_period ();

		for (subparam = e_soap_parameter_get_first_child (event_param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {
			name = e_soap_parameter_get_name (subparam);

			if (!g_ascii_strcasecmp (name, "StartTime")) {
				ICalTime *itt;

				value = e_soap_parameter_get_string_value (subparam);
				/*We are sending UTC timezone and expect server to return in same*/

				/*Remove leading and trailing whitespace*/
				g_strstrip (value);

				if (g_utf8_strlen (value, -1) == 19) {
					/*If server returns time without zone add Z to treat it in UTC*/
					new_val = g_strdup_printf ("%sZ", value);
					g_free (value);
				} else
					new_val = value;

				g_time_val_from_iso8601 (new_val, &t_val);
				g_free (new_val);

				itt = i_cal_time_new_from_timet_with_zone (t_val.tv_sec, 0, utc_zone);
				i_cal_period_set_start (ipt, itt);
				g_clear_object (&itt);

			} else if (!g_ascii_strcasecmp (name, "EndTime")) {
				ICalTime *itt;

				value = e_soap_parameter_get_string_value (subparam);
				/*We are sending UTC timezone and expect server to return in same*/

				/*Remove leading and trailing whitespace*/
				g_strstrip (value);

				if (g_utf8_strlen (value, -1) == 19) {
					/*If server returns time without zone add Z to treat it in UTC*/
					new_val = g_strdup_printf ("%sZ", value);
					g_free (value);
				} else
					new_val = value;

				g_time_val_from_iso8601 (new_val, &t_val);
				g_free (new_val);

				itt = i_cal_time_new_from_timet_with_zone (t_val.tv_sec, 0, utc_zone);
				i_cal_period_set_end (ipt, itt);
				g_clear_object (&itt);

				prop = i_cal_property_new_freebusy (ipt);
			} else if (!g_ascii_strcasecmp (name, "BusyType")) {
				value = e_soap_parameter_get_string_value (subparam);
				if (!strcmp (value, "Busy"))
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY");
				else if (!strcmp (value, "Tentative"))
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY-TENTATIVE");
				else if (!strcmp (value, "OOF"))
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY-UNAVAILABLE");
				else if (!strcmp (value, "Free"))
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "FREE");
				g_free (value);
			} else if (!g_ascii_strcasecmp (name, "CalendarEventDetails")) {
				ESoapParameter *dparam;

				dparam = e_soap_parameter_get_first_child_by_name (subparam, "ID");
				if (dparam) {
					g_clear_pointer (&id, g_free);
					id = e_soap_parameter_get_string_value (dparam);
				}

				dparam = e_soap_parameter_get_first_child_by_name (subparam, "Subject");
				if (dparam) {
					g_clear_pointer (&summary, g_free);
					summary = e_soap_parameter_get_string_value (dparam);
				}

				dparam = e_soap_parameter_get_first_child_by_name (subparam, "Location");
				if (dparam) {
					g_clear_pointer (&location, g_free);
					location = e_soap_parameter_get_string_value (dparam);
				}

				dparam = e_soap_parameter_get_first_child_by_name (subparam, "IsRecurring");
				if (dparam) {
					value = e_soap_parameter_get_string_value (dparam);
					is_recurring = g_strcmp0 (value, "true") == 0;
					g_free (value);
				}
			}
		}

		if (prop) {
			if (id) {
				/* Unique-ize the ID for the recurring events, otherwise the ID clashes when
				   more items are from the same series/event. */
				if (is_recurring) {
					ICalTime *itt;

					itt = i_cal_period_get_start (ipt);
					if (itt) {
						gchar *itt_str, *tmp;

						itt_str = i_cal_time_as_ical_string (itt);
						tmp = g_strconcat (id, "-", itt_str, NULL);

						g_free (id);
						g_free (itt_str);

						id = tmp;
					}
					g_clear_object (&itt);
				}

				i_cal_property_set_parameter_from_string (prop, "X-EWS-ID", id);
			}
			if (summary)
				i_cal_property_set_parameter_from_string (prop, "X-SUMMARY", summary);
			if (location)
				i_cal_property_set_parameter_from_string (prop, "X-LOCATION", location);
			i_cal_component_take_property (vfb, prop);
			prop = NULL;
		}

		g_clear_pointer (&summary, g_free);
		g_clear_pointer (&location, g_free);
		g_clear_pointer (&id, g_free);
		g_clear_object (&ipt);
	}

	*out_components = g_slist_prepend (*out_components, vfb);
}

static gboolean
e_ews_process_get_free_busy_response (EEwsConnection *cnc,
				      ESoapResponse *response,
				      GSList **out_components,
				      GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "FreeBusyResponseArray", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		ESoapParameter *subsubparam;

		subsubparam = e_soap_parameter_get_first_child_by_name (
			subparam, "ResponseMessage");

		if (subsubparam) {
			if (!ews_get_response_status (subsubparam, error))
				return FALSE;

			if (out_components)
				ews_handle_free_busy_view (subparam, out_components);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_get_free_busy_sync (EEwsConnection *cnc,
                                     gint pri,
                                     EEwsRequestCreationCallback free_busy_cb,
                                     gpointer free_busy_user_data,
                                     GSList **out_free_busy, /* ICalComponent * */
                                     GCancellable *cancellable,
                                     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_free_busy != NULL, FALSE);

	*out_free_busy = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetUserAvailabilityRequest",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	if (!free_busy_cb (request, free_busy_user_data, error)) {
		g_clear_object (&request);
		return FALSE;
	}

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_free_busy_response (cnc, response, out_free_busy, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && out_free_busy) {
		*out_free_busy = g_slist_reverse (*out_free_busy);
	} else if (out_free_busy) {
		g_slist_free_full (*out_free_busy, g_object_unref);
		*out_free_busy = NULL;
	}

	return success;
}

static EwsPermissionLevel
get_permission_from_string (const gchar *permission)
{
	g_return_val_if_fail (permission != NULL, EwsPermissionLevel_Unknown);

	if (!g_ascii_strcasecmp (permission, "Editor"))
		return EwsPermissionLevel_Editor;
	else if (!g_ascii_strcasecmp (permission, "Author"))
		return EwsPermissionLevel_Author;
	else if (!g_ascii_strcasecmp (permission, "Reviewer"))
		return EwsPermissionLevel_Reviewer;
	else if (!g_ascii_strcasecmp (permission, "Custom"))
		return EwsPermissionLevel_Custom;
	else
		return EwsPermissionLevel_None;

}

static void
ews_handle_delegate_user_param (ESoapParameter *param,
				GSList **out_delegates)
{
	ESoapParameter *subparam, *node, *child;
	EwsDelegateInfo *data;
	gchar *value;

	node = e_soap_parameter_get_first_child_by_name (param, "DelegateUser");
	if (!node)
		return;

	subparam = e_soap_parameter_get_first_child_by_name (node, "UserId");
	if (!subparam)
		return;

	data = g_new0 (EwsDelegateInfo, 1);
	data->user_id = g_new0 (EwsUserId, 1);

	/*Parse User Id*/

	child = e_soap_parameter_get_first_child_by_name (subparam, "SID");
	data->user_id->sid = e_soap_parameter_get_string_value (child);

	child = e_soap_parameter_get_first_child_by_name (subparam, "PrimarySmtpAddress");
	data->user_id->primary_smtp = e_soap_parameter_get_string_value (child);

	child = e_soap_parameter_get_first_child_by_name (subparam, "DisplayName");
	data->user_id->display_name = e_soap_parameter_get_string_value (child);

	subparam = e_soap_parameter_get_first_child_by_name (node, "DelegatePermissions");

	/*Parse Delegate Permissions*/
	child = e_soap_parameter_get_first_child_by_name (subparam, "CalendarFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->calendar = get_permission_from_string (value);
		g_free (value);
	}

	child = e_soap_parameter_get_first_child_by_name (subparam, "ContactsFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->contacts = get_permission_from_string (value);
		g_free (value);
	}

	child = e_soap_parameter_get_first_child_by_name (subparam, "InboxFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->inbox = get_permission_from_string (value);
		g_free (value);
	}

	child = e_soap_parameter_get_first_child_by_name (subparam, "TasksFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->tasks = get_permission_from_string (value);
		g_free (value);
	}

	child = e_soap_parameter_get_first_child_by_name (subparam, "NotesFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->notes = get_permission_from_string (value);
		g_free (value);
	}

	child = e_soap_parameter_get_first_child_by_name (subparam, "JournalFolderPermissionLevel");
	if (child) {
		value = e_soap_parameter_get_string_value (child);
		data->journal = get_permission_from_string (value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "ReceiveCopiesOfMeetingMessages");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		data->meetingcopies = g_strcmp0 (value, "true") == 0;
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "ViewPrivateItems");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		data->view_priv_items = g_strcmp0 (value, "true") == 0;
		g_free (value);
	}

	*out_delegates = g_slist_prepend (*out_delegates, data);
}

static gboolean
e_ews_process_get_delegate_response (EEwsConnection *cnc,
				     ESoapResponse *response,
				     EwsDelegateDeliver *out_deliver_to,
				     GSList **out_delegates,
				     GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	if (ews_get_response_status (e_soap_response_get_parameter (response), &local_error))
		param = e_soap_response_get_first_parameter_by_name (response, "DeliverMeetingRequests", &local_error);
	else
		param = NULL;

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (out_deliver_to) {
		gchar *value;

		value = e_soap_parameter_get_string_value (param);

		if (g_strcmp0 (value, "DelegatesOnly") == 0)
			*out_deliver_to = EwsDelegateDeliver_DelegatesOnly;
		else if (g_strcmp0 (value, "DelegatesAndMe") == 0)
			*out_deliver_to = EwsDelegateDeliver_DelegatesAndMe;
		else if (g_strcmp0 (value, "DelegatesAndSendInformationToMe") == 0)
			*out_deliver_to = EwsDelegateDeliver_DelegatesAndSendInformationToMe;
		else {
			g_message ("%s: Unknown deliver-to value '%s'", G_STRFUNC, value ? value : "[null]");
			*out_deliver_to = EwsDelegateDeliver_DelegatesAndSendInformationToMe;
		}

		g_free (value);
	}

	param = e_soap_response_get_first_parameter_by_name (
		response, "ResponseMessages", NULL);

	/* it's OK to not have set any delegate */
	if (!param)
		return TRUE;

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "DelegateUserResponseMessageType")) {
			ews_handle_delegate_user_param (subparam, out_delegates);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_get_delegate_sync (EEwsConnection *cnc,
                                    gint pri,
                                    const gchar *mail_id,
                                    gboolean include_permissions,
                                    EwsDelegateDeliver *out_deliver_to,
                                    GSList **out_delegates, /* EwsDelegateInfo * */
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_deliver_to != NULL, FALSE);
	g_return_val_if_fail (out_delegates != NULL, FALSE);

	*out_delegates = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetDelegate",
		"IncludePermissions",
		include_permissions ? "true" : "false",
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", "messages", NULL);

	e_ews_request_write_string_parameter (request, "EmailAddress", NULL, mail_id ? mail_id : cnc->priv->email);

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_delegate_response (cnc, response, out_deliver_to, out_delegates, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success) {
		*out_delegates = g_slist_reverse (*out_delegates);
	} else {
		g_slist_free_full (*out_delegates, (GDestroyNotify) ews_delegate_info_free);
		*out_delegates = NULL;
	}

	return success;
}

static gboolean
e_ews_process_update_delegate_response (EEwsConnection *cnc,
					ESoapResponse *response,
					GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	if (ews_get_response_status (e_soap_response_get_parameter (response), &local_error)) {
		param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", NULL);
		/* that's OK to not receive any ResponseMessages here */
		if (!param)
			return TRUE;
	} else {
		param = NULL;
	}

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		if (!ews_get_response_status (subparam, error))
			return FALSE;

		subparam = e_soap_parameter_get_next_child (param);
	}

	return TRUE;
}

static void
set_delegate_permission (ESoapRequest *request,
                         const gchar *elem_name,
                         EwsPermissionLevel perm_level)
{
	const gchar *level_name = NULL;

	if (perm_level == EwsPermissionLevel_None)
		level_name = "None";
	else if (perm_level == EwsPermissionLevel_Reviewer)
		level_name = "Reviewer";
	else if (perm_level == EwsPermissionLevel_Author)
		level_name = "Author";
	else if (perm_level == EwsPermissionLevel_Editor)
		level_name = "Editor";

	if (!level_name)
		return;

	e_ews_request_write_string_parameter (request, elem_name, NULL, level_name);
}

gboolean
e_ews_connection_add_delegate_sync (EEwsConnection *cnc,
                                    gint pri,
                                    const gchar *mail_id,
                                    const GSList *delegates, /* EwsDelegateInfo * */
                                    GCancellable *cancellable,
                                    GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (delegates != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"AddDelegate",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", "messages", NULL);
	e_ews_request_write_string_parameter (request, "EmailAddress", NULL, mail_id ? mail_id : cnc->priv->email);
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "DelegateUsers", "messages", NULL);
	for (link = delegates; link; link = g_slist_next (link)) {
		const EwsDelegateInfo *di = link->data;

		if (!di)
			continue;

		e_soap_request_start_element (request, "DelegateUser", NULL, NULL);

		e_soap_request_start_element (request, "UserId", NULL, NULL);
		e_ews_request_write_string_parameter (request, "PrimarySmtpAddress", NULL, di->user_id->primary_smtp);
		e_soap_request_end_element (request); /* UserId */

		e_soap_request_start_element (request, "DelegatePermissions", NULL, NULL);
		set_delegate_permission (request, "CalendarFolderPermissionLevel", di->calendar);
		set_delegate_permission (request, "TasksFolderPermissionLevel", di->tasks);
		set_delegate_permission (request, "InboxFolderPermissionLevel", di->inbox);
		set_delegate_permission (request, "ContactsFolderPermissionLevel", di->contacts);
		set_delegate_permission (request, "NotesFolderPermissionLevel", di->notes);
		set_delegate_permission (request, "JournalFolderPermissionLevel", di->journal);
		e_soap_request_end_element (request); /* DelegatePermissions */

		e_ews_request_write_string_parameter (request, "ReceiveCopiesOfMeetingMessages", NULL, di->meetingcopies ? "true" : "false");
		e_ews_request_write_string_parameter (request, "ViewPrivateItems", NULL, di->view_priv_items ? "true" : "false");

		e_soap_request_end_element (request); /* DelegateUser */
	}

	e_soap_request_end_element (request); /* DelegateUsers */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_update_delegate_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_remove_delegate_sync (EEwsConnection *cnc,
                                       gint pri,
                                       const gchar *mail_id,
                                       const GSList *delegate_ids, /* EwsUserId * */
                                       GCancellable *cancellable,
                                       GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (delegate_ids != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"RemoveDelegate",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", "messages", NULL);
	e_ews_request_write_string_parameter (request, "EmailAddress", NULL, mail_id ? mail_id : cnc->priv->email);
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "UserIds", "messages", NULL);

	for (link = delegate_ids; link; link = g_slist_next (link)) {
		const EwsUserId *user_id = link->data;

		if (!user_id)
			continue;

		e_soap_request_start_element (request, "UserId", NULL, NULL);
		e_ews_request_write_string_parameter (request, "PrimarySmtpAddress", NULL, user_id->primary_smtp);
		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request); /* UserIds */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_update_delegate_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_update_delegate_sync (EEwsConnection *cnc,
                                       gint pri,
                                       const gchar *mail_id,
                                       EwsDelegateDeliver deliver_to,
                                       const GSList *delegates, /* EwsDelegateInfo * */
                                       GCancellable *cancellable,
                                       GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"UpdateDelegate",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", "messages", NULL);
	e_ews_request_write_string_parameter (request, "EmailAddress", NULL, mail_id ? mail_id : cnc->priv->email);
	e_soap_request_end_element (request);

	if (delegates) {
		e_soap_request_start_element (request, "DelegateUsers", "messages", NULL);

		for (link = delegates; link; link = g_slist_next (link)) {
			const EwsDelegateInfo *di = link->data;

			if (!di)
				continue;

			e_soap_request_start_element (request, "DelegateUser", NULL, NULL);

			e_soap_request_start_element (request, "UserId", NULL, NULL);
			e_ews_request_write_string_parameter (request, "PrimarySmtpAddress", NULL, di->user_id->primary_smtp);
			e_soap_request_end_element (request); /* UserId */

			e_soap_request_start_element (request, "DelegatePermissions", NULL, NULL);
			set_delegate_permission (request, "CalendarFolderPermissionLevel", di->calendar);
			set_delegate_permission (request, "TasksFolderPermissionLevel", di->tasks);
			set_delegate_permission (request, "InboxFolderPermissionLevel", di->inbox);
			set_delegate_permission (request, "ContactsFolderPermissionLevel", di->contacts);
			set_delegate_permission (request, "NotesFolderPermissionLevel", di->notes);
			set_delegate_permission (request, "JournalFolderPermissionLevel", di->journal);
			e_soap_request_end_element (request); /* DelegatePermissions */

			e_ews_request_write_string_parameter (request, "ReceiveCopiesOfMeetingMessages", NULL, di->meetingcopies ? "true" : "false");
			e_ews_request_write_string_parameter (request, "ViewPrivateItems", NULL, di->view_priv_items ? "true" : "false");

			e_soap_request_end_element (request); /* DelegateUser */
		}

		e_soap_request_end_element (request); /* DelegateUsers */
	}

	e_ews_request_write_string_parameter (request, "DeliverMeetingRequests", "messages",
		deliver_to == EwsDelegateDeliver_DelegatesOnly ? "DelegatesOnly" :
		deliver_to == EwsDelegateDeliver_DelegatesAndMe ? "DelegatesAndMe" :
		"DelegatesAndSendInformationToMe");

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_update_delegate_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gboolean
e_ews_process_get_folder_permissions_response (EEwsConnection *cnc,
					       ESoapResponse *response,
					       GSList **out_permissions,
					       GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetFolderResponseMessage")) {
			ESoapParameter *node;

			node = e_soap_parameter_get_first_child_by_name (subparam, "Folders");
			if (node) {
				subparam = node;

				node = e_soap_parameter_get_first_child (subparam);
				if (node && node->name && g_str_has_suffix ((const gchar *) node->name, "Folder")) {
					node = e_soap_parameter_get_first_child_by_name (node, "PermissionSet");
					if (node) {
						*out_permissions = e_ews_permissions_from_soap_param (node);
					}
				}
			}

			break;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

/* free permissions with e_ews_permissions_free() */
gboolean
e_ews_connection_get_folder_permissions_sync (EEwsConnection *cnc,
                                              gint pri,
                                              EwsFolderId *folder_id,
                                              GSList **out_permissions,
                                              GCancellable *cancellable,
                                              GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (out_permissions != NULL, FALSE);

	*out_permissions = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, "IdOnly");
	e_soap_request_start_element (request, "AdditionalProperties", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:PermissionSet");
	e_soap_request_end_element (request); /* AdditionalProperties */
	e_soap_request_end_element (request); /* FolderShape */

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);
	e_ews_folder_id_append_to_request (request, cnc->priv->email, folder_id);
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_folder_permissions_response (cnc, response, out_permissions, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success) {
		e_ews_permissions_free (*out_permissions);
		*out_permissions = NULL;
	}

	return success;
}

gboolean
e_ews_connection_set_folder_permissions_sync (EEwsConnection *cnc,
                                              gint pri,
                                              EwsFolderId *folder_id,
                                              EEwsFolderType folder_type,
                                              const GSList *permissions,
                                              GCancellable *cancellable,
                                              GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (permissions != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"UpdateFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderChanges", "messages", NULL);
	e_ews_request_start_folder_change (request, cnc->priv->email, folder_id);

	e_soap_request_start_element (request, "SetFolderField", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:PermissionSet");

	switch (folder_type) {
	default:
	case E_EWS_FOLDER_TYPE_MAILBOX:
		e_soap_request_start_element (request, "Folder", NULL, NULL);
		break;
	case E_EWS_FOLDER_TYPE_CALENDAR:
		e_soap_request_start_element (request, "CalendarFolder", NULL, NULL);
		break;
	case E_EWS_FOLDER_TYPE_CONTACTS:
		e_soap_request_start_element (request, "ContactsFolder", NULL, NULL);
		break;
	case E_EWS_FOLDER_TYPE_SEARCH:
		e_soap_request_start_element (request, "SearchFolder", NULL, NULL);
		break;
	case E_EWS_FOLDER_TYPE_TASKS:
		e_soap_request_start_element (request, "TasksFolder", NULL, NULL);
		break;
	}

	e_soap_request_start_element (request, "PermissionSet", NULL, NULL);
	if (folder_type == E_EWS_FOLDER_TYPE_CALENDAR)
		e_soap_request_start_element (request, "CalendarPermissions", NULL, NULL);
	else
		e_soap_request_start_element (request, "Permissions", NULL, NULL);

	for (link = permissions; link; link = g_slist_next (link)) {
		EEwsPermission *perm = link->data;
		const gchar *perm_level_name;

		if (!perm)
			continue;

		if (folder_type == E_EWS_FOLDER_TYPE_CALENDAR)
			e_soap_request_start_element (request, "CalendarPermission", NULL, NULL);
		else
			e_soap_request_start_element (request, "Permission", NULL, NULL);

		e_soap_request_start_element (request, "UserId", NULL, NULL);

		switch (perm->user_type) {
		case E_EWS_PERMISSION_USER_TYPE_NONE:
			g_warn_if_reached ();
			break;
		case E_EWS_PERMISSION_USER_TYPE_ANONYMOUS:
			e_ews_request_write_string_parameter (request, "DistinguishedUser", NULL, "Anonymous");
			break;
		case E_EWS_PERMISSION_USER_TYPE_DEFAULT:
			e_ews_request_write_string_parameter (request, "DistinguishedUser", NULL, "Default");
			break;
		case E_EWS_PERMISSION_USER_TYPE_REGULAR:
			e_ews_request_write_string_parameter (request, "PrimarySmtpAddress", NULL, perm->primary_smtp);
			break;
		}

		e_soap_request_end_element (request); /* UserId */

		e_ews_permission_rights_to_level_name (perm->rights);

		perm_level_name = e_ews_permission_rights_to_level_name (perm->rights);

		if (g_strcmp0 (perm_level_name, "Custom") == 0) {
			e_ews_request_write_string_parameter (request, "CanCreateItems", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_CREATE) != 0 ? "true" : "false");
			e_ews_request_write_string_parameter (request, "CanCreateSubFolders", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER) != 0 ? "true" : "false");
			e_ews_request_write_string_parameter (request, "IsFolderOwner", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_FOLDER_OWNER) != 0 ? "true" : "false");
			e_ews_request_write_string_parameter (request, "IsFolderVisible", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_FOLDER_VISIBLE) != 0 ? "true" : "false");
			e_ews_request_write_string_parameter (request, "IsFolderContact", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_FOLDER_CONTACT) != 0 ? "true" : "false");
			e_ews_request_write_string_parameter (request, "EditItems", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_EDIT_ANY) != 0 ? "All" :
				(perm->rights & E_EWS_PERMISSION_BIT_EDIT_OWNED) != 0 ? "Owned" : "None");
			e_ews_request_write_string_parameter (request, "DeleteItems", NULL,
				(perm->rights & E_EWS_PERMISSION_BIT_DELETE_ANY) != 0 ? "All" :
				(perm->rights & E_EWS_PERMISSION_BIT_DELETE_OWNED) != 0 ? "Owned" : "None");
			if (folder_type == E_EWS_FOLDER_TYPE_CALENDAR)
				e_ews_request_write_string_parameter (request, "ReadItems", NULL,
					(perm->rights & E_EWS_PERMISSION_BIT_READ_ANY) != 0 ? "FullDetails" :
					(perm->rights & E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED) != 0 ? "TimeAndSubjectAndLocation" :
					(perm->rights & E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE) != 0 ? "TimeOnly" : "None");
			else
				e_ews_request_write_string_parameter (request, "ReadItems", NULL,
					(perm->rights & E_EWS_PERMISSION_BIT_READ_ANY) != 0 ? "FullDetails" : "None");
		}

		e_ews_request_write_string_parameter (request,
			folder_type == E_EWS_FOLDER_TYPE_CALENDAR ? "CalendarPermissionLevel" : "PermissionLevel", NULL,
			perm_level_name);

		e_soap_request_end_element (request); /* Permission/CalendarPermission */
	}

	e_soap_request_end_element (request); /* Permissions */
	e_soap_request_end_element (request); /* PermissionSet */
	e_soap_request_end_element (request); /* Folder/CalendarFolder/... */
	e_soap_request_end_element (request); /* SetFolderField */

	e_ews_request_end_item_change (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gboolean
e_ews_process_get_password_expiration_response (EEwsConnection *cnc,
						ESoapResponse *response,
						gchar **out_exp_date,
						GError **error)
{
	ESoapParameter *param;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "PasswordExpirationDate", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (out_exp_date)
		*out_exp_date = e_soap_parameter_get_string_value (param);

	return TRUE;
}

gboolean
e_ews_connection_get_password_expiration_sync (EEwsConnection *cnc,
                                               gint pri,
                                               const gchar *mail_id,
                                               gchar **out_exp_date,
                                               GCancellable *cancellable,
                                               GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_exp_date != NULL, FALSE);

	*out_exp_date = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetPasswordExpirationDate",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010_SP2,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_ews_request_write_string_parameter (request, "MailboxSmtpAddress", NULL, mail_id ? mail_id : cnc->priv->email);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_password_expiration_response (cnc, response, out_exp_date, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success)
		g_clear_pointer (out_exp_date, g_free);

	return success;
}

static gboolean
e_ews_process_get_folder_info_response (EEwsConnection *cnc,
					ESoapResponse *response,
					EEwsFolder **out_folder,
					GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetFolderResponseMessage")) {
			ESoapParameter *node;

			node = e_soap_parameter_get_first_child_by_name (subparam, "Folders");
			if (node && out_folder)
				*out_folder = e_ews_folder_new_from_soap_parameter (node);

			break;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_get_folder_info_sync (EEwsConnection *cnc,
                                       gint pri,
                                       const gchar *mail_id,
                                       const EwsFolderId *folder_id,
                                       EEwsFolder **out_folder,
                                       GCancellable *cancellable,
                                       GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (out_folder != NULL, FALSE);

	*out_folder = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, "Default");
	e_soap_request_start_element (request, "AdditionalProperties", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:FolderClass");
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:ParentFolderId");
	e_soap_request_end_element (request); /* AdditionalProperties */
	e_soap_request_end_element (request); /* FolderShape */

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);
	e_ews_folder_id_append_to_request (request, mail_id, folder_id);
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_folder_info_response (cnc, response, out_folder, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success && !*out_folder)
		success = FALSE;

	if (!success && *out_folder)
		g_clear_object (out_folder);

	return success;
}

static gboolean
e_ews_process_find_folder_response (EEwsConnection *cnc,
				    ESoapResponse *response,
				    gboolean *out_includes_last_item,
				    GSList **out_folders,
				    GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "FindFolderResponseMessage")) {
			ESoapParameter *node, *subparam1;
			gchar *last;
			EEwsFolder *folder;
			gboolean includes_last_item;

			node = e_soap_parameter_get_first_child_by_name (subparam, "RootFolder");
			last = e_soap_parameter_get_property (node, "IncludesLastItemInRange");
			/*
			 * Set the includes_last_item to TRUE as default.
			 * It can avoid an infinite loop in caller, when, for some reason,
			 * we don't receive the last_tag property from the server.
			 */
			includes_last_item = g_strcmp0 (last, "false") != 0;
			g_free (last);

			node = e_soap_parameter_get_first_child_by_name (node, "Folders");
			for (subparam1 = e_soap_parameter_get_first_child (node);
			     subparam1 && out_folders;
			     subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				folder = e_ews_folder_new_from_soap_parameter (subparam1);
				if (folder)
					*out_folders = g_slist_prepend (*out_folders, folder);
			}

			if (out_includes_last_item)
				*out_includes_last_item = includes_last_item;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return TRUE;
}

gboolean
e_ews_connection_find_folder_sync (EEwsConnection *cnc,
				   gint pri,
				   const EwsFolderId *fid,
				   gboolean *out_includes_last_item,
				   GSList **out_folders,
				   GCancellable *cancellable,
				   GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_folders != NULL, FALSE);

	*out_folders = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"FindFolder",
		"Traversal",
		"Shallow",
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, "Default");
	e_soap_request_start_element (request, "AdditionalProperties", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:FolderClass");
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", "folder:ChildFolderCount");
	e_soap_request_end_element (request); /* AdditionalProperties */
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "ParentFolderIds", "messages", NULL);

	if (fid->is_distinguished_id)
		e_ews_request_write_string_parameter_with_attribute (request, "DistinguishedFolderId", NULL, NULL, "Id", fid->id);
	else
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", fid->id);

	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_find_folder_response (cnc, response, out_includes_last_item, out_folders, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (success) {
		*out_folders = g_slist_reverse (*out_folders);
	} else {
		g_slist_free_full (*out_folders, g_object_unref);
		*out_folders = NULL;
	}

	return success;
}

typedef struct _AuthMethodsData {
	GCancellable *cancellable;
	GSList **out_auth_methods;
} AuthMethodsData;

static void
e_ews_auth_methods_got_headers_cb (SoupMessage *message,
				   gpointer user_data)
{
	AuthMethodsData *amd = user_data;
	const gchar *auths_lst;
	gboolean has_bearer = FALSE;
	gchar **auths;
	gint ii;

	g_return_if_fail (amd != NULL);

	auths_lst = soup_message_headers_get_list (soup_message_get_response_headers (message), "WWW-Authenticate");

	if (auths_lst) {
		auths = g_strsplit (auths_lst, ",", -1);

		for (ii = 0; auths && auths[ii]; ii++) {
			gchar *auth, *space;

			auth = g_strstrip (g_strdup (auths[ii]));

			if (auth && *auth) {
				space = strchr (auth, ' ');
				if (space)
					*space = '\0';

				has_bearer = has_bearer || g_ascii_strcasecmp (auth, "Bearer") == 0;
				*(amd->out_auth_methods) = g_slist_prepend (*(amd->out_auth_methods), auth);
			} else {
				g_free (auth);
			}
		}

		g_strfreev (auths);

		if (!has_bearer) {
			/* Special-case Office365 OAuth2, because outlook.office365.com doesn't advertise Bearer */
			*(amd->out_auth_methods) = g_slist_prepend (*(amd->out_auth_methods), g_strdup ("Bearer"));
		}
	}

	g_cancellable_cancel (amd->cancellable);
}

gboolean
e_ews_connection_query_auth_methods_sync (EEwsConnection *cnc,
					  gint pri,
					  GSList **out_auth_methods,
					  GCancellable *cancellable,
					  GError **error)
{
	AuthMethodsData amd;
	CamelEwsSettings *settings;
	ESoapRequest *request;
	ESoupSession *session;
	SoupMessage *message;
	GInputStream *stream;
	gulong cancelled_id = 0;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_auth_methods != NULL, FALSE);

	*out_auth_methods = NULL;

	/* use some simple operation to get WWW-Authenticate headers from the server */
	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetFolder",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		TRUE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "FolderShape", "messages", NULL);
	e_ews_request_write_string_parameter (request, "BaseShape", NULL, "IdOnly");
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "FolderIds", "messages", NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "DistinguishedFolderId", NULL, NULL, "Id", "inbox");
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	session = e_ews_connection_create_soup_session (cnc);

	settings = e_ews_connection_ref_settings (cnc);
	message = e_soap_request_persist (request, session, settings, error);
	g_clear_object (&settings);

	if (!message) {
		g_clear_object (&session);
		g_clear_object (&request);
		return FALSE;
	}

	e_ews_connection_maybe_prepare_message_for_testing_sources (cnc, message);

	amd.cancellable = g_cancellable_new ();
	amd.out_auth_methods = out_auth_methods;

	g_signal_connect (message, "got-headers",
		G_CALLBACK (e_ews_auth_methods_got_headers_cb), &amd);

	if (cancellable)
		cancelled_id = g_cancellable_connect (cancellable, G_CALLBACK (e_ews_cancel_cancellable_cb), amd.cancellable, NULL);

	stream = e_soup_session_send_message_sync (session, message, amd.cancellable, &local_error);

	/* The request should be cancelled in the got-headers handler */
	g_clear_object (&stream);

	if (local_error && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		success = FALSE;
		g_propagate_error (error, local_error);
		g_slist_free_full (*out_auth_methods, g_free);
		*out_auth_methods = NULL;
	} else {
		success = TRUE;
		*out_auth_methods = g_slist_reverse (*out_auth_methods);
	}

	if (cancelled_id)
		g_cancellable_disconnect (cancellable, cancelled_id);

	g_clear_object (&amd.cancellable);
	g_clear_object (&message);
	g_clear_object (&session);
	g_clear_object (&request);

	if (success && g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_slist_free_full (*out_auth_methods, g_free);
		*out_auth_methods = NULL;
		success = FALSE;
	}

	return success;
}

static void
ews_connection_build_subscribed_folders_list (gpointer key,
					      gpointer value,
					      gpointer user_data)
{
	GSList *folders = value, *l;
	EEwsConnection *cnc = user_data;

	for (l = folders; l != NULL; l = l->next) {
		if (g_slist_find_custom (cnc->priv->subscribed_folders, l->data, (GCompareFunc) g_strcmp0) == NULL) {
			cnc->priv->subscribed_folders =
				g_slist_prepend (cnc->priv->subscribed_folders, g_strdup (l->data));
		}
	}
}

static void
ews_connection_subscription_id_changed_cb (EEwsNotification *notification,
					   const gchar *subscription_id,
					   gpointer user_data)
{
	EEwsConnection *cnc = user_data;

	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	NOTIFICATION_LOCK (cnc);

	if (cnc->priv->notification == notification)
		g_signal_emit (cnc, signals[SUBSCRIPTION_ID_CHANGED], 0, subscription_id, NULL);

	NOTIFICATION_UNLOCK (cnc);
}

static gpointer
ews_connection_notification_start_thread (gpointer user_data)
{
	GWeakRef *weakref = user_data;
	EEwsConnection *cnc;

	g_return_val_if_fail (weakref != NULL, NULL);

	cnc = g_weak_ref_get (weakref);

	if (cnc && !e_ews_connection_get_disconnected_flag (cnc)) {
		gchar *last_subscription_id = e_ews_connection_dup_last_subscription_id (cnc);

		NOTIFICATION_LOCK (cnc);

		if (cnc->priv->subscribed_folders) {
			g_clear_object (&cnc->priv->notification);

			cnc->priv->notification = e_ews_notification_new (cnc, last_subscription_id);

			/* The 'notification' assumes ownership of the 'last_subscription_id' */
			last_subscription_id = NULL;

			g_signal_connect_object (cnc->priv->notification, "subscription-id-changed",
				G_CALLBACK (ews_connection_subscription_id_changed_cb), cnc, 0);

			e_ews_notification_start_listening_sync (cnc->priv->notification, cnc->priv->subscribed_folders);
		}

		NOTIFICATION_UNLOCK (cnc);

		g_free (last_subscription_id);
	}

	g_clear_object (&cnc);
	e_weak_ref_free (weakref);

	return NULL;
}

static gboolean
ews_connection_notification_delay_cb (gpointer user_data)
{
	GWeakRef *weakref = user_data;
	EEwsConnection *cnc;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (weakref != NULL, FALSE);

	cnc = g_weak_ref_get (weakref);

	if (cnc) {
		NOTIFICATION_LOCK (cnc);

		if (cnc->priv->notification_delay_id == g_source_get_id (g_main_current_source ())) {
			cnc->priv->notification_delay_id = 0;

			if (cnc->priv->subscribed_folders) {
				g_thread_unref (g_thread_new (NULL, ews_connection_notification_start_thread,
					e_weak_ref_new (cnc)));
			}
		}

		NOTIFICATION_UNLOCK (cnc);

		g_object_unref (cnc);
	}

	return FALSE;
}

static void
e_ews_connection_maybe_start_notifications_locked (EEwsConnection *cnc)
{
	if (camel_ews_settings_get_listen_notifications (cnc->priv->settings)) {
		e_ews_debug_print ("Start notifications for cnc:%p\n", cnc);

		if (cnc->priv->notification_delay_id)
			g_source_remove (cnc->priv->notification_delay_id);

		cnc->priv->notification_delay_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, 5,
			ews_connection_notification_delay_cb, e_weak_ref_new (cnc), (GDestroyNotify) e_weak_ref_free);
	}
}

/*
 * Enables server notification on a folder (or a set of folders).
 * The events we are listen for notifications are: Copied, Created, Deleted, Modified and Moved.
 *
 * As we have only one subscription per connection, for every enable_notifications_sync() call,
 * we do:
 * - Check if we already are subscribed
 * - If we are already subscribed:
 *  - Stop to send events regards to the already subscribed folders' list
 *  - Unsubscribe the already subscribed folders' list
 * - Add the user folders' lst to the hash table if subscribed folders
 * - Create a new list of the folders to subscribe for, based in the hash table of subscribed folders
 * - Subscribed to listen notifications for the folders
 * - Start to send events regards to the newly subscribed folders' list
 *
 * Pair function for this one is e_ews_connection_disable_notifications_sync() and we do
 * something really similar for every disable_notifications_sync() call.
 *
 * The notification is received to the caller with the "server-notification" signal.
 * Note that the signal is used for each notification, without distinction on the
 * enabled object.
 */
void
e_ews_connection_enable_notifications_sync (EEwsConnection *cnc,
					    GSList *folders,
					    guint *subscription_key)
{
	GSList *new_folders = NULL, *l, *flink;
	gint subscriptions_size;

	g_return_if_fail (cnc != NULL);
	g_return_if_fail (cnc->priv != NULL);
	g_return_if_fail (cnc->priv->version >= E_EWS_EXCHANGE_2010_SP1);
	g_return_if_fail (folders != NULL);

	NOTIFICATION_LOCK (cnc);
	subscriptions_size = g_hash_table_size (cnc->priv->subscriptions);

	if (subscriptions_size == G_MAXUINT - 1)
		goto exit;

	for (flink = folders; flink; flink = g_slist_next (flink)) {
		for (l = cnc->priv->subscribed_folders; l; l = g_slist_next (l)) {
			if (g_strcmp0 (l->data, flink->data) == 0)
				break;
		}

		if (!l)
			break;
	}

	/* All requested folders are already subscribed */
	if (!flink && cnc->priv->notification)
		goto exit;

	if (subscriptions_size > 0) {
		if (cnc->priv->notification) {
			e_ews_notification_stop_listening_sync (cnc->priv->notification);
			g_clear_object (&cnc->priv->notification);
		}

		g_slist_free_full (cnc->priv->subscribed_folders, g_free);
		cnc->priv->subscribed_folders = NULL;
	}

	while (g_hash_table_contains (cnc->priv->subscriptions, GINT_TO_POINTER (notification_key))) {
		notification_key++;
		if (notification_key == 0)
			notification_key++;
	}

	for (l = folders; l != NULL; l = l->next)
		new_folders = g_slist_prepend (new_folders, g_strdup (l->data));

	g_hash_table_insert (cnc->priv->subscriptions, GINT_TO_POINTER (notification_key), new_folders);
	new_folders = NULL;

	g_hash_table_foreach (cnc->priv->subscriptions, ews_connection_build_subscribed_folders_list, cnc);

	e_ews_connection_maybe_start_notifications_locked (cnc);

exit:
	*subscription_key = notification_key;
	notification_key++;
	if (notification_key == 0)
		notification_key++;

	NOTIFICATION_UNLOCK (cnc);
}

void
e_ews_connection_disable_notifications_sync (EEwsConnection *cnc,
					     guint subscription_key)
{
	g_return_if_fail (cnc != NULL);
	g_return_if_fail (cnc->priv != NULL);

	NOTIFICATION_LOCK (cnc);
	if (cnc->priv->notification == NULL)
		goto exit;

	if (!g_hash_table_remove (cnc->priv->subscriptions, GINT_TO_POINTER (subscription_key)))
		goto exit;

	e_ews_notification_stop_listening_sync (cnc->priv->notification);

	g_slist_free_full (cnc->priv->subscribed_folders, g_free);
	cnc->priv->subscribed_folders = NULL;

	g_hash_table_foreach (cnc->priv->subscriptions, ews_connection_build_subscribed_folders_list, cnc);
	if (cnc->priv->subscribed_folders != NULL && !e_ews_connection_get_disconnected_flag (cnc)) {
		e_ews_connection_maybe_start_notifications_locked (cnc);
	} else {
		g_clear_object (&cnc->priv->notification);
	}

exit:
	NOTIFICATION_UNLOCK (cnc);
}

static EEwsCalendarTo *
ews_get_to (ESoapParameter *node)
{
	EEwsCalendarTo *to = NULL;
	ESoapParameter *param;
	gchar *kind = NULL;
	gchar *value = NULL;
	gboolean success = FALSE;

	param = e_soap_parameter_get_first_child_by_name (node, "To");
	if (param == NULL)
		goto exit;

	kind = e_soap_parameter_get_property (param, "Kind");
	if (kind == NULL)
		goto exit;

	value = e_soap_parameter_get_string_value (param);
	if (value == NULL)
		goto exit;

	success = TRUE;

 exit:
	if (success) {
		to = e_ews_calendar_to_new ();
		to->kind = kind;
		to->value = value;
	} else {
		g_free (kind);
		g_free (value);
	}

	return to;
}

static EEwsCalendarPeriod *
ews_get_period (ESoapParameter *node)
{
	EEwsCalendarPeriod *period = NULL;
	gchar *bias = NULL;
	gchar *name = NULL;
	gchar *id = NULL;

	bias = e_soap_parameter_get_property (node, "Bias");
	name = e_soap_parameter_get_property (node, "Name");
	id = e_soap_parameter_get_property (node, "Id");

	if (bias == NULL || name == NULL || id == NULL) {
		g_free (bias);
		g_free (name);
		g_free (id);

		return NULL;
	}

	period = e_ews_calendar_period_new ();
	period->bias = bias;
	period->name = name;
	period->id = id;

	return period;
}

static GSList * /* EEwsCalendarPeriod * */
ews_get_periods_list (ESoapParameter *node)
{
	ESoapParameter *param;
	GSList *periods = NULL;

	for (param = e_soap_parameter_get_first_child_by_name (node, "Period");
	     param != NULL;
	     param = e_soap_parameter_get_next_child_by_name (param, "Period")) {
		EEwsCalendarPeriod *period;

		period = ews_get_period (param);
		if (period != NULL) {
			periods = g_slist_prepend (periods, period);
		} else {
			g_slist_free_full (periods, (GDestroyNotify) e_ews_calendar_period_free);
			return NULL;
		}
	}

	periods = g_slist_reverse (periods);
	return periods;
}

static EEwsCalendarAbsoluteDateTransition *
ews_get_absolute_date_transition (ESoapParameter *node)
{
	ESoapParameter *param;
	EEwsCalendarAbsoluteDateTransition *absolute_date_transition = NULL;
	EEwsCalendarTo *to = NULL;
	gchar *date_time = NULL;
	gboolean success = FALSE;

	param = e_soap_parameter_get_first_child_by_name (node, "To");
	if (param != NULL)
		to = ews_get_to (param);

	if (to == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "DateTime");
	if (param != NULL)
		date_time = e_soap_parameter_get_string_value (param);

	if (date_time == NULL)
		goto exit;

	success = TRUE;

 exit:
	if (success) {
		absolute_date_transition = e_ews_calendar_absolute_date_transition_new ();
		absolute_date_transition->to = to;
		absolute_date_transition->date_time = date_time;
	} else {
		e_ews_calendar_to_free (to);
		g_free (date_time);
	}

	return absolute_date_transition;
}

static EEwsCalendarRecurringDateTransition *
ews_get_recurring_date_transition (ESoapParameter *node)
{
	ESoapParameter *param;
	EEwsCalendarRecurringDateTransition *recurring_date_transition = NULL;
	EEwsCalendarTo *to = NULL;
	gchar *time_offset = NULL;
	gchar *month = NULL;
	gchar *day = NULL;
	gboolean success = FALSE;

	to = ews_get_to (node);
	if (to == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "TimeOffset");
	if (param != NULL)
		time_offset = e_soap_parameter_get_string_value (param);

	if (time_offset == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Month");
	if (param != NULL)
		month = e_soap_parameter_get_string_value (param);

	if (month == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Day");
	if (param != NULL)
		day = e_soap_parameter_get_string_value (param);

	if (day == NULL)
		goto exit;

	success = TRUE;

 exit:
	if (success) {
		recurring_date_transition = e_ews_calendar_recurring_date_transition_new ();
		recurring_date_transition->to = to;
		recurring_date_transition->time_offset = time_offset;
		recurring_date_transition->month = month;
		recurring_date_transition->day = day;
	} else {
		e_ews_calendar_to_free (to);
		g_free (time_offset);
		g_free (month);
		g_free (day);
	}

	return recurring_date_transition;
}

static EEwsCalendarRecurringDayTransition *
ews_get_recurring_day_transition (ESoapParameter *node)
{
	ESoapParameter *param;
	EEwsCalendarRecurringDayTransition *recurring_day_transition = NULL;
	EEwsCalendarTo *to = NULL;
	gchar *time_offset = NULL;
	gchar *month = NULL;
	gchar *day_of_week = NULL;
	gchar *occurrence = NULL;
	gboolean success = FALSE;

	to = ews_get_to (node);
	if (to == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "TimeOffset");
	if (param != NULL)
		time_offset = e_soap_parameter_get_string_value (param);

	if (time_offset == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Month");
	if (param != NULL)
		month = e_soap_parameter_get_string_value (param);

	if (month == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "DayOfWeek");
	if (param != NULL)
		day_of_week = e_soap_parameter_get_string_value (param);

	if (day_of_week == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Occurrence");
	if (param != NULL)
		occurrence = e_soap_parameter_get_string_value (param);

	if (occurrence == NULL)
		goto exit;

	success = TRUE;

 exit:
	if (success) {
		recurring_day_transition = e_ews_calendar_recurring_day_transition_new ();
		recurring_day_transition->to = to;
		recurring_day_transition->time_offset = time_offset;
		recurring_day_transition->month = month;
		recurring_day_transition->day_of_week = day_of_week;
		recurring_day_transition->occurrence = occurrence;
	} else {
		e_ews_calendar_to_free (to);
		g_free (time_offset);
		g_free (month);
		g_free (day_of_week);
		g_free (occurrence);
	}

	return recurring_day_transition;
}

static GSList * /* EEwsCalendarAbsoluteDateTransition * */
ews_get_absolute_date_transitions_list (ESoapParameter *node)
{
	ESoapParameter *param;
	GSList *absolute_date_transitions = NULL;

	for (param = e_soap_parameter_get_first_child_by_name (node, "AbsoluteDateTransition");
	     param != NULL;
	     param = e_soap_parameter_get_next_child_by_name (param, "AbsoluteDateTransition")) {
		EEwsCalendarAbsoluteDateTransition *absolute_date_transition;

		absolute_date_transition = ews_get_absolute_date_transition (param);
		if (absolute_date_transition != NULL) {
			absolute_date_transitions =
				g_slist_prepend (absolute_date_transitions, absolute_date_transition);
		} else {
			g_slist_free_full (
				absolute_date_transitions,
				(GDestroyNotify) e_ews_calendar_absolute_date_transition_free);
			return NULL;
		}
	}

	absolute_date_transitions = g_slist_reverse (absolute_date_transitions);
	return absolute_date_transitions;
}

static GSList * /* EEwsCalendarRecurringDayTransition * */
ews_get_recurring_day_transitions_list (ESoapParameter *node)
{
	ESoapParameter *param;
	GSList *recurring_day_transitions = NULL;

	for (param = e_soap_parameter_get_first_child_by_name (node, "RecurringDayTransition");
	     param != NULL;
	     param = e_soap_parameter_get_next_child_by_name (param, "RecurringDayTransition")) {
		EEwsCalendarRecurringDayTransition *recurring_day_transition;

		recurring_day_transition = ews_get_recurring_day_transition (param);
		if (recurring_day_transition != NULL) {
			recurring_day_transitions =
				g_slist_prepend (recurring_day_transitions, recurring_day_transition);
		} else {
			g_slist_free_full (
				recurring_day_transitions,
				(GDestroyNotify) e_ews_calendar_recurring_day_transition_free);
			return NULL;
		}
	}

	recurring_day_transitions = g_slist_reverse (recurring_day_transitions);
	return recurring_day_transitions;
}

static GSList * /* EEwsCalendarRecurringDateTransition * */
ews_get_recurring_date_transitions_list (ESoapParameter *node)
{
	ESoapParameter *param;
	GSList *recurring_date_transitions = NULL;

	for (param = e_soap_parameter_get_first_child_by_name (node, "RecurringDateTransition");
	     param != NULL;
	     param = e_soap_parameter_get_next_child_by_name (param, "RecurringDateTransition")) {
		EEwsCalendarRecurringDateTransition *recurring_date_transition;

		recurring_date_transition = ews_get_recurring_date_transition (param);
		if (recurring_date_transition != NULL) {
			recurring_date_transitions =
				g_slist_prepend (recurring_date_transitions, recurring_date_transition);
		} else {
			g_slist_free_full (
				recurring_date_transitions,
				(GDestroyNotify) e_ews_calendar_recurring_date_transition_free);
			return NULL;
		}
	}

	recurring_date_transitions = g_slist_reverse (recurring_date_transitions);
	return recurring_date_transitions;
}

static EEwsCalendarTransitionsGroup *
ews_get_transitions_group (ESoapParameter *node)
{
	EEwsCalendarTransitionsGroup *tg = NULL;
	EEwsCalendarTo *transition = NULL;
	ESoapParameter *param = NULL;
	gchar *id = NULL;
	GSList *absolute_date_transitions = NULL;
	GSList *recurring_date_transitions = NULL;
	GSList *recurring_day_transitions = NULL;

	id = e_soap_parameter_get_property (node, "Id");
	if (id == NULL)
		return NULL;

	param = e_soap_parameter_get_first_child_by_name (node, "Transition");
	if (param != NULL)
		transition = ews_get_to (param);

	absolute_date_transitions = ews_get_absolute_date_transitions_list (node);
	recurring_date_transitions = ews_get_recurring_date_transitions_list (node);
	recurring_day_transitions = ews_get_recurring_day_transitions_list (node);

	tg = e_ews_calendar_transitions_group_new ();
	tg->id = id;
	tg->transition = transition;
	tg->absolute_date_transitions = absolute_date_transitions;
	tg->recurring_date_transitions = recurring_date_transitions;
	tg->recurring_day_transitions = recurring_day_transitions;

	return tg;
}

static GSList * /* EEwsCalendarTransitionsGroup * */
ews_get_transitions_groups_list (ESoapParameter *node)
{
	ESoapParameter *param;
	GSList *transitions_groups = NULL;

	for (param = e_soap_parameter_get_first_child_by_name (node, "TransitionsGroup");
	     param != NULL;
	     param = e_soap_parameter_get_next_child_by_name (param, "TransitionsGroup")) {
		EEwsCalendarTransitionsGroup *tg;

		tg = ews_get_transitions_group (param);
		if (tg != NULL) {
			transitions_groups = g_slist_prepend (transitions_groups, tg);
		} else {
			g_slist_free_full (transitions_groups, (GDestroyNotify) e_ews_calendar_transitions_group_free);
			return NULL;
		}
	}

	transitions_groups = g_slist_reverse (transitions_groups);
	return transitions_groups;
}

static EEwsCalendarTransitions *
ews_get_transitions (ESoapParameter *node)
{
	ESoapParameter *param;
	EEwsCalendarTransitions *transitions = NULL;
	EEwsCalendarTo *transition = NULL;
	GSList *absolute_date_transitions = NULL;
	GSList *recurring_date_transitions = NULL;
	GSList *recurring_day_transitions = NULL;

	param = e_soap_parameter_get_first_child_by_name (node, "Transition");
	if (param != NULL)
		transition = ews_get_to (param);

	if (transition == NULL)
		return NULL;

	absolute_date_transitions = ews_get_absolute_date_transitions_list (node);
	recurring_day_transitions = ews_get_recurring_day_transitions_list (node);
	recurring_date_transitions = ews_get_recurring_date_transitions_list (node);

	transitions = e_ews_calendar_transitions_new ();
	transitions->transition = transition;
	transitions->absolute_date_transitions = absolute_date_transitions;
	transitions->recurring_day_transitions = recurring_day_transitions;
	transitions->recurring_date_transitions = recurring_date_transitions;

	return transitions;
}

static EEwsCalendarTimeZoneDefinition *
ews_get_time_zone_definition (ESoapParameter *node)
{
	ESoapParameter *param;
	gchar *name = NULL;
	gchar *id = NULL;
	GSList *periods = NULL;
	GSList *transitions_groups = NULL;
	EEwsCalendarTransitions *transitions = NULL;
	EEwsCalendarTimeZoneDefinition *tzd = NULL;
	gboolean success = FALSE;

	name = e_soap_parameter_get_property (node, "Name");
	if (name == NULL)
		goto exit;

	id = e_soap_parameter_get_property (node, "Id");
	if (id == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Periods");
	if (param != NULL)
		periods = ews_get_periods_list (param);
	if (periods == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "TransitionsGroups");
	if (param != NULL)
		transitions_groups = ews_get_transitions_groups_list (param);
	if (transitions_groups == NULL)
		goto exit;

	param = e_soap_parameter_get_first_child_by_name (node, "Transitions");
	if (param != NULL)
		transitions = ews_get_transitions (param);
	if (transitions == NULL)
		goto exit;

	success = TRUE;

 exit:
	if (success) {
		tzd = e_ews_calendar_time_zone_definition_new ();
		tzd->name = name;
		tzd->id = id;
		tzd->periods = periods;
		tzd->transitions_groups = transitions_groups;
		tzd->transitions = transitions;
	} else {
		g_free (name);
		g_free (id);
		g_slist_free_full (periods, (GDestroyNotify) e_ews_calendar_period_free);
		g_slist_free_full (transitions_groups, (GDestroyNotify) e_ews_calendar_transitions_group_free);
		e_ews_calendar_transitions_free (transitions);
	}

	return tzd;
}

static gboolean
e_ews_process_get_server_time_zones_response (EEwsConnection *cnc,
					      ESoapResponse *response,
					      GSList **out_tzds,
					      GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetServerTimeZonesResponseMessage") && out_tzds) {
			ESoapParameter *node, *node2;

			node = e_soap_parameter_get_first_child_by_name (subparam, "TimeZoneDefinitions");
			if (node != NULL) {
				node2 = e_soap_parameter_get_first_child_by_name (node, "TimeZoneDefinition");
				if (node2 != NULL) {
					EEwsCalendarTimeZoneDefinition *tzd;

					tzd = ews_get_time_zone_definition (node2);
					if (tzd != NULL)
						*out_tzds = g_slist_prepend (*out_tzds, tzd);
				}
			}
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	if (out_tzds)
		*out_tzds = g_slist_reverse (*out_tzds);

	return TRUE;
}

gboolean
e_ews_connection_get_server_time_zones_sync (EEwsConnection *cnc,
					     gint pri,
					     const GSList *msdn_locations,
					     GSList **out_tzds, /* EEwsCalendarTimeZoneDefinition */
					     GCancellable *cancellable,
					     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_tzds != NULL, FALSE);

	*out_tzds = NULL;

	/*
	 * EWS server version earlier than 2010 doesn't have support to "GetServerTimeZones".
	 * So, if the API is called with an older Exchange's version, let's just "fail" silently.
	 */
	if (!e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2010_SP1)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Requires at least Microsoft Exchange 2010 SP1 server"));
		return FALSE;
	}

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetServerTimeZones",
		"ReturnFullTimeZoneData",
		"true",
		cnc->priv->version,
		E_EWS_EXCHANGE_2010,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Ids", "messages", NULL);
	for (link = msdn_locations; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "Id", NULL, link->data, NULL, NULL);
	}
	e_soap_request_end_element (request); /* Ids */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_server_time_zones_response (cnc, response, out_tzds, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success) {
		g_slist_free_full (*out_tzds, (GDestroyNotify) e_ews_calendar_time_zone_definition_free);
		*out_tzds = NULL;
	}

	if (!*out_tzds)
		success = FALSE;

	return success;
}

static gboolean
e_ews_process_get_user_photo_response (EEwsConnection *cnc,
				       ESoapResponse *response,
				       gchar **out_picture_data,
				       GError **error)
{
	ESoapParameter *param;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "PictureData", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	*out_picture_data = e_soap_parameter_get_string_value (param);

	if (*out_picture_data && !**out_picture_data)
		g_clear_pointer (out_picture_data, g_free);

	return TRUE;
}

gboolean
e_ews_connection_get_user_photo_sync (EEwsConnection *cnc,
				      gint pri,
				      const gchar *email,
				      EEwsSizeRequested size_requested,
				      gchar **out_picture_data, /* base64-encoded */
				      GCancellable *cancellable,
				      GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gchar *tmp;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_picture_data != NULL, FALSE);

	*out_picture_data = NULL;

	/*
	 * EWS server version earlier than 2013 doesn't have support to "GetUserPhoto".
	 */
	if (!e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Requires at least Microsoft Exchange 2013 server"));
		return FALSE;
	}

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetUserPhoto",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2013,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Email", "messages", NULL);
	e_soap_request_write_string (request, email);
	e_soap_request_end_element (request); /* Email */

	e_soap_request_start_element (request, "SizeRequested", "messages", NULL);
	tmp = g_strdup_printf ("HR%dx%d", (gint) size_requested, size_requested);
	e_soap_request_write_string (request, tmp);
	g_free (tmp);
	e_soap_request_end_element (request); /* SizeRequested */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_user_photo_response (cnc, response, out_picture_data, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!*out_picture_data)
		success = FALSE;
	else if (!success)
		g_clear_pointer (out_picture_data, g_free);

	return success;
}

static gboolean
e_ews_process_get_user_configuration_response (EEwsConnection *cnc,
					       ESoapResponse *response,
					       gchar **out_properties,
					       GError **error)
{
	ESoapParameter *param, *subparam;
	GError *local_error = NULL;

	*out_properties = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	if (param) {
		param = e_soap_parameter_get_first_child_by_name (param, "GetUserConfigurationResponseMessage");
		if (!param) {
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Missing <%s> in SOAP response", "GetUserConfigurationResponseMessage");
		}
	}

	if (param) {
		param = e_soap_parameter_get_first_child_by_name (param, "UserConfiguration");
		if (!param) {
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Missing <%s> in SOAP response", "UserConfiguration");
		}
	}

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "ItemId");
	if (subparam) {
		gchar *id, *changekey;

		id = e_soap_parameter_get_property (subparam, "Id");
		changekey = e_soap_parameter_get_property (subparam, "ChangeKey");

		/* Encoded as: Id + "\n" + ChangeKey */
		*out_properties = g_strconcat (id ? id : "", "\n", changekey, NULL);

		g_free (changekey);
		g_free (id);
	}

	if (!subparam) {
		subparam = e_soap_parameter_get_first_child_by_name (param, "Dictionary");
		if (subparam)
			*out_properties = e_soap_response_dump_parameter (response, subparam);
	}

	if (!subparam) {
		subparam = e_soap_parameter_get_first_child_by_name (param, "XmlData");
		if (subparam) {
			*out_properties = e_soap_parameter_get_string_value (subparam);
		}
	}

	if (!subparam) {
		subparam = e_soap_parameter_get_first_child_by_name (param, "BinaryData");
		if (subparam) {
			*out_properties = e_soap_parameter_get_string_value (subparam);
		}
	}

	if (*out_properties && !**out_properties) {
		g_free (*out_properties);
		*out_properties = NULL;
	}

	return *out_properties != NULL;
}

gboolean
e_ews_connection_get_user_configuration_sync (EEwsConnection *cnc,
					      gint pri,
					      const EwsFolderId *fid,
					      const gchar *config_name,
					      EEwsUserConfigurationProperties props,
					      gchar **out_properties,
					      GCancellable *cancellable,
					      GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	EwsFolderId local_fid;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_properties != NULL, FALSE);

	*out_properties = NULL;

	/* EWS server version earlier than 2010 doesn't support it. */
	if (!e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2010)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Requires at least Microsoft Exchange 2010 server"));
		return FALSE;
	}

	local_fid = *fid;
	local_fid.change_key = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetUserConfiguration",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "UserConfigurationName", "messages", NULL);
	e_soap_request_add_attribute (request, "Name", config_name, NULL, NULL);

	e_ews_folder_id_append_to_request (request, cnc->priv->email, &local_fid);

	e_soap_request_end_element (request); /* UserConfigurationName */

	e_soap_request_start_element (request, "UserConfigurationProperties", "messages", NULL);

	switch (props) {
	case E_EWS_USER_CONFIGURATION_PROPERTIES_ID:
		e_soap_request_write_string (request, "Id");
		break;
	case E_EWS_USER_CONFIGURATION_PROPERTIES_DICTIONARY:
		e_soap_request_write_string (request, "Dictionary");
		break;
	case E_EWS_USER_CONFIGURATION_PROPERTIES_XMLDATA:
		e_soap_request_write_string (request, "XmlData");
		break;
	case E_EWS_USER_CONFIGURATION_PROPERTIES_BINARYDATA:
		e_soap_request_write_string (request, "BinaryData");
		break;
	/* case E_EWS_USER_CONFIGURATION_PROPERTIES_ALL:
		e_soap_request_write_string (request, "All");
		break; */
	default:
		e_soap_request_write_string (request, "Unknown");
		break;
	}

	e_soap_request_end_element (request); /* UserConfigurationProperties */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_user_configuration_response (cnc, response, out_properties, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success)
		g_clear_pointer (out_properties, g_free);

	return success;
}

static gboolean
e_ews_process_convert_id_response (EEwsConnection *cnc,
				   ESoapResponse *response,
				   gchar **out_converted_id,
				   GError **error)
{
	ESoapParameter *param;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	if (param) {
		param = e_soap_parameter_get_first_child_by_name (param, "ConvertIdResponseMessage");
		if (!param) {
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Missing <%s> in SOAP response", "ConvertIdResponseMessage");
		}
	}

	if (param) {
		param = e_soap_parameter_get_first_child_by_name (param, "AlternateId");
		if (!param) {
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Missing <%s> in SOAP response", "AlternateId");
		}
	}

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	*out_converted_id = e_soap_parameter_get_property (param, "Id");

	return TRUE;
}

gboolean
e_ews_connection_convert_id_sync (EEwsConnection *cnc,
				  gint pri,
				  const gchar *email,
				  const gchar *folder_id,
				  const gchar *from_format,
				  const gchar *to_format,
				  gchar **out_converted_id,
				  GCancellable *cancellable,
				  GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (from_format != NULL, FALSE);
	g_return_val_if_fail (to_format != NULL, FALSE);
	g_return_val_if_fail (out_converted_id != NULL, FALSE);

	*out_converted_id = NULL;

	/* EWS server version earlier than 2007 SP1 doesn't support it. */
	if (!e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2007_SP1)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Requires at least Microsoft Exchange 2007 SP1 server"));
		return FALSE;
	}

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"ConvertId",
		"DestinationFormat",
		to_format,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "SourceIds", "messages", NULL);
	e_soap_request_start_element (request, "AlternateId", NULL, NULL);

	e_soap_request_add_attribute (request, "Id", folder_id, NULL, NULL);
	e_soap_request_add_attribute (request, "Format", from_format, NULL, NULL);
	e_soap_request_add_attribute (request, "Mailbox", email, NULL, NULL);

	e_soap_request_end_element (request); /* AlternateId */
	e_soap_request_end_element (request); /* SourceIds */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_convert_id_response (cnc, response, out_converted_id, error);

	g_clear_object (&request);
	g_clear_object (&response);

	if (!success)
		g_clear_pointer (out_converted_id, g_free);

	return success;
}

static GDateTime *
ews_oof_settings_string_to_date_time (const gchar *string)
{
	GDateTime *date_time = NULL;

	if (string) {
		GTimeZone *utc = g_time_zone_new_utc ();

		date_time = g_date_time_new_from_iso8601 (string, utc);

		g_time_zone_unref (utc);
	}

	return date_time;
}

static gchar *
ews_oof_settings_text_from_html (gchar *html_text)
{
	gsize haystack_len;
	gchar *plain_text;
	gchar *start, *end;
	gchar *ii, *jj;

	g_return_val_if_fail (html_text != NULL, NULL);

	haystack_len = strlen (html_text);
	start = g_strstr_len (html_text, haystack_len, "<body");
	end = g_strstr_len (html_text, haystack_len, "</body>");

	/* Parse the status set by Outlook Web Access. */
	if (g_strrstr (html_text, "BodyFragment") != NULL && start == NULL) {
		start = html_text;
		end = html_text + haystack_len;
	}

	/* Strip HTML tags. */
	plain_text = jj = g_malloc (end - start);
	for (ii = start; ii < end; ii++) {
		if (*ii == '<') {
			while (*ii != '>')
				ii++;
		} else {
			*jj++ = *ii;
		}
	}
	*jj = '\0';

	return plain_text;
}

static gboolean
e_ews_process_get_user_oof_settings_response (EEwsConnection *cnc,
					      ESoapResponse *response,
					      EEwsOofSettings *oof_settings,
					      GError **error)
{
	ESoapParameter *param;
	ESoapParameter *subparam;
	ESoapParameter *subsubparam;
	GDateTime *date_time;
	gchar *string;
	gchar *text;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessage", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (!ews_get_response_status (param, error))
		return FALSE;

	param = e_soap_response_get_first_parameter_by_name (response, "OofSettings", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "OofState");
	string = e_soap_parameter_get_string_value (subparam);
	if (g_strcmp0 (string, "Disabled") == 0)
		e_ews_oof_settings_set_state (oof_settings, E_EWS_OOF_STATE_DISABLED);
	else if (g_strcmp0 (string, "Enabled") == 0)
		e_ews_oof_settings_set_state (oof_settings, E_EWS_OOF_STATE_ENABLED);
	else if (g_strcmp0 (string, "Scheduled") == 0)
		e_ews_oof_settings_set_state (oof_settings, E_EWS_OOF_STATE_SCHEDULED);
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (param, "ExternalAudience");
	string = e_soap_parameter_get_string_value (subparam);
	if (g_strcmp0 (string, "None") == 0)
		e_ews_oof_settings_set_external_audience (oof_settings, E_EWS_EXTERNAL_AUDIENCE_NONE);
	else if (g_strcmp0 (string, "Known") == 0)
		e_ews_oof_settings_set_external_audience (oof_settings, E_EWS_EXTERNAL_AUDIENCE_KNOWN);
	else if (g_strcmp0 (string, "All") == 0)
		e_ews_oof_settings_set_external_audience (oof_settings, E_EWS_EXTERNAL_AUDIENCE_ALL);
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Duration");
	subsubparam = e_soap_parameter_get_first_child_by_name (subparam, "StartTime");
	string = e_soap_parameter_get_string_value (subsubparam);
	date_time = ews_oof_settings_string_to_date_time (string);
	if (date_time != NULL) {
		e_ews_oof_settings_set_start_time (oof_settings, date_time);
		g_date_time_unref (date_time);
	}
	g_free (string);

	subsubparam = e_soap_parameter_get_first_child_by_name (subparam, "EndTime");
	string = e_soap_parameter_get_string_value (subsubparam);
	date_time = ews_oof_settings_string_to_date_time (string);
	if (date_time != NULL) {
		e_ews_oof_settings_set_end_time (oof_settings, date_time);
		g_date_time_unref (date_time);
	}
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (param, "InternalReply");
	subsubparam = e_soap_parameter_get_first_child_by_name (subparam, "Message");
	string = e_soap_parameter_get_string_value (subsubparam);
	if (string == NULL)
		text = NULL;
	else if (g_strrstr (string, "</body>") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else if (g_strrstr (string, "BodyFragment") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else
		text = g_strdup (string);
	e_ews_oof_settings_set_internal_reply (oof_settings, text ? text : "");
	g_free (string);
	g_free (text);

	subparam = e_soap_parameter_get_first_child_by_name (param, "ExternalReply");
	subsubparam = e_soap_parameter_get_first_child_by_name (subparam, "Message");
	string = e_soap_parameter_get_string_value (subsubparam);
	if (string == NULL)
		text = NULL;
	else if (g_strrstr (string, "</body>") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else if (g_strrstr (string, "BodyFragment") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else
		text = g_strdup (string);
	e_ews_oof_settings_set_external_reply (oof_settings, text ? text : "");
	g_free (string);
	g_free (text);

	return TRUE;
}

gboolean
e_ews_connection_get_user_oof_settings_sync (EEwsConnection *cnc,
					     gint pri,
					     EEwsOofSettings *inout_oof_settings,
					     GCancellable *cancellable,
					     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (inout_oof_settings != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetUserOofSettingsRequest",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", NULL, NULL);
	e_ews_request_write_string_parameter (request, "Address", NULL, e_ews_connection_get_mailbox (cnc));
	e_soap_request_end_element (request);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_get_user_oof_settings_response (cnc, response, inout_oof_settings, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gchar *
ews_oof_settings_date_time_to_string (const GDateTime *date_time)
{
	gchar *string = NULL;

	if (date_time)
		string = g_date_time_format_iso8601 ((GDateTime *) date_time);

	return string;
}

static gboolean
e_ews_process_set_user_oof_settings_response (EEwsConnection *cnc,
					      ESoapResponse *response,
					      GError **error)
{
	ESoapParameter *param;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessage", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return ews_get_response_status (param, error);
}

gboolean
e_ews_connection_set_user_oof_settings_sync (EEwsConnection *cnc,
					     gint pri,
					     EEwsOofState state,
					     EEwsExternalAudience external_audience,
					     const GDateTime *start_date,
					     const GDateTime *end_date,
					     const gchar *internal_reply,
					     const gchar *external_reply,
					     GCancellable *cancellable,
					     GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gchar *start_time;
	gchar *end_time;
	const gchar *string;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"SetUserOofSettingsRequest",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "Mailbox", NULL, NULL);
	e_ews_request_write_string_parameter (request, "Address", NULL, e_ews_connection_get_mailbox (cnc));
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "UserOofSettings", NULL, NULL);

	switch (state) {
		default:
			g_warn_if_reached ();
			/* fall through */
		case E_EWS_OOF_STATE_DISABLED:
			string = "Disabled";
			break;
		case E_EWS_OOF_STATE_ENABLED:
			string = "Enabled";
			break;
		case E_EWS_OOF_STATE_SCHEDULED:
			string = "Scheduled";
			break;
	}

	e_ews_request_write_string_parameter (request, "OofState", NULL, string);

	switch (external_audience) {
		default:
			g_warn_if_reached ();
			/* fall through */
		case E_EWS_EXTERNAL_AUDIENCE_NONE:
			string = "None";
			break;
		case E_EWS_EXTERNAL_AUDIENCE_KNOWN:
			string = "Known";
			break;
		case E_EWS_EXTERNAL_AUDIENCE_ALL:
			string = "All";
			break;
	}

	e_ews_request_write_string_parameter (request, "ExternalAudience", NULL, string);

	start_time = ews_oof_settings_date_time_to_string (start_date);
	end_time = ews_oof_settings_date_time_to_string (end_date);

	e_soap_request_start_element (request, "Duration", NULL, NULL);
	e_ews_request_write_string_parameter (request, "StartTime", NULL, start_time);
	e_ews_request_write_string_parameter (request, "EndTime", NULL, end_time);
	e_soap_request_end_element (request);

	g_free (start_time);
	g_free (end_time);

	e_soap_request_start_element (request, "InternalReply", NULL, NULL);
	e_ews_request_write_string_parameter (request, "Message", NULL, internal_reply);
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "ExternalReply", NULL, NULL);
	e_ews_request_write_string_parameter (request, "Message", NULL, external_reply);
	e_soap_request_end_element (request);

	e_soap_request_end_element (request); /* UserOofSettings */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_set_user_oof_settings_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

static gboolean
e_ews_process_subscribe_response (EEwsConnection *cnc,
				  ESoapResponse *response,
				  gchar **out_subscription_id,
				  GError **error)
{
	ESoapParameter *param, *subparam;
	GError *local_error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &local_error);

	/* Sanity check */
	g_return_val_if_fail (
		(param != NULL && local_error == NULL) ||
		(param == NULL && local_error != NULL), FALSE);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, error))
			return FALSE;

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "SubscribeResponseMessage")) {
			ESoapParameter *node;

			node = e_soap_parameter_get_first_child_by_name (subparam, "SubscriptionId");
			*out_subscription_id = e_soap_parameter_get_string_value (node);

			break;
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	return *out_subscription_id != NULL;
}

gboolean
e_ews_connection_subscribe_sync (EEwsConnection *cnc,
				 gint pri,
				 const GSList *folder_ids, /* gchar * */
				 gchar **out_subscription_id,
				 GCancellable *cancellable,
				 GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	const GSList *link;
	const gchar * const *event_names;
	gint event_type;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (out_subscription_id != NULL, FALSE);

	*out_subscription_id = NULL;

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"Subscribe",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_soap_request_start_element (request, "StreamingSubscriptionRequest", "messages", NULL);

	e_soap_request_start_element (request, "FolderIds", NULL, NULL);
	for (link = folder_ids; link; link = g_slist_next (link)) {
		e_ews_request_write_string_parameter_with_attribute (request, "FolderId", NULL, NULL, "Id", link->data);
	}
	e_soap_request_end_element (request); /* FolderIds */

	event_names = e_ews_notification_get_event_names ();

	e_soap_request_start_element (request, "EventTypes", NULL, NULL);
	for (event_type = 0; event_names[event_type] != NULL; event_type++) {
		if (g_strcmp0 (event_names[event_type], "StatusEvent") == 0)
			continue;

		e_ews_request_write_string_parameter_with_attribute (request, "EventType", NULL, event_names[event_type], NULL, NULL);
	}
	e_soap_request_end_element (request); /* EventTypes */

	e_soap_request_end_element (request); /* StreamingSubscriptionRequest */

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_subscribe_response (cnc, response, out_subscription_id, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

gboolean
e_ews_connection_unsubscribe_sync (EEwsConnection *cnc,
				   gint pri,
				   const gchar *subscription_id,
				   GCancellable *cancellable,
				   GError **error)
{
	ESoapRequest *request;
	ESoapResponse *response;
	gboolean success;

	g_return_val_if_fail (cnc != NULL, FALSE);
	g_return_val_if_fail (subscription_id != NULL, FALSE);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"Unsubscribe",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		error);

	if (!request)
		return FALSE;

	e_ews_request_write_string_parameter_with_attribute (request, "SubscriptionId", "messages", subscription_id, NULL, NULL);

	e_ews_request_write_footer (request);

	response = e_ews_connection_send_request_sync (cnc, request, cancellable, error);

	if (!response) {
		g_clear_object (&request);
		return FALSE;
	}

	success = e_ews_process_generic_response (cnc, response, error);

	g_clear_object (&request);
	g_clear_object (&response);

	return success;
}

GInputStream *
e_ews_connection_prepare_streaming_events_sync (EEwsConnection *cnc,
						gint pri,
						const gchar *subscription_id,
						ESoupSession **out_session,
						SoupMessage **out_message,
						GCancellable *cancellable,
						GError **error)
{
	ESoapRequest *request;
	CamelEwsSettings *settings;
	GInputStream *input_stream;

	g_return_val_if_fail (cnc != NULL, NULL);
	g_return_val_if_fail (subscription_id != NULL, NULL);
	g_return_val_if_fail (out_session != NULL, NULL);
	g_return_val_if_fail (out_message != NULL, NULL);

	request = e_ews_request_new_with_header (
		cnc->priv->uri,
		cnc->priv->impersonate_user,
		"GetStreamingEvents",
		NULL,
		NULL,
		cnc->priv->version,
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		error);

	if (!request)
		return NULL;

	e_soap_request_start_element (request, "SubscriptionIds", "messages", NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "SubscriptionId", NULL, subscription_id, NULL, NULL);
	e_soap_request_end_element (request);

	e_ews_request_write_string_parameter_with_attribute (request, "ConnectionTimeout", "messages", "10", NULL, NULL);

	e_ews_request_write_footer (request);

	settings = e_ews_connection_ref_settings (cnc);

	*out_session = e_ews_connection_create_soup_session (cnc);
	*out_message = e_soap_request_persist (request, *out_session, settings, error);

	g_clear_object (&settings);

	if (!*out_message) {
		g_clear_object (out_session);
		g_clear_object (&request);
		return NULL;
	}

	e_ews_connection_maybe_prepare_message_for_testing_sources (cnc, *out_message);

	g_mutex_lock (&cnc->priv->property_lock);
	e_soup_session_set_credentials (*out_session, cnc->priv->credentials);
	g_mutex_unlock (&cnc->priv->property_lock);

	input_stream = e_soup_session_send_message_sync (*out_session, *out_message, cancellable, error);

	g_clear_object (&request);

	if (!input_stream) {
		g_clear_object (out_message);
		g_clear_object (out_session);
	}

	return input_stream;
}
