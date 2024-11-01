/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include "e-ews-connection-utils.h"
#include "e-ews-debug.h"
#include "e-ews-notification.h"

struct _EEwsNotificationPrivate {
	GMutex thread_lock;
	GWeakRef connection_wk;
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

const gchar * const *
e_ews_notification_get_event_names (void)
{
	return (const gchar * const *) default_events_names;
}

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
ews_notification_dispose (GObject *object)
{
	EEwsNotification *notif = E_EWS_NOTIFICATION (object);

	if (notif->priv->cancellable != NULL)
		g_cancellable_cancel (notif->priv->cancellable);

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
	notification->priv = e_ews_notification_get_instance_private (notification);

	g_weak_ref_init (&notification->priv->connection_wk, NULL);
	g_mutex_init (&notification->priv->thread_lock);
}

static gboolean
e_ews_notification_subscribe_folder_sync (EEwsNotification *notification,
					  GSList *folder_ids, /* gchar * */
					  gchar **out_subscription_id,
					  GCancellable *cancellable)
{
	EEwsConnection *cnc;
	gboolean success;

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	cnc = e_ews_notification_ref_connection (notification);

	/* Can happen during process shutdown */
	if (!cnc)
		return FALSE;

	success = e_ews_connection_subscribe_sync (cnc, G_PRIORITY_DEFAULT, folder_ids, out_subscription_id, cancellable, NULL);

	g_signal_emit (notification, signals[SUBSCRIPTION_ID_CHANGED], 0, success ? *out_subscription_id : NULL, NULL);

	g_object_unref (cnc);

	return success;
}

static gboolean
e_ews_notification_unsubscribe_folder_sync (EEwsNotification *notification,
					    const gchar *subscription_id,
					    GCancellable *cancellable)
{
	EEwsConnection *cnc;
	gboolean success;

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	cnc = e_ews_notification_ref_connection (notification);

	/* Can happen during process shutdown */
	if (!cnc)
		return FALSE;

	success = e_ews_connection_unsubscribe_sync (cnc, G_PRIORITY_DEFAULT, subscription_id, cancellable, NULL);

	g_signal_emit (notification, signals[SUBSCRIPTION_ID_CHANGED], 0, NULL, NULL);

	g_object_unref (cnc);

	return success;
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
					    ESoapResponse *response,
					    gboolean *out_success)
{
	ESoapParameter *param, *subparam;
	GSList *events = NULL;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages", &error);

	/* Sanity check */
	g_warn_if_fail ((param != NULL && error == NULL) || (param == NULL && error != NULL));

	if (error != NULL) {
		e_ews_debug_print (G_STRLOC ": %s\n", error->message);
		g_error_free (error);
		*out_success = FALSE;
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child (param);

	while (subparam != NULL) {
		const gchar *name = (const gchar *) subparam->name;

		if (!ews_get_response_status (subparam, &error)) {
			e_ews_debug_print (G_STRLOC ": %s\n", error->message);
			*out_success =
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_EXPIREDSUBSCRIPTION) &&
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDPULLSUBSCRIPTIONID) &&
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSUBSCRIPTION) &&
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_SUBSCRIPTIONACCESSDENIED) &&
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_SUBSCRIPTIONDELEGATEACCESSNOTSUPPORTED) &&
				!g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_SUBSCRIPTIONNOTFOUND);
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
ews_notification_process_chunk (EEwsNotification *notification,
				GByteArray *chunk_data,
				GCancellable *cancellable)
{
	const gchar *chunk_str;
	gsize chunk_len;
	gboolean success = TRUE;

	/*
	 * Here we receive, in chunks, "well-formed" messages that contain:
	 * <Envelope>...</Envelope><Envelope>...</Envelope><Envelope>....
	 *
	 * We need to treat chunks which don't end with </Envelope> (it can be
	 * also in the middle of the chunk, or even cut into two pieces by chunk
	 * division -- one part already read, the other just arriving)
	 *
	 * We are parsing those chunks in the following way:
	 * 1. Search for </Envelope> in chunk_data
	 * 2.1 </Envelope> is not found: Do nothing. Waiting for the next chunk
	 * 2.2 </Envelope> is found: Get the pair <Envelope>...</Envelope> and handle it
	 * 3. Update the chunk_data, removing the pair used in 2.2
	 * 4. Repeat from 1, until the 2.1 happens
	 */

	chunk_str = (const gchar *) chunk_data->data;
	chunk_len = chunk_data->len;

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

		if (!ews_notification_fire_events_from_response (notification, response, &success)) {
			g_object_unref (response);
			break;
		}
		g_object_unref (response);

		g_byte_array_remove_range (chunk_data, 0, len);

		chunk_str = (gchar *) chunk_data->data;
		chunk_len = chunk_data->len;
	} while (chunk_len > 0 && !g_cancellable_is_cancelled (cancellable));

	return success;
}

static gboolean
e_ews_notification_get_events_sync (EEwsNotification *notification,
				    const gchar *subscription_id,
				    gboolean *out_fatal_error,
				    GCancellable *cancellable)
{
	EEwsConnection *cnc;
	ESoupSession *session = NULL;
	SoupMessage *message = NULL;
	GInputStream *input_stream = NULL;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (out_fatal_error != NULL, FALSE);

	*out_fatal_error = TRUE;

	g_return_val_if_fail (notification != NULL, FALSE);
	g_return_val_if_fail (notification->priv != NULL, FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	cnc = e_ews_notification_ref_connection (notification);

	if (!cnc)
		return FALSE;

	input_stream = e_ews_connection_prepare_streaming_events_sync (cnc, G_PRIORITY_DEFAULT, subscription_id, &session, &message, cancellable, &local_error);

	if (input_stream) {
		GByteArray *chunk_data;
		gpointer buffer;
		gssize nread;
		gboolean subscription_failed = FALSE;

		buffer = g_malloc (EWS_BUFFER_SIZE);
		chunk_data = g_byte_array_new ();

		e_ews_debug_print ("%s: %p: started reading events\n", G_STRFUNC, notification);

		/* Unref early, thus it can be freed and will cancel this thread. */
		g_clear_object (&cnc);

		while (nread = g_input_stream_read (input_stream, buffer, EWS_BUFFER_SIZE, cancellable, &local_error),
		       nread > 0 && !subscription_failed) {
			g_byte_array_append (chunk_data, buffer, nread);
			subscription_failed = !ews_notification_process_chunk (notification, chunk_data, cancellable);
		}

		e_ews_debug_print ("%s: %p: finished reading events; cancelled:%d err:%s is-partial-input:%d subscription-failed:%d\n", G_STRFUNC, notification,
			g_cancellable_is_cancelled (cancellable),
			local_error ? local_error->message : "no-err",
			g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT),
			subscription_failed);

		/* It's okay when read failed on EOF */
		*out_fatal_error = (local_error != NULL && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT)) ||
			g_cancellable_is_cancelled (cancellable);
		success = !local_error && !*out_fatal_error && !subscription_failed;

		g_byte_array_unref (chunk_data);
		g_free (buffer);
	} else {
		e_ews_debug_print ("%s: %p: failed to start reading events: %s\n", G_STRFUNC, notification, local_error ? local_error->message : "no-err");

		*out_fatal_error = !local_error || local_error->domain == E_SOUP_SESSION_ERROR || local_error->domain == G_TLS_ERROR;
		success = FALSE;
	}

	g_clear_object (&input_stream);
	g_clear_object (&message);
	g_clear_object (&session);
	g_clear_object (&cnc);
	g_clear_error (&local_error);

	return success;
}

static gpointer
e_ews_notification_get_events_thread (gpointer user_data)
{
	EEwsNotificationThreadData *td = user_data;
	gchar *subscription_id = NULL;
	gboolean ret, fatal_error = FALSE;

	g_return_val_if_fail (td != NULL, NULL);
	g_return_val_if_fail (td->notification != NULL, NULL);
	g_return_val_if_fail (td->folders != NULL, NULL);

	g_mutex_lock (&td->notification->priv->thread_lock);

	if (td->notification->priv->last_subscription_id) {
		e_ews_notification_unsubscribe_folder_sync (td->notification, td->notification->priv->last_subscription_id, td->cancellable);
		g_clear_pointer (&td->notification->priv->last_subscription_id, g_free);
	}

	if (!e_ews_notification_subscribe_folder_sync (td->notification, td->folders, &subscription_id, td->cancellable))
		goto exit;

	do {
		if (g_cancellable_is_cancelled (td->cancellable))
			goto exit;

		ret = e_ews_notification_get_events_sync (td->notification, subscription_id, &fatal_error, td->cancellable);

		if (!ret && !g_cancellable_is_cancelled (td->cancellable)) {
			e_ews_debug_print ("%s: Failed to get notification events (SubscriptionId: '%s')\n", G_STRFUNC, subscription_id);

			e_ews_notification_unsubscribe_folder_sync (td->notification, subscription_id, td->cancellable);
			g_free (subscription_id);
			subscription_id = NULL;

			if (!fatal_error) {
				ret = e_ews_notification_subscribe_folder_sync (td->notification, td->folders, &subscription_id, td->cancellable);
				if (ret) {
					e_ews_debug_print ("%s: Re-subscribed to get notifications events (SubscriptionId: '%s')\n", G_STRFUNC, subscription_id);
				} else {
					e_ews_debug_print ("%s: Failed to re-subscribed to get notifications events\n", G_STRFUNC);
				}
			}
		}
	} while (ret);

exit:
	if (subscription_id != NULL) {
		e_ews_notification_unsubscribe_folder_sync (td->notification, subscription_id, td->cancellable);
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
