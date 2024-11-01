/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_FOLDER_SEARCH_H
#define CAMEL_M365_FOLDER_SEARCH_H

#include <camel/camel.h>

#include "camel-m365-store.h"

/* Standard GObject macros */
#define CAMEL_TYPE_M365_FOLDER_SEARCH \
	(camel_m365_folder_search_get_type ())
#define CAMEL_M365_FOLDER_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_FOLDER_SEARCH, CamelM365FolderSearch))
#define CAMEL_M365_FOLDER_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_FOLDER_SEARCH, CamelM365FolderSearchClass))
#define CAMEL_IS_M365_FOLDER_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_FOLDER_SEARCH))
#define CAMEL_IS_M365_FOLDER_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_FOLDER_SEARCH))
#define CAMEL_M365_FOLDER_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_FOLDER_SEARCH, CamelM365FolderSearchClass))

G_BEGIN_DECLS

typedef struct _CamelM365FolderSearch CamelM365FolderSearch;
typedef struct _CamelM365FolderSearchClass CamelM365FolderSearchClass;
typedef struct _CamelM365FolderSearchPrivate CamelM365FolderSearchPrivate;

/**
 * CamelM365FolderSearch:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.24
 **/
struct _CamelM365FolderSearch {
	/*< private >*/
	CamelFolderSearch parent;
	CamelM365FolderSearchPrivate *priv;
};

struct _CamelM365FolderSearchClass {
	CamelFolderSearchClass parent_class;
};

GType		camel_m365_folder_search_get_type	(void) G_GNUC_CONST;
CamelFolderSearch *
		camel_m365_folder_search_new		(CamelM365Store *m365_store);
CamelM365Store *
		camel_m365_folder_search_ref_store	(CamelM365FolderSearch *self);
void		camel_m365_folder_search_set_store	(CamelM365FolderSearch *self,
							 CamelM365Store *m365_store);
void		camel_m365_folder_search_clear_cached_results
							(CamelM365FolderSearch *self);
void		camel_m365_folder_search_set_cancellable_and_error
							(CamelM365FolderSearch *self,
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* CAMEL_M365_FOLDER_SEARCH_H */
