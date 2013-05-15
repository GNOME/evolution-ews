/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* ews-oal-decompress.h - Ews contact backend.
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
