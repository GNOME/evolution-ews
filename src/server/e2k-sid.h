/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __E2K_SID_H__
#define __E2K_SID_H__

#include "e2k-types.h"

#include <glib-object.h>
#include <libxml/tree.h>

#define E2K_TYPE_SID            (e2k_sid_get_type ())
#define E2K_SID(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_SID, E2kSid))
#define E2K_SID_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_SID, E2kSidClass))
#define E2K_IS_SID(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_SID))
#define E2K_IS_SID_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_SID))

typedef enum {
	E2K_SID_TYPE_INVALID,
	E2K_SID_TYPE_USER,
	E2K_SID_TYPE_ALIAS,
	E2K_SID_TYPE_GROUP,
	E2K_SID_TYPE_WELL_KNOWN_GROUP,
	E2K_SID_TYPE_DOMAIN,
	E2K_SID_TYPE_DELETED_ACCOUNT,
	E2K_SID_TYPE_UNKNOWN,
	E2K_SID_TYPE_COMPUTER
} E2kSidType;

struct _E2kSid {
	GObject parent;

	E2kSidPrivate *priv;
};

struct _E2kSidClass {
	GObjectClass parent_class;

};

GType         e2k_sid_get_type            (void);

E2kSid       *e2k_sid_new_from_string_sid (E2kSidType     type,
					   const gchar    *string_sid,
					   const gchar    *display_name);
E2kSid       *e2k_sid_new_from_binary_sid (E2kSidType     type,
					   const guint8  *binary_sid,
					   const gchar    *display_name);

E2kSidType    e2k_sid_get_sid_type        (E2kSid        *sid);
const gchar   *e2k_sid_get_string_sid      (E2kSid        *sid);
const guint8 *e2k_sid_get_binary_sid      (E2kSid        *sid);
const gchar   *e2k_sid_get_display_name    (E2kSid        *sid);

#define E2K_SID_BINARY_SID_MIN_LEN   8
#define E2K_SID_BINARY_SID_LEN(bsid) (8 + ((guint8 *)bsid)[1] * 4)
guint         e2k_sid_binary_sid_hash     (gconstpointer  key);
gint          e2k_sid_binary_sid_equal    (gconstpointer  a,
					   gconstpointer  b);

/* Some well-known SIDs */
#define E2K_SID_WKS_EVERYONE       "S-1-1-0"
#define E2K_SID_WKS_EVERYONE_NAME  "Default"
#define E2K_SID_WKS_ANONYMOUS      "S-1-5-7"
#define E2K_SID_WKS_ANONYMOUS_NAME "Anonymous"

#endif

