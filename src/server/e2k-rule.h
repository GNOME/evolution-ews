/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_RULE_H__
#define __E2K_RULE_H__

#include "e2k-types.h"
#include "e2k-properties.h"
#include "e2k-restriction.h"

/* We define these types here because they're private to libexchange,
 * so code that #includes e2k-restriction, etc, shouldn't see them.
 */

typedef struct {
	const gchar *name;
	guint32     proptag;
} E2kRuleProp;

void e2k_rule_prop_set (E2kRuleProp *prop, const gchar *propname);

typedef struct {
	E2kRuleProp  prop;
	E2kPropType  type;
	gpointer     value;
} E2kPropValue;

typedef enum {
	E2K_RESTRICTION_AND		= 0,
	E2K_RESTRICTION_OR		= 1,
	E2K_RESTRICTION_NOT		= 2,
	E2K_RESTRICTION_CONTENT		= 3,
	E2K_RESTRICTION_PROPERTY	= 4,
	E2K_RESTRICTION_COMPAREPROPS	= 5,
	E2K_RESTRICTION_BITMASK		= 6,
	E2K_RESTRICTION_SIZE		= 7,
	E2K_RESTRICTION_EXIST		= 8,
	E2K_RESTRICTION_SUBRESTRICTION	= 9,
	E2K_RESTRICTION_COMMENT		= 10
} E2kRestrictionType;

struct _E2kRestriction {
	/*< private >*/

	E2kRestrictionType type;
	gint ref_count;

	union {
		struct {
			guint            nrns;
			E2kRestriction **rns;
		} and;

		struct {
			guint            nrns;
			E2kRestriction **rns;
		} or;

		struct {
			E2kRestriction *rn;
		} not;

		struct {
			E2kRestrictionFuzzyLevel fuzzy_level;
			E2kPropValue             pv;
		} content;

		struct {
			E2kRestrictionRelop  relop;
			E2kPropValue         pv;
		} property;

		struct {
			E2kRestrictionRelop  relop;
			E2kRuleProp          prop1;
			E2kRuleProp          prop2;
		} compare;

		struct {
			E2kRestrictionBitop  bitop;
			E2kRuleProp          prop;
			guint32              mask;
		} bitmask;

		struct {
			E2kRestrictionRelop  relop;
			E2kRuleProp          prop;
			guint32              size;
		} size;

		struct {
			E2kRuleProp prop;
		} exist;

		struct {
			E2kRuleProp     subtable;
			E2kRestriction *rn;
		} sub;

		struct {
			guint32         nprops;
			E2kRestriction *rn;
			E2kPropValue   *props;
		} comment;
	} res;
};

typedef enum {
	E2K_ACTION_MOVE         = 1,
	E2K_ACTION_COPY         = 2,
	E2K_ACTION_REPLY        = 3,
	E2K_ACTION_OOF_REPLY    = 4,
	E2K_ACTION_DEFER        = 5,
	E2K_ACTION_BOUNCE       = 6,
	E2K_ACTION_FORWARD      = 7,
	E2K_ACTION_DELEGATE     = 8,
	E2K_ACTION_TAG          = 9,
	E2K_ACTION_DELETE       = 10,
	E2K_ACTION_MARK_AS_READ = 11
} E2kActionType;

typedef enum {
	E2K_ACTION_REPLY_FLAVOR_NOT_ORIGINATOR = 1,
	E2K_ACTION_REPLY_FLAVOR_STOCK_TEMPLATE = 2
} E2kActionReplyFlavor;

typedef enum {
	E2K_ACTION_FORWARD_FLAVOR_PRESERVE_SENDER = 1,
	E2K_ACTION_FORWARD_FLAVOR_DO_NOT_MUNGE    = 2,
	E2K_ACTION_FORWARD_FLAVOR_REDIRECT        = 3,
	E2K_ACTION_FORWARD_FLAVOR_AS_ATTACHMENT   = 4
} E2kActionForwardFlavor;

typedef enum {
	E2K_ACTION_BOUNCE_CODE_TOO_LARGE     = 13,
	E2K_ACTION_BOUNCE_CODE_FORM_MISMATCH = 31,
	E2K_ACTION_BOUNCE_CODE_ACCESS_DENIED = 38
} E2kActionBounceCode;

struct _E2kAddrEntry {
	/*< private >*/
	guint32        nvalues;
	E2kPropValue  *propval;
};

struct _E2kAddrList {
	/*< private >*/
	guint32        nentries;
	E2kAddrEntry   entry[1];
};

struct _E2kAction {
	/*< private >*/

	E2kActionType type;
	guint32       flavor;
	guint32       flags;

	union {
		struct {
			GByteArray *store_entryid;
			GByteArray *folder_source_key;
		} xfer;

		struct {
			GByteArray *entryid;
			guint8      reply_template_guid[16];
		} reply;

		GByteArray   *defer_data;
		guint32       bounce_code;
		E2kAddrList  *addr_list;
		E2kPropValue  proptag;
	} act;
};

typedef enum {
	E2K_RULE_STATE_DISABLED          = 0x00,
	E2K_RULE_STATE_ENABLED           = 0x01,
	E2K_RULE_STATE_ERROR             = 0x02,
	E2K_RULE_STATE_ONLY_WHEN_OOF     = 0x04,
	E2K_RULE_STATE_KEEP_OOF_HISTORY  = 0x08,
	E2K_RULE_STATE_EXIT_LEVEL        = 0x10,

	E2K_RULE_STATE_CLEAR_OOF_HISTORY = 0x80000000
} E2kRuleState;

typedef struct {
	gchar           *name;
	guint32         sequence;
	guint32         state;
	guint32         user_flags;
	guint32         level;
	guint32         condition_lcid;
	E2kRestriction *condition;
	GPtrArray      *actions;
	gchar           *provider;
	GByteArray     *provider_data;
} E2kRule;

typedef struct {
	guint8     version;
	guint32    codepage;
	GPtrArray *rules;
} E2kRules;

E2kRules   *e2k_rules_from_binary (GByteArray *rules_data);
GByteArray *e2k_rules_to_binary   (E2kRules   *rules);
void        e2k_rules_free        (E2kRules   *rules);
void        e2k_rule_free         (E2kRule    *rule);

/* Generic rule read/write code */

void     e2k_rule_write_uint32      (guint8 *ptr, guint32 val);
void     e2k_rule_append_uint32     (GByteArray *ba, guint32 val);
guint32  e2k_rule_read_uint32       (guint8 *ptr);
gboolean e2k_rule_extract_uint32    (guint8 **ptr, gint *len,
				     guint32 *val);

void     e2k_rule_write_uint16      (guint8 *ptr, guint16 val);
void     e2k_rule_append_uint16     (GByteArray *ba, guint16 val);
guint16  e2k_rule_read_uint16       (guint8 *ptr);
gboolean e2k_rule_extract_uint16    (guint8 **ptr, gint *len,
				     guint16 *val);

void     e2k_rule_append_byte       (GByteArray *ba, guint8 val);
gboolean e2k_rule_extract_byte      (guint8 **ptr, gint *len,
				     guint8 *val);

void     e2k_rule_append_string     (GByteArray *ba, const gchar *str);
gboolean e2k_rule_extract_string    (guint8 **ptr, gint *len,
				     gchar **str);

void     e2k_rule_append_unicode    (GByteArray *ba, const gchar *str);
gboolean e2k_rule_extract_unicode   (guint8 **ptr, gint *len,
				     gchar **str);

void     e2k_rule_append_binary     (GByteArray *ba, GByteArray *data);
gboolean e2k_rule_extract_binary    (guint8 **ptr, gint *len,
				     GByteArray **data);

void     e2k_rule_append_proptag    (GByteArray *ba, E2kRuleProp *prop);
gboolean e2k_rule_extract_proptag   (guint8 **ptr, gint *len,
				     E2kRuleProp *prop);

void     e2k_rule_append_propvalue  (GByteArray *ba, E2kPropValue *pv);
gboolean e2k_rule_extract_propvalue (guint8 **ptr, gint *len,
				     E2kPropValue *pv);
void     e2k_rule_free_propvalue    (E2kPropValue *pv);

#endif /* __E2K_RULE_H__ */
