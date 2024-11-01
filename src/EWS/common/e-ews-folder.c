/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include "e-ews-folder.h"
#include "e-ews-request.h"
#include "e-ews-enumtypes.h"
#include "ews-errors.h"
#include "e-source-ews-folder.h"
#include "camel-ews-settings.h"

struct _EEwsFolderPrivate {
	GError *error;
	gchar *name;
	gchar *escaped_name;
	EwsFolderId *fid;
	EwsFolderId *parent_fid;
	EEwsFolderType folder_type;
	guint32 unread;
	guint32 total;
	guint32 child_count;
	guint64 size;
	gboolean foreign;
	gchar *foreign_mail;
	gboolean is_public;
	gboolean is_hidden;
};

G_DEFINE_TYPE_WITH_PRIVATE (EEwsFolder, e_ews_folder, G_TYPE_OBJECT)

static void
e_ews_folder_dispose (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	G_OBJECT_CLASS (e_ews_folder_parent_class)->dispose (object);
}

static void
e_ews_folder_finalize (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	priv = folder->priv;

	g_clear_error (&priv->error);
	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->escaped_name, g_free);
	g_clear_pointer (&priv->foreign_mail, g_free);

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

	G_OBJECT_CLASS (e_ews_folder_parent_class)->finalize (object);
}

static void
e_ews_folder_class_init (EEwsFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_ews_folder_dispose;
	object_class->finalize = e_ews_folder_finalize;
}

static void
e_ews_folder_init (EEwsFolder *folder)
{
	folder->priv = e_ews_folder_get_instance_private (folder);
	folder->priv->error = NULL;
	folder->priv->folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;
	folder->priv->foreign = FALSE;
	folder->priv->is_public = FALSE;
	folder->priv->is_hidden = FALSE;
}

static gboolean
e_ews_folder_set_from_soap_parameter (EEwsFolder *folder,
                                      ESoapParameter *param)
{
	EEwsFolderPrivate *priv = folder->priv;
	ESoapParameter *subparam, *node;

	g_return_val_if_fail (param != NULL, FALSE);

	if (g_strcmp0 (e_soap_parameter_get_name (param), "Folder") == 0 ||
	    g_strcmp0 (e_soap_parameter_get_name (param), "SearchFolder") == 0 ) {
		node = param;
		priv->folder_type = E_EWS_FOLDER_TYPE_MAILBOX;
	} else if (g_strcmp0 (e_soap_parameter_get_name (param), "CalendarFolder") == 0) {
		node = param;
		priv->folder_type = E_EWS_FOLDER_TYPE_CALENDAR;
	} else if (g_strcmp0 (e_soap_parameter_get_name (param), "ContactsFolder") == 0) {
		node = param;
		priv->folder_type = E_EWS_FOLDER_TYPE_CONTACTS;
	} else if (g_strcmp0 (e_soap_parameter_get_name (param), "TasksFolder") == 0) {
		node = param;
		priv->folder_type = E_EWS_FOLDER_TYPE_TASKS;
	} else if ((node = e_soap_parameter_get_first_child_by_name (param, "Folder")) ||
		   (node = e_soap_parameter_get_first_child_by_name (param, "SearchFolder")))
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

	if (priv->folder_type == E_EWS_FOLDER_TYPE_MAILBOX) {
		subparam = e_soap_parameter_get_first_child_by_name (node, "FolderClass");
		if (subparam) {
			EEwsFolderType folder_type;
			gchar *folder_class = e_soap_parameter_get_string_value (subparam);

			folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;

			if (g_strcmp0 (folder_class, "IPF.Note") == 0 || (folder_class && g_str_has_prefix (folder_class, "IPF.Note."))) {
				folder_type = E_EWS_FOLDER_TYPE_MAILBOX;
			} else if (g_strcmp0 (folder_class, "IPF.Contact") == 0) {
				folder_type = E_EWS_FOLDER_TYPE_CONTACTS;
			} else if (g_strcmp0 (folder_class, "IPF.Appointment") == 0) {
				folder_type = E_EWS_FOLDER_TYPE_CALENDAR;
			} else if (g_strcmp0 (folder_class, "IPF.Task") == 0) {
				folder_type = E_EWS_FOLDER_TYPE_TASKS;
			} else if (g_strcmp0 (folder_class, "IPF.StickyNote") == 0) {
				folder_type = E_EWS_FOLDER_TYPE_MEMOS;
			}

			priv->folder_type = folder_type;

			g_free (folder_class);
		}
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

	subparam = e_soap_parameter_get_first_child_by_name (node, "DisplayName");
	if (subparam) {
		priv->name = e_soap_parameter_get_string_value (subparam);
		priv->escaped_name = e_ews_folder_utils_escape_name (priv->name);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "UnreadCount");
	if (subparam)
		priv->unread = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "TotalCount");
	if (subparam)
		priv->total = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "ChildFolderCount");
	if (subparam)
		priv->child_count = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "ExtendedProperty");
	if (subparam) {
		ESoapParameter *subparam1;
		gchar *prop_tag = NULL;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "ExtendedFieldURI");
		if (subparam1) {
			prop_tag = e_soap_parameter_get_property (subparam1, "PropertyTag");
			if (prop_tag && g_ascii_strcasecmp (prop_tag, "0xe08") == 0) {
				subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Value");
				if (subparam1)
					priv->size = e_soap_parameter_get_uint64_value (subparam1);
			} else if (prop_tag && g_ascii_strcasecmp (prop_tag, "0x10f4") == 0) { /* PidTagAttributeHidden */
				subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Value");
				if (subparam1) {
					gchar *value;

					value = e_soap_parameter_get_string_value (subparam1);
					priv->is_hidden = g_strcmp0 (value, "true") == 0;
					g_free (value);
				}
			}
			g_free (prop_tag);
		}
	}

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

EEwsFolder *
e_ews_folder_new_from_error (const GError *error)
{
	EEwsFolder *folder;

	g_return_val_if_fail (error != NULL, NULL);

	folder = g_object_new (E_TYPE_EWS_FOLDER, NULL);
	folder->priv->error = g_error_copy (error);

	return folder;
}

gboolean
e_ews_folder_is_error (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), TRUE);

	return folder->priv->error != NULL;
}

const GError *
e_ews_folder_get_error (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return folder->priv->error;
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

gboolean
e_ews_folder_id_is_equal (const EwsFolderId *a,
			  const EwsFolderId *b,
			  gboolean check_change_key)
{
	if (a == NULL && b == NULL)
		return TRUE;

	if (a == NULL || b == NULL)
		return FALSE;

	if ((a->is_distinguished_id ? 1 : 0) != (b->is_distinguished_id ? 1 : 0))
		return FALSE;

	if (g_strcmp0 (a->id, b->id) != 0)
		return FALSE;

	if (check_change_key)
		if (g_strcmp0 (a->change_key, b->change_key) != 0)
			return FALSE;

	return TRUE;
}

void
e_ews_folder_id_append_to_request (ESoapRequest *request,
				   const gchar *email,
				   const EwsFolderId *fid)
{
	g_return_if_fail (request != NULL);
	g_return_if_fail (fid != NULL);

	if (fid->is_distinguished_id)
		e_soap_request_start_element (request, "DistinguishedFolderId", NULL, NULL);
	else
		e_soap_request_start_element (request, "FolderId", NULL, NULL);

	e_soap_request_add_attribute (request, "Id", fid->id, NULL, NULL);
	if (fid->change_key)
		e_soap_request_add_attribute (request, "ChangeKey", fid->change_key, NULL, NULL);

	if (fid->is_distinguished_id && email) {
		e_soap_request_start_element (request, "Mailbox", NULL, NULL);
		e_ews_request_write_string_parameter (request, "EmailAddress", NULL, email);
		e_soap_request_end_element (request);
	}

	e_soap_request_end_element (request);
}

const gchar *
e_ews_folder_get_name (const EEwsFolder *folder)
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

	g_free (priv->name);
	g_free (priv->escaped_name);

	priv->name = g_strdup (new_name);
	priv->escaped_name = e_ews_folder_utils_escape_name (priv->name);
}

const gchar *
e_ews_folder_get_escaped_name (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return folder->priv->escaped_name;
}

const EwsFolderId *
e_ews_folder_get_id (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const EwsFolderId *) folder->priv->fid;
}

void
e_ews_folder_set_id (EEwsFolder *folder,
		     EwsFolderId *fid)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	e_ews_folder_id_free (folder->priv->fid);
	folder->priv->fid = fid;
}

const EwsFolderId *
e_ews_folder_get_parent_id (const EEwsFolder *folder)
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

gboolean
e_ews_folder_get_is_hidden (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), FALSE);

	return folder->priv->is_hidden;
}

EEwsFolderType
e_ews_folder_get_folder_type (const EEwsFolder *folder)
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
e_ews_folder_get_total_count (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->total;
}

guint32
e_ews_folder_get_unread_count (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->unread;
}

guint32
e_ews_folder_get_child_count (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->child_count;
}

guint64
e_ews_folder_get_size (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->size;
}

gboolean
e_ews_folder_get_foreign (const EEwsFolder *folder)
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

const gchar *
e_ews_folder_get_foreign_mail (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return folder->priv->foreign_mail;
}

void
e_ews_folder_set_foreign_mail (EEwsFolder *folder,
			       const gchar *foreign_mail)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	g_free (folder->priv->foreign_mail);
	folder->priv->foreign_mail = g_strdup (foreign_mail);
}

gboolean
e_ews_folder_get_public (const EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), FALSE);

	return folder->priv->is_public;
}

void
e_ews_folder_set_public (EEwsFolder *folder,
			 gboolean is_public)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	folder->priv->is_public = is_public;
}

/* escapes backslashes with \5C and forward slashes with \2F */
gchar *
e_ews_folder_utils_escape_name (const gchar *folder_name)
{
	gint ii, jj, count = 0;
	gchar *res;

	if (!folder_name)
		return NULL;

	for (ii = 0; folder_name[ii]; ii++) {
		if (folder_name[ii] == '\\' || folder_name[ii] == '/')
			count++;
	}

	if (!count)
		return g_strdup (folder_name);

	res = g_malloc0 (sizeof (gchar) * (1 + ii + (2 * count)));
	for (ii = 0, jj = 0; folder_name[ii]; ii++, jj++) {
		if (folder_name[ii] == '\\') {
			res[jj] = '\\';
			res[jj + 1] = '5';
			res[jj + 2] = 'C';
			jj += 2;
		} else if (folder_name[ii] == '/') {
			res[jj] = '\\';
			res[jj + 1] = '2';
			res[jj + 2] = 'F';
			jj += 2;
		} else {
			res[jj] = folder_name[ii];
		}
	}

	res[jj] = '\0';

	return res;
}

/* reverses e_ews_folder_utils_escape_name() processing */
gchar *
e_ews_folder_utils_unescape_name (const gchar *escaped_folder_name)
{
	gchar *res = g_strdup (escaped_folder_name);
	gint ii, jj;

	if (!res)
		return res;

	for (ii = 0, jj = 0; res[ii]; ii++, jj++) {
		if (res[ii] == '\\' && g_ascii_isxdigit (res[ii + 1]) && g_ascii_isxdigit (res[ii + 2])) {
			res[jj] = ((g_ascii_xdigit_value (res[ii + 1]) & 0xF) << 4) | (g_ascii_xdigit_value (res[ii + 2]) & 0xF);
			ii += 2;
		} else if (ii != jj) {
			res[jj] = res[ii];
		}
	}

	res[jj] = '\0';

	return res;
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
				     EEwsESourceFlags flags,
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
			case E_EWS_FOLDER_TYPE_MEMOS:
				backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_MEMO_LIST);
				break;
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

			e_source_backend_set_backend_name (backend_ext, "ews");

			folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);
			e_source_ews_folder_set_id (folder_ext, folder_id->id);
			e_source_ews_folder_set_change_key (folder_ext, NULL);
			e_source_ews_folder_set_name (folder_ext, e_ews_folder_get_name (folder));
			e_source_ews_folder_set_foreign (folder_ext, e_ews_folder_get_foreign (folder));
			e_source_ews_folder_set_foreign_subfolders (folder_ext, (flags & E_EWS_ESOURCE_FLAG_INCLUDE_SUBFOLDERS) != 0);
			e_source_ews_folder_set_foreign_mail (folder_ext, e_ews_folder_get_foreign_mail (folder));
			e_source_ews_folder_set_public (folder_ext, (flags & E_EWS_ESOURCE_FLAG_PUBLIC_FOLDER) != 0);

			offline_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
			e_source_offline_set_stay_synchronized (offline_ext, (flags & E_EWS_ESOURCE_FLAG_OFFLINE_SYNC) != 0);

			/* set also color for calendar-like sources */
			if (folder_type != E_EWS_FOLDER_TYPE_CONTACTS) {
				ESourceAlarms *alarms;
				gchar *color_str;

				color_str = e_ews_folder_utils_pick_color_spec (
					1 + g_list_length ((GList *) sources),
					folder_type != E_EWS_FOLDER_TYPE_CALENDAR);
				e_source_selectable_set_color (E_SOURCE_SELECTABLE (backend_ext), color_str);
				g_free (color_str);

				alarms = e_source_get_extension (source, E_SOURCE_EXTENSION_ALARMS);
				e_source_alarms_set_include_me (alarms, FALSE);
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
				   EEwsESourceFlags flags,
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
			_("Cannot add folder, folder already exists as “%s”"), e_source_get_display_name (old_source)));
	} else if (e_ews_folder_utils_populate_esource (
		source,
		sources,
		master_hosturl,
		master_username,
		folder,
		flags,
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
e_ews_folder_utils_remove_as_esource (const gchar *master_hosturl,
                                      const gchar *master_username,
                                      const gchar *folder_id,
                                      GCancellable *cancellable,
                                      GError **perror)
{
	ESourceRegistry *registry;
	ESource *source;
	GList *sources;
	gboolean res = TRUE;

	registry = e_source_registry_new_sync (cancellable, perror);
	if (!registry)
		return FALSE;

	sources = e_source_registry_list_sources (registry, NULL);
	source = e_ews_folder_utils_get_source_for_folder (sources, master_hosturl, master_username, folder_id);

	if (source) {
		if (e_source_get_removable (source))
			res = e_source_remove_sync (source, cancellable, perror);
		else
			res = e_source_remote_delete_sync (source, cancellable, perror);
	}

	g_list_free_full (sources, g_object_unref);
	g_object_unref (registry);

	return res;
}

GList *
e_ews_folder_utils_get_esources (const gchar *master_hosturl,
				 const gchar *master_username,
				 GCancellable *cancellable,
				 GError **perror)
{
	ESourceRegistry *registry;
	GList *all_sources, *esources = NULL;

	registry = e_source_registry_new_sync (cancellable, perror);
	if (!registry)
		return NULL;

	all_sources = e_source_registry_list_sources (registry, NULL);
	esources = e_ews_folder_utils_filter_sources_for_account (all_sources, master_hosturl, master_username);

	g_list_free_full (all_sources, g_object_unref);
	g_object_unref (registry);

	return esources;
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
