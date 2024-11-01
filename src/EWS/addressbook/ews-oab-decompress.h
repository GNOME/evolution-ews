/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __EWS_OAB_DECOMPRESS_H__
#define __EWS_OAB_DECOMPRESS_H__

#include <glib.h>

gboolean ews_oab_decompress_full (const gchar *filename,
				  const gchar *output_filename,
				  GError **error);
gboolean ews_oab_decompress_patch (const gchar *filename,
				   const gchar *orig_filename,
				   const gchar *output_filename,
				   GError **error);

#endif
