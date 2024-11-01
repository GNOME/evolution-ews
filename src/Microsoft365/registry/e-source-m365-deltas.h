/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_SOURCE_M365_DELTAS_H
#define E_SOURCE_M365_DELTAS_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_M365_DELTAS \
	(e_source_m365_deltas_get_type ())
#define E_SOURCE_M365_DELTAS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_M365_DELTAS, ESourceM365Deltas))
#define E_SOURCE_M365_DELTAS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_M365_DELTAS, ESourceM365DeltasClass))
#define E_IS_SOURCE_M365_DELTAS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_M365_DELTAS))
#define E_IS_SOURCE_M365_DELTAS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_M365_DELTAS))
#define E_SOURCE_M365_DELTAS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_M365_DELTAS, ESourceM365DeltasClass))

#define E_SOURCE_EXTENSION_M365_DELTAS "Microsoft365 Deltas"

G_BEGIN_DECLS

typedef struct _ESourceM365Deltas ESourceM365Deltas;
typedef struct _ESourceM365DeltasClass ESourceM365DeltasClass;
typedef struct _ESourceM365DeltasPrivate ESourceM365DeltasPrivate;

struct _ESourceM365Deltas {
	ESourceExtension parent;
	ESourceM365DeltasPrivate *priv;
};

struct _ESourceM365DeltasClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_m365_deltas_get_type	(void) G_GNUC_CONST;
void		e_source_m365_deltas_type_register
						(GTypeModule *type_module);
const gchar *	e_source_m365_deltas_get_contacts_link
						(ESourceM365Deltas *extension);
gchar *		e_source_m365_deltas_dup_contacts_link
						(ESourceM365Deltas *extension);
void		e_source_m365_deltas_set_contacts_link
						(ESourceM365Deltas *extension,
						 const gchar *delta_link);

G_END_DECLS

#endif /* E_SOURCE_M365_DELTAS_H */
