/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
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
#include <glib/gi18n-lib.h>

#include "e-ews-folder.h"
#include "e-ews-message.h"
#include "e-ews-enumtypes.h"
#include "ews-errors.h"
#include "e-source-ews-folder.h"
#include "camel-ews-settings.h"

G_DEFINE_TYPE (EEwsFolder, e_ews_folder, G_TYPE_OBJECT)

struct _EEwsFolderPrivate {
	gchar *name;
	EwsFolderId *fid;
	EwsFolderId *parent_fid;
	gchar *folder_class;
	EEwsFolderType folder_type;
	guint32 unread;
	guint32 total;
	guint32 child_count;
	gboolean foreign;
};

static GObjectClass *parent_class = NULL;

static void
e_ews_folder_dispose (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_folder_finalize (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	priv = folder->priv;

	if (priv->name) {
		g_free (priv->name);
		priv->name = NULL;
	}

	if (priv->fid) {
		g_free (priv->fid->id);
		g_free (priv->fid->change_key);
		g_free (priv->fid);
		priv->fid = NULL;
	}

	if (priv->parent_fid) {
		g_free (priv->parent_fid->id);
		g_free (priv->parent_fid->change_key);
		g_free (priv->parent_fid);
		priv->parent_fid = NULL;
	}

	g_free (priv);
	folder->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_ews_folder_class_init (EEwsFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_folder_dispose;
	object_class->finalize = e_ews_folder_finalize;
}

static void
e_ews_folder_init (EEwsFolder *folder)
{
	EEwsFolderPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsFolderPrivate, 1);
	folder->priv = priv;

	priv->folder_type = E_EWS_FOLDER_TYPE_MAILBOX;
	priv->foreign = FALSE;
}

/* FIXME pick it from folder_type and make it set folder_type */
static void
e_ews_folder_set_folder_class (EEwsFolder *folder,
                               const gchar *folder_class)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (folder_class != NULL);

	priv = folder->priv;

	if (priv->folder_class)
		g_free (priv->folder_class);
	priv->folder_class = g_strdup (folder_class);
}

static gboolean
e_ews_folder_set_from_soap_parameter (EEwsFolder *folder,
                                      ESoapParameter *param)
{
	EEwsFolderPrivate *priv = folder->priv;
	gchar *value;
	ESoapParameter *subparam, *node;

	g_return_val_if_fail (param != NULL, FALSE);

	if ((node = e_soap_parameter_get_first_child_by_name (param, "Folder")))
		priv->folder_type = E_EWS_FOLDER_TYPE_MAILBOX;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "CalendarFolder")))
		priv->folder_type = E_EWS_FOLDER_TYPE_CALENDAR;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "ContactsFolder")))
		priv->folder_type = E_EWS_FOLDER_TYPE_CONTACTS;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "TasksFolder")))
		priv->folder_type = E_EWS_FOLDER_TYPE_TASKS;
	else {
		g_warning ("Unable to find the Folder node \n");
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderId");
	if (subparam) {
		priv->fid = g_new0 (EwsFolderId, 1);
		priv->fid->id = e_soap_parameter_get_property (subparam, "Id");
		priv->fid->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "ParentFolderId");
	if (subparam) {
		priv->parent_fid = g_new0 (EwsFolderId, 1);
		priv->parent_fid->id = e_soap_parameter_get_property (subparam, "Id");
		priv->parent_fid->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderClass");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		e_ews_folder_set_folder_class (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "DisplayName");
	if (subparam)
		priv->name = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "UnreadCount");
	if (subparam)
		priv->unread = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "TotalCount");
	if (subparam)
		priv->total = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "ChildFolderCount");
	if (subparam)
		priv->child_count = e_soap_parameter_get_int_value (subparam);

	return TRUE;
}

const gchar *
e_ews_folder_type_to_nick (EEwsFolderType folder_type)
{
	GEnumClass *enum_class;
	GEnumValue *enum_value;
	const gchar *folder_type_nick;

	enum_class = g_type_class_ref (E_TYPE_EWS_FOLDER_TYPE);
	enum_value = g_enum_get_value (enum_class, folder_type);

	if (enum_value == NULL) {
		folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;
		enum_value = g_enum_get_value (enum_class, folder_type);
	}

	g_return_val_if_fail (enum_value != NULL, NULL);

	folder_type_nick = g_intern_string (enum_value->value_nick);

	g_type_class_unref (enum_class);

	return folder_type_nick;
}

EEwsFolderType
e_ews_folder_type_from_nick (const gchar *folder_type_nick)
{
	GEnumClass *enum_class;
	GEnumValue *enum_value;
	EEwsFolderType folder_type;

	g_return_val_if_fail (
		folder_type_nick != NULL,
		E_EWS_FOLDER_TYPE_UNKNOWN);

	enum_class = g_type_class_ref (E_TYPE_EWS_FOLDER_TYPE);
	enum_value = g_enum_get_value_by_nick (enum_class, folder_type_nick);

	if (enum_value != NULL)
		folder_type = enum_value->value;
	else
		folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;

	g_type_class_unref (enum_class);

	return folder_type;
}

EEwsFolder *
e_ews_folder_new_from_soap_parameter (ESoapParameter *param)
{
	EEwsFolder *folder;

	g_return_val_if_fail (param != NULL, NULL);

	folder = g_object_new (E_TYPE_EWS_FOLDER, NULL);
	if (!e_ews_folder_set_from_soap_parameter (folder, param)) {
		g_object_unref (folder);
		return NULL;
	}

	return folder;
}

EwsFolderId *
e_ews_folder_id_new (const gchar *id,
                     const gchar *change_key,
                     gboolean is_distinguished_id)
{
	EwsFolderId *fid;

	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (id);
	fid->change_key = g_strdup (change_key);
	fid->is_distinguished_id = is_distinguished_id;

	return fid;
}

void
e_ews_folder_id_free (EwsFolderId *fid)
{
	if (fid) {
		g_free (fid->id);
		g_free (fid->change_key);
		g_free (fid);
	}
}

const gchar *
e_ews_folder_get_name (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->name;
}

void
e_ews_folder_set_name (EEwsFolder *folder,
                       const gchar *new_name)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (new_name != NULL);

	priv = folder->priv;

	if (priv->name)
		g_free (priv->name);
	priv->name = g_strdup (new_name);
}

const EwsFolderId *
e_ews_folder_get_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const EwsFolderId *) folder->priv->fid;
}

const EwsFolderId *
e_ews_folder_get_parent_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const EwsFolderId *) folder->priv->parent_fid;
}

void
e_ews_folder_set_parent_id (EEwsFolder *folder,
                            EwsFolderId *parent_fid)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (parent_fid != NULL);

	priv = folder->priv;

	if (priv->parent_fid) {
		g_free (priv->parent_fid->id);
		g_free (priv->parent_fid->change_key);
		g_free (priv->parent_fid);
	}

	priv->parent_fid = parent_fid;
}

EEwsFolderType
e_ews_folder_get_folder_type (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->folder_type;
}

void
e_ews_folder_set_folder_type (EEwsFolder *folder,
                              EEwsFolderType folder_type)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	folder->priv->folder_type = folder_type;
}

guint32
e_ews_folder_get_total_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->total;
}

guint32
e_ews_folder_get_unread_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->unread;
}

guint32
e_ews_folder_get_child_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->child_count;
}

gboolean
e_ews_folder_get_foreign (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), FALSE);

	return folder->priv->foreign;
}

void
e_ews_folder_set_foreign (EEwsFolder *folder,
                          gboolean is_foreign)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	folder->priv->foreign = is_foreign;
}

gchar *
e_ews_folder_utils_pick_color_spec (gint move_by,
                                    gboolean around_middle)
{
	static gint color_mover = 0;
	static gint color_indexer = -1;
	const guint32 colors[] = {
		0x1464ae, /* dark blue */
		0x14ae64, /* dark green */
		0xae1464, /* dark red */
		0
	};
	guint32 color;

	if (move_by <= 0)
		move_by = 1;

	while (move_by > 0) {
		move_by--;

		color_indexer++;
		if (colors[color_indexer] == 0) {
			color_mover += 1;
			color_indexer = 0;
		}
	}

	color = colors[color_indexer];
	color = (color & ~(0xFF << (color_indexer * 8))) |
		(((((color >> (color_indexer * 8)) & 0xFF) + (0x33 * color_mover)) % 0xFF) << (color_indexer * 8));

	if (around_middle) {
		gint rr, gg, bb, diff;

		rr = (0xFF0000 & color) >> 16;
		gg = (0x00FF00 & color) >>  8;
		bb = (0x0000FF & color);

		diff = 0x80 - rr;
		if (diff < 0x80 - gg)
			diff = 0x80 - gg;
		if (diff < 0x80 - bb)
			diff = 0x80 - bb;

		rr = rr + diff < 0 ? 0 : rr + diff > 0xCC ? 0xCC : rr + diff;
		gg = gg + diff < 0 ? 0 : gg + diff > 0xCC ? 0xCC : gg + diff;
		bb = bb + diff < 0 ? 0 : bb + diff > 0xCC ? 0xCC : bb + diff;

		color = (rr << 16) + (gg << 8) + bb;
	}

	return g_strdup_printf ("#%06x", color);
}

gboolean
e_ews_folder_utils_populate_esource (ESource *source,
                                     const GList *sources,
                                     const gchar *master_hosturl,
                                     const gchar *master_username,
                                     EEwsFolder *folder,
                                     gboolean offline_sync,
                                     gint color_seed,
                                     GCancellable *cancellable,
                                     GError **perror)
{
	ESource *master_source;
	gboolean res = FALSE;

	master_source = e_ews_folder_utils_get_master_source (sources, master_hosturl, master_username);

	if (master_source) {
		ESourceBackend *backend_ext;
		EEwsFolderType folder_type;
		const EwsFolderId *folder_id = e_ews_folder_get_id (folder);

		g_return_val_if_fail (folder_id != NULL, FALSE);

		folder_type = e_ews_folder_get_folder_type (folder);

		e_source_set_parent (source, e_source_get_uid (master_source));
		e_source_set_display_name (source, e_ews_folder_get_name (folder));

		switch (folder_type) {
			case E_EWS_FOLDER_TYPE_CALENDAR:
				backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
				break;
			/*case E_EWS_FOLDER_TYPE_JOURNAL:
			case E_EWS_FOLDER_TYPE_MEMO:
				backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_MEMO_LIST);
				break;*/
			case E_EWS_FOLDER_TYPE_TASKS:
				backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
				break;
			case E_EWS_FOLDER_TYPE_CONTACTS:
				backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK);
				break;
			default:
				backend_ext = NULL;
				break;
		}

		if (backend_ext) {
			ESourceEwsFolder *folder_ext;
			ESourceOffline *offline_ext;

			e_source_backend_set_backend_name (backend_ext , "ews");

			folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);
			e_source_ews_folder_set_id (folder_ext, folder_id->id);
			e_source_ews_folder_set_change_key (folder_ext, NULL);
			e_source_ews_folder_set_foreign (folder_ext, e_ews_folder_get_foreign (folder));

			offline_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
			e_source_offline_set_stay_synchronized (offline_ext, offline_sync);

			/* set also color for calendar-like sources */
			if (folder_type != E_EWS_FOLDER_TYPE_CONTACTS) {
				gchar *color_str;

				color_str = e_ews_folder_utils_pick_color_spec (
					1 + g_list_length ((GList *) sources),
					folder_type != E_EWS_FOLDER_TYPE_CALENDAR);
				e_source_selectable_set_color (E_SOURCE_SELECTABLE (backend_ext), color_str);
				g_free (color_str);
			}

			res = TRUE;
		} else {
			g_propagate_error (
				perror, g_error_new_literal (EWS_CONNECTION_ERROR,
				EWS_CONNECTION_ERROR_NORESPONSE, _("Cannot add folder, unsupported folder type")));
		}
	} else {
		g_propagate_error (
			perror, g_error_new_literal (EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_NORESPONSE, _("Cannot add folder, master source not found")));
	}

	return res;
}

gboolean
e_ews_folder_utils_add_as_esource (ESourceRegistry *pregistry,
                                   const gchar *master_hosturl,
                                   const gchar *master_username,
                                   EEwsFolder *folder,
                                   gboolean offline_sync,
                                   gint color_seed,
                                   GCancellable *cancellable,
                                   GError **perror)
{
	ESourceRegistry *registry;
	GList *sources;
	ESource *source, *old_source;
	const EwsFolderId *fid;
	gboolean res = FALSE;

	registry = pregistry;
	if (!registry) {
		registry = e_source_registry_new_sync (cancellable, perror);
		if (!registry)
			return FALSE;
	}

	sources = e_source_registry_list_sources (registry, NULL);
	source = e_source_new (NULL, NULL, NULL);
	fid = e_ews_folder_get_id (folder);

	old_source = e_ews_folder_utils_get_source_for_folder (sources, master_hosturl, master_username, fid->id);
	if (old_source) {
		res = FALSE;

		g_propagate_error (
			perror,
			g_error_new (EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDEREXISTS,
			_("Cannot add folder, folder already exists as '%s'"), e_source_get_display_name (old_source)));
	} else if (e_ews_folder_utils_populate_esource (
		source,
		sources,
		master_hosturl,
		master_username,
		folder,
		offline_sync,
		color_seed,
		cancellable,
		perror)) {
		res = e_source_registry_commit_source_sync (registry, source, cancellable, perror);
	}
	g_object_unref (source);

	g_list_free_full (sources, g_object_unref);
	if (!pregistry)
		g_object_unref (registry);

	return res;
}

gboolean
e_ews_folder_utils_remove_as_esource (ESourceRegistry *pregistry,
                                      const gchar *master_hosturl,
                                      const gchar *master_username,
                                      const gchar *folder_id,
                                      GCancellable *cancellable,
                                      GError **perror)
{
	ESourceRegistry *registry;
	ESource *source;
	GList *sources;
	gboolean res = TRUE;

	registry = pregistry;
	if (!registry) {
		registry = e_source_registry_new_sync (cancellable, perror);
		if (!registry)
			return FALSE;
	}

	sources = e_source_registry_list_sources (registry, NULL);
	source = e_ews_folder_utils_get_source_for_folder (sources, master_hosturl, master_username, folder_id);

	if (source)
		res = e_source_remove_sync (source, cancellable, perror);

	g_list_free_full (sources, g_object_unref);
	if (!pregistry)
		g_object_unref (registry);

	return res;
}

gboolean
e_ews_folder_utils_is_subscribed_as_esource (const GList *esources,
                                             const gchar *master_hosturl,
                                             const gchar *master_username,
                                             const gchar *folder_id)
{
	return e_ews_folder_utils_get_source_for_folder (esources, master_hosturl, master_username, folder_id) != NULL;
}

static gboolean
is_for_account (ESource *source,
                const gchar *master_hosturl,
                const gchar *master_username)
{
	ESourceCamel *camel_extension;
	ESourceAuthentication *auth_extension;
	CamelEwsSettings *settings;
	const gchar *extension_name;

	if (!source)
		return FALSE;

	if (!master_hosturl && !master_username)
		return TRUE;

	extension_name = e_source_camel_get_extension_name ("ews");
	if (!e_source_has_extension (source, extension_name))
		return FALSE;

	camel_extension = e_source_get_extension (source, extension_name);
	settings = CAMEL_EWS_SETTINGS (e_source_camel_get_settings (camel_extension));

	if (!settings || g_strcmp0 (camel_ews_settings_get_hosturl (settings), master_hosturl) != 0)
		return FALSE;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (!e_source_has_extension (source, extension_name))
		return FALSE;

	auth_extension = e_source_get_extension (source, extension_name);
	return g_strcmp0 (e_source_authentication_get_user (auth_extension), master_username) == 0;
}

/* filters @esources thus the resulting list will contain ESource-s only for @profile;
 * free returned list with g_list_free_full (list, g_object_unref); */
GList *
e_ews_folder_utils_filter_sources_for_account (const GList *esources,
                                               const gchar *master_hosturl,
                                               const gchar *master_username)
{
	GList *found = NULL;
	const GList *iter;
	ESource *master_source;

	master_source = e_ews_folder_utils_get_master_source (esources, master_hosturl, master_username);
	if (!master_source)
		return NULL;

	for (iter = esources; iter; iter = iter->next) {
		ESource *source = iter->data;

		if (is_for_account (source, master_hosturl, master_username) ||
		    g_strcmp0 (e_source_get_uid (master_source), e_source_get_parent (source)) == 0)
			found = g_list_prepend (found, g_object_ref (source));
	}

	return g_list_reverse (found);
}

/* returns (not-reffed) member of @esources, which is for @profile and @folder_id */
ESource *
e_ews_folder_utils_get_source_for_folder (const GList *esources,
                                          const gchar *master_hosturl,
                                          const gchar *master_username,
                                          const gchar *folder_id)
{
	ESource *master_source;
	const GList *iter;

	master_source = e_ews_folder_utils_get_master_source (esources, master_hosturl, master_username);
	if (!master_source)
		return NULL;

	for (iter = esources; iter; iter = iter->next) {
		ESource *source = iter->data;

		if ((is_for_account (source, master_hosturl, master_username) ||
		    g_strcmp0 (e_source_get_uid (master_source), e_source_get_parent (source)) == 0) &&
		    e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
			ESourceEwsFolder *folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);

			g_return_val_if_fail (folder_ext != NULL, NULL);

			if (g_strcmp0 (e_source_ews_folder_get_id (folder_ext), folder_id) == 0)
				return source;
		}
	}

	return NULL;
}

/* returns (not-reffed) member of @esources, which is master (with no parent) source for @profile */
ESource *
e_ews_folder_utils_get_master_source (const GList *esources,
                                      const gchar *master_hosturl,
                                      const gchar *master_username)
{
	const GList *iter;

	for (iter = esources; iter; iter = iter->next) {
		ESource *source = iter->data;

		if (!e_source_get_parent (source) &&
		    is_for_account (source, master_hosturl, master_username))
			return source;
	}

	return NULL;
}
