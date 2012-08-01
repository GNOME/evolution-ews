/*
 * e-ews-backend.h
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

#ifndef E_EWS_BACKEND_H
#define E_EWS_BACKEND_H

#include <libebackend/libebackend.h>

#include "server/e-ews-connection.h"

/* Standard GObject macros */
#define E_TYPE_EWS_BACKEND \
	(e_ews_backend_get_type ())
#define E_EWS_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_BACKEND, EEwsBackend))
#define E_EWS_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_BACKEND, EEwsBackendClass))
#define E_IS_EWS_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_BACKEND))
#define E_IS_EWS_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_BACKEND))
#define E_EWS_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_BACKEND, EEwsBackendClass))

G_BEGIN_DECLS

typedef struct _EEwsBackend EEwsBackend;
typedef struct _EEwsBackendClass EEwsBackendClass;
typedef struct _EEwsBackendPrivate EEwsBackendPrivate;

struct _EEwsBackend {
	ECollectionBackend parent;
	EEwsBackendPrivate *priv;
};

struct _EEwsBackendClass {
	ECollectionBackendClass parent_class;
};

GType		e_ews_backend_get_type		(void) G_GNUC_CONST;
void		e_ews_backend_type_register	(GTypeModule *type_module);
EEwsConnection *
		e_ews_backend_ref_connection_sync
						(EEwsBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_backend_ref_connection	(EEwsBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EEwsConnection *
		e_ews_backend_ref_connection_finish
						(EEwsBackend *backend,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_backend_sync_folders_sync	(EEwsBackend *backend,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_backend_sync_folders	(EEwsBackend *backend,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_backend_sync_folders_finish
						(EEwsBackend *backend,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_EWS_BACKEND_H */

