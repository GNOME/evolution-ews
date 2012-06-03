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

#ifndef __E_BOOK_BACKEND_EWS_H__
#define __E_BOOK_BACKEND_EWS_H__

#include <libedata-book/libedata-book.h>

#define E_TYPE_BOOK_BACKEND_EWS        (e_book_backend_ews_get_type ())
#define E_BOOK_BACKEND_EWS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_EWS, EBookBackendEws))
#define E_BOOK_BACKEND_EWS_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_EWS, EBookBackendEwsClass))
#define E_IS_BOOK_BACKEND_EWS(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_EWS))
#define E_IS_BOOK_BACKEND_EWS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_EWS))
#define E_BOOK_BACKEND_EWS_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_EWS, EBookBackenEwsClass))
typedef struct _EBookBackendEwsPrivate EBookBackendEwsPrivate;

typedef struct {
	EBookBackend         parent_object;
	EBookBackendEwsPrivate *priv;
} EBookBackendEws;

typedef struct {
	EBookBackendClass parent_class;
} EBookBackendEwsClass;

EBookBackend *e_book_backend_ews_new      (void);
GType       e_book_backend_ews_get_type (void);

#endif /* __E_BOOK_BACKEND_EWS_H__ */

