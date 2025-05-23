/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_SEARCH_H
#define CAMEL_EWS_SEARCH_H

#include <camel/camel.h>

#include "camel-ews-store.h"

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_SEARCH \
	(camel_ews_search_get_type ())
#define CAMEL_EWS_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_SEARCH, CamelEwsSearch))
#define CAMEL_EWS_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_SEARCH, CamelEwsSearchClass))
#define CAMEL_IS_EWS_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_SEARCH))
#define CAMEL_IS_EWS_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_SEARCH))
#define CAMEL_EWS_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_SEARCH, CamelEwsSearchClass))

G_BEGIN_DECLS

typedef struct _CamelEwsSearch CamelEwsSearch;
typedef struct _CamelEwsSearchClass CamelEwsSearchClass;
typedef struct _CamelEwsSearchPrivate CamelEwsSearchPrivate;

/**
 * CamelEwsSearch:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.24
 **/
struct _CamelEwsSearch {
	/*< private >*/
	CamelFolderSearch parent;
	CamelEwsSearchPrivate *priv;
};

struct _CamelEwsSearchClass {
	CamelFolderSearchClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_ews_search_get_type	(void) G_GNUC_CONST;
CamelFolderSearch *
		camel_ews_search_new		(CamelEwsStore *ews_store);
CamelEwsStore *
		camel_ews_search_ref_store	(CamelEwsSearch *search);
void		camel_ews_search_set_store	(CamelEwsSearch *search,
						 CamelEwsStore *ews_store);
void		camel_ews_search_clear_cached_results
						(CamelEwsSearch *search);
void		camel_ews_search_set_cancellable_and_error
						(CamelEwsSearch *search,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_EWS_SEARCH_H */
