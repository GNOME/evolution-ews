/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
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
#include "e-ews-connection.h"
#include "e-ews-sendoptions.h"
#include "e-ews-message.h"

G_DEFINE_TYPE (EEwsSendOptions, e_ews_sendoptions, G_TYPE_OBJECT)

struct _EEwsSendOptionsPrivate {
	EEwsSendOptionsGeneral *gopts;
	EEwsSendOptionsStatusTracking *mopts;
	EEwsSendOptionsStatusTracking *copts;
	EEwsSendOptionsStatusTracking *topts;
};

static GObjectClass *parent_class = NULL;

static gboolean e_ews_sendoptions_store_settings (SoupSoapParameter *param, EEwsSendOptions *opts);

EEwsSendOptionsGeneral*
e_ews_sendoptions_get_general_options (EEwsSendOptions *opts)
{
	g_return_val_if_fail (opts != NULL || E_IS_GW_SENDOPTIONS (opts), NULL);

	return opts->priv->gopts;
}

EEwsSendOptionsStatusTracking*
e_ews_sendoptions_get_status_tracking_options (EEwsSendOptions *opts, const gchar *type)
{
	g_return_val_if_fail (opts != NULL || E_IS_GW_SENDOPTIONS (opts), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	if (!g_ascii_strcasecmp (type, "mail"))
		return opts->priv->mopts;
	else if (!g_ascii_strcasecmp (type, "calendar"))
		return opts->priv->copts;
	else if (!g_ascii_strcasecmp (type, "task"))
		return opts->priv->topts;
	else
		return NULL;
}

static void
e_ews_sendoptions_dispose (GObject *object)
{
	EEwsSendOptions *opts = (EEwsSendOptions *) object;

	g_return_if_fail (E_IS_GW_SENDOPTIONS (opts));

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_sendoptions_finalize (GObject *object)
{
	EEwsSendOptions *opts = (EEwsSendOptions *) object;
	EEwsSendOptionsPrivate *priv;

	g_return_if_fail (E_IS_GW_SENDOPTIONS (opts));

	priv = opts->priv;

	if (priv->gopts) {
		g_free (priv->gopts);
		priv->gopts = NULL;
	}

	if (priv->mopts) {
		g_free (priv->mopts);
		priv->mopts = NULL;
	}

	if (priv->copts) {
		g_free (priv->copts);
		priv->copts = NULL;
	}

	if (priv->topts) {
		g_free (priv->topts);
		priv->topts = NULL;
	}

	if (priv) {
		g_free (priv);
		opts->priv = NULL;
	}

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_ews_sendoptions_class_init (EEwsSendOptionsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_sendoptions_dispose;
	object_class->finalize = e_ews_sendoptions_finalize;
}

static void
e_ews_sendoptions_init (EEwsSendOptions *opts)
{
	EEwsSendOptionsPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsSendOptionsPrivate, 1);
	priv->gopts = g_new0 (EEwsSendOptionsGeneral, 1);
	priv->mopts = g_new0 (EEwsSendOptionsStatusTracking, 1);
	priv->copts = g_new0 (EEwsSendOptionsStatusTracking, 1);
	priv->topts = g_new0 (EEwsSendOptionsStatusTracking, 1);
	opts->priv = priv;
}

static void
parse_status_tracking_options (SoupSoapParameter *group_param, guint i, EEwsSendOptionsStatusTracking *sopts)
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting");
			     subparam != NULL;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {

		gchar *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");

		if (field_param) {
			field = soup_soap_parameter_get_string_value (field_param);
			if (!field)
				continue;
		} else
			continue;

		if (!g_ascii_strcasecmp (field + i, "StatusInfo")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				sopts->tracking_enabled = TRUE;
				if (!strcmp (val, "Delivered"))
					sopts->track_when = E_EWS_DELIVERED;
				if (!strcmp (val, "DeliveredAndOpened"))
					sopts->track_when = E_EWS_DELIVERED_OPENED;
				if (!strcmp (val, "Full"))
					sopts->track_when = E_EWS_ALL;
				if (!strcmp (val, "None"))
					sopts->tracking_enabled = FALSE;
			} else
				sopts->tracking_enabled = FALSE;

		} else	if (!g_ascii_strcasecmp (field + i, "AutoDelete")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				if (!strcmp (val, "1"))
					sopts->autodelete = TRUE;
				else
					sopts->autodelete = FALSE;
			} else
				sopts->autodelete = FALSE;

		} else if (!g_ascii_strcasecmp (field + i, "ReturnOpen")) {
			if (val_param)
				val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->opened = E_EWS_RETURN_NOTIFY_MAIL;
			} else
				sopts->opened = E_EWS_RETURN_NOTIFY_NONE;

		} else if (!g_ascii_strcasecmp (field + i, "ReturnDelete")) {
			if (val_param)
				val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");

			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->declined = E_EWS_RETURN_NOTIFY_MAIL;
			} else
				sopts->declined = E_EWS_RETURN_NOTIFY_NONE;

		} else if (!g_ascii_strcasecmp (field + i, "ReturnAccept")) {
			if (val_param)
				val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");

			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->accepted = E_EWS_RETURN_NOTIFY_MAIL;
			} else
				sopts->accepted = E_EWS_RETURN_NOTIFY_NONE;

		} else if (!g_ascii_strcasecmp (field + i, "ReturnCompleted")) {
			if (val_param)
				val_param = soup_soap_parameter_get_first_child_by_name (val_param, "mail");

			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val && !strcmp (val, "1")) {
				sopts->completed = E_EWS_RETURN_NOTIFY_MAIL;
			} else
				sopts->completed = E_EWS_RETURN_NOTIFY_NONE;

		}
		g_free (field);
		g_free (val);
	}
}

/* These are not actually general Options. These can be configured seperatly for
   each component. Since win32 shows them as general options, we too do the same
   way. So the Options are take from the mail setttings */

static void
parse_general_options (SoupSoapParameter *group_param, EEwsSendOptionsGeneral *gopts)
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting");
			     subparam != NULL;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {
		gchar *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");

		if (field_param) {
			field = soup_soap_parameter_get_string_value (field_param);
			if (!field)
				continue;
		} else
			continue;

		if (!g_ascii_strcasecmp (field, "mailPriority")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				if (!g_ascii_strcasecmp (val, "High"))
					gopts->priority = E_EWS_PRIORITY_HIGH;
				else if (!g_ascii_strcasecmp (val, "Standard")) {
					gopts->priority = E_EWS_PRIORITY_STANDARD;
				} else if (!g_ascii_strcasecmp (val, "Low"))
					gopts->priority = E_EWS_PRIORITY_LOW;
				else
					gopts->priority = E_EWS_PRIORITY_UNDEFINED;

			} else
				gopts->priority = E_EWS_PRIORITY_UNDEFINED;
		} else if (!g_ascii_strcasecmp (field, "mailReplyRequested")) {
		       if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				if (!g_ascii_strcasecmp (val, "None"))
					gopts->reply_enabled = FALSE;
				else if (!g_ascii_strcasecmp (val, "WhenConvenient")) {
					gopts->reply_enabled = TRUE;
					gopts->reply_convenient = TRUE;
				} else {
					gchar *temp;
					gint i = 0;

					val_param = soup_soap_parameter_get_first_child_by_name (val_param, "WithinNDays");
					temp = soup_soap_parameter_get_string_value (val_param);

					if (temp)
						i = atoi (temp);

					gopts->reply_within = i;
					gopts->reply_enabled = TRUE;
					g_free (temp);
				}
			}
		} else if (!g_ascii_strcasecmp (field, "mailExpireDays")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);

			if (val) {
				gint i = atoi (val);
				if (i != 0)
					gopts->expiration_enabled = TRUE;
				else
					gopts->expiration_enabled = FALSE;

				gopts->expire_after = i;
			} else
				gopts->expiration_enabled = FALSE;
		}
		g_free (field);
		g_free (val);
	}
}

/* These settings are common to all components */

static void
parse_advanced_settings (SoupSoapParameter *group_param, EEwsSendOptionsGeneral *gopts)
{
	SoupSoapParameter *subparam, *field_param, *val_param;

	for (subparam = soup_soap_parameter_get_first_child_by_name(group_param, "setting");
			     subparam != NULL;
			     subparam = soup_soap_parameter_get_next_child_by_name (subparam, "setting")) {
		gchar *field = NULL, *val = NULL;
		field_param = soup_soap_parameter_get_first_child_by_name (subparam, "field");
		val_param = soup_soap_parameter_get_first_child_by_name (subparam, "value");

		if (field_param) {
			field = soup_soap_parameter_get_string_value (field_param);
			if (!field)
				continue;
		} else
			continue;

		if (!g_ascii_strcasecmp (field, "delayDelivery")) {
			if (val_param)
				val = soup_soap_parameter_get_string_value (val_param);
			if (val) {
				gint i = atoi (val);
				if (i > 0 ) {
					gopts->delay_enabled = TRUE;
					gopts->delay_until = i;
				} else
					gopts->delay_enabled = FALSE;
			} else
				gopts->delay_enabled = FALSE;
		}

		g_free (field);
		g_free (val);
	}
}

/* TODO have to handle the locked settings */
static gboolean
e_ews_sendoptions_store_settings (SoupSoapParameter *param, EEwsSendOptions *opts)
{
	SoupSoapParameter *group_param;
	EEwsSendOptionsPrivate *priv;

	priv = opts->priv;

	for (group_param = soup_soap_parameter_get_first_child_by_name(param, "group");
			     group_param != NULL;
			     group_param = soup_soap_parameter_get_next_child_by_name (group_param, "group")) {
		gchar *temp = NULL;

		temp = soup_soap_parameter_get_property (group_param, "type");

		if (!temp)
			continue;

		if (!g_ascii_strcasecmp (temp, "MailMessageSettings")) {
			parse_status_tracking_options (group_param, 4, priv->mopts);
			parse_general_options (group_param, priv->gopts);
		}

		if (!g_ascii_strcasecmp (temp, "AppointmentMessageSettings")) {
			parse_status_tracking_options (group_param, 11, priv->copts);
		}
		if (!g_ascii_strcasecmp (temp, "TaskMessageSettings"))
			parse_status_tracking_options (group_param, 4, priv->topts);

		if (!g_ascii_strcasecmp (temp, "AdvancedSettings"))
			parse_advanced_settings (group_param, priv->gopts);

		g_free (temp);
	}

	return TRUE;
}

static void
e_ews_sendoptions_write_settings (SoupSoapMessage *msg, const gchar *field_name, const gchar *value, const gchar *value_name, gboolean value_direct)
{
	soup_soap_message_start_element (msg, "setting", NULL, NULL);

	soup_soap_message_start_element (msg, "field", NULL, NULL);
	soup_soap_message_write_string (msg, field_name);
	soup_soap_message_end_element (msg);

	soup_soap_message_start_element (msg, "value", NULL, NULL);

	if (!value_direct)
		e_ews_message_write_string_parameter (msg, value_name, NULL, value);
	else
		soup_soap_message_write_string (msg, value);

	soup_soap_message_end_element (msg);

	soup_soap_message_end_element (msg);
}

static void
set_status_tracking_changes (SoupSoapMessage *msg, EEwsSendOptionsStatusTracking *n_sopts, EEwsSendOptionsStatusTracking *o_sopts, const gchar *comp)
{
	gchar *value, *comp_name = NULL;

	if (n_sopts->tracking_enabled != o_sopts->tracking_enabled || n_sopts->track_when != o_sopts->track_when) {
		if (n_sopts->tracking_enabled) {
			if (n_sopts->track_when == E_EWS_DELIVERED)
				value = g_strdup ("Delivered");
			else if (n_sopts->track_when == E_EWS_DELIVERED_OPENED)
				value = g_strdup ("DeliveredAndOpened");
			else
				value = g_strdup ("Full");
		} else
			value = g_strdup ("None");
		comp_name = g_strconcat (comp, "StatusInfo", NULL);
		e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
		g_free (comp_name), comp_name = NULL;
		g_free (value), value = NULL;
	}

	if (!strcmp (comp, "mail")) {
		if (n_sopts->autodelete != o_sopts->autodelete) {
			if (n_sopts->autodelete)
				value = g_strdup ("1");
			else
				value = g_strdup ("0");
			comp_name = g_strconcat (comp, "AutoDelete", NULL);
			e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
			g_free (comp_name), comp_name = NULL;
			g_free (value), value = NULL;
		}
	}

	if (n_sopts->opened != o_sopts->opened) {
		comp_name = g_strconcat (comp, "ReturnOpen", NULL);
		if (n_sopts->opened == E_EWS_RETURN_NOTIFY_MAIL) {
			value = g_strdup ("1");
			e_ews_sendoptions_write_settings (msg, comp_name, value, "mail", FALSE);
		} else {
			value = g_strdup ("None");
			e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
		}

		g_free (comp_name), comp_name = NULL;
		g_free (value), value = NULL;
	}

	if (n_sopts->declined != o_sopts->declined) {
		comp_name = g_strconcat (comp, "ReturnDelete", NULL);
		if (n_sopts->declined == E_EWS_RETURN_NOTIFY_MAIL) {
			value = g_strdup ("1");
			e_ews_sendoptions_write_settings (msg, comp_name, value, "mail", FALSE);
		} else {
			value = g_strdup ("None");
			e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
		}

		g_free (comp_name), comp_name = NULL;
		g_free (value), value = NULL;
	}

	if (!strcmp (comp, "appointment") || !strcmp (comp, "task")) {
		if (n_sopts->accepted != o_sopts->accepted) {
			comp_name = g_strconcat (comp, "ReturnAccept", NULL);
			if (n_sopts->accepted == E_EWS_RETURN_NOTIFY_MAIL) {
				value = g_strdup ("1");
				e_ews_sendoptions_write_settings (msg, comp_name, value, "mail", FALSE);
			} else {
				value = g_strdup ("None");
				e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
			}

			g_free (comp_name), comp_name = NULL;
			g_free (value), value = NULL;
		}
	}

	if (!strcmp (comp, "task")) {
		if (n_sopts->completed != o_sopts->completed) {
			comp_name = g_strconcat (comp, "ReturnCompleted", NULL);
			if (n_sopts->completed == E_EWS_RETURN_NOTIFY_MAIL) {
				value = g_strdup ("1");
				e_ews_sendoptions_write_settings (msg, comp_name, value, "mail", FALSE);
			} else {
				value = g_strdup ("None");
				e_ews_sendoptions_write_settings (msg, comp_name, value, NULL, TRUE);
			}

			g_free (comp_name), comp_name = NULL;
			g_free (value), value = NULL;
		}
	}

}

static void
set_general_options_changes (SoupSoapMessage *msg, EEwsSendOptionsGeneral *n_gopts, EEwsSendOptionsGeneral *o_gopts)
{
	gchar *value;

	if (n_gopts->priority != o_gopts->priority) {
		if (n_gopts->priority == E_EWS_PRIORITY_HIGH)
			value = g_strdup ("High");
		else if (n_gopts->priority == E_EWS_PRIORITY_STANDARD)
			value = g_strdup ("Standard");
		else if (n_gopts->priority == E_EWS_PRIORITY_LOW)
			value = g_strdup ("Low");
		else
			value = NULL;
		e_ews_sendoptions_write_settings (msg, "mailPriority", value, NULL, TRUE);
		e_ews_sendoptions_write_settings (msg, "appointmentPriority", value, NULL, TRUE);
		e_ews_sendoptions_write_settings (msg, "taskPriority", value, NULL, TRUE);
		g_free (value), value = NULL;
	}

	if (n_gopts->reply_enabled != o_gopts->reply_enabled || n_gopts->reply_convenient != o_gopts->reply_convenient ||
			n_gopts->reply_within != o_gopts->reply_within) {

		if (n_gopts->reply_enabled) {
			if (n_gopts->reply_convenient)
				value = g_strdup ("WhenConvenient");
			else
				value = g_strdup_printf ("%d", n_gopts->reply_within);
		} else
			value = g_strdup ("None");

		if (n_gopts->reply_enabled && !n_gopts->reply_convenient)
			e_ews_sendoptions_write_settings (msg, "mailReplyRequested", value, "WithinNDays" , FALSE);
		else
			e_ews_sendoptions_write_settings (msg, "mailReplyRequested", value, NULL, TRUE);

		g_free (value), value = NULL;
	}

	if (n_gopts->expiration_enabled != o_gopts->expiration_enabled || n_gopts->expire_after != o_gopts->expire_after) {
		if (n_gopts->expiration_enabled) {
			value = g_strdup_printf ("%d", n_gopts->expire_after);
		} else
			value = g_strdup ("0");

		e_ews_sendoptions_write_settings (msg, "mailExpireDays", value, NULL, TRUE);
		g_free (value), value = NULL;
	}

	if (n_gopts->delay_enabled != o_gopts->delay_enabled || n_gopts->delay_until != o_gopts->delay_until) {
		if (n_gopts->delay_enabled) {
			value = g_strdup_printf ("%d", n_gopts->delay_until);
		} else
			value = g_strdup ("-1");

		e_ews_sendoptions_write_settings (msg, "delayDelivery", value, NULL, TRUE);
		g_free (value), value = NULL;
	}
}

/* n_opts has the new options, o_opts has the old options settings */
gboolean
e_ews_sendoptions_form_message_to_modify (SoupSoapMessage *msg, EEwsSendOptions *n_opts, EEwsSendOptions *o_opts)
{
	g_return_val_if_fail (n_opts != NULL || o_opts != NULL, FALSE);
	g_return_val_if_fail (E_IS_GW_SENDOPTIONS (n_opts) || E_IS_GW_SENDOPTIONS (o_opts), FALSE);

	soup_soap_message_start_element (msg, "settings", NULL, NULL);

	set_general_options_changes (msg, n_opts->priv->gopts, o_opts->priv->gopts);
	set_status_tracking_changes (msg, n_opts->priv->mopts, o_opts->priv->mopts, "mail");
	set_status_tracking_changes (msg, n_opts->priv->copts, o_opts->priv->copts, "appointment");
	set_status_tracking_changes (msg, n_opts->priv->topts, o_opts->priv->topts, "task");

	soup_soap_message_end_element (msg);

	return TRUE;
}

EEwsSendOptions *
e_ews_sendoptions_new (void)
{
	EEwsSendOptions *opts;

	opts = g_object_new (E_TYPE_EWS_SENDOPTIONS, NULL);

	return opts;
}

EEwsSendOptions *
e_ews_sendoptions_new_from_soap_parameter (SoupSoapParameter *param)
{
	EEwsSendOptions *opts;

	g_return_val_if_fail (param != NULL, NULL);

	opts = g_object_new (E_TYPE_EWS_SENDOPTIONS, NULL);

	if (!e_ews_sendoptions_store_settings (param, opts)) {
		g_object_unref (opts);
		return NULL;
	}

	return opts;
}

