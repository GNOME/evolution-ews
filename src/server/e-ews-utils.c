/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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
#include "config.h"
#endif

#include "e-ews-utils.h"

#include <libedataserver/e-time-utils.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * ews_strdup_with_trailing_slash:
 * @path: a URI or path
 *
 * Copies @path, appending a "/" to it if and only if it did not
 * already end in "/".
 *
 * Return value: the path, which the caller must free
 **/
gchar *
ews_strdup_with_trailing_slash (const gchar *path)
{
	gchar *p;

	if (!path || !*path)
		return NULL;

	p = strrchr (path, '/');
	if (p && !p[1])
		return g_strdup (path);
	else
		return g_strdup_printf ("%s/", path);
}
