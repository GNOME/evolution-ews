/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef E_BOOK_BACKEND_O365_H
#define E_BOOK_BACKEND_O365_H

#include <libedata-book/libedata-book.h>

#define E_TYPE_BOOK_BACKEND_O365         (e_book_backend_o365_get_type ())
#define E_BOOK_BACKEND_O365(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_O365, EBookBackendO365))
#define E_BOOK_BACKEND_O365_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_O365, EBookBackendO365Class))
#define E_IS_BOOK_BACKEND_O365(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_O365))
#define E_IS_BOOK_BACKEND_O365_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_O365))
#define E_BOOK_BACKEND_O365_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_O365, EBookBackenO365Class))

typedef struct _EBookBackendO365Private EBookBackendO365Private;

typedef struct {
	EBookMetaBackend parent_object;
	EBookBackendO365Private *priv;
} EBookBackendO365;

typedef struct {
	EBookMetaBackendClass parent_class;
} EBookBackendO365Class;

GType       e_book_backend_o365_get_type (void);

#endif /* E_BOOK_BACKEND_O365_H */
