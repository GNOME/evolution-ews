/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include "e-ews-connection-utils.h"
#include "e-ews-debug.h"
#include "e-ews-notification.h"
#include "e-soup-auth-negotiate.h"

struct _EEwsNotificationPrivate {
	GMutex thread_lock;
	SoupSession *soup_session;
	GWeakRef connection_wk;
	GByteArray *chunk;
	GCancellable *cancellable;
	gchar *last_subscription_id; /* guarded by the caller, because it can be set only after construct */
};

G_DEFINE_TYPE_WITH_PRIVATE (EEwsNotification, e_ews_notification, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_CONNECTION
};

enum {
	SUBSCRIPTION_ID_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static const gchar *default_events_names[] = {
	"CopiedEvent",
	"CreatedEvent",
	"DeletedEvent",
	"ModifiedEvent",
	"MovedEvent",
	"StatusEvent",
	NULL
};

typedef struct _EEwsNotificationThreadData EEwsNotificationThreadData;

struct _EEwsNotificationThreadData {
	EEwsNotification *notification;
	GCancellable *cancellable;
	GSList *folders;
};

static EEwsConnection *
e_ews_notification_ref_connection (const EEwsNotification *notification)
{
	g_return_val_if_fail (E_IS_EWS_NOTIFICATION (notification), NULL);
	g_return_val_if_fail (notification->priv != NULL, NULL);

	return g_weak_ref_get (&notification->priv->connection_wk);
}

static void
ews_notification_authenticate (SoupSession *session,
			       SoupMessage *message,
			       SoupAuth *auth,
			       gboolean retrying,
			       gpointer data)
{
	EEwsNotification *notification = data;
	EEwsConnection *cnc;

	g_return_if_fail (notification != NULL);

	cnc = e_ews_notification_ref_connection (notification);

	if (cnc) {
		e_ews_connection_utils_authenticate (cnc, session, message, auth, retrying);
		g_clear_object (&cnc);
	}
}

EEwsNotification *
e_ews_notification_new (EEwsConnection *connection,
			gchar *last_subscription_id)
{
	EEwsNotification *notif;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (connection), NULL);

	notif = g_object_new (
		E_TYPE_EWS_NOTIFICATION,
		"connection", connection, NULL);

	notif->priv->last_subscription_id = last_subscription_id;

	return notif;
}

static void
e_ews_notification_set_connection (EEwsNotification *notification,
				   EEwsConnection *connection)
{
	g_return_if_fail (E_IS_EWS_NOTIFICATION (notification));
	g_return_if_fail (E_IS_EWS_CONNECTION (connection));

	g_weak_ref_set (&notification->priv->connection_wk, connection);
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
				e_ews_notification_ref_connection (
				E_EWS_NOTIFICATION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_notification_constructed (GObject *object)
{
	EEwsNotification *notif;
	EEwsConnection *cnc;
	CamelEwsSettings *ews_settings;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_notification_parent_class)->constructed (object);

	notif = E_EWS_NOTIFICATION (object);
	cnc = e_ews_notification_ref_connection (notif);

	if (cnc) {
		ews_settings = e_ews_connection_ref_settings (cnc);

		e_ews_connection_utils_prepare_auth_method (notif->priv->soup_session,
			camel_ews_settings_get_auth_mechanism (ews_settings));

		g_object_unref (ews_settings);
		g_object_unref (cnc);
	}
}

static void
ews_notification_dispose (GObject *object)
{
	EEwsNotification *notif = E_EWS_NOTIFICATION (object);

	if (notif->priv->cancellable != NULL)
		g_cancellable_cancel (notif->priv->cancellable);

	if (notif->priv->soup_session != NULL) {
		g_signal_handlers_disconnect_by_func (notif->priv->soup_session, ews_notification_authenticate, object);
		g_clear_object (&notif->priv->soup_session);
	}

	g_clear_object (&notif->priv->cancellable);
	g_weak_ref_set (&notif->priv->connection_wk, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_notification_parent_class)->dispose (object);
}

static void
ews_notification_finalize (GObject *object)
{
	EEwsNotification *notif;

	notif = E_EWS_NOTIFICATION (object);

	g_weak_ref_clear (&notif->priv->connection_wk);
	g_free (notif->priv->last_subscription_id);
	g_mutex_clear (&notif->priv->thread_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_notification_parent_class)->finalize (object);
}

static void
e_ews_notification_class_init (EEwsNotificationClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_notification_set_property;
	object_class->get_property = ews_notification_get_property;
	object_class->constructed = ews_notification_constructed;
	object_class->dispose = ews_notification_dispose;
	object_class->finalize = ews_notification_finalize;

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

	signals[SUBSCRIPTION_ID_CHANGED] = g_signal_new (
		"subscription-id-changed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);
}

static void
e_ews_notification_init (EEwsNotification *notification)
{
	gint log_level;

	notification->priv = e_ews_notification_get_instance_private (notification);

	g_weak_ref_init (&notification->priv->connection_wk, NULL);
	g_mutex_init (&notification->priv->thread_lock);

	notification->priv->soup_session = soup_session_new ();

	soup_session_add_feature_by_type (notification->priv->soup_session,
					  SOUP_TYPE_COOKIE_JAR);

	log_level = e_ews_debug_get_log_level ();
	if (log_level >= 2) {
		SoupLogger *logger;
		logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);

		soup_session_add_feature (notification->priv->soup_session, SOUP_SESSION_FEATURE (logger));
		g_object_unref (logger);
	}

	g_signal_connect (notification->priv->soup_session, "authenticate", G_CALLBACK (ews_notification_authenticate), notification);
}

static gboolean
e_ews_notification_subscribe_folder_sync (EEwsNotification *notification,
					  GSList *folders,
					  gchar **subscription_id,
					  GCancellable *cancellable)
{
	EEwsConnection *cnc;
	ESoapMessage *msg;
	ESoapResponse *response;
	ESoapParameter *param, *subparam;
	CamelEwsSettings *settings;
	GError *error = NULL;
	GSList *l;
	guint event_type;
	xmlDoc *doc;
	gint log_level = e_ews_debug_get_log_level ();

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	cnc = e_ews_notification_ref_connection (notification);

	/* Can happen during process shutdown */
	if (!cnc)
		return FALSE;

	settings = e_ews_connection_ref_settings (cnc);

	msg = e_ews_message_new_with_header (
		settings,
		e_ews_connection_get_uri (cnc),
		e_ews_connection_get_impersonate_user (cnc),
		"Subscribe",
		NULL,
		NULL,
		e_ews_connection_get_server_version (cnc),
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		FALSE);

	g_clear_object (&settings);

	if (!msg) {
		g_warning ("%s: Failed to create Soup message for URI '%s'", G_STRFUNC, e_ews_connection_get_uri (cnc));
		g_object_unref (cnc);
		return FALSE;
	}

	e_soap_message_start_element (msg, "StreamingSubscriptionRequest", "messages", NULL);

	e_soap_message_start_element (msg, "FolderIds", NULL, NULL);
	for (l = folders; l; l = l->next) {
		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"FolderId",
			NULL,
			NULL,
			"Id",
			l->data);
	}
	e_soap_message_end_element (msg); /* FolderIds */

	e_soap_message_start_element (msg, "EventTypes", NULL, NULL);
	for (event_type = 0; default_events_names[event_type] != NULL; event_type++) {
		if (g_strcmp0 (default_events_names[event_type], "StatusEvent") == 0)
			continue;

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"EventType",
			NULL,
			default_events_names[event_type],
			NULL,
			NULL);
	}
	e_soap_message_end_element (msg); /* EventTypes */

	e_soap_message_end_element (msg); /* StreamingSubscriptionRequest */
	e_ews_message_write_footer (msg); /* Complete the footer and print the request */

	soup_message_body_set_accumulate (SOUP_MESSAGE (msg)->response_body, TRUE);

	if (g_cancellable_is_cancelled (cancellable)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	if (log_level >= 1 && log_level < 3) {
		e_ews_debug_dump_raw_soup_request (SOUP_MESSAGE (msg));
	}

	if (!e_ews_connection_utils_prepare_message (cnc, notification->priv->soup_session, SOUP_MESSAGE (msg), cancellable)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	soup_session_send_message (notification->priv->soup_session, SOUP_MESSAGE (msg));
	if (!SOUP_STATUS_IS_SUCCESSFUL (SOUP_MESSAGE (msg)->status_code)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	doc = xmlReadMemory (
		SOUP_MESSAGE (msg)->response_body->data,
		SOUP_MESSAGE (msg)->response_body->length,
		"response.xml", NULL, 0);

	response = e_soap_response_new_from_xmldoc (doc);

	if (log_level >= 1 && log_level < 3) {
		e_ews_debug_dump_raw_soup_response (SOUP_MESSAGE (msg));
	}
	g_object_unref (msg);
	g_object_unref (cnc);

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &error);

	/* Sanity check */
	g_warn_if_fail ((param != NULL && error == NULL) || (param == NULL && error != NULL));

	if (error != NULL) {
		g_warning (G_STRLOC ": %s\n", error->message);
		g_error_free (error);

		g_object_unref (response);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, &error)) {
			g_warning (G_STRLOC ": %s\n", error->message);
			g_error_free (error);

			g_object_unref (response);
			return FALSE;
		}

		if (E_EWS_CONNECTION_UTILS_CHECK_ELEMENT (name, "SubscribeResponseMessage")) {
			ESoapParameter *node;

			node = e_soap_parameter_get_first_child_by_name (subparam, "SubscriptionId");
			*subscription_id = e_soap_parameter_get_string_value (node);
		}

		subparam = e_soap_parameter_get_next_child (subparam);
	}

	g_object_unref (response);

	g_signal_emit (notification, signals[SUBSCRIPTION_ID_CHANGED], 0, *subscription_id, NULL);

	return TRUE;
}

static gboolean
e_ews_notification_unsubscribe_folder_sync (EEwsNotification *notification,
					    const gchar *subscription_id)
{
	EEwsConnection *cnc;
	ESoapMessage *msg;
	ESoapResponse *response;
	ESoapParameter *param;
	CamelEwsSettings *settings;
	GError *error = NULL;
	xmlDoc *doc;

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	cnc = e_ews_notification_ref_connection (notification);

	/* Can happen during process shutdown */
	if (!cnc)
		return FALSE;

	settings = e_ews_connection_ref_settings (cnc);

	msg = e_ews_message_new_with_header (
		settings,
		e_ews_connection_get_uri (cnc),
		e_ews_connection_get_impersonate_user (cnc),
		"Unsubscribe",
		NULL,
		NULL,
		e_ews_connection_get_server_version (cnc),
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		FALSE);

	g_clear_object (&settings);

	if (!msg) {
		g_warning ("%s: Failed to create Soup message for URI '%s'", G_STRFUNC, e_ews_connection_get_uri (cnc));
		g_object_unref (cnc);
		return FALSE;
	}

	e_ews_message_write_string_parameter_with_attribute (
		msg, "SubscriptionId", "messages", subscription_id, NULL, NULL);

	e_ews_message_write_footer (msg); /* Complete the footer and print the request */

	soup_message_body_set_accumulate (SOUP_MESSAGE (msg)->response_body, TRUE);

	if (!e_ews_connection_utils_prepare_message (cnc, notification->priv->soup_session, SOUP_MESSAGE (msg), notification->priv->cancellable)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	soup_session_send_message (notification->priv->soup_session, SOUP_MESSAGE (msg));
	if (!SOUP_STATUS_IS_SUCCESSFUL (SOUP_MESSAGE (msg)->status_code)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	doc = xmlReadMemory (
		SOUP_MESSAGE (msg)->response_body->data,
		SOUP_MESSAGE (msg)->response_body->length,
		"response.xml", NULL, 0);

	response = e_soap_response_new_from_xmldoc (doc);
	g_object_unref (msg);
	g_object_unref (cnc);

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &error);

	/* Sanity check */
	g_warn_if_fail ((param != NULL && error == NULL) || (param == NULL && error != NULL));

	g_object_unref (response);

	if (error != NULL) {
		g_warning (G_STRLOC ": %s\n", error->message);
		g_error_free (error);
		return FALSE;
	}

	g_signal_emit (notification, signals[SUBSCRIPTION_ID_CHANGED], 0, NULL, NULL);

	return TRUE;
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

static gboolean
ews_notification_fire_events_from_response (EEwsNotification *notification,
					    ESoapResponse *response)
{
	ESoapParameter *param, *subparam;
	GSList *events = NULL;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &error);

	/* Sanity check */
	g_warn_if_fail ((param != NULL && error == NULL) || (param == NULL && error != NULL));

	if (error != NULL) {
		g_warning (G_STRLOC ": %s\n", error->message);
		g_error_free (error);
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, &error)) {
			g_warning (G_STRLOC ": %s\n", error->message);
			g_error_free (error);
			g_slist_free_full (events, (GDestroyNotify) e_ews_notification_event_free);
			return FALSE;
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
		EEwsConnection *cnc;

		cnc = e_ews_notification_ref_connection (notification);

		if (cnc) {
			g_signal_emit_by_name (cnc, "server-notification", events);
			g_object_unref (cnc);
		}

		g_slist_free_full (events, (GDestroyNotify) e_ews_notification_event_free);
	}

	return TRUE;
}

static gboolean
ews_abort_session_idle_cb (gpointer user_data)
{
	SoupSession *session = user_data;

	g_return_val_if_fail (SOUP_IS_SESSION (session), FALSE);

	soup_session_abort (session);

	return FALSE;
}

static void
ews_notification_schedule_abort (SoupSession *session)
{
	g_return_if_fail (SOUP_IS_SESSION (session));

	g_idle_add_full (G_PRIORITY_HIGH_IDLE, ews_abort_session_idle_cb, g_object_ref (session), g_object_unref);
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
	gint log_level = e_ews_debug_get_log_level ();

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
		gboolean cancelled = FALSE;

		end = g_strstr_len (chunk_str, chunk_len, "</Envelope>");

		if (end == NULL)
			break;

		len = end + strlen ("</Envelope>") - chunk_str;

		response = e_soap_response_new_from_string (chunk_str, len);
		if (response == NULL)
			break;

		if (log_level >= 1 && log_level < 3) {
			e_ews_debug_dump_raw_soup_response (msg);
			e_soap_response_dump_response (response, stdout);
		}

		if (!ews_notification_fire_events_from_response (notification, response)) {
			ews_notification_schedule_abort (notification->priv->soup_session);

			g_object_unref (response);
			break;
		}
		g_object_unref (response);

		notification->priv->chunk = g_byte_array_remove_range (notification->priv->chunk, 0, len);

		chunk_str = (gchar *) notification->priv->chunk->data;
		chunk_len = notification->priv->chunk->len;

		cancelled = g_cancellable_is_cancelled (notification->priv->cancellable);
		if (chunk_len == 0 || cancelled) {
			g_byte_array_free (notification->priv->chunk, TRUE);
			notification->priv->chunk = NULL;
			keep_parsing = FALSE;

			if (cancelled) {
				/* Abort any pending operations, but not here, rather in another thread */
				ews_notification_schedule_abort (notification->priv->soup_session);
			}
		}
	} while (keep_parsing);
}

typedef struct _NotifcationCancelData {
	SoupSession *session;
	SoupMessage *msg;
} NotifcationCancelData;

static NotifcationCancelData *
notifcation_cancel_data_new (SoupSession *session,
			     SoupMessage *msg)
{
	NotifcationCancelData *ncd;

	ncd = g_slice_new (NotifcationCancelData);
	ncd->session = g_object_ref (session);
	ncd->msg = g_object_ref (msg);

	return ncd;
}

static void
notifcation_cancel_data_free (gpointer ptr)
{
	NotifcationCancelData *ncd = ptr;

	if (ncd) {
		g_clear_object (&ncd->session);
		g_clear_object (&ncd->msg);
		g_slice_free (NotifcationCancelData, ncd);
	}
}

static void
ews_notification_cancelled_cb (GCancellable *cancellable,
			       gpointer user_data)
{
	NotifcationCancelData *ncd = user_data;

	g_return_if_fail (ncd != NULL);

	soup_session_cancel_message (ncd->session, ncd->msg, SOUP_STATUS_CANCELLED);
}

static gboolean
e_ews_notification_get_events_sync (EEwsNotification *notification,
				    const gchar *subscription_id,
				    gboolean *out_fatal_error,
				    GCancellable *cancellable)
{
	EEwsConnection *cnc;
	ESoapMessage *msg;
	CamelEwsSettings *settings;
	gboolean ret;
	gulong handler_id, cancel_handler_id;
	guint status_code;

	g_return_val_if_fail (out_fatal_error != NULL, FALSE);

	*out_fatal_error = TRUE;

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	cnc = e_ews_notification_ref_connection (notification);

	if (!cnc)
		return FALSE;

	settings = e_ews_connection_ref_settings (cnc);

	msg = e_ews_message_new_with_header (
		settings,
		e_ews_connection_get_uri (cnc),
		e_ews_connection_get_impersonate_user (cnc),
		"GetStreamingEvents",
		NULL,
		NULL,
		e_ews_connection_get_server_version (cnc),
		E_EWS_EXCHANGE_2010_SP1,
		FALSE,
		FALSE);

	g_clear_object (&settings);

	if (!msg) {
		g_warning ("%s: Failed to create Soup message for URI '%s'", G_STRFUNC, e_ews_connection_get_uri (cnc));
		g_object_unref (cnc);
		return FALSE;
	}

	e_soap_message_start_element (msg, "SubscriptionIds", "messages", NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "SubscriptionId", NULL, subscription_id, NULL, NULL);
	e_soap_message_end_element (msg); /* SubscriptionIds */

	e_ews_message_write_string_parameter_with_attribute (msg, "ConnectionTimeout", "messages", "10", NULL, NULL);

	e_ews_message_write_footer (msg); /* Complete the footer and print the request */

	if (e_ews_debug_get_log_level () <= 2)
		soup_message_body_set_accumulate (SOUP_MESSAGE (msg)->response_body, FALSE);

	handler_id = g_signal_connect (
		SOUP_MESSAGE (msg), "got-chunk",
		G_CALLBACK (ews_notification_soup_got_chunk), notification);

	if (!e_ews_connection_utils_prepare_message (cnc, notification->priv->soup_session, SOUP_MESSAGE (msg), notification->priv->cancellable)) {
		g_object_unref (msg);
		g_object_unref (cnc);
		return FALSE;
	}

	/* Unref it early, thus it doesn't keep the connection alive after all backends are freed */
	g_object_unref (cnc);

	cancel_handler_id = g_cancellable_connect (cancellable, G_CALLBACK (ews_notification_cancelled_cb),
		notifcation_cancel_data_new (notification->priv->soup_session, SOUP_MESSAGE (msg)), notifcation_cancel_data_free);

	if (g_cancellable_is_cancelled (cancellable))
		status_code = SOUP_STATUS_CANCELLED;
	else
		status_code = soup_session_send_message (notification->priv->soup_session, SOUP_MESSAGE (msg));

	if (cancel_handler_id > 0)
		g_cancellable_disconnect (cancellable, cancel_handler_id);

	ret = SOUP_STATUS_IS_SUCCESSFUL (status_code);
	*out_fatal_error = SOUP_STATUS_IS_CLIENT_ERROR (status_code) || SOUP_STATUS_IS_SERVER_ERROR (status_code) || status_code == SOUP_STATUS_CANCELLED;

	g_signal_handler_disconnect (msg, handler_id);
	g_object_unref (msg);

	return ret;
}

static gpointer
e_ews_notification_get_events_thread (gpointer user_data)
{
	EEwsNotificationThreadData *td = user_data;
	EEwsConnection *cnc;
	gchar *subscription_id = NULL;
	gboolean ret, fatal_error = FALSE;

	g_return_val_if_fail (td != NULL, NULL);
	g_return_val_if_fail (td->notification != NULL, NULL);
	g_return_val_if_fail (td->folders != NULL, NULL);

	g_mutex_lock (&td->notification->priv->thread_lock);

	cnc = e_ews_notification_ref_connection (td->notification);

	if (cnc) {
		GProxyResolver *proxy_resolver = NULL;

		/* Skip GBinding here, due to the notification object can be freed in this thread,
		   which can cause a crash in the GBinding code. */
		g_object_get (cnc, "proxy-resolver", &proxy_resolver, NULL);
		g_object_set (td->notification->priv->soup_session, "proxy-resolver", proxy_resolver, NULL);

		g_clear_object (&proxy_resolver);
		g_object_unref (cnc);
	}

	if (td->notification->priv->last_subscription_id) {
		e_ews_notification_unsubscribe_folder_sync (td->notification, td->notification->priv->last_subscription_id);
		g_clear_pointer (&td->notification->priv->last_subscription_id, g_free);
	}

	if (!e_ews_notification_subscribe_folder_sync (td->notification, td->folders, &subscription_id, td->cancellable))
		goto exit;

	do {
		if (g_cancellable_is_cancelled (td->cancellable))
			goto exit;

		ret = e_ews_notification_get_events_sync (td->notification, subscription_id, &fatal_error, td->cancellable);

		if (!ret && !g_cancellable_is_cancelled (td->cancellable)) {
			g_debug ("%s: Failed to get notification events (SubscriptionId: '%s')", G_STRFUNC, subscription_id);

			e_ews_notification_unsubscribe_folder_sync (td->notification, subscription_id);
			g_free (subscription_id);
			subscription_id = NULL;

			if (!fatal_error) {
				ret = e_ews_notification_subscribe_folder_sync (td->notification, td->folders, &subscription_id, td->cancellable);
				if (ret) {
					g_debug ("%s: Re-subscribed to get notifications events (SubscriptionId: '%s')", G_STRFUNC, subscription_id);
				} else {
					g_debug ("%s: Failed to re-subscribed to get notifications events", G_STRFUNC);
				}
			}
		}
	} while (ret);

exit:
	if (subscription_id != NULL) {
		e_ews_notification_unsubscribe_folder_sync (td->notification, subscription_id);
		g_free (subscription_id);
	}

	g_mutex_unlock (&td->notification->priv->thread_lock);
	g_slist_free_full (td->folders, g_free);
	g_object_unref (td->cancellable);
	g_object_unref (td->notification);
	g_slice_free (EEwsNotificationThreadData, td);

	return NULL;
}

void
e_ews_notification_start_listening_sync (EEwsNotification *notification,
					 GSList *folders)
{
	EEwsNotificationThreadData *td;
	GSList *l;
	GThread *thread;

	g_return_if_fail (notification != NULL);
	g_return_if_fail (notification->priv != NULL);
	g_return_if_fail (folders != NULL);

	if (notification->priv->cancellable != NULL)
		e_ews_notification_stop_listening_sync (notification);

	notification->priv->cancellable = g_cancellable_new ();

	td = g_slice_new0 (EEwsNotificationThreadData);
	td->notification = g_object_ref (notification);
	td->cancellable = g_object_ref (notification->priv->cancellable);
	for (l = folders; l != NULL; l = l->next)
		td->folders = g_slist_prepend(td->folders, g_strdup (l->data));

	thread = g_thread_new (NULL, e_ews_notification_get_events_thread, td);
	g_thread_unref (thread);
}

void
e_ews_notification_stop_listening_sync (EEwsNotification *notification)
{
	g_return_if_fail (notification != NULL);
	g_return_if_fail (notification->priv != NULL);

	g_cancellable_cancel (notification->priv->cancellable);
	g_clear_object (&notification->priv->cancellable);
}
