/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_BACKEND_EWS_H
#define E_BOOK_BACKEND_EWS_H

#include <libedata-book/libedata-book.h>

#define E_TYPE_BOOK_BACKEND_EWS        (e_book_backend_ews_get_type ())
#define E_BOOK_BACKEND_EWS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_EWS, EBookBackendEws))
#define E_BOOK_BACKEND_EWS_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_EWS, EBookBackendEwsClass))
#define E_IS_BOOK_BACKEND_EWS(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_EWS))
#define E_IS_BOOK_BACKEND_EWS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_EWS))
#define E_BOOK_BACKEND_EWS_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_EWS, EBookBackenEwsClass))

typedef struct _EBookBackendEwsPrivate EBookBackendEwsPrivate;

typedef struct {
	EBookMetaBackend parent_object;
	EBookBackendEwsPrivate *priv;
} EBookBackendEws;

typedef struct {
	EBookMetaBackendClass parent_class;
} EBookBackendEwsClass;

GType       e_book_backend_ews_get_type (void);

#endif /* E_BOOK_BACKEND_EWS_H */
