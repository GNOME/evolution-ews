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

#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedataserver/eds-version.h>

void	e_data_book_respond_remove_contacts_compat (EDataBook *book, guint32 opid, GError *error, const GSList *ids);
void	e_data_book_respond_get_contact_list_compat (EDataBook *book, guint32 opid, GError *error, const GSList *cards);


#if ! EDS_CHECK_VERSION (3,1,0)
void	e_book_backend_set_online		(EBookBackend *backend, gboolean is_online);
void	e_book_backend_notify_opened		(EBookBackend *backend, GError *error);
void	e_data_book_view_notify_progress        (EDataBookView *book_view, guint percent, const gchar *message);

#endif
