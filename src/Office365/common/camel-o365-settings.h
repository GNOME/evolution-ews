/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CAMEL_O365_SETTINGS_H
#define CAMEL_O365_SETTINGS_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_O365_SETTINGS \
	(camel_o365_settings_get_type ())
#define CAMEL_O365_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_SETTINGS, CamelO365Settings))
#define CAMEL_O365_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_SETTINGS, CamelO365SettingsClass))
#define CAMEL_IS_O365_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_SETTINGS))
#define CAMEL_IS_O365_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_SETTINGS))
#define CAMEL_O365_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_SETTINGS))

#define MIN_CONCURRENT_CONNECTIONS 1
#define MAX_CONCURRENT_CONNECTIONS 7

G_BEGIN_DECLS

/* Forward declaration, to not include libedataserver.h/libebackend.h here */
struct _EBackend;
struct _ESourceRegistry;

typedef struct _CamelO365Settings CamelO365Settings;
typedef struct _CamelO365SettingsClass CamelO365SettingsClass;
typedef struct _CamelO365SettingsPrivate CamelO365SettingsPrivate;

struct _CamelO365Settings {
	CamelOfflineSettings parent;
	CamelO365SettingsPrivate *priv;
};

struct _CamelO365SettingsClass {
	CamelOfflineSettingsClass parent_class;
};

GType		camel_o365_settings_get_type	(void) G_GNUC_CONST;
CamelO365Settings *
		camel_o365_settings_get_from_backend
						(struct _EBackend *backend,
						 struct _ESourceRegistry *registry);
gboolean	camel_o365_settings_get_check_all
						(CamelO365Settings *settings);
void		camel_o365_settings_set_check_all
						(CamelO365Settings *settings,
						 gboolean check_all);
const gchar *	camel_o365_settings_get_email	(CamelO365Settings *settings);
gchar *		camel_o365_settings_dup_email	(CamelO365Settings *settings);
void		camel_o365_settings_set_email	(CamelO365Settings *settings,
						 const gchar *email);
gboolean	camel_o365_settings_get_filter_junk
						(CamelO365Settings *settings);
void		camel_o365_settings_set_filter_junk
						(CamelO365Settings *settings,
						 gboolean filter_junk);
gboolean	camel_o365_settings_get_filter_junk_inbox
						(CamelO365Settings *settings);
void		camel_o365_settings_set_filter_junk_inbox
						(CamelO365Settings *settings,
						 gboolean filter_junk_inbox);
guint		camel_o365_settings_get_timeout	(CamelO365Settings *settings);
void		camel_o365_settings_set_timeout	(CamelO365Settings *settings,
						 guint timeout);
gboolean	camel_o365_settings_get_override_oauth2
						(CamelO365Settings *settings);
void		camel_o365_settings_set_override_oauth2
						(CamelO365Settings *settings,
						 gboolean override_oauth2);
const gchar *	camel_o365_settings_get_oauth2_tenant
						(CamelO365Settings *settings);
gchar *		camel_o365_settings_dup_oauth2_tenant
						(CamelO365Settings *settings);
void		camel_o365_settings_set_oauth2_tenant
						(CamelO365Settings *settings,
						 const gchar *tenant);
const gchar *	camel_o365_settings_get_oauth2_client_id
						(CamelO365Settings *settings);
gchar *		camel_o365_settings_dup_oauth2_client_id
						(CamelO365Settings *settings);
void		camel_o365_settings_set_oauth2_client_id
						(CamelO365Settings *settings,
						 const gchar *client_id);
const gchar *	camel_o365_settings_get_oauth2_redirect_uri
						(CamelO365Settings *settings);
gchar *		camel_o365_settings_dup_oauth2_redirect_uri
						(CamelO365Settings *settings);
void		camel_o365_settings_set_oauth2_redirect_uri
						(CamelO365Settings *settings,
						 const gchar *redirect_uri);
guint		camel_o365_settings_get_concurrent_connections
						(CamelO365Settings *settings);
void		camel_o365_settings_set_concurrent_connections
						(CamelO365Settings *settings,
						 guint concurrent_connections);

G_END_DECLS

#endif /* CAMEL_O365_SETTINGS_H */
