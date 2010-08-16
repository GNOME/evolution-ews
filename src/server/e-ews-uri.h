/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef E2K_URI_H
#define E2K_URI_H

#include <glib.h>

typedef struct {
	gchar  *protocol;
	gchar  *user;
	gchar  *domain;
	gchar  *authmech;
	gchar  *passwd;
	gchar  *host;
	gint    port;
	gchar  *path;
	GData *params;
	gchar  *query;
	gchar  *fragment;
} E2kUri;

E2kUri *    e2k_uri_new       (const gchar *uri_string);
void        e2k_uri_free      (E2kUri *uri);
const gchar *e2k_uri_get_param (E2kUri *uri, const gchar *name);

void        e2k_uri_decode         (gchar *part);
gchar *      e2k_uri_encode         (const gchar *in,
				    gboolean    wss_encode,
				    const gchar *extra_enc_chars);
void        e2k_uri_append_encoded (GString    *str,
				    const gchar *in,
				    gboolean    wss_encode,
				    const gchar *extra_enc_chars);

const gchar *e2k_uri_path      (const gchar *uri_string);

gchar       *e2k_uri_concat    (const gchar *uri_prefix, const gchar *tail);
const gchar *e2k_uri_relative  (const gchar *uri_prefix, const gchar *uri);

#endif /* E2K_URI_H */
