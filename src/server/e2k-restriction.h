/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_RESTRICTION_H__
#define __E2K_RESTRICTION_H__

#include "e2k-types.h"

G_BEGIN_DECLS

typedef enum {
	E2K_RELOP_LT = 0,
	E2K_RELOP_LE = 1,
	E2K_RELOP_GT = 2,
	E2K_RELOP_GE = 3,
	E2K_RELOP_EQ = 4,
	E2K_RELOP_NE = 5,
	E2K_RELOP_RE = 6,

	E2K_RELOP_DL_MEMBER = 100
} E2kRestrictionRelop;

typedef enum {
	E2K_BMR_EQZ = 0,
	E2K_BMR_NEZ = 1
} E2kRestrictionBitop;

typedef enum {
	E2K_FL_FULLSTRING     = 0x00000,
	E2K_FL_SUBSTRING      = 0x00001,
	E2K_FL_PREFIX         = 0x00002,
	E2K_FL_SUFFIX         = 0x00003, /* Not a MAPI constant */

	E2K_FL_IGNORECASE     = 0x10000,
	E2K_FL_IGNORENONSPACE = 0x20000,
	E2K_FL_LOOSE          = 0x40000
} E2kRestrictionFuzzyLevel;

#define E2K_FL_MATCH_TYPE(fl) ((fl) & 0x3)

E2kRestriction *e2k_restriction_and         (gint                   nrns,
					     E2kRestriction      **rns,
					     gboolean              unref);
E2kRestriction *e2k_restriction_andv        (E2kRestriction       *rn, ...);
E2kRestriction *e2k_restriction_or          (gint                   nrns,
					     E2kRestriction      **rns,
					     gboolean              unref);
E2kRestriction *e2k_restriction_orv         (E2kRestriction       *rn, ...);
E2kRestriction *e2k_restriction_not         (E2kRestriction       *rn,
					     gboolean              unref);
E2kRestriction *e2k_restriction_content     (const gchar           *propname,
					     E2kRestrictionFuzzyLevel fuzzy_level,
					     const gchar           *value);
E2kRestriction *e2k_restriction_prop_bool   (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     gboolean              value);
E2kRestriction *e2k_restriction_prop_int    (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     gint                   value);
E2kRestriction *e2k_restriction_prop_date   (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     const gchar           *value);
E2kRestriction *e2k_restriction_prop_string (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     const gchar           *value);
E2kRestriction *e2k_restriction_prop_binary (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     gconstpointer         data,
					     gint                   len);
E2kRestriction *e2k_restriction_compare     (const gchar           *propname1,
					     E2kRestrictionRelop   relop,
					     const gchar           *propname2);
E2kRestriction *e2k_restriction_bitmask     (const gchar           *propname,
					     E2kRestrictionBitop   bitop,
					     guint32               mask);
E2kRestriction *e2k_restriction_size        (const gchar           *propname,
					     E2kRestrictionRelop   relop,
					     guint32               size);
E2kRestriction *e2k_restriction_exist       (const gchar           *propname);
E2kRestriction *e2k_restriction_sub         (const gchar           *subtable,
					     E2kRestriction       *rn,
					     gboolean              unref);

void            e2k_restriction_ref         (E2kRestriction       *rn);
void            e2k_restriction_unref       (E2kRestriction       *rn);

gchar           *e2k_restriction_to_sql      (E2kRestriction       *rn);

gboolean        e2k_restriction_extract     (guint8              **data,
					     gint                  *len,
					     E2kRestriction      **rn);
void            e2k_restriction_append      (GByteArray           *ba,
					     E2kRestriction       *rn);

G_END_DECLS

#endif /* __E2K_RESTRICTION_H__ */
