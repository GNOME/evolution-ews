/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* camel-ews-utils.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include "camel-ews-utils.h"

#define SUBFOLDER_DIR_NAME     "subfolders"
#define SUBFOLDER_DIR_NAME_LEN 10
#define RFC_822	"message/rfc822"

/**
 * e_path_to_physical:
 * @prefix: a prefix to prepend to the path, or %NULL
 * @path: the virtual path to convert to a filesystem path.
 *
 * This converts the "virtual" path @path into an expanded form that
 * allows a given name to refer to both a file and a directory. The
 * expanded path will have a "subfolders" directory inserted between
 * each path component. If the path ends with "/", the returned
 * physical path will end with "/subfolders"
 *
 * If @prefix is non-%NULL, it will be prepended to the returned path.
 *
 * Returns: the expanded path
 **/
gchar *
e_path_to_physical (const gchar *prefix, const gchar *vpath)
{
	const gchar *p, *newp;
	gchar *dp;
	gchar *ppath;
	gint ppath_len;
	gint prefix_len;

	while (*vpath == '/')
		vpath++;
	if (!prefix)
		prefix = "";

	/* Calculate the length of the real path. */
	ppath_len = strlen (vpath);
	ppath_len++;	/* For the ending zero.  */

	prefix_len = strlen (prefix);
	ppath_len += prefix_len;
	ppath_len++;	/* For the separating slash.  */

	/* Take account of the fact that we need to translate every
	 * separator into 'subfolders/'.
	 */
	p = vpath;
	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL)
			break;

		ppath_len += SUBFOLDER_DIR_NAME_LEN;
		ppath_len++; /* For the separating slash.  */

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	};

	ppath = g_malloc (ppath_len);
	dp = ppath;

	memcpy (dp, prefix, prefix_len);
	dp += prefix_len;
	*(dp++) = '/';

	/* Copy the mangled path.  */
	p = vpath;
	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL) {
			strcpy (dp, p);
			break;
		}

		memcpy (dp, p, newp - p + 1); /* '+ 1' to copy the slash too.  */
		dp += newp - p + 1;

		memcpy (dp, SUBFOLDER_DIR_NAME, SUBFOLDER_DIR_NAME_LEN);
		dp += SUBFOLDER_DIR_NAME_LEN;

		*(dp++) = '/';

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	}

	return ppath;
}

static gboolean
find_folders_recursive (const gchar *physical_path, const gchar *path,
			EPathFindFoldersCallback callback, gpointer data)
{
	GDir *dir;
	gchar *subfolder_directory_path;
	gboolean ok;

	if (*path) {
		if (!callback (physical_path, path, data))
			return FALSE;

		subfolder_directory_path = g_strdup_printf ("%s/%s", physical_path, SUBFOLDER_DIR_NAME);
	} else {
		/* On the top level, we have no folders and,
		 * consequently, no subfolder directory.
		 */

		subfolder_directory_path = g_strdup (physical_path);
	}

	/* Now scan the subfolders and load them. */
	dir = g_dir_open (subfolder_directory_path, 0, NULL);
	if (dir == NULL) {
		g_free (subfolder_directory_path);
		return TRUE;
	}

	ok = TRUE;
	while (ok) {
		struct stat file_stat;
		const gchar *dirent;
		gchar *file_path;
		gchar *new_path;

		dirent = g_dir_read_name (dir);
		if (dirent == NULL)
			break;

		file_path = g_strdup_printf ("%s/%s", subfolder_directory_path, dirent);

		if (g_stat (file_path, &file_stat) < 0 ||
		    !S_ISDIR (file_stat.st_mode)) {
			g_free (file_path);
			continue;
		}

		new_path = g_strdup_printf ("%s/%s", path, dirent);

		ok = find_folders_recursive (file_path, new_path, callback, data);

		g_free (file_path);
		g_free (new_path);
	}

	g_dir_close (dir);
	g_free (subfolder_directory_path);

	return ok;
}

/**
 * e_path_find_folders:
 * @prefix: directory to start from
 * @callback: Callback to invoke on each folder
 * @data: Data for @callback
 *
 * Walks the folder tree starting at @prefix and calls @callback
 * on each folder.
 *
 * Returns: %TRUE on success, %FALSE if an error occurs at any point
 **/
gboolean
e_path_find_folders (const gchar *prefix,
		     EPathFindFoldersCallback callback,
		     gpointer data)
{
	return find_folders_recursive (prefix, "", callback, data);
}

/**
 * e_path_rmdir:
 * @prefix: a prefix to prepend to the path, or %NULL
 * @path: the virtual path to convert to a filesystem path.
 *
 * This removes the directory pointed to by @prefix and @path
 * and attempts to remove its parent "subfolders" directory too
 * if it's empty.
 *
 * Returns: -1 (with errno set) if it failed to rmdir the
 * specified directory. 0 otherwise, whether or not it removed
 * the parent directory.
 **/
gint
e_path_rmdir (const gchar *prefix, const gchar *vpath)
{
	gchar *physical_path, *p;

	/* Remove the directory itself */
	physical_path = e_path_to_physical (prefix, vpath);
	if (g_rmdir (physical_path) == -1) {
		g_free (physical_path);
		return -1;
	}

	/* Attempt to remove its parent "subfolders" directory,
	 * ignoring errors since it might not be empty.
	 */

	p = strrchr (physical_path, '/');
	if (p[1] == '\0') {
		g_free (physical_path);
		return 0;
	}
	*p = '\0';
	p = strrchr (physical_path, '/');
	if (!p || strcmp (p + 1, SUBFOLDER_DIR_NAME) != 0) {
		g_free (physical_path);
		return 0;
	}

	g_rmdir (physical_path);
	g_free (physical_path);
	return 0;
}

void
do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new)
{
	diff->changed = old ^ _new;
	diff->bits = _new & diff->changed;
}

gchar *
ews_concat ( const gchar *prefix, const gchar *suffix)
{
	gsize len;

	len = strlen (prefix);
	if (len == 0 || prefix[len - 1] == '/')
		return g_strdup_printf ("%s%s", prefix, suffix);
	else
		return g_strdup_printf ("%s%c%s", prefix, '/', suffix);
}

void
strip_lt_gt (gchar **string, gint s_offset, gint e_offset)
{
	gchar *temp = NULL;
	gint len;

	temp = g_strdup (*string);
	len = strlen (*string);

	*string = (gchar *)g_malloc0 (len-1);
	*string = memcpy(*string, temp+s_offset, len-e_offset);
	g_free (temp);
}

CamelFolderInfo *
camel_ews_utils_build_folder_info (CamelEwsStore *store, const gchar *fname)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	CamelFolderInfo *fi;
	gchar *url;

	url = camel_url_to_string (CAMEL_SERVICE (store)->url,
			(CAMEL_URL_HIDE_PASSWORD|
			 CAMEL_URL_HIDE_PARAMS|
			 CAMEL_URL_HIDE_AUTH) );

	if ( url[strlen (url) - 1] != '/') {
		gchar *temp_url;

		temp_url = g_strconcat (url, "/", NULL);
		g_free ((gchar *)url);
		url = temp_url;
	}

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (fname);
	fi->name = g_strdup (camel_ews_store_summary_get_folder_name	(ews_summary,
				fi->full_name,
				NULL));
	fi->uri = g_strconcat (url, fi->full_name, NULL);
	fi->flags = camel_ews_store_summary_get_folder_flags	(ews_summary,
			fi->full_name,
			NULL);
	fi->unread = camel_ews_store_summary_get_folder_unread	(ews_summary,
			fi->full_name,
			NULL);
	fi->total = camel_ews_store_summary_get_folder_total	(ews_summary,
			fi->full_name,
			NULL);

	g_free (url);

	return fi;
}
	
static void
sync_deleted_folders (CamelEwsStore *store, GSList *deleted_folders)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	GSList *l;

	for (l = deleted_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *ews_folder = (EEwsFolder *)	l->data;
		GError *error = NULL;
		const gchar *folder_name;
		const EwsFolderId *fid;
		CamelFolderInfo *fi;

		fid = e_ews_folder_get_id (ews_folder);
		folder_name = camel_ews_store_summary_get_folder_name_from_id (ews_summary, fid->id);
		fi = camel_ews_utils_build_folder_info (store, folder_name);

		camel_ews_store_summary_remove_folder (ews_summary, folder_name, &error);
		camel_store_folder_deleted ((CamelStore *) store, fi);

		g_clear_error (&error);
	}
}

static void
sync_updated_folders (CamelEwsStore *store, GSList *updated_folders)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	GSList *l;
	
	for (l = updated_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *ews_folder = (EEwsFolder *)	l->data;
		const gchar *folder_name, *display_name;
		const EwsFolderId *fid, *pfid;

		fid = e_ews_folder_get_id (ews_folder);
		folder_name = camel_ews_store_summary_get_folder_name_from_id (ews_summary, fid->id);

		pfid = e_ews_folder_get_parent_id (ews_folder);
		display_name = e_ews_folder_get_name (ews_folder);

		if (pfid || display_name) {
			gchar *new_fname = NULL;
			const gchar *pfname;
			guint64 flags;
			CamelFolderInfo *fi;
			GError *error = NULL;

			if (pfid)
				pfname = camel_ews_store_summary_get_folder_name_from_id (ews_summary, pfid->id);
			if (!display_name)
				display_name = camel_ews_store_summary_get_folder_name (ews_summary, folder_name, &error);

			if (pfname) {
				new_fname = g_strconcat (pfname, "/", display_name, NULL);
				camel_ews_store_summary_set_parent_folder_id (ews_summary, new_fname, pfid->id);
			} else {
				const gchar *last_slash, *o_pfid;
				gchar *tmp;

				last_slash = g_strrstr (folder_name, "/");
				tmp = g_strndup (folder_name, (last_slash - folder_name));
				new_fname = g_strconcat (tmp, "/", display_name, NULL);

				o_pfid = camel_ews_store_summary_get_parent_folder_id (ews_summary, folder_name, NULL);
				camel_ews_store_summary_set_parent_folder_id (ews_summary, new_fname, o_pfid);

				g_free (tmp);
			}
			
			camel_ews_store_summary_set_folder_id (ews_summary, new_fname, fid->id);
			camel_ews_store_summary_set_change_key (ews_summary, new_fname, fid->change_key);
			camel_ews_store_summary_set_folder_name (ews_summary, new_fname, fid->id);

			flags = camel_ews_store_summary_get_folder_flags (ews_summary, folder_name, NULL);
			camel_ews_store_summary_set_folder_flags (ews_summary, new_fname, flags);
		
			fi = camel_ews_utils_build_folder_info (store, new_fname);
			camel_store_folder_renamed ((CamelStore *) store, folder_name, fi);

			/* TODO set total and unread count. Check if server returns all properties on update */

			camel_ews_store_summary_remove_folder (ews_summary, folder_name, &error);

			g_free (new_fname);
			g_clear_error (&error);
		}
	}
}


static void
add_folder_to_summary (CamelEwsStore *store, const gchar *fname, EEwsFolder *folder)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	const EwsFolderId *pfid, *fid;
	const gchar *dname;
	gint64 flags = 0, unread, total;

	fid = e_ews_folder_get_id (folder);
	pfid = e_ews_folder_get_parent_id (folder);
	dname = e_ews_folder_get_name (folder);
	total = e_ews_folder_get_total_count (folder);
	unread = e_ews_folder_get_unread_count (folder);

	camel_ews_store_summary_set_folder_id (ews_summary, fname, fid->id);
	camel_ews_store_summary_set_change_key (ews_summary, fname, fid->change_key);
	camel_ews_store_summary_set_parent_folder_id (ews_summary, fname, pfid->id);
	camel_ews_store_summary_set_folder_name (ews_summary, fname, dname);
	camel_ews_store_summary_set_folder_total (ews_summary, fname, total);
	camel_ews_store_summary_set_folder_unread (ews_summary, fname, unread);
	
	if (!g_ascii_strcasecmp (fname, "Inbox")) {
		flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX; 
	} else if (!g_ascii_strcasecmp (fname, "Drafts")) {
		flags |= CAMEL_FOLDER_SYSTEM; 
	} else if (!g_ascii_strcasecmp (fname, "Deleted items")) {
		flags |= CAMEL_FOLDER_SYSTEM; 
	} else if (!g_ascii_strcasecmp (fname, "Outbox")) {
		flags |= CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_OUTBOX; 
	}
	
	camel_ews_store_summary_set_folder_flags (ews_summary, fname, unread);
}

static void
sync_created_folders (CamelEwsStore *store, GSList *created_folders)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	GSList *l;
	GHashTable *c_folders_hash;

	c_folders_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (l = created_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *folder = (EEwsFolder *) l->data;
		const EwsFolderId *fid;
		const gchar *display_name;

		fid = e_ews_folder_get_id (folder);
		display_name = e_ews_folder_get_name (folder);
		g_hash_table_insert (c_folders_hash, fid->id, (gpointer) display_name);
	}
	
	for (l = created_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *folder = (EEwsFolder *) l->data;
		const EwsFolderId *fid, *pfid;
		const gchar *display_name, *pfname;
		gchar *fname = NULL;
		
		fid = e_ews_folder_get_id (folder);
		pfid = e_ews_folder_get_parent_id (folder);
		display_name = e_ews_folder_get_name (folder);
		pfname = camel_ews_store_summary_get_folder_name_from_id (ews_summary, pfid->id);

		if (pfname)
			fname = g_strconcat (pfname, "/", display_name, NULL);
		else {
			GString *full_name;
			const gchar *p_dname;

			full_name = g_string_new (display_name);
			while ((p_dname = g_hash_table_lookup (c_folders_hash, pfid->id)))
				g_string_append_printf (full_name, "%s/", p_dname);

			fname = full_name->str;
			g_string_free (full_name, FALSE);
		}

		add_folder_to_summary (store, fname, folder);
	}

	g_hash_table_destroy (c_folders_hash);
}

void
ews_utils_sync_folders (CamelEwsStore *ews_store, GSList *created_folders, GSList *deleted_folders, GSList *updated_folders)
{
	sync_deleted_folders (ews_store, deleted_folders);
	sync_updated_folders (ews_store, updated_folders);
	sync_created_folders (ews_store, created_folders);
}
