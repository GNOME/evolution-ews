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

#ifndef E_SOURCE_O365_DELTAS_H
#define E_SOURCE_O365_DELTAS_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_O365_DELTAS \
	(e_source_o365_deltas_get_type ())
#define E_SOURCE_O365_DELTAS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_O365_DELTAS, ESourceO365Deltas))
#define E_SOURCE_O365_DELTAS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_O365_DELTAS, ESourceO365DeltasClass))
#define E_IS_SOURCE_O365_DELTAS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_O365_DELTAS))
#define E_IS_SOURCE_O365_DELTAS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_O365_DELTAS))
#define E_SOURCE_O365_DELTAS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_O365_DELTAS, ESourceO365DeltasClass))

#define E_SOURCE_EXTENSION_O365_DELTAS "Office365 Deltas"

G_BEGIN_DECLS

typedef struct _ESourceO365Deltas ESourceO365Deltas;
typedef struct _ESourceO365DeltasClass ESourceO365DeltasClass;
typedef struct _ESourceO365DeltasPrivate ESourceO365DeltasPrivate;

struct _ESourceO365Deltas {
	ESourceExtension parent;
	ESourceO365DeltasPrivate *priv;
};

struct _ESourceO365DeltasClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_o365_deltas_get_type	(void) G_GNUC_CONST;
void		e_source_o365_deltas_type_register
						(GTypeModule *type_module);
const gchar *	e_source_o365_deltas_get_contacts_link
						(ESourceO365Deltas *extension);
gchar *		e_source_o365_deltas_dup_contacts_link
						(ESourceO365Deltas *extension);
void		e_source_o365_deltas_set_contacts_link
						(ESourceO365Deltas *extension,
						 const gchar *delta_link);

G_END_DECLS

#endif /* E_SOURCE_O365_DELTAS_H */
