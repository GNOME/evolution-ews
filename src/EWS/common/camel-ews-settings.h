/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_SETTINGS_H
#define CAMEL_EWS_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_SETTINGS \
	(camel_ews_settings_get_type ())
#define CAMEL_EWS_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_SETTINGS, CamelEwsSettings))
#define CAMEL_EWS_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_SETTINGS, CamelEwsSettingsClass))
#define CAMEL_IS_EWS_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_SETTINGS))
#define CAMEL_IS_EWS_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_SETTINGS))
#define CAMEL_EWS_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_SETTINGS))

#define MIN_CONCURRENT_CONNECTIONS 1
#define MAX_CONCURRENT_CONNECTIONS 7

G_BEGIN_DECLS

typedef struct _CamelEwsSettings CamelEwsSettings;
typedef struct _CamelEwsSettingsClass CamelEwsSettingsClass;
typedef struct _CamelEwsSettingsPrivate CamelEwsSettingsPrivate;

struct _CamelEwsSettings {
	CamelOfflineSettings parent;
	CamelEwsSettingsPrivate *priv;
};

struct _CamelEwsSettingsClass {
	CamelOfflineSettingsClass parent_class;
};

typedef enum {
	EWS_AUTH_TYPE_NTLM,
	EWS_AUTH_TYPE_BASIC,
	EWS_AUTH_TYPE_GSSAPI,
	EWS_AUTH_TYPE_OAUTH2
} EwsAuthType;


GType		camel_ews_settings_get_type	(void) G_GNUC_CONST;
void		camel_ews_settings_lock		(CamelEwsSettings *settings);
void		camel_ews_settings_unlock	(CamelEwsSettings *settings);
EwsAuthType	camel_ews_settings_get_auth_mechanism
						(CamelEwsSettings *settings);
const gchar *	camel_ews_settings_get_auth_mechanism_string
						(CamelEwsSettings *settings);
gboolean	camel_ews_settings_get_check_all
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_check_all
						(CamelEwsSettings *settings,
						 gboolean check_all);
gboolean	camel_ews_settings_get_listen_notifications
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_listen_notifications
						(CamelEwsSettings *settings,
						 gboolean listen_notifications);
const gchar *	camel_ews_settings_get_email	(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_email	(CamelEwsSettings *settings);
void		camel_ews_settings_set_email	(CamelEwsSettings *settings,
						 const gchar *email);
gboolean	camel_ews_settings_get_filter_junk
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_filter_junk
						(CamelEwsSettings *settings,
						 gboolean filter_junk);
gboolean	camel_ews_settings_get_filter_junk_inbox
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_filter_junk_inbox
						(CamelEwsSettings *settings,
						 gboolean filter_junk_inbox);
const gchar *	camel_ews_settings_get_gal_uid	(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_gal_uid	(CamelEwsSettings *settings);
void		camel_ews_settings_set_gal_uid	(CamelEwsSettings *settings,
						 const gchar *gal_uid);
const gchar *	camel_ews_settings_get_hosturl	(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_hosturl	(CamelEwsSettings *settings);
void		camel_ews_settings_set_hosturl	(CamelEwsSettings *settings,
						 const gchar *hosturl);
const gchar *	camel_ews_settings_get_oaburl	(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oaburl	(CamelEwsSettings *settings);
void		camel_ews_settings_set_oaburl	(CamelEwsSettings *settings,
						 const gchar *oaburl);
gboolean	camel_ews_settings_get_oab_offline
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oab_offline
						(CamelEwsSettings *settings,
						 gboolean oab_offline);
const gchar *	camel_ews_settings_get_oal_selected
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oal_selected
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oal_selected
						(CamelEwsSettings *settings,
						 const gchar *oal_selected);
guint		camel_ews_settings_get_timeout	(CamelEwsSettings *settings);
void		camel_ews_settings_set_timeout	(CamelEwsSettings *settings,
						 guint timeout);
gboolean	camel_ews_settings_get_use_impersonation
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_use_impersonation
						(CamelEwsSettings *settings,
						 gboolean use_impersonation);
const gchar *	camel_ews_settings_get_impersonate_user
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_impersonate_user
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_impersonate_user
						(CamelEwsSettings *settings,
						 const gchar *impersonate_user);
gboolean	camel_ews_settings_get_override_user_agent
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_override_user_agent
						(CamelEwsSettings *settings,
						 gboolean override_user_agent);
const gchar *	camel_ews_settings_get_user_agent
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_user_agent
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_user_agent
						(CamelEwsSettings *settings,
						 const gchar *user_agent);
gboolean	camel_ews_settings_get_override_oauth2
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_override_oauth2
						(CamelEwsSettings *settings,
						 gboolean override_oauth2);
const gchar *	camel_ews_settings_get_oauth2_tenant
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oauth2_tenant
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oauth2_tenant
						(CamelEwsSettings *settings,
						 const gchar *tenant);
const gchar *	camel_ews_settings_get_oauth2_client_id
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oauth2_client_id
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oauth2_client_id
						(CamelEwsSettings *settings,
						 const gchar *client_id);
const gchar *	camel_ews_settings_get_oauth2_redirect_uri
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oauth2_redirect_uri
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oauth2_redirect_uri
						(CamelEwsSettings *settings,
						 const gchar *redirect_uri);
const gchar *	camel_ews_settings_get_oauth2_resource_uri
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oauth2_resource_uri
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oauth2_resource_uri
						(CamelEwsSettings *settings,
						 const gchar *resource_uri);
const gchar *	camel_ews_settings_get_oauth2_endpoint_host
						(CamelEwsSettings *settings);
gchar *		camel_ews_settings_dup_oauth2_endpoint_host
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_oauth2_endpoint_host
						(CamelEwsSettings *settings,
						 const gchar *endpoint_host);
gboolean	camel_ews_settings_get_show_public_folders
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_show_public_folders
						(CamelEwsSettings *settings,
						 gboolean show_public_folders);
guint		camel_ews_settings_get_concurrent_connections
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_concurrent_connections
						(CamelEwsSettings *settings,
						 guint concurrent_connections);
guint		camel_ews_settings_get_sync_tag_stamp
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_sync_tag_stamp
						(CamelEwsSettings *settings,
						 guint value);
void		camel_ews_settings_inc_sync_tag_stamp
						(CamelEwsSettings *settings);
gboolean	camel_ews_settings_get_force_http1
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_force_http1
						(CamelEwsSettings *settings,
						 gboolean force_http1);
gboolean	camel_ews_settings_get_use_oauth2_v2
						(CamelEwsSettings *settings);
void		camel_ews_settings_set_use_oauth2_v2
						(CamelEwsSettings *settings,
						 gboolean use_oauth2_v2);
G_END_DECLS

#endif /* CAMEL_EWS_SETTINGS_H */
