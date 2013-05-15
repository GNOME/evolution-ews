/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews.h - Ews contact backend.
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include "ews-oab-decompress.h"
#include <mspack.h>

/* This contains the minimal implementation of these functions, for use with
   a sufficiently up-to-date version of libmspack. For the full implementation
   for use without libmspack, see the lzx/ directory. */

gboolean
ews_oab_decompress_full (const gchar *filename, const gchar *output_filename,
			 GError **error)
{
	struct msoab_decompressor *msoab;
	int ret;

	msoab = mspack_create_oab_decompressor (NULL);
	if (!msoab) {
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1,
				     "Unable to create msoab decompressor");
		return FALSE;
	}
	ret = msoab->decompress (msoab, filename, output_filename);
	mspack_destroy_oab_decompressor (msoab);
	if (ret != MSPACK_ERR_OK) {
		g_set_error (error, g_quark_from_string ("lzx"), 1,
			     "Failed to decompress LZX file: %d", ret);
		return FALSE;
	}

	return TRUE;
}


gboolean
ews_oab_decompress_patch (const gchar *filename, const gchar *orig_filename,
			  const gchar *output_filename, GError **error)
{
	struct msoab_decompressor *msoab;
	int ret;

	msoab = mspack_create_oab_decompressor (NULL);
	if (!msoab) {
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1,
				     "Unable to create msoab decompressor");
		return FALSE;
	}
	ret = msoab->decompress_incremental (msoab, filename,
					     orig_filename, output_filename);
	mspack_destroy_oab_decompressor (msoab);
	if (ret != MSPACK_ERR_OK) {
		g_set_error (error, g_quark_from_string ("lzx"), 1,
			     "Failed to apply LZX patch file: %d", ret);
		return FALSE;
	}

	return TRUE;
}

