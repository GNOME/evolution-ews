/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_M365_FOLDER_H
#define E_SOURCE_M365_FOLDER_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_M365_FOLDER \
	(e_source_m365_folder_get_type ())
#define E_SOURCE_M365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_M365_FOLDER, ESourceM365Folder))
#define E_SOURCE_M365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_M365_FOLDER, ESourceM365FolderClass))
#define E_IS_SOURCE_M365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_M365_FOLDER))
#define E_IS_SOURCE_M365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_M365_FOLDER))
#define E_SOURCE_M365_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_M365_FOLDER, ESourceM365FolderClass))

#define E_SOURCE_EXTENSION_M365_FOLDER "Microsoft365 Folder"

G_BEGIN_DECLS

typedef struct _ESourceM365Folder ESourceM365Folder;
typedef struct _ESourceM365FolderClass ESourceM365FolderClass;
typedef struct _ESourceM365FolderPrivate ESourceM365FolderPrivate;

struct _ESourceM365Folder {
	ESourceExtension parent;
	ESourceM365FolderPrivate *priv;
};

struct _ESourceM365FolderClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_m365_folder_get_type	(void) G_GNUC_CONST;
void		e_source_m365_folder_type_register
						(GTypeModule *type_module);
const gchar *	e_source_m365_folder_get_id	(ESourceM365Folder *extension);
gchar *		e_source_m365_folder_dup_id	(ESourceM365Folder *extension);
void		e_source_m365_folder_set_id	(ESourceM365Folder *extension,
						 const gchar *id);
gboolean	e_source_m365_folder_get_is_default
						(ESourceM365Folder *extension);
void		e_source_m365_folder_set_is_default
						(ESourceM365Folder *extension,
						 gboolean value);
const gchar *	e_source_m365_folder_get_group_id
						(ESourceM365Folder *extension);
gchar *		e_source_m365_folder_dup_group_id
						(ESourceM365Folder *extension);
void		e_source_m365_folder_set_group_id
						(ESourceM365Folder *extension,
						 const gchar *group_id);
const gchar *	e_source_m365_folder_get_display_name
						(ESourceM365Folder *extension);
gchar *		e_source_m365_folder_dup_display_name
						(ESourceM365Folder *extension);
void		e_source_m365_folder_set_display_name
						(ESourceM365Folder *extension,
						 const gchar *display_name);
const gchar *	e_source_m365_folder_get_color	(ESourceM365Folder *extension);
gchar *		e_source_m365_folder_dup_color	(ESourceM365Folder *extension);
void		e_source_m365_folder_set_color	(ESourceM365Folder *extension,
						 const gchar *color);
guint		e_source_m365_folder_get_max_people
						(ESourceM365Folder *extension);
void		e_source_m365_folder_set_max_people
						(ESourceM365Folder *extension,
						 guint max_people);

G_END_DECLS

#endif /* E_SOURCE_M365_FOLDER_H */
