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

#ifndef E_EWS_FOLDER_H
#define E_EWS_FOLDER_H

#include <libedataserver/libedataserver.h>

#include "e-ews-enums.h"
#include "e-soap-message.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_FOLDER            (e_ews_folder_get_type ())
#define E_EWS_FOLDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_FOLDER, EEwsFolder))
#define E_EWS_FOLDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_FOLDER, EEwsFolderClass))
#define E_IS_EWS_FOLDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_FOLDER))
#define E_IS_EWS_FOLDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_FOLDER))

typedef struct _EEwsFolder        EEwsFolder;
typedef struct _EEwsFolderClass   EEwsFolderClass;
typedef struct _EEwsFolderPrivate EEwsFolderPrivate;

struct _EEwsFolder {
	GObject parent;
	EEwsFolderPrivate *priv;
};

struct _EEwsFolderClass {
	GObjectClass parent_class;
};

typedef struct {
	gchar *id;
	gchar *change_key;
	gboolean is_distinguished_id;
} EwsFolderId;

GType		e_ews_folder_get_type (void);
const gchar *	e_ews_folder_type_to_nick (EEwsFolderType folder_type);
EEwsFolderType	e_ews_folder_type_from_nick (const gchar *folder_type_nick);
EEwsFolder *	e_ews_folder_new_from_soap_parameter (ESoapParameter *param);
const gchar *	e_ews_folder_get_name (EEwsFolder *folder);
void		e_ews_folder_set_name (EEwsFolder *folder, const gchar *new_name);
void		e_ews_folder_set_parent_id (EEwsFolder *folder, EwsFolderId *fid);
const EwsFolderId *
		e_ews_folder_get_parent_id (EEwsFolder *folder);
const EwsFolderId *
		e_ews_folder_get_id (EEwsFolder *folder);
guint32		e_ews_folder_get_total_count (EEwsFolder *folder);
guint32		e_ews_folder_get_unread_count (EEwsFolder *folder);
guint32		e_ews_folder_get_child_count (EEwsFolder *folder);
gboolean	e_ews_folder_get_is_writable (EEwsFolder *folder);
void		e_ews_folder_set_is_writable (EEwsFolder *folder, gboolean writable);
EEwsFolderType	e_ews_folder_get_folder_type (EEwsFolder *folder);
void		e_ews_folder_set_folder_type (EEwsFolder *folder, EEwsFolderType folder_type);
gboolean	e_ews_folder_get_foreign (EEwsFolder *folder);
void		e_ews_folder_set_foreign (EEwsFolder *folder, gboolean is_foreign);

EwsFolderId *	e_ews_folder_id_new (const gchar *id,
				     const gchar *change_key,
				     gboolean is_distinguished_id);
void		e_ews_folder_id_free (EwsFolderId *fid);

gchar *		e_ews_folder_utils_pick_color_spec		(gint move_by,
								 gboolean around_middle);
gboolean	e_ews_folder_utils_populate_esource		(ESource *source,
								 const GList *sources,
								 const gchar *master_hosturl,
								 const gchar *master_username,
								 EEwsFolder *folder,
								 gboolean include_subfolders,
								 gboolean offline_sync,
								 gint color_seed,
								 GCancellable *cancellable,
								 GError **perror);
gboolean	e_ews_folder_utils_add_as_esource		(ESourceRegistry *pregistry,
								 const gchar *master_hosturl,
								 const gchar *master_username,
								 EEwsFolder *folder,
								 gboolean include_subfolders,
								 gboolean offline_sync,
								 gint color_seed,
								 GCancellable *cancellable,
								 GError **perror);
gboolean	e_ews_folder_utils_remove_as_esource		(ESourceRegistry *pregistry,
								 const gchar *master_hosturl,
								 const gchar *master_username,
								 const gchar *folder_id,
								 GCancellable *cancellable,
								 GError **perror);
gboolean	e_ews_folder_utils_is_subscribed_as_esource	(const GList *esources,
								 const gchar *master_hosturl,
								 const gchar *master_username,
								 const gchar *folder_id);
GList *		e_ews_folder_utils_filter_sources_for_account	(const GList *esources,
								 const gchar *master_hosturl,
								 const gchar *master_username);
ESource *	e_ews_folder_utils_get_source_for_folder	(const GList *esources,
								 const gchar *master_hosturl,
								 const gchar *master_username,
								 const gchar *folder_id);
ESource *	e_ews_folder_utils_get_master_source		(const GList *esources,
								 const gchar *master_hosturl,
								 const gchar *master_username);

G_END_DECLS

#endif
