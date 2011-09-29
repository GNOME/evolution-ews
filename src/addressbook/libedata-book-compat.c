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

#include "libedata-book-compat.h"

#if ! EDS_CHECK_VERSION (3,1,0)
static GList *
convert_slist_to_list (GSList *slist)
{
	GSList *sl = NULL;
	GList *l = NULL;

	for (sl = slist; sl != NULL; sl = g_slist_next (sl))
		l = g_list_prepend (l, sl->data);
	l = g_list_reverse (l);

	return l;
}
#endif

void	e_data_book_respond_remove_contacts_compat (EDataBook *book, guint32 opid, GError *error, const GSList *ids)
{
#if ! EDS_CHECK_VERSION (3,1,0)
	GList *l = convert_slist_to_list (ids);
	e_data_book_respond_remove_contacts (book, opid, error, l);
	g_list_free (l);
#else
	e_data_book_respond_remove_contacts (book, opid, error, ids);
#endif	
}

void	e_data_book_respond_get_contact_list_compat (EDataBook *book, guint32 opid, GError *error, const GSList *cards)
{
#if ! EDS_CHECK_VERSION (3,1,0)
	GList *l = convert_slist_to_list (cards);
	e_data_book_respond_get_contact_list (book, opid, error, l);
	g_list_free (l);
#else
	e_data_book_respond_get_contact_list (book, opid, error, cards);
#endif	
}

#if ! EDS_CHECK_VERSION (3,1,0)

void	
e_book_backend_set_online (EBookBackend *backend, gboolean is_online)
{
	e_book_backend_notify_connection_status (backend, is_online);
}

void	
e_book_backend_notify_opened (EBookBackend *backend, GError *error)
{
	if (!error)
		e_book_backend_set_is_loaded (backend, TRUE);
	else	
		e_book_backend_set_is_loaded (backend, FALSE);
}

void	
e_data_book_view_notify_progress        (EDataBookView *book_view, guint percent, const gchar *message)
{
	e_data_book_view_notify_status_message (book_view, message);
}

#endif
