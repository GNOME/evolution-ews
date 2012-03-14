/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>

#include "ews-esource-utils.h"

ESource *
ews_find_source_by_matched_prop (GSList *sources,
                                 const gchar *prop,
                                 const gchar *value)
{
	GSList *s;

	if (!sources)
		return NULL;

	for (s = sources; s != NULL; s = g_slist_next (s)) {
		ESource *source = s->data;

		if (source && E_IS_SOURCE (source)) {
			const gchar *has_fid = e_source_get_property (source, prop);

			if (has_fid && g_str_equal (value, has_fid))
				return source;
		}
	}

	return NULL;
}

static gchar *
ews_construct_base_uri (CamelURL *account_url)
{
	gchar *base_uri;
	CamelURL *base_curi;

	g_return_val_if_fail (account_url != NULL, NULL);

	base_curi = camel_url_new ("ews://", NULL);
	g_return_val_if_fail (base_curi != NULL, NULL);

	camel_url_set_user (base_curi, account_url->user);
	camel_url_set_host (base_curi, account_url->host);

	base_uri = camel_url_to_string (base_curi, 0);

	camel_url_free (base_curi);

	return base_uri;
}

ESourceGroup *
ews_esource_utils_ensure_group (ESourceList *source_list,
                                CamelURL *account_url)
{
	ESourceGroup *group = NULL;
	gchar *base_uri;

	base_uri = ews_construct_base_uri (account_url);
	g_return_val_if_fail (base_uri != NULL, NULL);

	group = e_source_list_peek_group_by_base_uri (source_list, base_uri);
	if (group) {
		/* update group description, if base uri is "too short" */
		if (g_strcmp0 (e_source_group_peek_base_uri (group), EWS_BASE_URI) == 0) {
			e_source_list_remove_group (source_list, group);
		} else {
			g_free (base_uri);
			return g_object_ref (group);
		}
	}

	group = e_source_group_new (camel_url_get_param (account_url, "email"), base_uri);
	e_source_group_set_property (group, "create_source", "no");

	g_free (base_uri);

	if (!e_source_list_add_group (source_list, group, -1)) {
		g_warning ("Unable to add the group to the source list \n");
		g_object_unref (group);
		return NULL;
	}

	return group;
}

gboolean
ews_esource_utils_add_esource (EEwsFolder *folder,
                               const gchar *account_uri,
                               const gchar *account_name,
                               const gchar *username,
                               const gchar *email_id,
                               const gchar *hosturl,
                               gint refresh_timeout)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	EwsFolderType ftype;
	const EwsFolderId *fid;
	GConfClient * client;
	const gchar *conf_key, *selection_key = NULL;
	const gchar *source_name;
	gchar *source_uri;
	GSList *sources;
	CamelURL *account_url;
	gboolean ret = TRUE;

	ftype = e_ews_folder_get_folder_type (folder);
	source_name = e_ews_folder_get_name (folder);
	fid = e_ews_folder_get_id (folder);

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
		selection_key = SELECTED_CALENDARS;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS) {
		conf_key = TASKS_SOURCES;
		selection_key = SELECTED_TASKS;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	account_url = camel_url_new (account_uri, NULL);
	g_return_val_if_fail (account_url != NULL, FALSE);

	camel_url_set_param (account_url, "email", account_name);

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = ews_esource_utils_ensure_group (source_list, account_url);

	camel_url_free (account_url);

	sources = e_source_group_peek_sources (group);
	if (ews_find_source_by_matched_prop (sources, "folder-id", fid->id)) {
		ret = FALSE;
		goto exit;
	}

	/* We use the same URI for all calendars as for the mail, since
	 * there's no other way to set the key used for password storage
	 * in *either* calendar or mail code. Note the tricks we have to
	 * play in the calendar back end to make the cache directory
	 * unique again. */
	if (ftype == EWS_FOLDER_TYPE_CONTACTS)
		source_uri = g_strdup_printf ("%s/;folderid=%s", account_uri + strlen (EWS_BASE_URI), fid->id);
	else
		source_uri = g_strdup (account_uri + strlen (EWS_BASE_URI));

	source = e_source_new (source_name, source_uri);
	e_source_set_property (source, "username", username);
	e_source_set_property (source, "auth-domain", "Exchange Web Services");
	e_source_set_property (source, "folder-id", fid->id);
	e_source_set_property (source, "change-key", fid->change_key);
	e_source_set_property (source, "email", email_id);
	e_source_set_property (source, "hosturl", hosturl);
	e_source_set_property (source, "delete", "no");
	e_source_set_property (source, "offline_sync", "1");
	if (ftype != EWS_FOLDER_TYPE_CONTACTS)
		e_source_set_color_spec (source, "#EEBC60");

	g_free (source_uri);
	/* set props required for contacts */
	if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "completion", "true");
	} else
		e_source_set_property (source, "auth", "1");

	e_source_group_add_source (group, source, -1);
	e_source_list_sync (source_list, NULL);

	if (selection_key) {
		GSList *ids;

		ids = gconf_client_get_list (client, selection_key , GCONF_VALUE_STRING, NULL);
		ids = g_slist_append (ids, g_strdup (e_source_peek_uid (source)));
		gconf_client_set_list (client,  selection_key, GCONF_VALUE_STRING, ids, NULL);

		g_slist_foreach (ids, (GFunc) g_free, NULL);
		g_slist_free (ids);
	}
	g_object_unref (source);

exit:
	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);

	return ret;
}

/* FIXME remove cache */
gboolean
ews_esource_utils_remove_esource (const gchar *fid,
                                  CamelURL *account_url,
                                  EwsFolderType ftype)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	GConfClient * client;
	const gchar *conf_key;
	GSList *sources;
	gboolean ret = TRUE;

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS) {
		conf_key = TASKS_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = ews_esource_utils_ensure_group (source_list, account_url);

	sources = e_source_group_peek_sources (group);
	if (!(source = ews_find_source_by_matched_prop (sources, "folder-id", fid))) {
		ret = FALSE;
		goto exit;
	}

	e_source_group_remove_source (group, source);
	e_source_list_sync (source_list, NULL);

exit:
	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);

	return ret;
}

gboolean
ews_source_utils_remove_group (CamelURL *account_url,
                               EwsFolderType ftype)
{
	ESourceList *source_list;
	ESourceGroup *group;
	GConfClient * client;
	const gchar *conf_key;
	gboolean ret = TRUE;
	gchar *base_uri;

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS) {
		conf_key = TASKS_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	base_uri = ews_construct_base_uri (account_url);
	g_return_val_if_fail (base_uri != NULL, FALSE);

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = e_source_list_peek_group_by_base_uri (source_list, base_uri);

	/* there were done a change in base_uri, thus remove the old group, if any */
	if (!group)
		group = e_source_list_peek_group_by_base_uri (source_list, EWS_BASE_URI);

	if (group) {
		e_source_list_remove_group (source_list, group);
		e_source_list_sync (source_list, NULL);
	} else
		ret = FALSE;

	g_object_unref (source_list);
	g_object_unref (client);
	g_free (base_uri);

	return ret;

}

void
ews_esource_utils_remove_groups (CamelURL *account_url)
{
	ews_source_utils_remove_group (account_url, EWS_FOLDER_TYPE_CALENDAR);
	ews_source_utils_remove_group (account_url, EWS_FOLDER_TYPE_CONTACTS);
	ews_source_utils_remove_group (account_url, EWS_FOLDER_TYPE_TASKS);
}
