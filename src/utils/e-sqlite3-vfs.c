/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <sqlite3.h>
#include <glib.h>
#include <config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>
#include <libedataserver/e-flag.h>

#include "e-sqlite3-vfs.h"

#define SYNC_TIMEOUT_SECONDS 5

static sqlite3_vfs *old_vfs = NULL;
static GThreadPool *sync_pool = NULL;

typedef struct {
	sqlite3_file parent;
	sqlite3_file *old_vfs_file; /* pointer to old_vfs' file */
	GStaticRecMutex sync_mutex;
	guint timeout_id;
	gint flags;
} ESqlite3File;

static gint
call_old_file_Sync (ESqlite3File *cFile, gint flags)
{
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (cFile != NULL, SQLITE_ERROR);

	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);
	return cFile->old_vfs_file->pMethods->xSync (cFile->old_vfs_file, flags);
}

struct SyncRequestData
{
	ESqlite3File *cFile;
	guint32 flags;
	EFlag *sync_op; /* not NULL when waiting for a finish; will be freed by the caller */
};

static void
sync_request_thread_cb (gpointer task_data, gpointer null_data)
{
	struct SyncRequestData *sync_data = task_data;
	EFlag *sync_op;

	g_return_if_fail (sync_data != NULL);
	g_return_if_fail (sync_data->cFile != NULL);

	call_old_file_Sync (sync_data->cFile, sync_data->flags);

	sync_op = sync_data->sync_op;
	g_free (sync_data);

	if (sync_op)
		e_flag_set (sync_op);
}

static void
sync_push_request (ESqlite3File *cFile, gboolean wait_for_finish)
{
	struct SyncRequestData *data;
	EFlag *sync_op = NULL;
	GError *error = NULL;

	g_return_if_fail (cFile != NULL);
	g_return_if_fail (sync_pool != NULL);

	g_static_rec_mutex_lock (&cFile->sync_mutex);

	if (wait_for_finish)
		sync_op = e_flag_new ();

	data = g_new0 (struct SyncRequestData, 1);
	data->cFile = cFile;
	data->flags = cFile->flags;
	data->sync_op = sync_op;

	cFile->flags = 0;

	g_static_rec_mutex_unlock (&cFile->sync_mutex);

	g_thread_pool_push (sync_pool, data, &error);

	if (error) {
		g_warning ("%s: Failed to push to thread pool: %s\n", G_STRFUNC, error->message);
		g_error_free (error);

		if (sync_op)
			e_flag_free (sync_op);

		return;
	}

	if (sync_op) {
		e_flag_wait (sync_op);
		e_flag_free (sync_op);
	}
}

static gboolean
sync_push_request_timeout (ESqlite3File *cFile)
{
	g_static_rec_mutex_lock (&cFile->sync_mutex);

	if (cFile->timeout_id != 0) {
		sync_push_request (cFile, FALSE);
		cFile->timeout_id = 0;
	}

	g_static_rec_mutex_unlock (&cFile->sync_mutex);

	return FALSE;
}

#define def_subclassed(_nm, _params, _call)			\
static gint							\
e_sqlite3_file_ ## _nm _params				\
{								\
	ESqlite3File *cFile;				\
								\
	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);	\
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);	\
								\
	cFile = (ESqlite3File *) pFile;		\
	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);	\
	return cFile->old_vfs_file->pMethods->_nm _call;	\
}

def_subclassed (xRead, (sqlite3_file *pFile, gpointer pBuf, gint iAmt, sqlite3_int64 iOfst), (cFile->old_vfs_file, pBuf, iAmt, iOfst))
def_subclassed (xWrite, (sqlite3_file *pFile, gconstpointer pBuf, gint iAmt, sqlite3_int64 iOfst), (cFile->old_vfs_file, pBuf, iAmt, iOfst))
def_subclassed (xTruncate, (sqlite3_file *pFile, sqlite3_int64 size), (cFile->old_vfs_file, size))
def_subclassed (xFileSize, (sqlite3_file *pFile, sqlite3_int64 *pSize), (cFile->old_vfs_file, pSize))
def_subclassed (xLock, (sqlite3_file *pFile, gint lockType), (cFile->old_vfs_file, lockType))
def_subclassed (xUnlock, (sqlite3_file *pFile, gint lockType), (cFile->old_vfs_file, lockType))
def_subclassed (xFileControl, (sqlite3_file *pFile, gint op, gpointer pArg), (cFile->old_vfs_file, op, pArg))
def_subclassed (xSectorSize, (sqlite3_file *pFile), (cFile->old_vfs_file))
def_subclassed (xDeviceCharacteristics, (sqlite3_file *pFile), (cFile->old_vfs_file))

#undef def_subclassed

static gint
e_sqlite3_file_xCheckReservedLock (sqlite3_file *pFile, gint *pResOut)
{
	ESqlite3File *cFile;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (ESqlite3File *) pFile;
	g_return_val_if_fail (cFile->old_vfs_file->pMethods != NULL, SQLITE_ERROR);

	/* check version in runtime */
	if (sqlite3_libversion_number () < 3006000)
		return ((gint (*)(sqlite3_file *)) (cFile->old_vfs_file->pMethods->xCheckReservedLock)) (cFile->old_vfs_file);
	else
		return ((gint (*)(sqlite3_file *, gint *)) (cFile->old_vfs_file->pMethods->xCheckReservedLock)) (cFile->old_vfs_file, pResOut);
}

static gint
e_sqlite3_file_xClose (sqlite3_file *pFile)
{
	ESqlite3File *cFile;
	gint res;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (ESqlite3File *) pFile;

	g_static_rec_mutex_lock (&cFile->sync_mutex);

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0) {
		g_source_remove (cFile->timeout_id);
		cFile->timeout_id = 0;
	}

	g_static_rec_mutex_unlock (&cFile->sync_mutex);

	/* Make the last sync. */
	sync_push_request (cFile, TRUE);

	if (cFile->old_vfs_file->pMethods)
		res = cFile->old_vfs_file->pMethods->xClose (cFile->old_vfs_file);
	else
		res = SQLITE_OK;

	g_free (cFile->old_vfs_file);
	cFile->old_vfs_file = NULL;

	g_static_rec_mutex_free (&cFile->sync_mutex);

	return res;
}

static gint
e_sqlite3_file_xSync (sqlite3_file *pFile, gint flags)
{
	ESqlite3File *cFile;

	g_return_val_if_fail (old_vfs != NULL, SQLITE_ERROR);
	g_return_val_if_fail (pFile != NULL, SQLITE_ERROR);

	cFile = (ESqlite3File *) pFile;

	g_static_rec_mutex_lock (&cFile->sync_mutex);

	/* If a sync request is already scheduled, accumulate flags. */
	cFile->flags |= flags;

	/* Cancel any pending sync requests. */
	if (cFile->timeout_id > 0)
		g_source_remove (cFile->timeout_id);

	/* Wait SYNC_TIMEOUT_SECONDS before we actually sync. */
	cFile->timeout_id = g_timeout_add_seconds (
		SYNC_TIMEOUT_SECONDS, (GSourceFunc)
		sync_push_request_timeout, cFile);

	g_static_rec_mutex_unlock (&cFile->sync_mutex);

	return SQLITE_OK;
}

static gint
e_sqlite3_vfs_xOpen (sqlite3_vfs *pVfs, const gchar *zPath, sqlite3_file *pFile, gint flags, gint *pOutFlags)
{
	static GStaticRecMutex only_once_lock = G_STATIC_REC_MUTEX_INIT;
	static sqlite3_io_methods io_methods = {0};
	ESqlite3File *cFile;
	gint res;

	g_return_val_if_fail (old_vfs != NULL, -1);
	g_return_val_if_fail (pFile != NULL, -1);

	cFile = (ESqlite3File *)pFile;
	cFile->old_vfs_file = g_malloc0 (old_vfs->szOsFile);

	res = old_vfs->xOpen (old_vfs, zPath, cFile->old_vfs_file, flags, pOutFlags);
	if (res != SQLITE_OK) {
		g_free (cFile->old_vfs_file);
		return res;
	}

	g_static_rec_mutex_init (&cFile->sync_mutex);

	g_static_rec_mutex_lock (&only_once_lock);

	if (!sync_pool)
		sync_pool = g_thread_pool_new (sync_request_thread_cb, NULL, 2, FALSE, NULL);

	/* cFile->old_vfs_file->pMethods is NULL when open failed for some reason,
	   thus do not initialize our structure when do not know the version */
	if (io_methods.xClose == NULL && cFile->old_vfs_file->pMethods) {
		/* initialize our subclass function only once */
		io_methods.iVersion = cFile->old_vfs_file->pMethods->iVersion;

		/* check version in compile time */
		#if SQLITE_VERSION_NUMBER < 3006000
		io_methods.xCheckReservedLock = (gint (*)(sqlite3_file *)) e_sqlite3_file_xCheckReservedLock;
		#else
		io_methods.xCheckReservedLock = e_sqlite3_file_xCheckReservedLock;
		#endif

		#define use_subclassed(x) io_methods.x = e_sqlite3_file_ ## x
		use_subclassed (xClose);
		use_subclassed (xRead);
		use_subclassed (xWrite);
		use_subclassed (xTruncate);
		use_subclassed (xSync);
		use_subclassed (xFileSize);
		use_subclassed (xLock);
		use_subclassed (xUnlock);
		use_subclassed (xFileControl);
		use_subclassed (xSectorSize);
		use_subclassed (xDeviceCharacteristics);
		#undef use_subclassed
	}

	g_static_rec_mutex_unlock (&only_once_lock);

	cFile->parent.pMethods = &io_methods;

	return res;
}

static gpointer
init_sqlite_vfs (void)
{
	static sqlite3_vfs vfs = { 0 };

	old_vfs = sqlite3_vfs_find (NULL);
	g_return_val_if_fail (old_vfs != NULL, NULL);

	memcpy (&vfs, old_vfs, sizeof (sqlite3_vfs));

	vfs.szOsFile = sizeof (ESqlite3File);
	vfs.zName = "e_sqlite3_vfs";
	vfs.xOpen = e_sqlite3_vfs_xOpen;

	sqlite3_vfs_register (&vfs, 1);

	return NULL;
}

void
e_sqlite3_vfs_init (void)
{
	static GOnce vfs_once = G_ONCE_INIT;

	g_once (&vfs_once, (GThreadFunc) init_sqlite_vfs, NULL);

	return;
}
