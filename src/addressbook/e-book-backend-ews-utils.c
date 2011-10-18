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

#include "e-book-backend-ews-utils.h"
#include "libedata-book/e-book-backend-sexp.h"
#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"

/**
 * e_book_backend_ews_utils_get_book_view:
 * @backend: an #EBookBackend
 *
 * Gets the currrent (first) #EDataBookView from this backend.
 *
 * Returns: An #EDataBookView object.
 **/

static gboolean
get_book_view (EDataBookView *view, gpointer user_data)
{
	EDataBookView **ret = (EDataBookView **) user_data;
	
	e_data_book_view_ref (view);
	*ret = view;

	return FALSE;
}
EDataBookView *
e_book_backend_ews_utils_get_book_view (EBookBackend *backend)
{
	EDataBookView *ret = NULL;

	e_book_backend_foreach_view (backend, get_book_view, &ret);

	return ret;
}

