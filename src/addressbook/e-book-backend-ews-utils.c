/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews-utils.c - Ews personal addressbook and GAL common funtions.
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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include "e-book-backend-ews-utils.h"


/**
 * e_book_backend_ews_utils_get_book_view:
 * @backend: an #EBookBackend
 *
 * Gets the currrent (first) #EDataBookView from this backend.
 *
 * Returns: An #EDataBookView object.
 **/
EDataBookView *
e_book_backend_ews_utils_get_book_view (EBookBackend *backend)
{
	EList *views = e_book_backend_get_book_views (backend);
	EIterator *iter;
	EDataBookView *rv = NULL;

	if (!views)
		return NULL;

	iter = e_list_get_iterator (views);

	if (!iter) {
		g_object_unref (views);
		return NULL;
	}

	if (e_iterator_is_valid (iter)) {
		/* just always use the first book view */
		EDataBookView *v = (EDataBookView*)e_iterator_get (iter);
		if (v) {
			rv = v;
			e_data_book_view_ref (rv);
		}
	}

	g_object_unref (iter);
	g_object_unref (views);

	return rv;
}
