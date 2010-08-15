/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _XNTLM_H
#define _XNTLM_H

#include <glib.h>

GByteArray *xntlm_negotiate       (void);
gboolean    xntlm_parse_challenge (gpointer challenge, gint len, gchar **nonce,
				   gchar **nt_domain, gchar **w2k_domain);
GByteArray *xntlm_authenticate    (const gchar *nonce, const gchar *domain,
				   const gchar *user, const gchar *password,
				   const gchar *workstation);

#endif /* _XNTLM_H */
