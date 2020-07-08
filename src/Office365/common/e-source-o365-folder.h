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

#ifndef E_SOURCE_O365_FOLDER_H
#define E_SOURCE_O365_FOLDER_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_O365_FOLDER \
	(e_source_o365_folder_get_type ())
#define E_SOURCE_O365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_O365_FOLDER, ESourceO365Folder))
#define E_SOURCE_O365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_O365_FOLDER, ESourceO365FolderClass))
#define E_IS_SOURCE_O365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_O365_FOLDER))
#define E_IS_SOURCE_O365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_O365_FOLDER))
#define E_SOURCE_O365_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_O365_FOLDER, ESourceO365FolderClass))

#define E_SOURCE_EXTENSION_O365_FOLDER "Office365 Folder"

G_BEGIN_DECLS

typedef struct _ESourceO365Folder ESourceO365Folder;
typedef struct _ESourceO365FolderClass ESourceO365FolderClass;
typedef struct _ESourceO365FolderPrivate ESourceO365FolderPrivate;

struct _ESourceO365Folder {
	ESourceExtension parent;
	ESourceO365FolderPrivate *priv;
};

struct _ESourceO365FolderClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_o365_folder_get_type	(void) G_GNUC_CONST;
void		e_source_o365_folder_type_register
						(GTypeModule *type_module);
const gchar *	e_source_o365_folder_get_id	(ESourceO365Folder *extension);
gchar *		e_source_o365_folder_dup_id	(ESourceO365Folder *extension);
void		e_source_o365_folder_set_id	(ESourceO365Folder *extension,
						 const gchar *id);
gboolean	e_source_o365_folder_get_is_default
						(ESourceO365Folder *extension);
void		e_source_o365_folder_set_is_default
						(ESourceO365Folder *extension,
						 gboolean value);

G_END_DECLS

#endif /* E_SOURCE_O365_FOLDER_H */
