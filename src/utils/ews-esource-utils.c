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
ews_find_source_by_matched_prop (GSList *sources, const gchar *prop, const gchar *value)
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

static ESourceGroup *
ews_find_group (GSList *groups, const gchar *account_name)
{
	GSList *p;
	gint len;

	len = strlen (EWS_BASE_URI);

	for (p = groups; p != NULL; p = g_slist_next (p)) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		const gchar *buri = e_source_group_peek_base_uri (group);
		const gchar *name = e_source_group_peek_name (group);

		if (buri && !g_ascii_strncasecmp (buri, EWS_BASE_URI, len) &&
		    !g_ascii_strcasecmp (name, account_name))
			return group;
	}

	return NULL;
}

ESourceGroup *
ews_esource_utils_ensure_group (ESourceList *source_list, const gchar *account_name)
{
	ESourceGroup *group = NULL;
	GSList *groups;

	groups = e_source_list_peek_groups (source_list);
	group = ews_find_group (groups, account_name);
	if (group)
		return g_object_ref (group);

	group = e_source_group_new (account_name, EWS_BASE_URI);
	e_source_group_set_property (group, "create_source", "no");

	if (!e_source_list_add_group (source_list, group, -1)) {
		g_warning ("Unable to add the group to the source list \n");
		g_object_unref (group);
		return NULL;
	}

	e_source_list_sync (source_list, NULL);
	return group;
}

gboolean
ews_esource_utils_add_esource	(EEwsFolder *folder,
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
	GConfClient* client;
	const gchar *conf_key, *selection_key = NULL;
	const gchar *source_name;
	gchar *source_uri;
	GSList *sources;
	gboolean ret = TRUE;

	ftype = e_ews_folder_get_folder_type (folder);
	source_name = e_ews_folder_get_name (folder);
	fid = e_ews_folder_get_id (folder);

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
		selection_key = SELECTED_CALENDARS;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS){
		conf_key = TASKS_SOURCES;
		selection_key = SELECTED_TASKS;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = ews_esource_utils_ensure_group (source_list, account_name);

	sources = e_source_group_peek_sources (group);
	if (ews_find_source_by_matched_prop (sources, "folder-id", fid->id)) {
		ret = FALSE;
		goto exit;
	}

	/* We use the same URI for all calendars as for the mail, since
	   there's no other way to set the key used for password storage
	   in *either* calendar or mail code. Note the tricks we have to
	   play in the calendar back end to make the cache directory
	   unique again. */
	if (ftype == EWS_FOLDER_TYPE_CONTACTS)
		source_uri = g_strdup_printf ("%s?folderid=%s", account_uri + strlen (EWS_BASE_URI), fid->id);
	else
		source_uri = g_strdup (account_uri + strlen (EWS_BASE_URI));

	source = e_source_new (source_name, source_uri);
	e_source_set_property (source, "username", username);
	e_source_set_property (source, "auth-domain", "Ews");
	e_source_set_property (source, "folder-id", fid->id);
	e_source_set_property (source, "change-key", fid->change_key);
	e_source_set_property (source, "email", email_id);
	e_source_set_property (source, "hosturl", hosturl);
	e_source_set_property (source, "delete", "no");
	e_source_set_property (source, "offline_sync", "1");
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
ews_esource_utils_remove_esource	(const gchar *fid,
					 const gchar *account_name,
					 EwsFolderType ftype)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	GConfClient* client;
	const gchar *conf_key;
	GSList *sources;
	gboolean ret = TRUE;

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS){
		conf_key = TASKS_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	group = ews_esource_utils_ensure_group (source_list, account_name);

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
ews_source_utils_remove_group (const gchar *account_name, EwsFolderType ftype)
{
	ESourceList *source_list;
	ESourceGroup *group;
	GSList *groups;
	GConfClient* client;
	const gchar *conf_key;
	gboolean ret = TRUE;

	if (ftype == EWS_FOLDER_TYPE_CALENDAR) {
		conf_key = CALENDAR_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_TASKS){
		conf_key = TASKS_SOURCES;
	} else if (ftype == EWS_FOLDER_TYPE_CONTACTS) {
		conf_key = CONTACT_SOURCES;
	} else
		return FALSE;

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (source_list);
	group = ews_find_group (groups, account_name);

	if (group) {
		e_source_list_remove_group (source_list, group);
		e_source_list_sync (source_list, NULL);
	} else
		ret = FALSE;


	g_object_unref (source_list);
	g_object_unref (client);

	return ret;

}

void
ews_esource_utils_remove_groups	(const gchar *account_name)
{
	ews_source_utils_remove_group (account_name, EWS_FOLDER_TYPE_CALENDAR);
	ews_source_utils_remove_group (account_name, EWS_FOLDER_TYPE_CONTACTS);
	ews_source_utils_remove_group (account_name, EWS_FOLDER_TYPE_TASKS);
}
