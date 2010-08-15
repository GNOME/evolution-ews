/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef E2K_UTILS_H
#define E2K_UTILS_H

#include <time.h>
#include "e2k-types.h"

time_t  e2k_parse_timestamp       (const gchar *timestamp);
gchar   *e2k_make_timestamp        (time_t when);
gchar   *e2k_make_timestamp_rfc822 (time_t when);

time_t  e2k_systime_to_time_t     (guint32 systime);
guint32 e2k_systime_from_time_t   (time_t tt);

time_t  e2k_filetime_to_time_t    (guint64 filetime);
guint64 e2k_filetime_from_time_t  (time_t tt);

gchar *e2k_lf_to_crlf (const gchar *in);
gchar *e2k_crlf_to_lf (const gchar *in);

gchar *e2k_strdup_with_trailing_slash (const gchar *path);

const gchar *e2k_entryid_to_dn           (GByteArray *entryid);

gchar       *e2k_entryid_to_permanenturl (GByteArray *entryid,
					 const gchar *base_uri);
GByteArray *e2k_permanenturl_to_entryid (const gchar *permanenturl);

gint  e2k_ascii_strcase_equal (gconstpointer v,
			       gconstpointer v2);
guint e2k_ascii_strcase_hash  (gconstpointer v);

gboolean e2k_restriction_folders_only (E2kRestriction *rn);

GByteArray *e2k_entryid_generate_oneoff  (const gchar *display_name,
					  const gchar *email,
					  gboolean    unicode);
GByteArray *e2k_entryid_generate_local   (const gchar *exchange_dn);
GByteArray *e2k_entryid_generate_contact (GByteArray *contact_entryid,
					  gint         nth_address);
GByteArray *e2k_search_key_generate      (const gchar *addrtype,
					  const gchar *address);

#endif /* E2K_UTILS_H */
