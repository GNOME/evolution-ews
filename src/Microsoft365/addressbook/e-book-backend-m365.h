/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_BACKEND_M365_H
#define E_BOOK_BACKEND_M365_H

#include <libedata-book/libedata-book.h>

#define E_TYPE_BOOK_BACKEND_M365         (e_book_backend_m365_get_type ())
#define E_BOOK_BACKEND_M365(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_M365, EBookBackendM365))
#define E_BOOK_BACKEND_M365_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_M365, EBookBackendM365Class))
#define E_IS_BOOK_BACKEND_M365(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_M365))
#define E_IS_BOOK_BACKEND_M365_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_M365))
#define E_BOOK_BACKEND_M365_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_M365, EBookBackenM365Class))

typedef struct _EBookBackendM365Private EBookBackendM365Private;

typedef struct {
	EBookMetaBackend parent_object;
	EBookBackendM365Private *priv;
} EBookBackendM365;

typedef struct {
	EBookMetaBackendClass parent_class;
} EBookBackendM365Class;

GType       e_book_backend_m365_get_type (void);

#endif /* E_BOOK_BACKEND_M365_H */
