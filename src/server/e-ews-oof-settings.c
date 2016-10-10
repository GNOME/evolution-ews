/*
 * e-ews-oof-settings.c
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

#include "evolution-ews-config.h"

#include "e-ews-oof-settings.h"

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "ews-errors.h"
#include "e-ews-enumtypes.h"
#include "e-ews-message.h"

#define E_EWS_OOF_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_EWS_OOF_SETTINGS, EEwsOofSettingsPrivate))

/* Forward Declarations */
static void	e_ews_oof_settings_initable_init
					(GInitableIface *iface);
static void	e_ews_oof_settings_async_initable_init
					(GAsyncInitableIface *iface);

struct _EEwsOofSettingsPrivate {
	GMutex property_lock;
	EEwsConnection *connection;
	EEwsOofState state;
	EEwsExternalAudience external_audience;
	GDateTime *start_time;
	GDateTime *end_time;
	gchar *internal_reply;
	gchar *external_reply;
};

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_END_TIME,
	PROP_EXTERNAL_AUDIENCE,
	PROP_EXTERNAL_REPLY,
	PROP_INTERNAL_REPLY,
	PROP_START_TIME,
	PROP_STATE
};

G_DEFINE_TYPE_WITH_CODE (
	EEwsOofSettings,
	e_ews_oof_settings,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_ews_oof_settings_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_ASYNC_INITABLE,
		e_ews_oof_settings_async_initable_init))

static GDateTime *
ews_oof_settings_string_to_date_time (const gchar *string)
{
	GTimeVal tv = { 0, 0 };
	GDateTime *date_time = NULL;

	if (g_time_val_from_iso8601 (string, &tv))
		date_time = g_date_time_new_from_timeval_utc (&tv);

	return date_time;
}

static gchar *
ews_oof_settings_date_time_to_string (GDateTime *date_time)
{
	GTimeVal tv = { 0, 0 };
	gchar *string = NULL;

	if (g_date_time_to_timeval (date_time, &tv))
		string = g_time_val_to_iso8601 (&tv);

	return string;
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

static void
ews_oof_settings_get_response_cb (ESoapResponse *response,
                                  GSimpleAsyncResult *simple)
{
	GAsyncResult *result;
	GObject *source_object;
	EEwsOofSettings *settings;
	ESoapParameter *param;
	ESoapParameter *subparam;
	ESoapParameter *subsubparam;
	GDateTime *date_time;
	gchar *string;
	gchar *text;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (
		response, "ResponseMessage", &error);

	/* Sanity check */
	g_return_if_fail (
		(param != NULL && error == NULL) ||
		(param == NULL && error != NULL));

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		return;
	}

	if (!ews_get_response_status (param, &error)) {
		g_simple_async_result_take_error (simple, error);
		return;
	}

	param = e_soap_response_get_first_parameter_by_name (
		response, "OofSettings", &error);

	/* Sanity check */
	g_return_if_fail (
		(param != NULL && error == NULL) ||
		(param == NULL && error != NULL));

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		return;
	}

	result = G_ASYNC_RESULT (simple);
	source_object = g_async_result_get_source_object (result);
	settings = E_EWS_OOF_SETTINGS (source_object);

	subparam = e_soap_parameter_get_first_child_by_name (
		param, "OofState");
	string = e_soap_parameter_get_string_value (subparam);
	if (g_strcmp0 (string, "Disabled") == 0)
		e_ews_oof_settings_set_state (
			settings, E_EWS_OOF_STATE_DISABLED);
	else if (g_strcmp0 (string, "Enabled") == 0)
		e_ews_oof_settings_set_state (
			settings, E_EWS_OOF_STATE_ENABLED);
	else if (g_strcmp0 (string, "Scheduled") == 0)
		e_ews_oof_settings_set_state (
			settings, E_EWS_OOF_STATE_SCHEDULED);
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (
		param, "ExternalAudience");
	string = e_soap_parameter_get_string_value (subparam);
	if (g_strcmp0 (string, "None") == 0)
		e_ews_oof_settings_set_external_audience (
			settings, E_EWS_EXTERNAL_AUDIENCE_NONE);
	else if (g_strcmp0 (string, "Known") == 0)
		e_ews_oof_settings_set_external_audience (
			settings, E_EWS_EXTERNAL_AUDIENCE_KNOWN);
	else if (g_strcmp0 (string, "All") == 0)
		e_ews_oof_settings_set_external_audience (
			settings, E_EWS_EXTERNAL_AUDIENCE_ALL);
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (
		param, "Duration");
	subsubparam = e_soap_parameter_get_first_child_by_name (
		subparam, "StartTime");
	string = e_soap_parameter_get_string_value (subsubparam);
	date_time = ews_oof_settings_string_to_date_time (string);
	if (date_time != NULL) {
		e_ews_oof_settings_set_start_time (settings, date_time);
		g_date_time_unref (date_time);
	}
	g_free (string);
	subsubparam = e_soap_parameter_get_first_child_by_name (
		subparam, "EndTime");
	string = e_soap_parameter_get_string_value (subsubparam);
	date_time = ews_oof_settings_string_to_date_time (string);
	if (date_time != NULL) {
		e_ews_oof_settings_set_end_time (settings, date_time);
		g_date_time_unref (date_time);
	}
	g_free (string);

	subparam = e_soap_parameter_get_first_child_by_name (
		param, "InternalReply");
	subsubparam = e_soap_parameter_get_first_child_by_name (
		subparam, "Message");
	string = e_soap_parameter_get_string_value (subsubparam);
	if (string == NULL)
		text = NULL;
	else if (g_strrstr (string, "</body>") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else if (g_strrstr (string, "BodyFragment") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else
		text = g_strdup (string);
	e_ews_oof_settings_set_internal_reply (settings, text ? text : "");
	g_free (string);
	g_free (text);

	subparam = e_soap_parameter_get_first_child_by_name (
		param, "ExternalReply");
	subsubparam = e_soap_parameter_get_first_child_by_name (
		subparam, "Message");
	string = e_soap_parameter_get_string_value (subsubparam);
	if (string == NULL)
		text = NULL;
	else if (g_strrstr (string, "</body>") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else if (g_strrstr (string, "BodyFragment") != NULL)
		text = ews_oof_settings_text_from_html (string);
	else
		text = g_strdup (string);
	e_ews_oof_settings_set_external_reply (settings, text ? text : "");
	g_free (string);
	g_free (text);

	g_object_unref (source_object);
}

static void
ews_oof_settings_set_connection (EEwsOofSettings *settings,
                                 EEwsConnection *connection)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (connection));
	g_return_if_fail (settings->priv->connection == NULL);

	settings->priv->connection = g_object_ref (connection);
}

static void
ews_oof_settings_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			ews_oof_settings_set_connection (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_object (value));
			return;

		case PROP_END_TIME:
			e_ews_oof_settings_set_end_time (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_boxed (value));
			return;

		case PROP_EXTERNAL_AUDIENCE:
			e_ews_oof_settings_set_external_audience (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_EXTERNAL_REPLY:
			e_ews_oof_settings_set_external_reply (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_INTERNAL_REPLY:
			e_ews_oof_settings_set_internal_reply (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_START_TIME:
			e_ews_oof_settings_set_start_time (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_boxed (value));
			return;

		case PROP_STATE:
			e_ews_oof_settings_set_state (
				E_EWS_OOF_SETTINGS (object),
				g_value_get_enum (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_oof_settings_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_ews_oof_settings_get_connection (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_END_TIME:
			g_value_take_boxed (
				value,
				e_ews_oof_settings_ref_end_time (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_EXTERNAL_AUDIENCE:
			g_value_set_enum (
				value,
				e_ews_oof_settings_get_external_audience (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_EXTERNAL_REPLY:
			g_value_take_string (
				value,
				e_ews_oof_settings_dup_external_reply (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_INTERNAL_REPLY:
			g_value_take_string (
				value,
				e_ews_oof_settings_dup_internal_reply (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_START_TIME:
			g_value_take_boxed (
				value,
				e_ews_oof_settings_ref_start_time (
				E_EWS_OOF_SETTINGS (object)));
			return;

		case PROP_STATE:
			g_value_set_enum (
				value,
				e_ews_oof_settings_get_state (
				E_EWS_OOF_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_oof_settings_dispose (GObject *object)
{
	EEwsOofSettingsPrivate *priv;

	priv = E_EWS_OOF_SETTINGS_GET_PRIVATE (object);

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_oof_settings_parent_class)->dispose (object);
}

static void
ews_oof_settings_finalize (GObject *object)
{
	EEwsOofSettingsPrivate *priv;

	priv = E_EWS_OOF_SETTINGS_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	g_date_time_unref (priv->start_time);
	g_date_time_unref (priv->end_time);

	g_free (priv->internal_reply);
	g_free (priv->external_reply);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_oof_settings_parent_class)->finalize (object);
}

static gboolean
ews_oof_settings_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	g_async_initable_init_async (
		G_ASYNC_INITABLE (initable),
		G_PRIORITY_DEFAULT, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = g_async_initable_init_finish (
		G_ASYNC_INITABLE (initable), result, error);

	e_async_closure_free (closure);

	return success;
}

static void
ews_oof_settings_initable_init_async (GAsyncInitable *initable,
                                      gint io_priority,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	EEwsOofSettings *settings;
	EEwsConnection *connection;
	ESoapMessage *message;
	CamelEwsSettings *ews_settings;
	const gchar *uri, *impersonate_user;
	const gchar *mailbox;
	EEwsServerVersion version;

	settings = E_EWS_OOF_SETTINGS (initable);
	connection = e_ews_oof_settings_get_connection (settings);

	uri = e_ews_connection_get_uri (connection);
	impersonate_user = e_ews_connection_get_impersonate_user (connection);
	mailbox = e_ews_connection_get_mailbox (connection);
	version = e_ews_connection_get_server_version (connection);
	ews_settings = e_ews_connection_ref_settings (connection);

	message = e_ews_message_new_with_header (
		ews_settings,
		uri,
		impersonate_user,
		"GetUserOofSettingsRequest",
		NULL,
		NULL,
		version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		TRUE);

	g_clear_object (&ews_settings);

	e_soap_message_start_element (message, "Mailbox", NULL, NULL);
	e_ews_message_write_string_parameter (
		message, "Address", NULL, mailbox);
	e_soap_message_end_element (message);

	e_ews_message_write_footer (message);

	simple = g_simple_async_result_new (
		G_OBJECT (initable), callback, user_data,
		ews_oof_settings_initable_init_async);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	e_ews_connection_queue_request (
		connection, message,
		ews_oof_settings_get_response_cb,
		EWS_PRIORITY_MEDIUM, cancellable, simple);

	g_object_unref (simple);
}

static gboolean
ews_oof_settings_initable_init_finish (GAsyncInitable *initable,
                                       GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (initable),
		ews_oof_settings_initable_init_async), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
e_ews_oof_settings_class_init (EEwsOofSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EEwsOofSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_oof_settings_set_property;
	object_class->get_property = ews_oof_settings_get_property;
	object_class->dispose = ews_oof_settings_dispose;
	object_class->finalize = ews_oof_settings_finalize;

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"Exchange Web Services connection object",
			E_TYPE_EWS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_END_TIME,
		g_param_spec_boxed (
			"end-time",
			"End Time",
			"The end of an Out of Office time span",
			G_TYPE_DATE_TIME,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_EXTERNAL_AUDIENCE,
		g_param_spec_enum (
			"external-audience",
			"External Audience",
			"Determines to whom external "
			"Out of Office messages are sent",
			E_TYPE_EWS_EXTERNAL_AUDIENCE,
			E_EWS_EXTERNAL_AUDIENCE_NONE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_EXTERNAL_REPLY,
		g_param_spec_string (
			"external-reply",
			"External Reply",
			"Out of Office reply to external senders",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_INTERNAL_REPLY,
		g_param_spec_string (
			"internal-reply",
			"Internal Reply",
			"Out of Office reply to internal senders",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_START_TIME,
		g_param_spec_boxed (
			"start-time",
			"Start Time",
			"The start of an Out of Office time span",
			G_TYPE_DATE_TIME,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_STATE,
		g_param_spec_enum (
			"state",
			"State",
			"Out of Office state",
			E_TYPE_EWS_OOF_STATE,
			E_EWS_OOF_STATE_DISABLED,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_ews_oof_settings_init (EEwsOofSettings *settings)
{
	settings->priv = E_EWS_OOF_SETTINGS_GET_PRIVATE (settings);

	g_mutex_init (&settings->priv->property_lock);

	/* This is just to make sure the values are never NULL.
	 * They will be destroyed as soon as we get real values. */
	settings->priv->start_time = g_date_time_new_now_local ();
	settings->priv->end_time = g_date_time_new_now_local ();
}

static void
e_ews_oof_settings_initable_init (GInitableIface *iface)
{
	iface->init = ews_oof_settings_initable_init;
}

static void
e_ews_oof_settings_async_initable_init (GAsyncInitableIface *iface)
{
	iface->init_async = ews_oof_settings_initable_init_async;
	iface->init_finish = ews_oof_settings_initable_init_finish;
}

EEwsOofSettings *
e_ews_oof_settings_new_sync (EEwsConnection *connection,
                             GCancellable *cancellable,
                             GError **error)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (connection), NULL);

	return g_initable_new (
		E_TYPE_EWS_OOF_SETTINGS, cancellable, error,
		"connection", connection, NULL);
}

void
e_ews_oof_settings_new (EEwsConnection *connection,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (connection));

	g_async_initable_new_async (
		E_TYPE_EWS_OOF_SETTINGS, io_priority,
		cancellable, callback, user_data,
		"connection", connection, NULL);
}

EEwsOofSettings *
e_ews_oof_settings_new_finish (GAsyncResult *result,
                               GError **error)
{
	GObject *source_object;
	GObject *object;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	source_object = g_async_result_get_source_object (result);
	g_return_val_if_fail (source_object != NULL, NULL);

	object = g_async_initable_new_finish (
		G_ASYNC_INITABLE (source_object), result, error);

	g_object_unref (source_object);

	return (object != NULL) ? E_EWS_OOF_SETTINGS (object) : NULL;
}

EEwsConnection *
e_ews_oof_settings_get_connection (EEwsOofSettings *settings)
{
	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	return settings->priv->connection;
}

EEwsOofState
e_ews_oof_settings_get_state (EEwsOofSettings *settings)
{
	g_return_val_if_fail (
		E_IS_EWS_OOF_SETTINGS (settings),
		E_EWS_OOF_STATE_DISABLED);

	return settings->priv->state;
}

void
e_ews_oof_settings_set_state (EEwsOofSettings *settings,
                              EEwsOofState state)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	if (settings->priv->state == state)
		return;

	settings->priv->state = state;

	g_object_notify (G_OBJECT (settings), "state");
}

EEwsExternalAudience
e_ews_oof_settings_get_external_audience (EEwsOofSettings *settings)
{
	g_return_val_if_fail (
		E_IS_EWS_OOF_SETTINGS (settings),
		E_EWS_EXTERNAL_AUDIENCE_NONE);

	return settings->priv->external_audience;
}

void
e_ews_oof_settings_set_external_audience (EEwsOofSettings *settings,
                                          EEwsExternalAudience external_audience)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	if (settings->priv->external_audience == external_audience)
		return;

	settings->priv->external_audience = external_audience;

	g_object_notify (G_OBJECT (settings), "external-audience");
}

GDateTime *
e_ews_oof_settings_ref_start_time (EEwsOofSettings *settings)
{
	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	return g_date_time_ref (settings->priv->start_time);
}

void
e_ews_oof_settings_set_start_time (EEwsOofSettings *settings,
                                   GDateTime *start_time)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));
	g_return_if_fail (start_time != NULL);

	g_mutex_lock (&settings->priv->property_lock);

	if (g_date_time_compare (settings->priv->start_time, start_time) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	if (start_time != settings->priv->start_time) {
		g_date_time_unref (settings->priv->start_time);
		settings->priv->start_time = g_date_time_ref (start_time);
	}

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "start-time");
}

GDateTime *
e_ews_oof_settings_ref_end_time (EEwsOofSettings *settings)
{
	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	return g_date_time_ref (settings->priv->end_time);
}

void
e_ews_oof_settings_set_end_time (EEwsOofSettings *settings,
                                 GDateTime *end_time)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));
	g_return_if_fail (end_time != NULL);

	g_mutex_lock (&settings->priv->property_lock);

	if (g_date_time_compare (settings->priv->end_time, end_time) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	if (end_time != settings->priv->end_time) {
		g_date_time_unref (settings->priv->end_time);
		settings->priv->end_time = g_date_time_ref (end_time);
	}

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "end-time");
}

const gchar *
e_ews_oof_settings_get_internal_reply (EEwsOofSettings *settings)
{
	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	return settings->priv->internal_reply;
}

gchar *
e_ews_oof_settings_dup_internal_reply (EEwsOofSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = e_ews_oof_settings_get_internal_reply (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
e_ews_oof_settings_set_internal_reply (EEwsOofSettings *settings,
                                       const gchar *internal_reply)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (internal_reply, settings->priv->internal_reply) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->internal_reply);
	settings->priv->internal_reply = g_strdup (internal_reply);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "internal-reply");
}

const gchar *
e_ews_oof_settings_get_external_reply (EEwsOofSettings *settings)
{
	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	return settings->priv->external_reply;
}

gchar *
e_ews_oof_settings_dup_external_reply (EEwsOofSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = e_ews_oof_settings_get_external_reply (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
e_ews_oof_settings_set_external_reply (EEwsOofSettings *settings,
                                       const gchar *external_reply)
{
	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (external_reply, settings->priv->external_reply) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->external_reply);
	settings->priv->external_reply = g_strdup (external_reply);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "external-reply");
}

gboolean
e_ews_oof_settings_submit_sync (EEwsOofSettings *settings,
                                GCancellable *cancellable,
                                GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), FALSE);

	closure = e_async_closure_new ();

	e_ews_oof_settings_submit (
		settings, cancellable, e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_ews_oof_settings_submit_finish (settings, result, error);

	e_async_closure_free (closure);

	return success;
}

static void
ews_oof_settings_submit_response_cb (ESoapResponse *response,
                                     GSimpleAsyncResult *simple)
{
	ESoapParameter *param;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (
		response, "ResponseMessage", &error);

	/* Sanity check */
	g_return_if_fail (
		(param != NULL && error == NULL) ||
		(param == NULL && error != NULL));

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	else if (!ews_get_response_status (param, &error))
		g_simple_async_result_take_error (simple, error);
}

void
e_ews_oof_settings_submit (EEwsOofSettings *settings,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	ESoapMessage *message;
	EEwsConnection *connection;
	GSimpleAsyncResult *simple;
	GDateTime *date_time;
	const gchar *mailbox;
	const gchar *string;
	const gchar *uri, *impersonate_user;
	gchar *internal_reply;
	gchar *external_reply;
	gchar *start_time;
	gchar *end_time;
	EEwsServerVersion version;
	CamelEwsSettings *ews_settings;

	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	connection = e_ews_oof_settings_get_connection (settings);
	mailbox = e_ews_connection_get_mailbox (connection);
	uri = e_ews_connection_get_uri (connection);
	impersonate_user = e_ews_connection_get_impersonate_user (connection);
	version = e_ews_connection_get_server_version (connection);
	ews_settings = e_ews_connection_ref_settings (connection);

	internal_reply = e_ews_oof_settings_dup_internal_reply (settings);
	external_reply = e_ews_oof_settings_dup_external_reply (settings);

	date_time = e_ews_oof_settings_ref_start_time (settings);
	start_time = ews_oof_settings_date_time_to_string (date_time);
	g_date_time_unref (date_time);

	date_time = e_ews_oof_settings_ref_end_time (settings);
	end_time = ews_oof_settings_date_time_to_string (date_time);
	g_date_time_unref (date_time);

	message = e_ews_message_new_with_header (
		ews_settings,
		uri,
		impersonate_user,
		"SetUserOofSettingsRequest",
		NULL,
		NULL,
		version,
		E_EWS_EXCHANGE_2007_SP1,
		FALSE,
		TRUE);

	g_clear_object (&ews_settings);

	/* <Mailbox> */

	e_soap_message_start_element (
		message, "Mailbox", NULL, NULL);
	e_ews_message_write_string_parameter (
		message, "Address", NULL, mailbox);
	e_soap_message_end_element (message);

	/* </Mailbox> */

	/* <UserOofSettings> */

	e_soap_message_start_element (
		message, "UserOofSettings", NULL, NULL);

	switch (e_ews_oof_settings_get_state (settings)) {
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

	e_ews_message_write_string_parameter (
		message, "OofState", NULL, string);

	switch (e_ews_oof_settings_get_external_audience (settings)) {
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

	e_ews_message_write_string_parameter (
		message, "ExternalAudience", NULL, string);

	/* <Duration> */

	e_soap_message_start_element (
		message, "Duration", NULL, NULL);
	e_ews_message_write_string_parameter (
		message, "StartTime", NULL, start_time);
	e_ews_message_write_string_parameter (
		message, "EndTime", NULL, end_time);
	e_soap_message_end_element (message);

	/* </Duration> */

	/* <InternalReply> */

	e_soap_message_start_element (
		message, "InternalReply", NULL, NULL);
	e_ews_message_write_string_parameter (
		message, "Message", NULL, internal_reply);
	e_soap_message_end_element (message);

	/* </InternalReply> */

	/* <ExternalReply> */

	e_soap_message_start_element (
		message, "ExternalReply", NULL, NULL);
	e_ews_message_write_string_parameter (
		message, "Message", NULL, external_reply);
	e_soap_message_end_element (message);

	/* </ExternalReply> */

	e_soap_message_end_element (message);

	/* </UserOofSettings> */

	e_ews_message_write_footer (message);

	simple = g_simple_async_result_new (
		G_OBJECT (settings), callback,
		user_data, e_ews_oof_settings_submit);

	e_ews_connection_queue_request (
		connection, message,
		ews_oof_settings_submit_response_cb,
		EWS_PRIORITY_MEDIUM, cancellable, simple);

	g_object_unref (simple);

	g_free (internal_reply);
	g_free (external_reply);
	g_free (start_time);
	g_free (end_time);
}

gboolean
e_ews_oof_settings_submit_finish (EEwsOofSettings *settings,
                                  GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (settings),
		e_ews_oof_settings_submit), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

