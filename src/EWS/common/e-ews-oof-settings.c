/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "ews-errors.h"
#include "e-ews-connection.h"
#include "e-ews-enumtypes.h"
#include "e-ews-request.h"

#include "e-ews-oof-settings.h"

/* Forward Declarations */
static void	e_ews_oof_settings_initable_init
					(GInitableIface *iface);

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

G_DEFINE_TYPE_WITH_CODE (EEwsOofSettings, e_ews_oof_settings, G_TYPE_OBJECT,
	G_ADD_PRIVATE (EEwsOofSettings)
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, e_ews_oof_settings_initable_init))

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
	EEwsOofSettings *settings = E_EWS_OOF_SETTINGS (object);

	g_clear_object (&settings->priv->connection);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_oof_settings_parent_class)->dispose (object);
}

static void
ews_oof_settings_finalize (GObject *object)
{
	EEwsOofSettings *settings = E_EWS_OOF_SETTINGS (object);

	g_mutex_clear (&settings->priv->property_lock);

	g_date_time_unref (settings->priv->start_time);
	g_date_time_unref (settings->priv->end_time);

	g_free (settings->priv->internal_reply);
	g_free (settings->priv->external_reply);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_oof_settings_parent_class)->finalize (object);
}

static gboolean
ews_oof_settings_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	EEwsOofSettings *settings = E_EWS_OOF_SETTINGS (initable);

	g_return_val_if_fail (settings->priv->connection != NULL, FALSE);

	return e_ews_connection_get_user_oof_settings_sync (settings->priv->connection,
		G_PRIORITY_DEFAULT, settings, cancellable, error);
}

static void
e_ews_oof_settings_class_init (EEwsOofSettingsClass *class)
{
	GObjectClass *object_class;

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
	settings->priv = e_ews_oof_settings_get_instance_private (settings);

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

typedef struct _SubmitData {
	EEwsOofState state;
	EEwsExternalAudience external_audience;
	GDateTime *date_start;
	GDateTime *date_end;
	gchar *internal_reply;
	gchar *external_reply;
} SubmitData;

static SubmitData *
submit_data_new (EEwsOofSettings *settings)
{
	SubmitData *sd;

	sd = g_slice_new0 (SubmitData);
	sd->state = e_ews_oof_settings_get_state (settings);
	sd->external_audience = e_ews_oof_settings_get_external_audience (settings);
	sd->date_start = e_ews_oof_settings_ref_start_time (settings);
	sd->date_end = e_ews_oof_settings_ref_end_time (settings);
	sd->internal_reply = e_ews_oof_settings_dup_internal_reply (settings);
	sd->external_reply = e_ews_oof_settings_dup_external_reply (settings);

	return sd;
}

static void
submit_data_free (gpointer ptr)
{
	SubmitData *sd = ptr;

	if (sd) {
		g_clear_pointer (&sd->date_start, g_date_time_unref);
		g_clear_pointer (&sd->date_end, g_date_time_unref);
		g_clear_pointer (&sd->internal_reply, g_free);
		g_clear_pointer (&sd->external_reply, g_free);
		g_slice_free (SubmitData, sd);
	}
}

static gboolean
ews_oof_settings_call_submit_sync (EEwsOofSettings *settings,
				   SubmitData *sd,
				   GCancellable *cancellable,
				   GError **error)
{
	EEwsConnection *cnc = e_ews_oof_settings_get_connection (settings);

	g_return_val_if_fail (sd != NULL, FALSE);
	g_return_val_if_fail (cnc != NULL, FALSE);

	return e_ews_connection_set_user_oof_settings_sync (cnc,
		G_PRIORITY_DEFAULT, sd->state, sd->external_audience, sd->date_start,
		sd->date_end, sd->internal_reply, sd->external_reply,
		cancellable, error);
}

gboolean
e_ews_oof_settings_submit_sync (EEwsOofSettings *settings,
                                GCancellable *cancellable,
                                GError **error)
{
	SubmitData *sd;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_OOF_SETTINGS (settings), FALSE);

	sd = submit_data_new (settings);
	success = ews_oof_settings_call_submit_sync (settings, sd, cancellable, error);
	submit_data_free (sd);

	return success;
}

static void
ews_oof_settings_submit_thread (GTask *task,
				gpointer source_object,
				gpointer task_data,
				GCancellable *cancellable)
{
	SubmitData *sd = task_data;
	GError *error = NULL;

	if (ews_oof_settings_call_submit_sync (E_EWS_OOF_SETTINGS (source_object), sd, cancellable, &error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, error);
}

void
e_ews_oof_settings_submit (EEwsOofSettings *settings,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GTask *task;
	SubmitData *sd;

	g_return_if_fail (E_IS_EWS_OOF_SETTINGS (settings));

	task = g_task_new (settings, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_ews_oof_settings_submit);

	sd = submit_data_new (settings);
	g_task_set_task_data (task, sd, submit_data_free);

	g_task_run_in_thread (task, ews_oof_settings_submit_thread);

	g_object_unref (task);
}

gboolean
e_ews_oof_settings_submit_finish (EEwsOofSettings *settings,
                                  GAsyncResult *result,
                                  GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, settings), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}
