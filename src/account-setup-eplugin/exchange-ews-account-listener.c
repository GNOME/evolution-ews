/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-ews-account-listener.h"
#include "exchange-ews-account-setup.h"
#include <string.h>
#include <glib/gi18n-lib.h>
#include <camel/camel.h>
#include <libedataserverui/e-passwords.h>
#include <libedataserver/e-account.h>
#include <libecal/e-cal.h>
#include <libedataserver/e-account-list.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>

#include <exchange-ews-folder.h>
#include <exchange-ews-connection.h>
#include <exchange-ews-utils.h>

#define d(x) x

G_DEFINE_TYPE (ExchangeEWSAccountListener, exchange_ews_account_listener, G_TYPE_OBJECT)

static gboolean create_profile_entry (CamelURL *url, EAccount *account);

struct _ExchangeEWSAccountListenerPrivate {
	GConfClient *gconf_client;
	/* we get notification about mail account changes from this object */
	EAccountList *account_list;
};

typedef struct _ExchangeEWSAccountInfo ExchangeEWSAccountInfo;

/* stores some info about all currently existing ews accounts */
struct _ExchangeEWSAccountInfo {
	gchar *uid;
	gchar *name;
	gchar *source_url;
	gboolean enabled;
};

/* list of ExchangeEWSAccountInfo structures */
static	GList *ews_accounts = NULL;

static GObjectClass *parent_class = NULL;

static void
dispose (GObject *object)
{
	ExchangeEWSAccountListener *config_listener = EXCHANGE_EWS_ACCOUNT_LISTENER (object);

	g_object_unref (config_listener->priv->gconf_client);
	g_object_unref (config_listener->priv->account_list);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeEWSAccountListener *config_listener = EXCHANGE_EWS_ACCOUNT_LISTENER (object);
	GList *list;

	if (config_listener->priv) {
		g_free (config_listener->priv);
	}

	for (list = g_list_first (ews_accounts); list; list = g_list_next (list)) {
		ExchangeEWSAccountInfo *info = (ExchangeEWSAccountInfo *)(list->data);
		if (info) {
			g_free (info->uid);
			g_free (info->name);
			g_free (info->source_url);
			g_free (info);
		}
	}

	g_list_free (ews_accounts);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
exchange_ews_account_listener_class_init (ExchangeEWSAccountListenerClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	object_class = G_OBJECT_CLASS (class);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
exchange_ews_account_listener_init (ExchangeEWSAccountListener *config_listener)
{
	config_listener->priv = g_new0 (ExchangeEWSAccountListenerPrivate, 1);
}

/*determines whehter the passed in account is exchange or not by looking at source url */

static gboolean
is_ews_account (EAccount *account)
{
	return (account->source->url && (g_ascii_strncasecmp (account->source->url, EWS_URI_PREFIX, EWS_PREFIX_LENGTH) == 0));
}

/* looks up for an existing exchange account info in the ews_accounts list based on uid */

static ExchangeEWSAccountInfo*
lookup_account_info (const gchar *key)
{
	GList *list;

	g_return_val_if_fail (key != NULL, NULL);

	for (list = g_list_first (ews_accounts); list; list = g_list_next (list)) {
		ExchangeEWSAccountInfo *info = (ExchangeEWSAccountInfo *)(list->data);
		if (g_ascii_strcasecmp (info->uid, key) == 0)
			return info;
	}

	return NULL;
}

static ESource *
find_source_by_fid (GSList *sources, const gchar *fid)
{
	GSList *s;

	g_return_val_if_fail (fid != NULL, NULL);

	if (!sources)
		return NULL;

	for (s = sources; s; s = s->next) {
		ESource *source = s->data;

		if (source && E_IS_SOURCE (source)) {
			const gchar *has_fid = e_source_get_property (source, "folder-id");

			if (has_fid && g_str_equal (fid, has_fid))
				return source;
		}
	}

	return NULL;
}

#define CALENDAR_SOURCES	"/apps/evolution/calendar/sources"
#define TASK_SOURCES		"/apps/evolution/tasks/sources"
#define JOURNAL_SOURCES		"/apps/evolution/memos/sources"
#define SELECTED_CALENDARS	"/apps/evolution/calendar/display/selected_calendars"
#define SELECTED_TASKS		"/apps/evolution/calendar/tasks/selected_tasks"
#define SELECTED_JOURNALS	"/apps/evolution/calendar/memos/selected_memos"
#define ADDRESSBOOK_SOURCES     "/apps/evolution/addressbook/sources"

static void
add_cal_esource (EAccount *account, GSList *folders, ExchangeEWSFolderType folder_type, CamelURL *url, ews_id_t trash_fid)
{
	ESourceList *source_list = NULL;
	ESourceGroup *group = NULL;
	const gchar *conf_key = NULL, *source_selection_key = NULL;
	GConfClient* client;
	GSList *ids, *temp_list, *old_sources = NULL;
	gchar *base_uri = NULL;
	gboolean is_new_group = FALSE;
	

	if (folder_type == EWS_FOLDER_TYPE_APPOINTMENT) {
		conf_key = CALENDAR_SOURCES;
		source_selection_key = SELECTED_CALENDARS;
	} else if (folder_type == EWS_FOLDER_TYPE_TASK) {
		conf_key = TASK_SOURCES;
		source_selection_key = SELECTED_TASKS;
	} else if (folder_type == EWS_FOLDER_TYPE_MEMO) {
		conf_key = JOURNAL_SOURCES;
		source_selection_key = SELECTED_JOURNALS;
	} else {
		g_warning ("%s: %s: Unknown ExchangeEWSFolderType\n", G_STRLOC, G_STRFUNC);
		return;
	}

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	base_uri = g_strdup_printf ("%s%s@%s/", EWS_URI_PREFIX, url->user, url->host);
	group = e_source_list_peek_group_by_base_uri (source_list, base_uri);
	if (group) {
		e_source_group_set_name (group, account->name);
		g_object_ref (group);
		is_new_group = FALSE;
		old_sources = NULL;
		for (temp_list = e_source_group_peek_sources (group); temp_list; temp_list = temp_list->next) {
			old_sources = g_slist_prepend (old_sources, temp_list->data);
		}
	} else {
		group = e_source_group_new (account->name, base_uri);
		is_new_group = TRUE;
		old_sources = NULL;
	}
	g_free (base_uri);
	e_source_group_set_property (group, "create_source", "yes");
	e_source_group_set_property (group, "username", url->user);
	e_source_group_set_property (group, "host", url->host);
	e_source_group_set_property (group, "profile", camel_url_get_param (url, "profile"));
	e_source_group_set_property (group, "domain", camel_url_get_param (url, "domain"));

	/* We set these because on new folder creation - these are required. */
	e_source_group_set_property (group, "acl-user-name", account->id->name);
	e_source_group_set_property (group, "acl-user-email", account->id->address);
	e_source_group_set_property (group, "acl-owner-name", account->id->name);
	e_source_group_set_property (group, "acl-owner-email", account->id->address);

	for (temp_list = folders; temp_list != NULL; temp_list = g_slist_next (temp_list)) {
		ExchangeEWSFolder *folder = temp_list->data;
		ESource *source = NULL;
		gchar *relative_uri = NULL, *fid = NULL;
		gboolean is_new_source = FALSE;

		if (folder->container_class != folder_type || trash_fid == exchange_ews_folder_get_parent_id (folder))
			continue;

		fid = exchange_ews_util_ews_id_to_string (folder->folder_id);
		relative_uri = g_strconcat (";", fid, NULL);
		source = find_source_by_fid (old_sources, fid);
		if (source) {
			is_new_source = FALSE;
			g_object_ref (source);
			old_sources = g_slist_remove (old_sources, source);
			e_source_set_name (source, folder->folder_name);
			e_source_set_relative_uri (source, relative_uri);
		} else {
			source = e_source_new (folder->folder_name, relative_uri);
			is_new_source = TRUE;
		}
		e_source_set_property (source, "auth", "1");
		e_source_set_property (source, "auth-domain", EXCHANGE_EWS_PASSWORD_COMPONENT);
		e_source_set_property (source, "auth-type", "plain/password");
		e_source_set_property (source, "username", url->user);
		e_source_set_property (source, "host", url->host);
		e_source_set_property (source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property (source, "domain", camel_url_get_param (url, "domain"));
		e_source_set_property (source, "folder-id", fid);
		e_source_set_property (source, "public", "no");
		e_source_set_property (source, "offline_sync",
					camel_url_get_param (url, "offline_sync") ? "1" : "0");

		if (folder->is_default)
			e_source_set_property (source, "delete", "no");

		if (folder->parent_folder_id) {
			gchar *tmp = exchange_ews_util_ews_id_to_string (folder->parent_folder_id);
			e_source_set_property (source, "parent-fid", tmp);
			g_free (tmp);
		}

		e_source_set_property (source, "acl-user-name", account->id->name);
		e_source_set_property (source, "acl-user-email", account->id->address);
		/* FIXME: this would change after foreign folders/delegation is implemented */
		e_source_set_property (source, "acl-owner-name", account->id->name);
		e_source_set_property (source, "acl-owner-email", account->id->address);

		if (is_new_source)
			e_source_group_add_source (group, source, -1);

		if (source_selection_key && folder->is_default) {
			ids = gconf_client_get_list (client, source_selection_key , GCONF_VALUE_STRING, NULL);
			ids = g_slist_append (ids, g_strdup (e_source_peek_uid (source)));
			gconf_client_set_list (client, source_selection_key, GCONF_VALUE_STRING, ids, NULL);

			g_slist_foreach (ids, (GFunc) g_free, NULL);
			g_slist_free (ids);
		}

		g_object_unref (source);
		g_free (relative_uri);
		g_free (fid);
	}

	if (old_sources) {
		/* these were not found on the server by fid, thus remove them */
		for (temp_list = old_sources; temp_list; temp_list = temp_list->next) {
			ESource *source = temp_list->data;
		
			if (source && E_IS_SOURCE (source)) {
				if (strcmp (e_source_get_property (source, "public"), "yes") != 0)
					e_source_group_remove_source (group, source);
			}
		}

		g_slist_free (old_sources);
	}

	if (is_new_group && !e_source_list_add_group (source_list, group, -1))
		g_warning ("%s: Failed to add new group", G_STRFUNC);

	if (!e_source_list_sync (source_list, NULL))
		g_warning ("%s: Failed to sync source list", G_STRFUNC);

	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);
}

void exchange_ews_add_esource (CamelURL *url, const gchar *folder_name, const gchar *fid, gint folder_type)
{
	ESourceList *source_list = NULL;
	ESourceGroup *group = NULL;
	const gchar *conf_key = NULL;
	GConfClient* client;
	GSList *sources;
	ESource *source = NULL;
	gchar *relative_uri = NULL;
	gchar *base_uri = NULL;
	
	if (url == NULL)
		return;
	
	if (folder_type == EWS_FOLDER_TYPE_APPOINTMENT) 
		conf_key = CALENDAR_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_TASK) 
		conf_key = TASK_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_MEMO) 
		conf_key = JOURNAL_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_JOURNAL)
		conf_key = JOURNAL_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_CONTACT)
		conf_key = ADDRESSBOOK_SOURCES;
	else {
		g_warning ("%s: %s: Unknown ExchangeEWSFolderType\n", G_STRLOC, G_STRFUNC);
		return;
	}

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	base_uri = g_strdup_printf ("%s%s@%s/", EWS_URI_PREFIX, url->user, url->host);
	group = e_source_list_peek_group_by_base_uri (source_list, base_uri);
	sources = e_source_group_peek_sources (group);	
	for (; sources != NULL; sources = g_slist_next (sources)) {
		ESource *source = E_SOURCE (sources->data);
		gchar* folder_id = e_source_get_duped_property (source, "folder-id");
		if (folder_id && fid) { 
			if (strcmp (fid, folder_id) != 0) 
				continue;
			else {
				g_warning ("%s: %s: Esource Already exist \n", G_STRLOC, G_STRFUNC);
				return;	
			}
		}
	}

	
	relative_uri = g_strconcat (";", fid, NULL);
	source = e_source_new (folder_name, relative_uri);
	e_source_set_property (source, "auth", "1");
	e_source_set_property (source, "auth-domain", EXCHANGE_EWS_PASSWORD_COMPONENT);
	e_source_set_property (source, "auth-type", "plain/password");
	e_source_set_property (source, "username", url->user);
	e_source_set_property (source, "host", url->host);
	e_source_set_property (source, "profile", camel_url_get_param (url, "profile"));
	e_source_set_property (source, "domain", camel_url_get_param (url, "domain"));
	e_source_set_property (source, "folder-id", fid);
	e_source_set_property (source, "offline_sync",
				camel_url_get_param (url, "offline_sync") ? "1" : "0");
	e_source_set_property (source, "public", "yes");
	e_source_set_property (source, "delete", "yes");

	e_source_group_add_source (group, source, -1);

	g_object_unref (source);
	g_free (relative_uri);
			
	if (!e_source_list_add_group (source_list, group, -1))
		return;

	if (!e_source_list_sync (source_list, NULL))
		return;

	g_object_unref (group);
	g_object_unref (source_list);
	g_object_unref (client);
}


void exchange_ews_remove_esource (CamelURL *url, const gchar* folder_name, const gchar *fid, gint folder_type)
{
	ESourceList *source_list = NULL;
	ESourceGroup *group = NULL;
	const gchar *conf_key = NULL;
	GConfClient* client;
	GSList *sources=NULL;
	gchar *base_uri = NULL;
	
	if (url == NULL)
		return;

	if (folder_type == EWS_FOLDER_TYPE_APPOINTMENT) 
		conf_key = CALENDAR_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_TASK) 
		conf_key = TASK_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_MEMO) 
		conf_key = JOURNAL_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_JOURNAL)
		conf_key = JOURNAL_SOURCES;
	else if (folder_type == EWS_FOLDER_TYPE_CONTACT)
		conf_key = ADDRESSBOOK_SOURCES;
	else {
		g_warning ("%s: %s: Unknown ExchangeEWSFolderType\n", G_STRLOC, G_STRFUNC);
		return;
	}

	client = gconf_client_get_default ();
	source_list = e_source_list_new_for_gconf (client, conf_key);
	base_uri = g_strdup_printf ("%s%s@%s/", EWS_URI_PREFIX, url->user, url->host);
	group = e_source_list_peek_group_by_base_uri (source_list, base_uri);
	sources = e_source_group_peek_sources (group);	
	
	for (; sources != NULL; sources = g_slist_next (sources)) {
		ESource *source = E_SOURCE (sources->data);
		gchar* folder_id = e_source_get_duped_property (source, "folder-id"); 
		if (folder_id && fid)
			if (strcmp(fid, folder_id) == 0) {
				e_source_group_remove_source(group, source);
				break;
			}
	}

	g_free (base_uri);
	g_object_unref (source_list);
	g_object_unref (client);

}


static void
remove_cal_esource (EAccount *existing_account_info, ExchangeEWSFolderType folder_type, CamelURL *url)
{
	ESourceList *list;
	const gchar *conf_key = NULL, *source_selection_key = NULL;
	GSList *groups;
	gboolean found_group;
	GConfClient* client;
	GSList *ids;
	GSList *node_tobe_deleted;
	gchar *base_uri;

	if (folder_type == EWS_FOLDER_TYPE_APPOINTMENT) {
		conf_key = CALENDAR_SOURCES;
		source_selection_key = SELECTED_CALENDARS;
	} else if (folder_type == EWS_FOLDER_TYPE_TASK) {
		conf_key = TASK_SOURCES;
		source_selection_key = SELECTED_TASKS;
	} else if (folder_type == EWS_FOLDER_TYPE_MEMO) {
		conf_key = JOURNAL_SOURCES;
		source_selection_key = SELECTED_JOURNALS;
	} else {
		g_warning ("%s: %s: Unknown ExchangeEWSFolderType\n", G_STRLOC, G_STRFUNC);
		return;
	}

	client = gconf_client_get_default();
	list = e_source_list_new_for_gconf (client, conf_key);
	groups = e_source_list_peek_groups (list);

	base_uri = g_strdup_printf ("ews://%s@%s/", url->user, url->host);

	found_group = FALSE;

	for (; groups != NULL && !found_group; groups = g_slist_next (groups)) {
		ESourceGroup *group = E_SOURCE_GROUP (groups->data);

		if (strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0 &&
		    strcmp (e_source_group_peek_base_uri (group), base_uri) == 0) {
			GSList *sources = e_source_group_peek_sources (group);

			for (; sources != NULL; sources = g_slist_next (sources)) {
				ESource *source = E_SOURCE (sources->data);

				if (source_selection_key) {
					ids = gconf_client_get_list (client, source_selection_key ,
								     GCONF_VALUE_STRING, NULL);
					node_tobe_deleted = g_slist_find_custom (ids, e_source_peek_uid (source), (GCompareFunc) strcmp);
					if (node_tobe_deleted) {
						g_free (node_tobe_deleted->data);
						ids = g_slist_delete_link (ids, node_tobe_deleted);
					}
					gconf_client_set_list (client, source_selection_key,
							       GCONF_VALUE_STRING, ids, NULL);
				}
			}
			e_source_list_remove_group (list, group);
			e_source_list_sync (list, NULL);
			found_group = TRUE;
			break;
		}
	}

	g_free (base_uri);
	g_object_unref (list);
	g_object_unref (client);
}

/* add sources for calendar and tasks if the account added is exchange account
   adds the new account info to ews_accounts list */

static void
add_calendar_sources (EAccount *account, GSList *folders, ews_id_t trash_fid)
{
	CamelURL *url;

	url = camel_url_new (account->source->url, NULL);

	if (url) {
		add_cal_esource (account, folders, EWS_FOLDER_TYPE_APPOINTMENT, url, trash_fid);
		add_cal_esource (account, folders, EWS_FOLDER_TYPE_TASK, url, trash_fid);
		add_cal_esource (account, folders, EWS_FOLDER_TYPE_MEMO, url, trash_fid);
	}

	camel_url_free (url);
}

/* removes calendar and tasks sources if the account removed is exchange account
   removes the the account info from ews_account list */

static void
remove_calendar_sources (EAccount *account)
{
	CamelURL *url;

	url = camel_url_new (account->source->url, NULL);

	if (url) {
		remove_cal_esource (account, EWS_FOLDER_TYPE_APPOINTMENT, url);
		remove_cal_esource (account, EWS_FOLDER_TYPE_TASK, url);
		remove_cal_esource (account, EWS_FOLDER_TYPE_MEMO, url);
	}

	camel_url_free (url);
}

static gboolean
add_addressbook_sources (EAccount *account, GSList *folders, ews_id_t trash_fid)
{
	CamelURL *url;
	ESourceList *list;
	ESourceGroup *group;
	ESource *source;
	gchar *base_uri;
	GSList *temp_list, *old_sources = NULL;
	GConfClient* client;
	gboolean is_new_group = FALSE;

	url = camel_url_new (account->source->url, NULL);
	if (url == NULL) {
		return FALSE;
	}

	base_uri = g_strdup_printf ("ews://%s@%s/", url->user, url->host);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	group = e_source_list_peek_group_by_base_uri (list, base_uri);
	if (group) {
		e_source_group_set_name (group, account->name);
		g_object_ref (group);
		is_new_group = FALSE;
		old_sources = NULL;
		for (temp_list = e_source_group_peek_sources (group); temp_list; temp_list = temp_list->next) {
			old_sources = g_slist_prepend (old_sources, temp_list->data);
		}
	} else {
		group = e_source_group_new (account->name, base_uri);
		is_new_group = TRUE;
		old_sources = NULL;
	}
	e_source_group_set_property (group, "user", url->user);
	e_source_group_set_property (group, "host", url->host);
	e_source_group_set_property (group, "profile", camel_url_get_param (url, "profile"));
	e_source_group_set_property (group, "domain", camel_url_get_param (url, "domain"));

	for (temp_list = folders; temp_list != NULL; temp_list = g_slist_next (temp_list)) {
		ExchangeEWSFolder *folder = temp_list->data;
		gchar *fid, *relative_uri;
		gboolean is_new_source = FALSE;

		if (folder->container_class != EWS_FOLDER_TYPE_CONTACT || trash_fid == exchange_ews_folder_get_parent_id (folder))
			continue;

		fid = exchange_ews_util_ews_id_to_string (folder->folder_id);
		relative_uri = g_strconcat (";", folder->folder_name, NULL);
		source = find_source_by_fid (old_sources, fid);
		if (source) {
			is_new_source = FALSE;
			g_object_ref (source);
			old_sources = g_slist_remove (old_sources, source);
			e_source_set_name (source, folder->folder_name);
			e_source_set_relative_uri (source, relative_uri);
		} else {
			source = e_source_new (folder->folder_name, relative_uri);
			is_new_source = TRUE;
		}
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "auth-domain", EXCHANGE_EWS_PASSWORD_COMPONENT);
		e_source_set_property(source, "user", url->user);
		e_source_set_property(source, "host", url->host);
		e_source_set_property(source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property(source, "domain", camel_url_get_param (url, "domain"));
		e_source_set_property(source, "folder-id", fid);
		e_source_set_property (source, "public", "no");
		e_source_set_property (source, "offline_sync",
					       camel_url_get_param (url, "offline_sync") ? "1" : "0");
		e_source_set_property (source, "completion", "true");
		if (folder->is_default)
			e_source_set_property (source, "delete", "no");
		if (folder->parent_folder_id) {
			gchar *tmp = exchange_ews_util_ews_id_to_string (folder->parent_folder_id);
			e_source_set_property (source, "parent-fid", tmp);
			g_free (tmp);
		}
		if (is_new_source)
			e_source_group_add_source (group, source, -1);
		g_object_unref (source);
		g_free (fid);
		g_free (relative_uri);
	}

	//Add GAL
	{
		gchar *uri;
		gboolean is_new_source = FALSE;

		source = NULL;
		uri = g_strdup_printf ("ewsgal://%s@%s/;Global Address List", url->user, url->host);
		for (temp_list = old_sources; temp_list; temp_list = temp_list->next) {
			source = temp_list->data;

			if (source && E_IS_SOURCE (source)
			    && e_source_peek_absolute_uri (source)
			    && g_str_equal (e_source_peek_absolute_uri (source), uri))
				break;
		}

		if (source) {
			is_new_source = FALSE;
			g_object_ref (source);
			old_sources = g_slist_remove (old_sources, source);
			e_source_set_name (source, _("Global Address List"));
		} else {
			source = e_source_new_with_absolute_uri (_("Global Address List"), uri);
			is_new_source = TRUE;
		}
		g_free (uri);
		e_source_set_property (source, "auth", "plain/password");
		e_source_set_property (source, "auth-domain", "EWSGAL");

		//FIXME: Offline handling
		e_source_set_property(source, "user", url->user);
		e_source_set_property(source, "host", camel_url_get_param (url, "ad_server"));
		e_source_set_property(source, "view-limit", camel_url_get_param (url, "ad_limit"));
		e_source_set_property(source, "profile", camel_url_get_param (url, "profile"));
		e_source_set_property(source, "domain", camel_url_get_param (url, "domain"));
		// e_source_set_property (source, "offline_sync",
		// camel_url_get_param (url, "offline_sync") ? "1" : "0");
		e_source_set_property(source, "offline_sync", "1");
		e_source_set_property (source, "completion", "true");
		e_source_set_property (source, "delete", "no");
		if (is_new_source)
			e_source_group_add_source (group, source, -1);
		g_object_unref (source);
	}

	if (old_sources) {
		/* these were not found on the server by fid, thus remove them */
		for (temp_list = old_sources; temp_list; temp_list = temp_list->next) {
			ESource *source = temp_list->data;

			if (source && E_IS_SOURCE (source)) {
				if (strcmp(e_source_get_property(source, "public"), "yes") != 0)
					e_source_group_remove_source (group, source);
			}
		}

		g_slist_free (old_sources);
	}

	if (is_new_group && !e_source_list_add_group (list, group, -1))
		g_warning ("%s: Failed to add new group", G_STRFUNC);

	if (!e_source_list_sync (list, NULL))
		g_warning ("%s: Failed to sync source list", G_STRFUNC);

	g_object_unref (group);
	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);

	return TRUE;
}

static void
remove_addressbook_sources (ExchangeEWSAccountInfo *existing_account_info)
{
	ESourceList *list;
	ESourceGroup *group;
	GSList *groups;
	gboolean found_group;
	CamelURL *url;
	gchar *base_uri;
	GConfClient *client;

	url = camel_url_new (existing_account_info->source_url, NULL);
	if (url == NULL) {
		return;
	}

	base_uri = g_strdup_printf ("ews://%s@%s/", url->user, url->host);
	client = gconf_client_get_default ();
	list = e_source_list_new_for_gconf (client, "/apps/evolution/addressbook/sources" );
	groups = e_source_list_peek_groups (list);

	found_group = FALSE;

	for (; groups != NULL && !found_group; groups = g_slist_next (groups)) {

		group = E_SOURCE_GROUP (groups->data);
		if ( strcmp ( e_source_group_peek_base_uri (group), base_uri) == 0 && strcmp (e_source_group_peek_name (group), existing_account_info->name) == 0) {

			e_source_list_remove_group (list, group);
			e_source_list_sync (list, NULL);
			found_group = TRUE;
		}
	}

	g_object_unref (list);
	g_object_unref (client);
	g_free (base_uri);
	camel_url_free (url);
}

static gboolean
update_sources_idle_cb (gpointer data)
{
	ExchangeMapiConnection *conn = data;
	EAccount *account;
	GSList *folders_list;

	g_return_val_if_fail (conn != NULL, FALSE);

	account = g_object_get_data (G_OBJECT (conn), "EAccount");
	if (!account) {
		g_object_unref (conn);
		g_return_val_if_fail (account != NULL, FALSE);
		return FALSE;
	}

	g_object_set_data (G_OBJECT (conn), "EAccount", NULL);

	folders_list = exchange_ews_connection_peek_folders_list (conn);

	if (account->enabled && lookup_account_info (account->uid)) {
		ews_id_t trash_fid = exchange_ews_connection_get_default_folder_id (conn, olFolderDeletedItems, NULL);

		add_addressbook_sources (account, folders_list, trash_fid);
		add_calendar_sources (account, folders_list, trash_fid);
	}

	g_object_unref (conn);
	g_object_unref (account);

	return FALSE;
}

static void
update_sources_fn (gpointer data, gpointer user_data)
{
	ExchangeMapiConnection *conn = data;

	g_return_if_fail (conn != NULL);

	/* this fetches folder_list to the connection cache,
	   thus next call will be quick as much as possible */
	exchange_ews_connection_peek_folders_list (conn);

	/* run a job in a main thread */
	g_idle_add (update_sources_idle_cb, conn);
}

static void
run_update_sources_thread (ExchangeMapiConnection *conn, EAccount *account)
{
	static GThreadPool *thread_pool = NULL;

	g_return_if_fail (conn != NULL);
	g_return_if_fail (account != NULL);

	/* this should be called only on the main thread, thus no locking needed */
	if (!thread_pool)
		thread_pool = g_thread_pool_new (update_sources_fn, NULL, 1, FALSE, NULL);

	g_object_set_data (G_OBJECT (conn), "EAccount", g_object_ref (account));

	if (!thread_pool)
		update_sources_fn (conn, NULL);
	else
		g_thread_pool_push (thread_pool, conn, NULL);
}

struct create_sources_data
{
	gchar *profile_name;
	EAccount *account;
};

static gboolean
check_for_account_conn_cb (gpointer data)
{
	struct create_sources_data *csd = data;

	g_return_val_if_fail (csd != NULL, FALSE);
	g_return_val_if_fail (csd->profile_name != NULL, FALSE);
	g_return_val_if_fail (csd->account != NULL, FALSE);

	if (csd->account->enabled && lookup_account_info (csd->account->uid)) {
		ExchangeMapiConnection *conn;

		conn = exchange_ews_connection_find (csd->profile_name);
		if (!conn) {
			/* try later, it's still trying to connect */
			return TRUE;
		}

		run_update_sources_thread (conn, csd->account);
	}

	g_object_unref (csd->account);
	g_free (csd->profile_name);
	g_free (csd);

	return FALSE;
}

static void
update_account_sources (EAccount *account, gboolean can_create_profile)
{
	CamelURL *url;
	ExchangeMapiConnection *conn;

	url = camel_url_new (account->source->url, NULL);
	g_return_if_fail (url != NULL);

	conn = exchange_ews_connection_find (camel_url_get_param (url, "profile"));
	if (!conn && can_create_profile) {
		/* connect to the server when not connected yet */
		if (!create_profile_entry (url, account)) {
			camel_url_free (url);
			g_warning ("%s: Failed to create EWS profile for '%s'", G_STRFUNC, account->name);
			return;
		}

		conn = exchange_ews_connection_find (camel_url_get_param (url, "profile"));
	}

	if (conn) {
		run_update_sources_thread (conn, account);
	} else {
		struct create_sources_data *csd;

		csd = g_new0 (struct create_sources_data, 1);
		csd->profile_name = g_strdup (camel_url_get_param (url, "profile"));
		csd->account = g_object_ref (account);

		g_timeout_add_seconds (1, check_for_account_conn_cb, csd);
	}

	camel_url_free (url);
}

static void
ews_account_added (EAccountList *account_listener, EAccount *account)
{
	ExchangeEWSAccountInfo *info = NULL;

	if (!is_ews_account (account))
		return;

	info = g_new0 (ExchangeEWSAccountInfo, 1);
	info->uid = g_strdup (account->uid);
	info->name = g_strdup (account->name);
	info->source_url = g_strdup (account->source->url);
	info->enabled = account->enabled;

	ews_accounts = g_list_append (ews_accounts, info);

	if (account->enabled)
		update_account_sources (account, TRUE);
}

static void
ews_account_removed (EAccountList *account_listener, EAccount *account)
{
	ExchangeEWSAccountInfo *info = NULL;
	CamelURL *url = NULL;

	if (!is_ews_account (account))
		return;

	/* We store a complete list of EWS accounts - both enabled and disabled */
	info = lookup_account_info (account->uid);
	g_return_if_fail (info != NULL);

	/* Remove from the local EWS accounts list */
	ews_accounts = g_list_remove (ews_accounts, info);

	/* If the account was disabled, then the corresponding ESource should have been removed
	 * when the account was disabled. We should only clean up the EWS profile database etc.
	 */
	if (info->enabled) {
		remove_addressbook_sources (info);
		remove_calendar_sources (account);
	}

	/* Now, clean up the profile database etc */
	url = camel_url_new (info->source_url, NULL);
	if (url != NULL) {
		const gchar *profile = camel_url_get_param (url, "profile");
		gchar *key = camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD | CAMEL_URL_HIDE_PARAMS);
		GError *error = NULL;

		exchange_ews_delete_profile (profile, &error);
		e_passwords_forget_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);

		g_free (key);
		camel_url_free (url);
		if (error) {
			g_warning ("%s: Failed to delete profile: %s", G_STRFUNC, error->message);
			g_error_free (error);
		}
	}

	/* Free up the structure */
	g_free (info->uid);
	g_free (info->name);
	g_free (info->source_url);
	g_free (info);
}

static gboolean
create_profile_entry (CamelURL *url, EAccount *account)
{
	gboolean status = FALSE;
	guint8 attempts = 0;

	while (!status && attempts <= 3) {
		gchar *password = NULL, *key = NULL;

		key = camel_url_to_string (url, CAMEL_URL_HIDE_PASSWORD | CAMEL_URL_HIDE_PARAMS);
		password = e_passwords_get_password (EXCHANGE_EWS_PASSWORD_COMPONENT, key);
		if (!password) {
			gboolean remember = account && e_account_get_bool (account, E_ACCOUNT_SOURCE_SAVE_PASSWD);
			gchar *title;

			title = g_strdup_printf (_("Enter Password for %s@%s"), url->user, url->host);
			password = e_passwords_ask_password (title, EXCHANGE_EWS_PASSWORD_COMPONENT, key, title,
					E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET,
					&remember, NULL);
			g_free (title);
		}
		g_free (key);

		if (password) {
			GError *error = NULL;
			guint32 cp_flags = (camel_url_get_param (url, "ssl") && g_str_equal (camel_url_get_param (url, "ssl"), "1")) ? CREATE_PROFILE_FLAG_USE_SSL : CREATE_PROFILE_FLAG_NONE;

			status = exchange_ews_create_profile (url->user, password, camel_url_get_param (url, "domain"), url->host, cp_flags, NULL, NULL, &error);
			if (status) {
				/* profile was created, try to connect to the server */
				ExchangeMapiConnection *conn;
				gchar *profname;

				status = FALSE;
				profname = exchange_ews_util_profile_name (url->user, camel_url_get_param (url, "domain"), url->host, FALSE);

				conn = exchange_ews_connection_new (profname, password, &error);
				if (conn) {
					status = exchange_ews_connection_connected (conn);
					g_object_unref (conn);
				}

				g_free (profname);
			}

			if (error) {
				g_warning ("%s: Failed to create profile: %s", G_STRFUNC, error->message);
				g_error_free (error);
			}
		}

		++attempts;
	}

	return status;
}

static gboolean
check_equal (const gchar *a, const gchar *b)
{
	if (!a && a == b)
		return TRUE;

	return a && b && g_ascii_strcasecmp (a, b) == 0;
}

static gboolean
ews_camel_url_equal (CamelURL *a, CamelURL *b)
{
	const gchar *params[] = { "profile", "domain", "ad_limit", "ad_server" };
	guint n_params = G_N_ELEMENTS (params), i;
	gboolean retval = TRUE;

	retval = camel_url_equal (a, b);

	for (i = 0; retval && i < n_params; ++i)
		retval = retval && check_equal (camel_url_get_param (a, params[i]), camel_url_get_param (b, params[i]));

	return retval;
}

static void
ews_account_changed (EAccountList *account_listener, EAccount *account)
{
	CamelURL *new_url = NULL, *old_url = NULL;
	gboolean isa_ews_account = FALSE;
	ExchangeEWSAccountInfo *existing_account_info = NULL;

	isa_ews_account = is_ews_account (account);

	if (isa_ews_account)
		existing_account_info = lookup_account_info (account->uid);

	if (existing_account_info)
		old_url = camel_url_new (existing_account_info->source_url, NULL);

	new_url = camel_url_new (account->source->url, NULL);

	if (existing_account_info == NULL && isa_ews_account) {
		/* some account of other type is changed to EWS */
		if (create_profile_entry (new_url, account)) {
			/* Things are successful */
			gchar *profname = NULL, *uri = NULL;
			ExchangeEWSAccountListener *config_listener = exchange_ews_accounts_peek_config_listener();

			profname = exchange_ews_util_profile_name (new_url->user, camel_url_get_param (new_url, "domain"), new_url->host, FALSE);
			camel_url_set_param(new_url, "profile", profname);
			g_free (profname);

			uri = camel_url_to_string(new_url, 0);
			/* FIXME: Find a better way to append to the Account source URL. The current
			 * method uses e_account_set_string() which initiates another signal emmission
			 * which we have to block for now. */
			g_signal_handlers_block_by_func (config_listener->priv->account_list, G_CALLBACK (ews_account_changed), NULL);
			e_account_set_string (account, E_ACCOUNT_SOURCE_URL, uri);
			e_account_set_string (account, E_ACCOUNT_TRANSPORT_URL, uri);
			g_signal_handlers_unblock_by_func (config_listener->priv->account_list, G_CALLBACK (ews_account_changed), NULL);
			g_free (uri);

			ews_account_added (account_listener, account);
		}
	} else if (existing_account_info != NULL && !isa_ews_account) {
		/* EWS account is changed to some other type */
		ews_account_removed (account_listener, account);
	} else if (existing_account_info != NULL && isa_ews_account) {
		/* Just disabling the account requires no further action */
		if (!account->enabled) {
			remove_addressbook_sources (existing_account_info);
			remove_calendar_sources (account);
			existing_account_info->enabled = FALSE;
		} else if (!ews_camel_url_equal (old_url, new_url) || (existing_account_info->enabled != account->enabled)) {
		/* Some or all of the account info changed OR the account has been moved from a disabled state to enabled state */
			ews_account_removed (account_listener, account);
			if (create_profile_entry (new_url, account)) {
				/* Things are successful */
				gchar *profname = NULL, *uri = NULL;
				ExchangeEWSAccountListener *config_listener = exchange_ews_accounts_peek_config_listener();

				profname = exchange_ews_util_profile_name (new_url->user, camel_url_get_param (new_url, "domain"), new_url->host, FALSE);
				camel_url_set_param(new_url, "profile", profname);
				g_free (profname);

				uri = camel_url_to_string(new_url, 0);
				/* FIXME: Find a better way to append to the Account source URL. The current
				 * method uses e_account_set_string() which initiates another signal emmission
				 * which we have to block for now. */
				g_signal_handlers_block_by_func (config_listener->priv->account_list, G_CALLBACK (ews_account_changed), NULL);
				e_account_set_string (account, E_ACCOUNT_SOURCE_URL, uri);
				e_account_set_string (account, E_ACCOUNT_TRANSPORT_URL, uri);
				g_signal_handlers_unblock_by_func (config_listener->priv->account_list, G_CALLBACK (ews_account_changed), NULL);
				g_free (uri);

				ews_account_added (account_listener, account);
			}
		}
	}

	if (old_url)
		camel_url_free (old_url);

	camel_url_free (new_url);
}

static void
exchange_ews_account_listener_construct (ExchangeEWSAccountListener *config_listener)
{
	EIterator *iter;

	config_listener->priv->account_list = e_account_list_new (config_listener->priv->gconf_client);

	for (iter = e_list_get_iterator (E_LIST(config_listener->priv->account_list)); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		EAccount *account = E_ACCOUNT (e_iterator_get (iter));
		if (is_ews_account (account)) {
			ExchangeEWSAccountInfo *info = g_new0 (ExchangeEWSAccountInfo, 1);
			info->uid = g_strdup (account->uid);
			info->name = g_strdup (account->name);
			info->source_url = g_strdup (account->source->url);
			info->enabled = account->enabled;

			ews_accounts = g_list_append (ews_accounts, info);

			if (!account->enabled) {
				remove_addressbook_sources (info);
				remove_calendar_sources (account);
			} else {
				/* fetch new calendars/remove dropped from a server, if any */
				update_account_sources (account, FALSE);
			}
		}
	}

	d(g_debug ("EWS listener is constructed with %d listed EWS accounts ", g_list_length (ews_accounts)));

	g_signal_connect (config_listener->priv->account_list, "account_added", G_CALLBACK (ews_account_added), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_changed", G_CALLBACK (ews_account_changed), NULL);
	g_signal_connect (config_listener->priv->account_list, "account_removed", G_CALLBACK (ews_account_removed), NULL);
}

ExchangeEWSAccountListener *
exchange_ews_account_listener_new (void)
{
	ExchangeEWSAccountListener *config_listener;

	config_listener = g_object_new (EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE, NULL);
	config_listener->priv->gconf_client = gconf_client_get_default();

	exchange_ews_account_listener_construct (config_listener);

	return config_listener;
}
