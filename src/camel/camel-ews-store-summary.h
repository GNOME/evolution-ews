/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Parthasarathi Susarla <sparthasrathi@novell.com>
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
 */

#ifndef CAMEL_EWS_STORE_SUMMARY_H
#define CAMEL_EWS_STORE_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_STORE_SUMMARY \
	(camel_ews_store_summary_get_type ())
#define CAMEL_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummary))
#define CAMEL_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))
#define CAMEL_IS_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_IS_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_EWS_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelEwsStoreSummary CamelEwsStoreSummary;
typedef struct _CamelEwsStoreSummaryClass CamelEwsStoreSummaryClass;
typedef struct _CamelEwsStoreSummaryPrivate CamelEwsStoreSummaryPrivate;

typedef struct _CamelEwsStoreInfo CamelEwsStoreInfo;

enum {
	CAMEL_GW_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_GW_STORE_INFO_LAST
};

struct _CamelEwsStoreInfo {
	CamelStoreInfo info;
	gchar *full_name;
};

typedef struct _CamelEwsStoreNamespace CamelEwsStoreNamespace;

struct _CamelEwsStoreNamespace {
	gchar *path;             /* display path */
	gchar *full_name;        /* real name */
	gchar sep;               /* directory separator */
};

struct _CamelEwsStoreSummary {
	CamelStoreSummary summary;
	CamelEwsStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;        /* version of base part of file */
	guint32 capabilities;
	CamelEwsStoreNamespace *namespace; /* eventually to be a list */
};

struct _CamelEwsStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

GType                        camel_ews_store_summary_get_type      (void);
CamelEwsStoreSummary      *camel_ews_store_summary_new        (void);
CamelEwsStoreInfo *camel_ews_store_summary_full_name(CamelEwsStoreSummary *s, const gchar *full_name);
CamelEwsStoreInfo *camel_ews_store_summary_add_from_full(CamelEwsStoreSummary *s, const gchar *full, gchar dir_sep);

gchar *camel_ews_store_summary_full_to_path(CamelEwsStoreSummary *s, const gchar *full_name, gchar dir_sep);
gchar *camel_ews_store_summary_path_to_full(CamelEwsStoreSummary *s, const gchar *path, gchar dir_sep);
gchar *camel_ews_store_summary_full_from_path(CamelEwsStoreSummary *s, const gchar *path);

CamelEwsStoreNamespace *camel_ews_store_summary_namespace_new(CamelEwsStoreSummary *s, const gchar *full_name, gchar dir_sep);
CamelEwsStoreNamespace *camel_ews_store_summary_namespace_find_path(CamelEwsStoreSummary *s, const gchar *path);
void camel_ews_store_summary_namespace_set(CamelEwsStoreSummary *s, CamelEwsStoreNamespace *ns);

#define camel_ews_store_info_full_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_LAST))

G_END_DECLS

#endif /* CAMEL_EWS_STORE_SUMMARY_H */
