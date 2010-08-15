/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_PROPERTIES_H__
#define __E2K_PROPERTIES_H__

#include <glib.h>
#include <libxml/tree.h>

typedef struct E2kProperties E2kProperties;

typedef enum {
	E2K_PROP_TYPE_UNKNOWN,

	E2K_PROP_TYPE_STRING,
	E2K_PROP_TYPE_BINARY,
	E2K_PROP_TYPE_STRING_ARRAY,
	E2K_PROP_TYPE_BINARY_ARRAY,
	E2K_PROP_TYPE_XML,

	/* These are all stored as STRING or STRING_ARRAY */
	E2K_PROP_TYPE_INT,
	E2K_PROP_TYPE_INT_ARRAY,
	E2K_PROP_TYPE_BOOL,
	E2K_PROP_TYPE_FLOAT,
	E2K_PROP_TYPE_DATE
} E2kPropType;

#define E2K_PROP_TYPE_IS_ARRAY(type) (((type) == E2K_PROP_TYPE_STRING_ARRAY) || ((type) == E2K_PROP_TYPE_BINARY_ARRAY) || ((type) == E2K_PROP_TYPE_INT_ARRAY))

E2kProperties *e2k_properties_new               (void);
E2kProperties *e2k_properties_copy              (E2kProperties *props);
void           e2k_properties_free              (E2kProperties *props);

gpointer       e2k_properties_get_prop          (E2kProperties *props,
						 const gchar    *propname);
gboolean       e2k_properties_empty             (E2kProperties *props);

void           e2k_properties_set_string        (E2kProperties *props,
						 const gchar    *propname,
						 gchar          *value);
void           e2k_properties_set_string_array  (E2kProperties *props,
						 const gchar    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_binary        (E2kProperties *props,
						 const gchar    *propname,
						 GByteArray    *value);
void           e2k_properties_set_binary_array  (E2kProperties *props,
						 const gchar    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_int           (E2kProperties *props,
						 const gchar    *propname,
						 gint            value);
void           e2k_properties_set_int_array     (E2kProperties *props,
						 const gchar    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_xml           (E2kProperties *props,
						 const gchar    *propname,
						 xmlNode       *value);
void           e2k_properties_set_bool          (E2kProperties *props,
						 const gchar    *propname,
						 gboolean       value);
void           e2k_properties_set_float         (E2kProperties *props,
						 const gchar    *propname,
						 gfloat          value);
void           e2k_properties_set_date          (E2kProperties *props,
						 const gchar    *propname,
						 gchar          *value);

void           e2k_properties_set_type_as_string       (E2kProperties *props,
							const gchar    *propname,
							E2kPropType    type,
							gchar          *value);
void           e2k_properties_set_type_as_string_array (E2kProperties *props,
							const gchar    *propname,
							E2kPropType    type,
							GPtrArray     *value);

void           e2k_properties_remove            (E2kProperties *props,
						 const gchar    *propname);

typedef void (*E2kPropertiesForeachFunc)        (const gchar    *propname,
						 E2kPropType    type,
						 gpointer       value,
						 gpointer       user_data);
void           e2k_properties_foreach           (E2kProperties *props,
						 E2kPropertiesForeachFunc callback,
						 gpointer       user_data);
void           e2k_properties_foreach_removed   (E2kProperties *props,
						 E2kPropertiesForeachFunc callback,
						 gpointer       user_data);

typedef void(*E2kPropertiesForeachNamespaceFunc)(const gchar    *namespace,
						 gchar           abbrev,
						 gpointer       user_data);
void           e2k_properties_foreach_namespace (E2kProperties *props,
						 E2kPropertiesForeachNamespaceFunc callback,
						 gpointer user_data);

const gchar *e2k_prop_namespace_name   (const gchar *prop);
gchar        e2k_prop_namespace_abbrev (const gchar *prop);
const gchar *e2k_prop_property_name    (const gchar *prop);

guint32     e2k_prop_proptag          (const gchar *prop);
const gchar *e2k_proptag_prop          (guint32     proptag);

#define E2K_PROPTAG_TYPE(proptag) (proptag & 0xFFFF)
#define E2K_PROPTAG_ID(proptag) (proptag & 0xFFFF0000)

#define E2K_PT_SHORT    0x0002
#define E2K_PT_LONG     0x0003
#define E2K_PT_ERROR    0x000a
#define E2K_PT_BOOLEAN  0x000b
#define E2K_PT_OBJECT   0x000d
#define E2K_PT_LONGLONG 0x0014
#define E2K_PT_STRING8  0x001e
#define E2K_PT_UNICODE  0x001f
#define E2K_PT_SYSTIME  0x0040
#define E2K_PT_BINARY   0x0102

#endif /* __E2K_PROPERTIES_H__ */
