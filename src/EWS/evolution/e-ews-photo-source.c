/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <e-util/e-util.h>

#include "common/camel-ews-settings.h"
#include "common/e-ews-connection.h"

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
	GThreadPool *pool;
};

struct _EEwsPhotoSourceClass {
	EExtensionClass parent_class;
};

GType e_ews_photo_source_get_type (void) G_GNUC_CONST;

static void ews_photo_source_iface_init (EPhotoSourceInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEwsPhotoSource, e_ews_photo_source, E_TYPE_EXTENSION, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_PHOTO_SOURCE, ews_photo_source_iface_init))

static void
e_ews_photo_source_pool_thread_func_cb (gpointer data,
					gpointer user_data)
{
	GTask *task = data;
	GCancellable *cancellable = g_task_get_cancellable (task);
	const gchar *email_address = g_task_get_task_data (task);
	GSList *connections, *link;
	GHashTable *covered_uris;
	GError *local_error = NULL;

	/* Most users connect to a single server anyway, thus no big deal doing
	   this in serial, instead of in parallel. */
	covered_uris = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
	connections = e_ews_connection_list_existing ();

	for (link = connections; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
		EEwsConnection *cnc = link->data;
		gchar *picture_data = NULL;
		const gchar *uri;

		if (!E_IS_EWS_CONNECTION (cnc) ||
		    !e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013))
			continue;

		uri = e_ews_connection_get_uri (cnc);
		if (!uri || !*uri || g_hash_table_contains (covered_uris, uri))
			continue;

		g_hash_table_insert (covered_uris, g_strdup (uri), NULL);

		if (e_ews_connection_get_user_photo_sync (cnc, G_PRIORITY_LOW, email_address, E_EWS_SIZE_REQUESTED_48X48,
			&picture_data, cancellable, local_error ? NULL : &local_error) && picture_data) {
			gsize len = 0;
			guchar *decoded;

			decoded = g_base64_decode (picture_data, &len);
			if (len && decoded) {
				GInputStream *stream;

				stream = g_memory_input_stream_new_from_data (decoded, len, g_free);
				decoded = NULL;

				g_task_return_pointer (task, stream, g_object_unref);
				g_clear_object (&task);
				g_free (decoded);
				break;
			}

			g_free (decoded);
		}
	}

	g_slist_free_full (connections, g_object_unref);
	g_hash_table_destroy (covered_uris);

	if (task) {
		if (!local_error) {
			/* Do not localize the string, it won't go into the UI/be visible to users */
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not Found");
		}

		g_task_return_error (task, local_error);
		g_clear_object (&task);
	} else {
		g_clear_error (&local_error);
	}
}

static void
ews_photo_source_get_photo (EPhotoSource *photo_source,
			    const gchar *email_address,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	EEwsPhotoSource *ews_photo_source;
	GTask *task;

	g_return_if_fail (E_IS_EWS_PHOTO_SOURCE (photo_source));
	g_return_if_fail (email_address != NULL);

	ews_photo_source = E_EWS_PHOTO_SOURCE (photo_source);

	task = g_task_new (photo_source, cancellable, callback, user_data);
	g_task_set_source_tag (task, ews_photo_source_get_photo);
	g_task_set_task_data (task, g_strdup (email_address), g_free);
	/* process only one request at a time, without using GTask threads, because
	   those are important to not be used for a long time */
	g_thread_pool_push (ews_photo_source->pool, task, NULL);
}

static gboolean
ews_photo_source_get_photo_finish (EPhotoSource *photo_source,
				   GAsyncResult *result,
				   GInputStream **out_stream,
				   gint *out_priority,
				   GError **error)
{
	GInputStream *input_stream;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_PHOTO_SOURCE (photo_source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, photo_source), FALSE);

	if (out_priority)
		*out_priority = G_PRIORITY_DEFAULT;

	input_stream = g_task_propagate_pointer (G_TASK (result), error);

	success = input_stream != NULL;

	if (out_stream)
		*out_stream = input_stream;
	else
		g_clear_object (&input_stream);

	return success;
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
ews_photo_source_finalize (GObject *object)
{
	EEwsPhotoSource *ews_photo_source = E_EWS_PHOTO_SOURCE (object);

	g_thread_pool_free (ews_photo_source->pool, FALSE, TRUE);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_photo_source_parent_class)->finalize (object);
}

static void
e_ews_photo_source_class_init (EEwsPhotoSourceClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = ews_photo_source_constructed;
	object_class->finalize = ews_photo_source_finalize;

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
	extension->pool = g_thread_pool_new (e_ews_photo_source_pool_thread_func_cb, NULL, 1, FALSE, NULL);
}

void
e_ews_photo_source_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_photo_source_register_type (type_module);
}
