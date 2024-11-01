/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_SETTINGS_H
#define CAMEL_M365_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_M365_SETTINGS \
	(camel_m365_settings_get_type ())
#define CAMEL_M365_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_SETTINGS, CamelM365Settings))
#define CAMEL_M365_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_SETTINGS, CamelM365SettingsClass))
#define CAMEL_IS_M365_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_SETTINGS))
#define CAMEL_IS_M365_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_SETTINGS))
#define CAMEL_M365_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_SETTINGS))

#define MIN_CONCURRENT_CONNECTIONS 1
#define MAX_CONCURRENT_CONNECTIONS 7

G_BEGIN_DECLS

/* Forward declaration, to not include libedataserver.h/libebackend.h here */
struct _EBackend;
struct _ESourceRegistry;

typedef struct _CamelM365Settings CamelM365Settings;
typedef struct _CamelM365SettingsClass CamelM365SettingsClass;
typedef struct _CamelM365SettingsPrivate CamelM365SettingsPrivate;

struct _CamelM365Settings {
	CamelOfflineSettings parent;
	CamelM365SettingsPrivate *priv;
};

struct _CamelM365SettingsClass {
	CamelOfflineSettingsClass parent_class;
};

GType		camel_m365_settings_get_type	(void) G_GNUC_CONST;
CamelM365Settings *
		camel_m365_settings_get_from_backend
						(struct _EBackend *backend,
						 struct _ESourceRegistry *registry);
void		camel_m365_settings_lock	(CamelM365Settings *settings);
void		camel_m365_settings_unlock	(CamelM365Settings *settings);
gboolean	camel_m365_settings_get_use_impersonation
						(CamelM365Settings *settings);
void		camel_m365_settings_set_use_impersonation
						(CamelM365Settings *settings,
						 gboolean use_impersonation);
const gchar *	camel_m365_settings_get_impersonate_user
						(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_impersonate_user
						(CamelM365Settings *settings);
void		camel_m365_settings_set_impersonate_user
						(CamelM365Settings *settings,
						 const gchar *impersonate_user);
gboolean	camel_m365_settings_get_check_all
						(CamelM365Settings *settings);
void		camel_m365_settings_set_check_all
						(CamelM365Settings *settings,
						 gboolean check_all);
const gchar *	camel_m365_settings_get_email	(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_email	(CamelM365Settings *settings);
void		camel_m365_settings_set_email	(CamelM365Settings *settings,
						 const gchar *email);
gboolean	camel_m365_settings_get_filter_junk
						(CamelM365Settings *settings);
void		camel_m365_settings_set_filter_junk
						(CamelM365Settings *settings,
						 gboolean filter_junk);
gboolean	camel_m365_settings_get_filter_junk_inbox
						(CamelM365Settings *settings);
void		camel_m365_settings_set_filter_junk_inbox
						(CamelM365Settings *settings,
						 gboolean filter_junk_inbox);
guint		camel_m365_settings_get_timeout	(CamelM365Settings *settings);
void		camel_m365_settings_set_timeout	(CamelM365Settings *settings,
						 guint timeout);
gboolean	camel_m365_settings_get_override_oauth2
						(CamelM365Settings *settings);
void		camel_m365_settings_set_override_oauth2
						(CamelM365Settings *settings,
						 gboolean override_oauth2);
const gchar *	camel_m365_settings_get_oauth2_tenant
						(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_oauth2_tenant
						(CamelM365Settings *settings);
void		camel_m365_settings_set_oauth2_tenant
						(CamelM365Settings *settings,
						 const gchar *tenant);
const gchar *	camel_m365_settings_get_oauth2_client_id
						(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_oauth2_client_id
						(CamelM365Settings *settings);
void		camel_m365_settings_set_oauth2_client_id
						(CamelM365Settings *settings,
						 const gchar *client_id);
const gchar *	camel_m365_settings_get_oauth2_redirect_uri
						(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_oauth2_redirect_uri
						(CamelM365Settings *settings);
void		camel_m365_settings_set_oauth2_redirect_uri
						(CamelM365Settings *settings,
						 const gchar *redirect_uri);
const gchar *	camel_m365_settings_get_oauth2_endpoint_host
						(CamelM365Settings *settings);
gchar *		camel_m365_settings_dup_oauth2_endpoint_host
						(CamelM365Settings *settings);
void		camel_m365_settings_set_oauth2_endpoint_host
						(CamelM365Settings *settings,
						 const gchar *endpoint_host);
guint		camel_m365_settings_get_concurrent_connections
						(CamelM365Settings *settings);
void		camel_m365_settings_set_concurrent_connections
						(CamelM365Settings *settings,
						 guint concurrent_connections);

G_END_DECLS

#endif /* CAMEL_M365_SETTINGS_H */
