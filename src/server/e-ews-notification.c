/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-ews-connection-utils.h"
#include "e-ews-debug.h"
#include "e-ews-notification.h"

#define E_EWS_NOTIFICATION_GET_PRIVATE(obj)\
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_EWS_NOTIFICATION, EEwsNotificationPrivate))

G_DEFINE_TYPE (EEwsNotification, e_ews_notification, G_TYPE_OBJECT)

struct _EEwsNotificationPrivate {
	SoupSession *soup_session;
	EEwsConnection *connection; /* not referred */
	ESoapMessage *msg;
	GByteArray *chunk;
};

enum {
	PROP_0,
	PROP_CONNECTION
};

static const gchar *default_events_names[] = {
	"CopiedEvent",
	"CreatedEvent",
	"DeletedEvent",
	"ModifiedEvent",
	"MovedEvent",
	"StatusEvent",
	NULL};

static void
ews_notification_authenticate (SoupSession *session,
			       SoupMessage *message,
			       SoupAuth *auth,
			       gboolean retrying,
			       gpointer data)
{
	EEwsNotification *notification = data;
	EEwsConnection *connection;
	CamelNetworkSettings *network_settings;
	gchar *user, *password;

	g_return_if_fail (notification != NULL);
	g_return_if_fail (notification->priv->connection != NULL);

	connection = notification->priv->connection;

	if (retrying)
		e_ews_connection_set_password (connection, NULL);

	network_settings = CAMEL_NETWORK_SETTINGS (e_ews_connection_ref_settings (connection));
	user = camel_network_settings_dup_user (network_settings);

	password = e_ews_connection_dup_password (connection);

	if (password != NULL)
		soup_auth_authenticate (auth, user, password);

	g_free (password);
	g_free (user);
	g_object_unref (network_settings);

}

EEwsNotification *
e_ews_notification_new (EEwsConnection *connection)
{
	EEwsNotification *notification;
	CamelEwsSettings *ews_settings;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (connection), NULL);

	notification = g_object_new (
		E_TYPE_EWS_NOTIFICATION,
		"connection", connection, NULL);

	ews_settings = e_ews_connection_ref_settings (connection);

	g_object_bind_property_full (
		ews_settings, "auth-mechanism",
		notification->priv->soup_session, "use-ntlm",
		G_BINDING_SYNC_CREATE,
		e_ews_connection_utils_auth_mech_to_use_ntlm,
		NULL,
		NULL, (GDestroyNotify) NULL);

	g_object_unref (ews_settings);

	return notification;
}

static void
e_ews_notification_set_connection (EEwsNotification *notification,
				   EEwsConnection *connection)
{
	g_return_if_fail (E_IS_EWS_NOTIFICATION (notification));
	g_return_if_fail (E_IS_EWS_CONNECTION (connection));
	g_return_if_fail (notification->priv->connection == NULL);

	notification->priv->connection = connection;
	g_object_weak_ref (
		G_OBJECT (notification->priv->connection),
		(GWeakNotify) g_nullify_pointer,
		&notification->priv->connection);
}

static void
ews_notification_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			e_ews_notification_set_connection (
				E_EWS_NOTIFICATION (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static EEwsConnection *
e_ews_notification_get_connection (const EEwsNotification *notification)
{
	g_return_val_if_fail (E_IS_EWS_NOTIFICATION (notification), NULL);
	g_return_val_if_fail (notification->priv == NULL, NULL);

	return notification->priv->connection;
}

static void
ews_notification_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			g_value_take_object (
				value,
				e_ews_notification_get_connection (
				E_EWS_NOTIFICATION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_notification_dispose (GObject *object)
{
	EEwsNotificationPrivate *priv;

	priv = E_EWS_NOTIFICATION_GET_PRIVATE (object);

	if (priv->soup_session) {
		g_signal_handlers_disconnect_by_func (
			priv->soup_session,
			ews_notification_authenticate, object);
	}

	if (priv->connection) {
		g_object_weak_unref (
			G_OBJECT (priv->connection),
			(GWeakNotify) g_nullify_pointer,
			&priv->connection);
		priv->connection = NULL;
	}
}

static void
e_ews_notification_class_init (EEwsNotificationClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EEwsNotificationPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_notification_set_property;
	object_class->get_property = ews_notification_get_property;
	object_class->dispose = ews_notification_dispose;

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"Connection",
			E_TYPE_EWS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_ews_notification_init (EEwsNotification *notification)
{
	gint log_level;

	notification->priv = E_EWS_NOTIFICATION_GET_PRIVATE (notification);

	notification->priv->soup_session = soup_session_sync_new_with_options (
		SOUP_SESSION_USE_NTLM, TRUE,
		NULL);

	log_level = e_ews_debug_get_log_level ();
	if (log_level >= 2) {
		SoupLogger *logger;
		logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);

		soup_session_add_feature (notification->priv->soup_session, SOUP_SESSION_FEATURE (logger));
		g_object_unref (logger);
	}

	g_signal_connect (notification->priv->soup_session, "authenticate", G_CALLBACK (ews_notification_authenticate), notification);
}

static EEwsNotificationEvent *
get_folder_event_info (ESoapParameter *param,
		       EEwsNotificationEventType event_type)
{
	ESoapParameter *subparam;
	EEwsNotificationEvent *event;

	event = e_ews_notification_event_new ();

	event->type = event_type;
	event->is_item = FALSE;

	subparam = e_soap_parameter_get_first_child_by_name (param, "FolderId");
	event->folder_id = e_soap_parameter_get_property (subparam, "Id");

	subparam = e_soap_parameter_get_first_child_by_name (param, "OldFolderId");
	if (subparam != NULL) {
		event->old_folder_id = e_soap_parameter_get_property (subparam, "Id");
	}

	return event;
}

static EEwsNotificationEvent *
get_item_event_info (ESoapParameter *param,
		     EEwsNotificationEventType event_type)
{
	ESoapParameter *subparam;
	EEwsNotificationEvent *event;

	event = e_ews_notification_event_new ();

	event->type = event_type;
	event->is_item = TRUE;

	subparam = e_soap_parameter_get_first_child_by_name (param, "ParentFolderId");
	event->folder_id = e_soap_parameter_get_property (subparam, "Id");

	subparam = e_soap_parameter_get_first_child_by_name (param, "OldParentFolderId");
	if (subparam != NULL) {
		event->old_folder_id = e_soap_parameter_get_property (subparam, "Id");
	}

	return event;
}

static EEwsNotificationEvent *
get_event_info (ESoapParameter *param,
		EEwsNotificationEventType event_type)
{
	ESoapParameter *subparam;

	subparam = e_soap_parameter_get_first_child_by_name (param, "ItemId");
	return (subparam != NULL) ? get_item_event_info (param, event_type) : get_folder_event_info (param, event_type);
}

static void
ews_notification_handle_events_param (ESoapParameter *node,
				      GSList **events)
{
	ESoapParameter *param;
	EEwsNotificationEvent *event;
	guint event_type;

	param = e_soap_parameter_get_first_child_by_name (node, "SubscriptionId");

	for (param = e_soap_parameter_get_next_child (param); param != NULL; param = e_soap_parameter_get_next_child (param)) {
		for (event_type = 0; default_events_names[event_type] != NULL; event_type++) {
			if (g_strcmp0 ((const gchar *) param->name, default_events_names[event_type]) == 0)
				break;

			continue;
		}

		if (default_events_names[event_type] == NULL)
			continue;

		if (event_type != E_EWS_NOTIFICATION_EVENT_STATUS) {
			event = get_event_info (param, event_type);
			*events = g_slist_prepend (*events, event);
		}
	}

	*events = g_slist_reverse (*events);
}

static void
ews_notification_fire_events_from_response (EEwsNotification *notification,
					    ESoapResponse *response)
{
	ESoapParameter *param, *subparam;
	GSList *events = NULL;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (
		response, "ResponseMessages", &error);

	/* Sanity check */
	g_return_if_fail (
		(param != NULL && error == NULL) ||
		(param == NULL && error != NULL));

	if (error != NULL) {
		g_warning (G_STRLOC ": %s\n", error->message);
		g_error_free (error);
		return;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, &error)) {
			g_warning (G_STRLOC ": %s\n", error->message);
			g_error_free (error);
			g_slist_free_full (events, (GDestroyNotify) e_ews_notification_event_free);
			return;
		}

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "GetStreamingEventsResponseMessage")) {
			ESoapParameter *node, *node2;

			node = e_soap_parameter_get_first_child_by_name (subparam, "Notifications");
			if (node) {
				node2 = e_soap_parameter_get_first_child_by_name (node, "Notification");
				if (node2)
					ews_notification_handle_events_param (node2, &events);
			}
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	if (events != NULL) {
		g_signal_emit_by_name (notification->priv->connection, "server-notification", events);
		g_slist_free_full (events, (GDestroyNotify) e_ews_notification_event_free);
	}
}

static void
ews_notification_soup_got_chunk (SoupMessage *msg,
				 SoupBuffer *chunk,
				 gpointer user_data)
{
	EEwsNotification *notification = user_data;
	const gchar *chunk_str;
	gsize chunk_len;
	gboolean keep_parsing = TRUE;

	/*
	 * Here we receive, in chunks, "well-formed" messages that contain:
	 * <Envelope>...</Envelope><Envelope>...</Envelope><Envelope>....
	 *
	 * We need to treat chunks which don't end with </Envelope> (it can be
	 * also in the middle of the chunk, or even cut into two pieces by chunk
	 * division -- one part already read, the other just arriving)
	 *
	 * We are parsing those chunks in the following way:
	 * 1. Append newly arrived chunk->data to notification->priv->chunk->data
	 * 2. Search for </Envelope> in notification->priv->chunk->data
	 * 3.1 </Envelope> is not found: Do nothing. Waiting for the next chunk
	 * 3.2 </Envelope> is found: Get the pair <Envelope>...</Envelope> and handle it
	 * 4. Update the notification->priv->chunk->{data,len}, removing the pair used in 3.2
	 * 5. Repeat from 2, until that 3.1 happens
	 */
	if (notification->priv->chunk == NULL)
		notification->priv->chunk = g_byte_array_new ();

	notification->priv->chunk =
		g_byte_array_append (notification->priv->chunk, (guint8 *) chunk->data, chunk->length);

	chunk_str = (gchar *) notification->priv->chunk->data;
	chunk_len = notification->priv->chunk->len;

	do {
		ESoapResponse *response;
		const gchar *end;
		gsize len;

		end = g_strstr_len (chunk_str, chunk_len, "</Envelope>");

		if (end == NULL)
			break;

		len = end + strlen ("</Envelope>") - chunk_str;

		response = e_soap_response_new_from_string (chunk_str, len);
		if (response == NULL)
			break;

		ews_notification_fire_events_from_response (notification, response);
		g_object_unref (response);

		notification->priv->chunk = g_byte_array_remove_range (notification->priv->chunk, 0, len);

		chunk_str = (gchar *) notification->priv->chunk->data;
		chunk_len = notification->priv->chunk->len;

		if (chunk_len == 0) {
			g_byte_array_free (notification->priv->chunk, TRUE);
			notification->priv->chunk = NULL;
			keep_parsing = FALSE;
		}
	} while (keep_parsing);
}

/*
 * e_ews_notification_get_events_sync
 *
 * @param notification:
 * @param pri
 * @param subscription_id
 * @param cancellable
 * @param user_data
 */
guint
e_ews_notification_get_events_sync (EEwsNotification *notification,
				    gint pri,
				    const gchar *subscription_id)
{
	CamelEwsSettings *ews_settings;
	ESoapMessage *msg;
	gchar *auth_mech = NULL;
	guint ret;

	g_return_val_if_fail (notification != NULL, SOUP_STATUS_CANCELLED);
	g_return_val_if_fail (notification->priv != NULL, SOUP_STATUS_CANCELLED);
	g_return_val_if_fail (notification->priv->connection != NULL, SOUP_STATUS_CANCELLED);

	notification->priv->msg = e_ews_message_new_with_header (
		e_ews_connection_get_uri (notification->priv->connection),
		e_ews_connection_get_impersonate_user (notification->priv->connection),
		"GetStreamingEvents",
		NULL,
		NULL,
		e_ews_connection_get_server_version (notification->priv->connection),
		E_EWS_EXCHANGE_2010_SP2,
		FALSE,
		FALSE);

	msg = notification->priv->msg;

	e_soap_message_start_element (msg, "SubscriptionIds", "messages", NULL);
	e_ews_message_write_string_parameter_with_attribute (
		msg, "SubscriptionId", NULL, subscription_id, NULL, NULL);
	e_soap_message_end_element (msg); /* SubscriptionIds */

	e_ews_message_write_string_parameter_with_attribute (
		msg, "ConnectionTimeout", "messages", "10", NULL, NULL);

	e_ews_message_write_footer (msg); /* Complete the footer and print the request */

	if (e_ews_debug_get_log_level () <= 2)
		soup_message_body_set_accumulate (SOUP_MESSAGE (msg)->response_body, FALSE);

	ews_settings = e_ews_connection_ref_settings (notification->priv->connection);

	g_object_get (G_OBJECT (ews_settings), "auth-mechanism", &auth_mech, NULL);
	if (g_strcmp0 (auth_mech, "GSSAPI") == 0)
		e_ews_connection_utils_setup_msg_gssapi_auth (
			notification->priv->connection,
			notification->priv->soup_session,
			SOUP_MESSAGE (msg));

	g_object_unref (ews_settings);
	g_free (auth_mech);

	g_signal_connect (
		SOUP_MESSAGE (msg), "got-chunk",
		G_CALLBACK (ews_notification_soup_got_chunk), notification);

	ret = soup_session_send_message (notification->priv->soup_session, SOUP_MESSAGE (msg));
	g_clear_object (&msg);

	return ret;
}

void
e_ews_notification_cancel_get_events (EEwsNotification *notification)
{
	soup_session_cancel_message (
		notification->priv->soup_session,
		SOUP_MESSAGE (notification->priv->msg),
		SOUP_STATUS_CANCELLED);
}
