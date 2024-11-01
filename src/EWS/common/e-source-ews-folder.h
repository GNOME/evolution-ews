/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_EWS_FOLDER_H
#define E_SOURCE_EWS_FOLDER_H

#include <libedataserver/libedataserver.h>
#include "common/e-ews-folder.h"

/* Standard GObject macros */
#define E_TYPE_SOURCE_EWS_FOLDER \
	(e_source_ews_folder_get_type ())
#define E_SOURCE_EWS_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_EWS_FOLDER, ESourceEwsFolder))
#define E_SOURCE_EWS_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_EWS_FOLDER, ESourceEwsFolderClass))
#define E_IS_SOURCE_EWS_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_EWS_FOLDER))
#define E_IS_SOURCE_EWS_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_EWS_FOLDER))
#define E_SOURCE_EWS_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_EWS_FOLDER, ESourceEwsFolderClass))

#define E_SOURCE_EXTENSION_EWS_FOLDER "Exchange Web Services Folder"

G_BEGIN_DECLS

typedef struct _ESourceEwsFolder ESourceEwsFolder;
typedef struct _ESourceEwsFolderClass ESourceEwsFolderClass;
typedef struct _ESourceEwsFolderPrivate ESourceEwsFolderPrivate;

struct _ESourceEwsFolder {
	ESourceExtension parent;
	ESourceEwsFolderPrivate *priv;
};

struct _ESourceEwsFolderClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_ews_folder_get_type	(void) G_GNUC_CONST;
void		e_source_ews_folder_type_register
						(GTypeModule *type_module);
const gchar *	e_source_ews_folder_get_change_key
						(ESourceEwsFolder *extension);
gchar *		e_source_ews_folder_dup_change_key
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_change_key
						(ESourceEwsFolder *extension,
						 const gchar *change_key);
const gchar *	e_source_ews_folder_get_id	(ESourceEwsFolder *extension);
gchar *		e_source_ews_folder_dup_id	(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_id	(ESourceEwsFolder *extension,
						 const gchar *id);
const gchar *	e_source_ews_folder_get_name	(ESourceEwsFolder *extension);
gchar *		e_source_ews_folder_dup_name	(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_name	(ESourceEwsFolder *extension,
						 const gchar *name);

EwsFolderId *	e_source_ews_folder_dup_folder_id
						(ESourceEwsFolder *extension);

gboolean	e_source_ews_folder_get_foreign	(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_foreign	(ESourceEwsFolder *extension,
						 gboolean is_foreign);
gboolean	e_source_ews_folder_get_foreign_subfolders
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_foreign_subfolders
						(ESourceEwsFolder *extension,
						 gboolean foreign_subfolders);
const gchar *	e_source_ews_folder_get_foreign_mail
						(ESourceEwsFolder *extension);
gchar *		e_source_ews_folder_dup_foreign_mail
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_foreign_mail
						(ESourceEwsFolder *extension,
						 const gchar *foreign_mail);
guint		e_source_ews_folder_get_freebusy_weeks_before
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_freebusy_weeks_before
						(ESourceEwsFolder *extension,
						 guint freebusy_weeks_before);
guint		e_source_ews_folder_get_freebusy_weeks_after
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_freebusy_weeks_after
						(ESourceEwsFolder *extension,
						 guint freebusy_weeks_after);
gboolean	e_source_ews_folder_get_public	(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_public	(ESourceEwsFolder *extension,
						 gboolean is_public);
gboolean	e_source_ews_folder_get_use_primary_address
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_use_primary_address
						(ESourceEwsFolder *extension,
						 gboolean use_primary_address);
gboolean	e_source_ews_folder_get_fetch_gal_photos
						(ESourceEwsFolder *extension);
void		e_source_ews_folder_set_fetch_gal_photos
						(ESourceEwsFolder *extension,
						 gboolean fetch_gal_photos);

G_END_DECLS

#endif /* E_SOURCE_EWS_FOLDER_H */

