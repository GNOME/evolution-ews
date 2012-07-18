/*
 * e-ews-oof-settings.h
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

#ifndef E_EWS_OOF_SETTINGS_H
#define E_EWS_OOF_SETTINGS_H

#include <server/e-ews-connection.h>
#include <server/e-ews-enums.h>

/* Standard GObject macros */
#define E_TYPE_EWS_OOF_SETTINGS \
	(e_ews_oof_settings_get_type ())
#define E_EWS_OOF_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_OOF_SETTINGS, EEwsOofSettings))
#define E_EWS_OOF_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_OOF_SETTINGS, EEwsOofSettingsClass))
#define E_IS_EWS_OOF_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_OOF_SETTINGS))
#define E_IS_EWS_OOF_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_OOF_SETTINGS))
#define E_EWS_OOF_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_OOF_SETTINGS, EEwsOofSettingsClass))

G_BEGIN_DECLS

typedef struct _EEwsOofSettings EEwsOofSettings;
typedef struct _EEwsOofSettingsClass EEwsOofSettingsClass;
typedef struct _EEwsOofSettingsPrivate EEwsOofSettingsPrivate;

struct _EEwsOofSettings {
	GObject parent;
	EEwsOofSettingsPrivate *priv;
};

struct _EEwsOofSettingsClass {
	GObjectClass parent_class;
};

GType		e_ews_oof_settings_get_type	(void) G_GNUC_CONST;
EEwsOofSettings *
		e_ews_oof_settings_new_sync	(EEwsConnection *connection,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_oof_settings_new		(EEwsConnection *connection,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EEwsOofSettings *
		e_ews_oof_settings_new_finish	(GAsyncResult *result,
						 GError **error);
EEwsConnection *
		e_ews_oof_settings_get_connection
						(EEwsOofSettings *settings);
EEwsOofState	e_ews_oof_settings_get_state	(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_state	(EEwsOofSettings *settings,
						 EEwsOofState state);
EEwsExternalAudience
		e_ews_oof_settings_get_external_audience
						(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_external_audience
						(EEwsOofSettings *settings,
						 EEwsExternalAudience external_audience);
GDateTime *	e_ews_oof_settings_ref_start_time
						(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_start_time
						(EEwsOofSettings *settings,
						 GDateTime *start_time);
GDateTime *	e_ews_oof_settings_ref_end_time	(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_end_time	(EEwsOofSettings *settings,
						 GDateTime *end_time);
const gchar *	e_ews_oof_settings_get_internal_reply
						(EEwsOofSettings *settings);
gchar *		e_ews_oof_settings_dup_internal_reply
						(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_internal_reply
						(EEwsOofSettings *settings,
						 const gchar *internal_reply);
const gchar *	e_ews_oof_settings_get_external_reply
						(EEwsOofSettings *settings);
gchar *		e_ews_oof_settings_dup_external_reply
						(EEwsOofSettings *settings);
void		e_ews_oof_settings_set_external_reply
						(EEwsOofSettings *settings,
						 const gchar *external_reply);
gboolean	e_ews_oof_settings_submit_sync	(EEwsOofSettings *settings,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_oof_settings_submit	(EEwsOofSettings *settings,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_oof_settings_submit_finish
						(EEwsOofSettings *settings,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_EWS_OOF_SETTINGS_H */

