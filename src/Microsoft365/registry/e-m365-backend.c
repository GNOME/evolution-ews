/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/e-m365-connection.h"
#include "common/e-source-m365-folder.h"
#include "common/camel-m365-settings.h"

#include "e-source-m365-deltas.h"

#include "e-m365-backend.h"

#define LOCK(_backend) g_mutex_lock (&_backend->priv->property_lock)
#define UNLOCK(_backend) g_mutex_unlock (&_backend->priv->property_lock)

struct _EM365BackendPrivate {
	GMutex property_lock;

	GHashTable *folder_sources; /* gchar *folder_id ~> ESource * */

	gboolean need_update_folders;

	gulong source_changed_id;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EM365Backend, e_m365_backend, E_TYPE_COLLECTION_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EM365Backend))

static void
m365_backend_claim_old_resources (ECollectionBackend *backend)
{
	ESourceRegistryServer *registry;
	GList *old_resources, *iter;

	g_return_if_fail (E_IS_COLLECTION_BACKEND (backend));

	registry = e_collection_backend_ref_server (backend);
	old_resources = e_collection_backend_claim_all_resources (backend);

	for (iter = old_resources; iter; iter = g_list_next (iter)) {
		ESource *source = iter->data;

		e_source_registry_server_add_source (registry, source);
	}

	g_list_free_full (old_resources, g_object_unref);
	g_clear_object (&registry);
}

static void m365_backend_populate (ECollectionBackend *backend);

static void
m365_backend_source_changed_cb (ESource *source,
				EM365Backend *backend)
{
	if (!e_collection_backend_get_part_enabled (E_COLLECTION_BACKEND (backend), E_COLLECTION_BACKEND_PART_ANY)) {
		backend->priv->need_update_folders = TRUE;
		return;
	}

	if (!backend->priv->need_update_folders)
		return;

	m365_backend_populate (E_COLLECTION_BACKEND (backend));
}

static void
m365_backend_populate (ECollectionBackend *collection_backend)
{
	ESource *source;
	EM365Backend *m365_backend = E_M365_BACKEND (collection_backend);
	EBackend *backend = E_BACKEND (m365_backend);

	source = e_backend_get_source (E_BACKEND (backend));

	m365_backend->priv->need_update_folders = TRUE;

	if (!m365_backend->priv->source_changed_id) {
		m365_backend->priv->source_changed_id = g_signal_connect (source, "changed",
			G_CALLBACK (m365_backend_source_changed_cb), m365_backend);
	}

	/* do not do anything, if account is disabled */
	if (!e_collection_backend_get_part_enabled (collection_backend, E_COLLECTION_BACKEND_PART_ANY))
		return;

	if (!e_collection_backend_freeze_populate (collection_backend)) {
		e_collection_backend_thaw_populate (collection_backend);
		return;
	}

	m365_backend_claim_old_resources (collection_backend);

	if (e_backend_get_online (backend))
		e_backend_schedule_authenticate (backend, NULL);

	e_collection_backend_thaw_populate (collection_backend);
}

static void
m365_backend_update_resource (EM365Backend *m365_backend,
			      const gchar *extension_name,
			      const gchar *id,
			      const gchar *group_id,
			      const gchar *display_name,
			      gboolean is_default,
			      const gchar *calendar_color)
{
	ESource *source;
	gboolean is_new;

	LOCK (m365_backend);
	source = g_hash_table_lookup (m365_backend->priv->folder_sources, id);
	if (source)
		g_object_ref (source);
	UNLOCK (m365_backend);

	is_new = !source;

	if (is_new)
		source = e_collection_backend_new_child (E_COLLECTION_BACKEND (m365_backend), id);

	if (source) {
		ESourceM365Folder *folder_ext;

		folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);

		if (is_new || !e_source_m365_folder_get_display_name (folder_ext) ||
		    g_strcmp0 (e_source_m365_folder_get_display_name (folder_ext), e_source_get_display_name (source)) == 0)
			e_source_set_display_name (source, display_name);

		e_source_m365_folder_set_display_name (folder_ext, display_name);

		if (calendar_color && g_ascii_strcasecmp (calendar_color, "auto") != 0 && (
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0 ||
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0 ||
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_MEMO_LIST) == 0)) {
			ESourceSelectable *selectable;

			selectable = e_source_get_extension (source, extension_name);

			if (is_new || !e_source_m365_folder_get_color (folder_ext) ||
			    g_strcmp0 (e_source_m365_folder_get_color (folder_ext), e_source_selectable_get_color (selectable)) == 0) {
				e_source_selectable_set_color (selectable, calendar_color);
			}

			e_source_m365_folder_set_color (folder_ext, calendar_color);
		}

		if (is_new) {
			ESourceRegistryServer *server;
			gpointer extension;

			extension = e_source_get_extension (source, extension_name);
			e_source_backend_set_backend_name (E_SOURCE_BACKEND (extension), "microsoft365");

			/* Do not notify with too old reminders */
			if (g_strcmp0 (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0 ||
			    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0) {
				ESourceAlarms *alarms;
				gchar *today;
				GTimeVal today_tv;
				GDate dt;

				g_date_clear (&dt, 1);
				g_get_current_time (&today_tv);
				g_date_set_time_val (&dt, &today_tv);

				/* midnight UTC */
				today = g_strdup_printf ("%04d-%02d-%02dT00:00:00Z", g_date_get_year (&dt), g_date_get_month (&dt), g_date_get_day (&dt));

				alarms = e_source_get_extension (source, E_SOURCE_EXTENSION_ALARMS);
				e_source_alarms_set_last_notified (alarms, today);

				g_free (today);
			}

			if (g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS) == 0 ||
			    g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_USERS) == 0) {
				ESourceOffline *offline_ext;

				offline_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
				/* look up by default, these can be very large */
				e_source_offline_set_stay_synchronized (offline_ext, FALSE);
			}

			e_source_m365_folder_set_id (folder_ext, id);
			e_source_m365_folder_set_group_id (folder_ext, group_id);
			e_source_m365_folder_set_is_default (folder_ext, is_default);

			server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (m365_backend));

			e_source_registry_server_add_source (server, source);

			g_clear_object (&server);
		} else if (!e_source_has_extension (source, E_SOURCE_EXTENSION_OFFLINE) && (
			   g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS) == 0 ||
			   g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_USERS) == 0)) {
			ESourceOffline *offline_ext;

			offline_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
			/* existing books without the extension mean they had been synchronized for offline use */
			e_source_offline_set_stay_synchronized (offline_ext, TRUE);
		}
	}

	g_clear_object (&source);
}

static void
m365_backend_remove_resource (EM365Backend *m365_backend,
			      const gchar *extension_name,
			      const gchar *id) /* NULL to remove the "is-default" resource for the extension_name */
{
	ESource *existing_source = NULL;

	LOCK (m365_backend);

	if (id) {
		existing_source = g_hash_table_lookup (m365_backend->priv->folder_sources, id);
	} else {
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, m365_backend->priv->folder_sources);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			ESource *source = value;

			if (value && e_source_has_extension (source, extension_name) &&
			    e_source_m365_folder_get_is_default (e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER))) {
				existing_source = source;
				break;
			}
		}
	}

	if (existing_source)
		g_object_ref (existing_source);

	UNLOCK (m365_backend);

	if (existing_source)
		e_source_remove_sync (existing_source, NULL, NULL);

	g_clear_object (&existing_source);
}

static GHashTable * /* gchar *uid ~> NULL */
m365_backend_get_known_folder_ids (EM365Backend *m365_backend,
				   const gchar *extension_name,
				   gboolean with_the_default)
{
	GHashTable *ids;
	GHashTableIter iter;
	gpointer value;

	ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	LOCK (m365_backend);

	g_hash_table_iter_init (&iter, m365_backend->priv->folder_sources);

	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		ESource *source = value;

		if (source && e_source_has_extension (source, extension_name)) {
			ESourceM365Folder *m365_folder;

			m365_folder = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);

			if (with_the_default || !e_source_m365_folder_get_is_default (m365_folder))
				g_hash_table_insert (ids, e_source_m365_folder_dup_id (m365_folder), NULL);
		}
	}

	UNLOCK (m365_backend);

	return ids;
}

static void
m365_backend_forget_folders_hash (EM365Backend *m365_backend,
				  const gchar *extension_name,
				  GHashTable *ids) /* gchar *id ~> NULL */
{
	GHashTableIter iter;
	gpointer key;

	g_hash_table_iter_init (&iter, ids);

	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		const gchar *id = key;

		if (id)
			m365_backend_remove_resource (m365_backend, extension_name, id);
	}
}

static void
m365_backend_forget_book_folders (EM365Backend *m365_backend,
				  const gchar *extension_name)
{
	GHashTable *ids;

	ids = m365_backend_get_known_folder_ids (m365_backend, extension_name, FALSE);

	g_hash_table_remove (ids, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS);
	g_hash_table_remove (ids, E_M365_ARTIFICIAL_FOLDER_ID_USERS);
	g_hash_table_remove (ids, E_M365_ARTIFICIAL_FOLDER_ID_PEOPLE);

	m365_backend_forget_folders_hash (m365_backend, extension_name, ids);

	g_hash_table_destroy (ids);
}

static gboolean
m365_backend_got_contact_folders_delta_cb (EM365Connection *cnc,
					   const GSList *results, /* JsonObject * - the returned objects from the server */
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error)
{
	EM365Backend *m365_backend = user_data;
	GSList *link;

	g_return_val_if_fail (E_IS_M365_BACKEND (m365_backend), FALSE);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *object = link->data;
		const gchar *id = e_m365_folder_get_id (object);

		if (!id)
			continue;

		if (e_m365_delta_is_removed_object (object)) {
			m365_backend_remove_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, id);
		} else {
			m365_backend_update_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK,
			      id, NULL, e_m365_folder_get_display_name (object),
			      FALSE, NULL);
		}
	}

	return TRUE;
}

static void
m365_backend_sync_contact_folders_sync (EM365Backend *m365_backend,
					EM365Connection *cnc,
					GCancellable *cancellable)
{
	EM365Folder *user_contacts = NULL;
	ESourceM365Deltas *m365_deltas;
	gchar *old_delta_link, *new_delta_link;
	gboolean success;
	GError *error = NULL;

	m365_deltas = e_source_get_extension (e_backend_get_source (E_BACKEND (m365_backend)), E_SOURCE_EXTENSION_M365_DELTAS);

	if (e_m365_connection_get_contacts_folder_sync (cnc, NULL, NULL, NULL, &user_contacts, cancellable, &error)) {
		const gchar *id, *display_name;

		id = e_m365_folder_get_id (user_contacts);
		display_name = e_m365_folder_get_display_name (user_contacts);

		g_warn_if_fail (id != NULL);
		g_warn_if_fail (display_name != NULL);

		m365_backend_update_resource (m365_backend,
			E_SOURCE_EXTENSION_ADDRESS_BOOK,
			id, NULL, display_name, TRUE, NULL);

		json_object_unref (user_contacts);
	} else if (g_error_matches (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND) ||
		   g_error_matches (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		m365_backend_remove_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, NULL);
	}

	g_clear_error (&error);

	new_delta_link = NULL;
	old_delta_link = e_source_m365_deltas_dup_contacts_link (m365_deltas);

	success = e_m365_connection_get_folders_delta_sync (cnc, NULL, E_M365_FOLDER_KIND_CONTACTS, NULL, old_delta_link, 0,
		m365_backend_got_contact_folders_delta_cb, m365_backend, &new_delta_link, cancellable, &error);

	if (old_delta_link && *old_delta_link && e_m365_connection_util_delta_token_failed (error)) {
		g_clear_pointer (&old_delta_link, g_free);
		g_clear_error (&error);

		m365_backend_forget_book_folders (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK);

		success = e_m365_connection_get_folders_delta_sync (cnc, NULL, E_M365_FOLDER_KIND_CONTACTS, NULL, NULL, 0,
			m365_backend_got_contact_folders_delta_cb, m365_backend, &new_delta_link, cancellable, &error);
	}

	if (success)
		e_source_m365_deltas_set_contacts_link (m365_deltas, new_delta_link);

	g_clear_pointer (&old_delta_link, g_free);
	g_clear_pointer (&new_delta_link, g_free);
	g_clear_error (&error);

	if (e_m365_connection_get_org_contacts_accessible_sync (cnc, NULL, cancellable, NULL)) {
		m365_backend_update_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK,
			E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS, NULL, _("Organizational Contacts"), TRUE, NULL);
	} else {
		m365_backend_remove_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS);
	}

	if (e_m365_connection_get_users_accessible_sync (cnc, NULL, cancellable, NULL)) {
		m365_backend_update_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK,
			E_M365_ARTIFICIAL_FOLDER_ID_USERS, NULL, _("Organizational Users"), TRUE, NULL);
	} else {
		m365_backend_remove_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, E_M365_ARTIFICIAL_FOLDER_ID_USERS);
	}

	if (e_m365_connection_get_people_accessible_sync (cnc, NULL, cancellable, NULL)) {
		m365_backend_update_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK,
			E_M365_ARTIFICIAL_FOLDER_ID_PEOPLE, NULL, _("Recent Contacts"), TRUE, NULL);
	} else {
		m365_backend_remove_resource (m365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, E_M365_ARTIFICIAL_FOLDER_ID_PEOPLE);
	}
}

static void
m365_backend_sync_calendar_folders_sync (EM365Backend *m365_backend,
					 EM365Connection *cnc,
					 GCancellable *cancellable)
{
	const gchar *extension_name = E_SOURCE_EXTENSION_CALENDAR;
	GHashTable *known_ids; /* gchar *id ~> NULL */
	gboolean success = FALSE;
	GSList *groups = NULL, *link;
	GError *error = NULL;

	known_ids = m365_backend_get_known_folder_ids (m365_backend, extension_name, FALSE);

	if (e_m365_connection_list_calendar_groups_sync (cnc, NULL, &groups, cancellable, &error) && groups) {
		success = TRUE;

		for (link = groups; link && success; link = g_slist_next (link)) {
			EM365CalendarGroup *group = link->data;
			GSList *calendars = NULL;

			if (!group)
				continue;

			if (e_m365_connection_list_calendars_sync (cnc, NULL, e_m365_calendar_group_get_id (group), NULL, &calendars, cancellable, &error)) {
				GSList *clink;

				for (clink = calendars; clink; clink = g_slist_next (clink)) {
					EM365Calendar *calendar = clink->data;
					const gchar *color;

					if (!calendar || !e_m365_calendar_get_id (calendar))
						continue;

					color = e_m365_calendar_get_hex_color (calendar);
					if (!color || !*color)
						color = e_m365_calendar_color_to_rgb (e_m365_calendar_get_color (calendar));

					m365_backend_update_resource (m365_backend, extension_name,
						e_m365_calendar_get_id (calendar),
						e_m365_calendar_group_get_id (group),
						e_m365_calendar_get_name (calendar),
						FALSE,
						color);

					g_hash_table_remove (known_ids, e_m365_calendar_get_id (calendar));
				}

				g_slist_free_full (calendars, (GDestroyNotify) json_object_unref);
			} else {
				success = FALSE;
			}
		}

		g_slist_free_full (groups, (GDestroyNotify) json_object_unref);
	}

	if (success)
		m365_backend_forget_folders_hash (m365_backend, extension_name, known_ids);

	g_hash_table_destroy (known_ids);
	g_clear_error (&error);
}

static void
m365_backend_sync_task_folders_sync (EM365Backend *m365_backend,
				     EM365Connection *cnc,
				     GCancellable *cancellable)
{
	const gchar *extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	GHashTable *known_ids; /* gchar *id ~> NULL */
	gboolean success = FALSE;
	GSList *task_lists = NULL, *link;
	GError *error = NULL;

	known_ids = m365_backend_get_known_folder_ids (m365_backend, extension_name, FALSE);

	if (e_m365_connection_list_task_lists_sync (cnc, NULL, &task_lists, cancellable, &error)) {
		for (link = task_lists; link; link = g_slist_next (link)) {
			EM365TaskList *task_list = link->data;

			if (!task_list || !e_m365_task_list_get_id (task_list))
				continue;

			m365_backend_update_resource (m365_backend, extension_name,
				e_m365_task_list_get_id (task_list),
				NULL,
				e_m365_task_list_get_display_name (task_list),
				e_m365_task_list_get_kind (task_list) == E_M365_TASK_LIST_KIND_DEFAULT_LIST,
				NULL);

			g_hash_table_remove (known_ids, e_m365_task_list_get_id (task_list));
		}

		g_slist_free_full (task_lists, (GDestroyNotify) json_object_unref);
	}

	if (success)
		m365_backend_forget_folders_hash (m365_backend, extension_name, known_ids);

	g_hash_table_destroy (known_ids);
	g_clear_error (&error);
}

static void
m365_backend_sync_folders_thread (GTask *task,
				  gpointer source_object,
				  gpointer task_data,
				  GCancellable *cancellable)
{
	EM365Backend *m365_backend = source_object;
	EM365Connection *cnc = task_data;
	ESourceCollection *collection_extension = NULL;
	ESource *source;

	g_return_if_fail (E_IS_M365_BACKEND (m365_backend));
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	source = e_backend_get_source (E_BACKEND (m365_backend));
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_collection_get_contacts_enabled (collection_extension)) {
		m365_backend_sync_contact_folders_sync (m365_backend, cnc, cancellable);
	}

	if (e_source_collection_get_calendar_enabled (collection_extension)) {
		m365_backend_sync_calendar_folders_sync (m365_backend, cnc, cancellable);
		m365_backend_sync_task_folders_sync (m365_backend, cnc, cancellable);
	}

	e_collection_backend_thaw_populate (E_COLLECTION_BACKEND (m365_backend));
}

static void
m365_backend_sync_folders (EM365Backend *m365_backend,
			   EM365Connection *cnc,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer user_data)
{
	GTask *task;

	m365_backend->priv->need_update_folders = FALSE;

	task = g_task_new (m365_backend, cancellable, callback, user_data);

	g_task_set_check_cancellable (task, TRUE);
	g_task_set_task_data (task, g_object_ref (cnc), g_object_unref);
	g_task_run_in_thread (task, m365_backend_sync_folders_thread);

	g_object_unref (task);
}

static gchar *
m365_backend_dup_resource_id (ECollectionBackend *backend,
			      ESource *child_source)
{
	ESourceM365Folder *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_M365_FOLDER;
	extension = e_source_get_extension (child_source, extension_name);

	return e_source_m365_folder_dup_id (extension);
}

static void
m365_backend_child_added (ECollectionBackend *backend,
			  ESource *child_source)
{
	ESource *collection_source;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_child_extension;
		ESourceAuthentication *auth_collection_extension;

		auth_child_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_collection_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);

		e_binding_bind_property (
			auth_collection_extension, "host",
			auth_child_extension, "host",
			G_BINDING_SYNC_CREATE);

		e_binding_bind_property (
			auth_collection_extension, "user",
			auth_child_extension, "user",
			G_BINDING_SYNC_CREATE);

		e_binding_bind_property (
			auth_collection_extension, "method",
			auth_child_extension, "method",
			G_BINDING_SYNC_CREATE);
	}

	/* We track M365 folders in a hash table by folder ID. */
	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_M365_FOLDER)) {
		ESourceM365Folder *extension;
		gchar *folder_id;

		extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_M365_FOLDER);
		folder_id = e_source_m365_folder_dup_id (extension);

		if (folder_id) {
			EM365Backend *m365_backend = E_M365_BACKEND (backend);

			LOCK (m365_backend);
			g_hash_table_insert (m365_backend->priv->folder_sources, folder_id, g_object_ref (child_source));
			UNLOCK (m365_backend);
		}
	}

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_m365_backend_parent_class)->child_added (backend, child_source);
}

static void
m365_backend_child_removed (ECollectionBackend *backend,
			    ESource *child_source)
{
	/* We track M365 folders in a hash table by folder ID. */
	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_M365_FOLDER)) {
		ESourceM365Folder *extension;
		const gchar *folder_id;

		extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_M365_FOLDER);
		folder_id = e_source_m365_folder_get_id (extension);

		if (folder_id) {
			EM365Backend *m365_backend = E_M365_BACKEND (backend);

			LOCK (m365_backend);
			g_hash_table_remove (m365_backend->priv->folder_sources, folder_id);
			UNLOCK (m365_backend);
		}
	}

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_m365_backend_parent_class)->child_removed (backend, child_source);
}

static gboolean
m365_backend_create_resource_sync (ECollectionBackend *backend,
				   ESource *source,
				   GCancellable *cancellable,
				   GError **error)
{
#if 0
	EM365Connection *connection = NULL;
	M365FolderId *out_folder_id = NULL;
	EM365FolderType folder_type = E_M365_FOLDER_TYPE_UNKNOWN;
	const gchar *extension_name;
	const gchar *parent_folder_id = NULL;
	gchar *folder_name;
	gboolean success = FALSE;

	extension_name = E_SOURCE_EXTENSION_M365_FOLDER;
	if (e_source_has_extension (source, extension_name)) {
		ESourceM365Folder *extension;

		/* foreign and public folders are just added */
		extension = e_source_get_extension (source, extension_name);
		if (e_source_m365_folder_get_foreign (extension) ||
		    e_source_m365_folder_get_public (extension))
			success = TRUE;
	}

	if (!success) {
		connection = e_m365_backend_ref_connection_sync (E_M365_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
		if (connection == NULL)
			return FALSE;

		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_M365_FOLDER_TYPE_CONTACTS;
			parent_folder_id = "contacts";
		}

		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_M365_FOLDER_TYPE_CALENDAR;
			parent_folder_id = "calendar";
		}

		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_M365_FOLDER_TYPE_TASKS;
			parent_folder_id = "tasks";
		}

		/* FIXME No support for memo lists. */

		if (parent_folder_id == NULL) {
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_INVALID_ARGUMENT,
				_("Could not determine a suitable folder "
				"class for a new folder named “%s”"),
				e_source_get_display_name (source));
			goto exit;
		}

		folder_name = e_source_dup_display_name (source);

		success = e_m365_connection_create_folder_sync (
			connection, M365_PRIORITY_MEDIUM,
			parent_folder_id, TRUE,
			folder_name, folder_type,
			&out_folder_id, cancellable, error);

		g_free (folder_name);

		/* Sanity check */
		g_warn_if_fail (
			(success && out_folder_id != NULL) ||
			(!success && out_folder_id == NULL));

		if (out_folder_id != NULL) {
			ESourceM365Folder *extension;
			const gchar *extension_name;

			extension_name = E_SOURCE_EXTENSION_M365_FOLDER;
			extension = e_source_get_extension (source, extension_name);
			e_source_m365_folder_set_id (
				extension, out_folder_id->id);
			e_source_m365_folder_set_change_key (
				extension, out_folder_id->change_key);

			e_m365_folder_id_free (out_folder_id);
		}
	}

	if (success) {
		ESourceRegistryServer *server;
		ESource *parent_source;
		const gchar *cache_dir;
		const gchar *parent_uid;

		/* Configure the source as a collection member. */
		parent_source = e_backend_get_source (E_BACKEND (backend));
		parent_uid = e_source_get_uid (parent_source);
		e_source_set_parent (source, parent_uid);

		/* Changes should be written back to the cache directory. */
		cache_dir = e_collection_backend_get_cache_dir (backend);
		e_server_side_source_set_write_directory (
			E_SERVER_SIDE_SOURCE (source), cache_dir);

		/* Set permissions for clients. */
		e_server_side_source_set_writable (E_SERVER_SIDE_SOURCE (source), TRUE);
		e_server_side_source_set_remote_deletable (E_SERVER_SIDE_SOURCE (source), TRUE);

		server = e_collection_backend_ref_server (backend);
		e_source_registry_server_add_source (server, source);
		g_object_unref (server);
	}

 exit:
	if (connection)
		g_object_unref (connection);

	return success;
#endif
	return FALSE;
}

static gboolean
m365_backend_delete_resource_sync (ECollectionBackend *backend,
				   ESource *source,
				   GCancellable *cancellable,
				   GError **error)
{
#if 0
	EM365Connection *connection;
	ESourceM365Folder *extension;
	const gchar *extension_name;
	gboolean success = FALSE;

	connection = e_m365_backend_ref_connection_sync (E_M365_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
	if (connection == NULL)
		return FALSE;

	extension_name = E_SOURCE_EXTENSION_M365_FOLDER;
	if (!e_source_has_extension (source, extension_name)) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Data source “%s” does not represent a Microsoft 365 folder"),
			e_source_get_display_name (source));
		goto exit;
	}
	extension = e_source_get_extension (source, extension_name);

	if (e_source_m365_folder_get_foreign (extension) ||
	    e_source_m365_folder_get_public (extension)) {
		/* do not delete foreign or public folders,
		 * just remove them from local store */
		success = TRUE;
	} else {
		gchar *folder_id;

		folder_id = e_source_m365_folder_dup_id (extension);

		success = e_m365_connection_delete_folder_sync (
			connection, M365_PRIORITY_MEDIUM, folder_id,
			FALSE, "HardDelete", cancellable, error);

		g_free (folder_id);
	}

	if (success)
		success = e_source_remove_sync (source, cancellable, error);

 exit:
	g_object_unref (connection);

	return success;
#endif
	return FALSE;
}

static gboolean
m365_backend_get_destination_address (EBackend *backend,
				      gchar **host,
				      guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static ESourceAuthenticationResult
m365_backend_authenticate_sync (EBackend *backend,
				const ENamedParameters *credentials,
				gchar **out_certificate_pem,
				GTlsCertificateFlags *out_certificate_errors,
				GCancellable *cancellable,
				GError **error)
{
	CamelM365Settings *m365_settings;
	EM365Connection *cnc;
	ESourceAuthenticationResult result;
	gboolean in_sync_folders = FALSE;

	g_return_val_if_fail (E_IS_M365_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	e_collection_backend_freeze_populate (E_COLLECTION_BACKEND (backend));

	m365_settings = camel_m365_settings_get_from_backend (backend, NULL);
	g_return_val_if_fail (m365_settings != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	cnc = e_m365_connection_new (e_backend_get_source (backend), m365_settings);

	e_binding_bind_property (
		backend, "proxy-resolver",
		cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_UNKNOWN, NULL, NULL, out_certificate_pem, out_certificate_errors, cancellable, error);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);
		m365_backend_sync_folders (E_M365_BACKEND (backend), cnc, NULL, NULL, NULL);

		in_sync_folders = TRUE;
	} else if (result == E_SOURCE_AUTHENTICATION_REJECTED &&
		   !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		result = E_SOURCE_AUTHENTICATION_REQUIRED;
	}

	g_clear_object (&cnc);

	if (!in_sync_folders)
		e_collection_backend_thaw_populate (E_COLLECTION_BACKEND (backend));

	return result;
}

static void
m365_backend_constructed (GObject *object)
{
	EBackend *backend;
	ESource *source;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_backend_parent_class)->constructed (object);

	backend = E_BACKEND (object);
	source = e_backend_get_source (backend);

	e_server_side_source_set_remote_creatable (E_SERVER_SIDE_SOURCE (source), TRUE);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (backend, NULL);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_collection_set_allow_sources_rename (collection_extension, TRUE);
	}
}

static void
m365_backend_dispose (GObject *object)
{
	EM365Backend *m365_backend = E_M365_BACKEND (object);
	ESource *source;

	source = e_backend_get_source (E_BACKEND (object));
	if (source && m365_backend->priv->source_changed_id) {
		g_signal_handler_disconnect (source, m365_backend->priv->source_changed_id);
		m365_backend->priv->source_changed_id = 0;
	}

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_backend_parent_class)->dispose (object);
}

static void
m365_backend_finalize (GObject *object)
{
	EM365Backend *m365_backend = E_M365_BACKEND (object);

	g_hash_table_destroy (m365_backend->priv->folder_sources);
	g_mutex_clear (&m365_backend->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_backend_parent_class)->finalize (object);
}

static void
e_m365_backend_class_init (EM365BackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = m365_backend_dispose;
	object_class->finalize = m365_backend_finalize;
	object_class->constructed = m365_backend_constructed;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = m365_backend_populate;
	collection_backend_class->dup_resource_id = m365_backend_dup_resource_id;
	collection_backend_class->child_added = m365_backend_child_added;
	collection_backend_class->child_removed = m365_backend_child_removed;
	collection_backend_class->create_resource_sync = m365_backend_create_resource_sync;
	collection_backend_class->delete_resource_sync = m365_backend_delete_resource_sync;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->get_destination_address = m365_backend_get_destination_address;
	backend_class->authenticate_sync = m365_backend_authenticate_sync;

	/* This generates an ESourceCamel subtype for CamelM365Settings. */
	e_source_camel_generate_subtype ("microsoft365", CAMEL_TYPE_M365_SETTINGS);
}

static void
e_m365_backend_class_finalize (EM365BackendClass *class)
{
}

static void
e_m365_backend_init (EM365Backend *backend)
{
	backend->priv = e_m365_backend_get_instance_private (backend);
	backend->priv->folder_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	g_mutex_init (&backend->priv->property_lock);
}

void
e_m365_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_m365_backend_register_type (type_module);
}
