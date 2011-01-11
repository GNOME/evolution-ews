#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "camel-ews-store-summary.h"

#define S_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->s_lock))
#define S_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->s_lock))

#define STORE_GROUP_NAME "##storepriv"

struct _CamelEwsStoreSummaryPrivate {
	GKeyFile *key_file;
	gboolean dirty;
	gchar *path;
	GHashTable *id_fname_hash;
	GStaticRecMutex s_lock;
};

G_DEFINE_TYPE (CamelEwsStoreSummary, camel_ews_store_summary, CAMEL_TYPE_OBJECT)

static void
ews_store_summary_finalize (GObject *object)
{
	CamelEwsStoreSummary *ews_summary = CAMEL_EWS_STORE_SUMMARY (object);
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;

	g_key_file_free (priv->key_file);
	g_free (priv->path);
	g_hash_table_destroy (priv->id_fname_hash);
	g_static_rec_mutex_free (&priv->s_lock);

	g_free (priv);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_summary_parent_class)->finalize (object);
}

static void
camel_ews_store_summary_class_init (CamelEwsStoreSummaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ews_store_summary_finalize;
}

static void
camel_ews_store_summary_init (CamelEwsStoreSummary *ews_summary)
{
	CamelEwsStoreSummaryPrivate *priv;

	priv = g_new0 (CamelEwsStoreSummaryPrivate, 1);
	ews_summary->priv = priv;

	priv->key_file = g_key_file_new ();
	priv->dirty = FALSE;
	priv->id_fname_hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
	g_static_rec_mutex_lock (&priv->s_lock);
}

static void
load_id_fname_hash (CamelEwsStoreSummary *ews_summary)
{
	GSList *folders, *l;

	folders = camel_ews_store_summary_get_folders (ews_summary);

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		gchar *fname = l->data;
		gchar *id;

		id = g_strdup (camel_ews_store_summary_get_folder_id	
						(ews_summary, fname, NULL));

		g_hash_table_insert (ews_summary->priv->id_fname_hash, id, fname);
	}

	g_slist_free (folders);
}

CamelEwsStoreSummary *
camel_ews_store_summary_new (const gchar *path)
{
	CamelEwsStoreSummary *ews_summary;

	ews_summary = g_object_new (CAMEL_TYPE_EWS_STORE_SUMMARY, NULL);

	ews_summary->priv->path = g_strdup (path);

	return ews_summary;
}

gboolean
camel_ews_store_summary_load	(CamelEwsStoreSummary *ews_summary,
				 GError **error)
{
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;
	gboolean ret;

	S_LOCK(ews_summary);
	
	ret = g_key_file_load_from_file	(priv->key_file,
					 priv->path,
					 0, error);

	load_id_fname_hash (ews_summary);	
	S_UNLOCK(ews_summary);

	return ret;
}

gboolean	
camel_ews_store_summary_save	(CamelEwsStoreSummary *ews_summary,
				 GError **error)
{
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;
	gboolean ret;
	GFile *file;
	gchar *contents = NULL;
	
	S_LOCK(ews_summary);

	if (!priv->dirty)
		goto exit;

	contents = g_key_file_to_data	(priv->key_file, NULL,
		       			 NULL);
	file = g_file_new_for_path	(priv->path);
	ret = g_file_replace_contents	(file, contents, strlen (contents),
					 NULL, FALSE, G_FILE_CREATE_PRIVATE,
					 NULL, NULL, error);
	priv->dirty = FALSE;

exit:
	S_UNLOCK(ews_summary);
	
	g_free (contents);
	return ret;
}

gboolean	
camel_ews_store_summary_clear	(CamelEwsStoreSummary *ews_summary)
{

	S_LOCK(ews_summary);

	g_key_file_free (ews_summary->priv->key_file);
	ews_summary->priv->key_file = g_key_file_new ();
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK(ews_summary);

	return TRUE;
}

void		
camel_ews_store_summary_set_folder_name	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name, 
					 const gchar *display_name)
{
	
	S_LOCK(ews_summary);

	g_key_file_set_string	(ews_summary->priv->key_file, folder_full_name,
				 "DisplayName", display_name);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}


void		
camel_ews_store_summary_set_folder_id	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name, 
					 const gchar *folder_id)
{
	S_LOCK(ews_summary);

	g_key_file_set_string	(ews_summary->priv->key_file, folder_full_name,
				 "FolderId", folder_id);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}


void		
camel_ews_store_summary_set_parent_folder_id	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 const gchar *parent_fid)
{
	S_LOCK(ews_summary);

	g_key_file_set_string	(ews_summary->priv->key_file, folder_full_name,
				 "ParentFolderId", parent_fid);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}


void		
camel_ews_store_summary_set_change_key	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name, 
					 const gchar *change_key)
{
	S_LOCK(ews_summary);

	g_key_file_set_string	(ews_summary->priv->key_file, folder_full_name,
				 "ChangeKey", change_key);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}


void		
camel_ews_store_summary_set_folder_flags	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 flags)
{
	S_LOCK(ews_summary);

	g_key_file_set_uint64	(ews_summary->priv->key_file, folder_full_name,
				 "Flags", flags);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}

void		
camel_ews_store_summary_set_folder_unread	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 unread)
{
	S_LOCK(ews_summary);

	g_key_file_set_uint64	(ews_summary->priv->key_file, folder_full_name,
				 "UnRead", unread);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}

void		
camel_ews_store_summary_set_folder_total	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 total)
{
	S_LOCK(ews_summary);

	g_key_file_set_uint64	(ews_summary->priv->key_file, folder_full_name,
				 "Total", total);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}

void
camel_ews_store_summary_store_string_val	(CamelEwsStoreSummary *ews_summary,
						 const gchar *key, 
						 const gchar *value)
{
	S_LOCK(ews_summary);

	g_key_file_set_string	(ews_summary->priv->key_file, STORE_GROUP_NAME,
				 key, value);
	ews_summary->priv->dirty = TRUE;
		
	S_UNLOCK(ews_summary);
}

const gchar *	
camel_ews_store_summary_get_folder_name	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name,
					 GError **error)
{
	gchar *ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_string	(ews_summary->priv->key_file, folder_full_name,
					 "DisplayName", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}

const gchar *	
camel_ews_store_summary_get_folder_id	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name,
					 GError **error)
{
	gchar *ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_string	(ews_summary->priv->key_file, folder_full_name,
					 "FolderId", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}


const gchar *	
camel_ews_store_summary_get_parent_folder_id	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error)
{
	gchar *ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_string	(ews_summary->priv->key_file, folder_full_name,
					 "ParentFolderId", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}


const gchar *	
camel_ews_store_summary_get_change_key	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name,
					 GError **error)
{
	gchar *ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_string	(ews_summary->priv->key_file, folder_full_name,
					 "ChangeKey", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}


guint64		
camel_ews_store_summary_get_folder_flags	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error)
{
	guint64 ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_uint64	(ews_summary->priv->key_file, folder_full_name,
					 "Flags", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}


guint64		
camel_ews_store_summary_get_folder_unread	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error)
{
	guint64 ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_uint64	(ews_summary->priv->key_file, folder_full_name,
					 "UnRead", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}

guint64		
camel_ews_store_summary_get_folder_total	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error)
{
	guint64 ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_uint64	(ews_summary->priv->key_file, folder_full_name,
					 "Total", error);
	
	S_UNLOCK(ews_summary);

	return ret;
}

const gchar *	
camel_ews_store_summary_get_string_val	(CamelEwsStoreSummary *ews_summary,
					 const gchar *key,
					 GError **error)
{
	gchar *ret;

	S_LOCK(ews_summary);

	ret = g_key_file_get_string	(ews_summary->priv->key_file, STORE_GROUP_NAME,
					 key, error);
	
	S_UNLOCK(ews_summary);

	return ret;
}

GSList *	
camel_ews_store_summary_get_folders	(CamelEwsStoreSummary *ews_summary)
{
	GSList *folders = NULL;
	gchar **groups = NULL;
	gsize length;
	gint i;

	groups = g_key_file_get_groups (ews_summary->priv->key_file, &length);
	for (i = 0; i < length; i++) {
		if (!g_ascii_strcasecmp (groups [i], STORE_GROUP_NAME))
			continue;
		folders = g_slist_append (folders, g_strdup (groups [i]));
	}
	
	g_strfreev (groups);
	return folders;
}

gboolean	
camel_ews_store_summary_remove_folder	(CamelEwsStoreSummary *ews_summary,
					 const gchar *folder_full_name,
					 GError **error)
{
	gboolean ret;
	const gchar *id;

	S_LOCK(ews_summary);

	id = camel_ews_store_summary_get_folder_id	(ews_summary, folder_full_name,
							 NULL);
	g_hash_table_remove (ews_summary->priv->id_fname_hash, id);

	ret = g_key_file_remove_group (ews_summary->priv->key_file, folder_full_name,
					error);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK(ews_summary);

	return ret;	
}

const gchar *
camel_ews_store_summary_get_folder_name_from_id	(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id)
{
	const gchar *folder_name;
	
	S_LOCK(ews_summary);
	
	folder_name = g_hash_table_lookup (ews_summary->priv->id_fname_hash, folder_id);

	S_UNLOCK(ews_summary);

	return folder_name;
}
