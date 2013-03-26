/*
 * e-ews-ooo-notificator.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_EWS_OOO_NOTIFICATOR_H
#define E_EWS_OOO_NOTIFICATOR_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_EWS_OOO_NOTIFICATOR \
	(e_ews_ooo_notificator_get_type ())
#define E_EWS_OOO_NOTIFICATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_OOO_NOTIFICATOR, EEwsOooNotificator))
#define E_EWS_OOO_NOTIFICATOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_OOO_NOTIFICATOR, EEwsOooNotificatorClass))
#define E_IS_EWS_OOO_NOTIFICATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_OOO_NOTIFICATOR))
#define E_IS_EWS_OOO_NOTIFICATOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_OOO_NOTIFICATOR))
#define E_EWS_OOO_NOTIFICATOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_OOO_NOTIFICATOR, EEwsOooNotificatorClass))

G_BEGIN_DECLS

typedef struct _EEwsOooNotificator EEwsOooNotificator;
typedef struct _EEwsOooNotificatorClass EEwsOooNotificatorClass;
typedef struct _EEwsOooNotificatorPrivate EEwsOooNotificatorPrivate;

struct _EEwsOooNotificator {
	EExtension parent;
	EEwsOooNotificatorPrivate *priv;
};

struct _EEwsOooNotificatorClass {
	EExtensionClass parent_class;
};

GType		e_ews_ooo_notificator_get_type (void);
void		e_ews_ooo_notificator_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* E_EWS_OOO_NOTIFICATOR_H */

