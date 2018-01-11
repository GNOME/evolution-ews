/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <e-util/e-util.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"

#include "e-ews-photo-source.h"

/* Standard GObject macros */
#define E_TYPE_EWS_PHOTO_SOURCE \
	(e_ews_photo_source_get_type ())
#define E_EWS_PHOTO_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_PHOTO_SOURCE, EEwsPhotoSource))
#define E_EWS_PHOTO_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_PHOTO_SOURCE, EEwsPhotoSourceClass))
#define E_IS_EWS_PHOTO_SOURCE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_PHOTO_SOURCE))
#define E_IS_EWS_PHOTO_SOURCE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_PHOTO_SOURCE))
#define E_EWS_PHOTO_SOURCE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_PHOTO_SOURCE, EEwsPhotoSourceClass))

typedef struct _EEwsPhotoSource EEwsPhotoSource;
typedef struct _EEwsPhotoSourceClass EEwsPhotoSourceClass;

struct _EEwsPhotoSource {
	EExtension parent;
};

struct _EEwsPhotoSourceClass {
	EExtensionClass parent_class;
};

GType e_ews_photo_source_get_type (void) G_GNUC_CONST;

static void ews_photo_source_iface_init (EPhotoSourceInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEwsPhotoSource, e_ews_photo_source, E_TYPE_EXTENSION, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_PHOTO_SOURCE, ews_photo_source_iface_init))

typedef struct _PhotoSourceData {
	GMutex lock;
	guint n_running;
	ESimpleAsyncResult *simple;
	GCancellable *cancellable;
} PhotoSourceData;

static void
ews_photo_source_dec_running (PhotoSourceData *psd)
{
	if (!g_atomic_int_dec_and_test (&psd->n_running))
		return;

	if (psd->simple)
		e_simple_async_result_complete_idle (psd->simple);

	g_clear_object (&psd->simple);
	g_clear_object (&psd->cancellable);
	g_mutex_clear (&psd->lock);
	g_free (psd);
}

static void
ews_photo_source_get_user_photo_cb (GObject *source_object,
				    GAsyncResult *result,
				    gpointer user_data)
{
	PhotoSourceData *psd = user_data;
	GCancellable *cancellable = NULL;
	gchar *picture_data = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_EWS_CONNECTION (source_object));
	g_return_if_fail (psd != NULL);

	g_mutex_lock (&psd->lock);

	if (e_ews_connection_get_user_photo_finish (E_EWS_CONNECTION (source_object), result, &picture_data, &error)) {
		if (psd->simple && picture_data && *picture_data) {
			gsize len = 0;
			guchar *decoded;

			decoded = g_base64_decode (picture_data, &len);
			if (len && decoded) {
				GInputStream *stream;

				stream = g_memory_input_stream_new_from_data (decoded, len, g_free);
				decoded = NULL;

				e_simple_async_result_set_op_pointer (psd->simple, stream, g_object_unref);
				e_simple_async_result_complete_idle (psd->simple);
				g_clear_object (&psd->simple);

				cancellable = g_object_ref (psd->cancellable);
			}

			g_free (decoded);
		}
	} else {
		if (psd->simple && error) {
			e_simple_async_result_take_error (psd->simple, error);
			error = NULL;
		}
	}

	g_mutex_unlock (&psd->lock);

	ews_photo_source_dec_running (psd);

	if (cancellable)
		g_cancellable_cancel (cancellable);

	g_clear_object (&cancellable);
	g_clear_error (&error);
	g_free (picture_data);
}

static void
ews_photo_source_get_photo (EPhotoSource *photo_source,
			    const gchar *email_address,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	GSList *connections, *link;
	GHashTable *covered_uris;
	PhotoSourceData *psd;

	g_return_if_fail (E_IS_EWS_PHOTO_SOURCE (photo_source));
	g_return_if_fail (email_address != NULL);

	psd = g_new0 (PhotoSourceData, 1);
	psd->n_running = 1;
	psd->simple = e_simple_async_result_new (G_OBJECT (photo_source), callback, user_data, ews_photo_source_get_photo);
	psd->cancellable = camel_operation_new_proxy (cancellable);
	g_mutex_init (&psd->lock);

	covered_uris = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
	connections = e_ews_connection_list_existing ();

	for (link = connections; link; link = g_slist_next (link)) {
		EEwsConnection *cnc = link->data;
		const gchar *uri;

		if (!E_IS_EWS_CONNECTION (cnc) ||
		    !e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013))
			continue;

		uri = e_ews_connection_get_uri (cnc);
		if (!uri || !*uri || g_hash_table_contains (covered_uris, uri))
			continue;

		g_hash_table_insert (covered_uris, g_strdup (uri), NULL);

		g_atomic_int_inc (&psd->n_running);

		e_ews_connection_get_user_photo (cnc, G_PRIORITY_LOW, email_address, E_EWS_SIZE_REQUESTED_48X48,
			psd->cancellable, ews_photo_source_get_user_photo_cb, psd);
	}

	g_slist_free_full (connections, g_object_unref);
	g_hash_table_destroy (covered_uris);

	ews_photo_source_dec_running (psd);
}

static gboolean
ews_photo_source_get_photo_finish (EPhotoSource *photo_source,
				   GAsyncResult *result,
				   GInputStream **out_stream,
				   gint *out_priority,
				   GError **error)
{
	ESimpleAsyncResult *simple;

	g_return_val_if_fail (E_IS_EWS_PHOTO_SOURCE (photo_source), FALSE);
	g_return_val_if_fail (E_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

	g_return_val_if_fail (e_simple_async_result_is_valid (result, G_OBJECT (photo_source), ews_photo_source_get_photo), FALSE);

	if (out_priority)
		*out_priority = G_PRIORITY_DEFAULT;

	simple = E_SIMPLE_ASYNC_RESULT (result);

	if (e_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*out_stream = e_simple_async_result_get_op_pointer (simple);
	if (*out_stream) {
		g_object_ref (*out_stream);
		return TRUE;
	}

	/* Do not localize the string, it won't go into the UI/be visible to users */
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not Found");

	return FALSE;
}

static void
ews_photo_source_constructed (GObject *object)
{
	EPhotoCache *photo_cache;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_photo_source_parent_class)->constructed (object);

	photo_cache = E_PHOTO_CACHE (e_extension_get_extensible (E_EXTENSION (object)));

	e_photo_cache_add_photo_source (photo_cache, E_PHOTO_SOURCE (object));
}

static void
e_ews_photo_source_class_init (EEwsPhotoSourceClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = ews_photo_source_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_PHOTO_CACHE;
}

static void
e_ews_photo_source_class_finalize (EEwsPhotoSourceClass *class)
{
}

static void
ews_photo_source_iface_init (EPhotoSourceInterface *iface)
{
	iface->get_photo = ews_photo_source_get_photo;
	iface->get_photo_finish = ews_photo_source_get_photo_finish;
}

static void
e_ews_photo_source_init (EEwsPhotoSource *extension)
{
}

void
e_ews_photo_source_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_photo_source_register_type (type_module);
}
