/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EWS_TYPES_H__
#define __EWS_TYPES_H__

#include <glib.h>
#include <glib/gi18n.h>

typedef struct _EWSAction                     EWSAction;
typedef struct _EWSAddrEntry                  EWSAddrEntry;
typedef struct _EWSAddrList                   EWSAddrList;

typedef struct _EWSContext                    EWSContext;
typedef struct _EWSContextPrivate             EWSContextPrivate;
typedef struct _EWSContextClass               EWSContextClass;

typedef struct _EWSGlobalCatalog              EWSGlobalCatalog;
typedef struct _EWSGlobalCatalogPrivate       EWSGlobalCatalogPrivate;
typedef struct _EWSGlobalCatalogClass         EWSGlobalCatalogClass;

typedef struct _EWSOperation                  EWSOperation;

typedef struct _EWSRestriction                EWSRestriction;

typedef struct _EWSSecurityDescriptor         EWSSecurityDescriptor;
typedef struct _EWSSecurityDescriptorPrivate  EWSSecurityDescriptorPrivate;
typedef struct _EWSSecurityDescriptorClass    EWSSecurityDescriptorClass;

typedef struct _EWSSid                        EWSSid;
typedef struct _EWSSidPrivate                 EWSSidPrivate;
typedef struct _EWSSidClass                   EWSSidClass;

#define EWS_MAKE_TYPE(type_name,TypeName,class_init,init,parent) \
GType type_name##_get_type(void)			\
{							\
	static volatile gsize type_id__volatile = 0;	\
	if (g_once_init_enter (&type_id__volatile)) {	\
		static GTypeInfo const object_info = {	\
			sizeof (TypeName##Class),	\
							\
			(GBaseInitFunc) NULL,		\
			(GBaseFinalizeFunc) NULL,	\
							\
			(GClassInitFunc) class_init,	\
			(GClassFinalizeFunc) NULL,	\
			NULL,	/* class_data */	\
							\
			sizeof (TypeName),		\
			0,	/* n_preallocs */	\
			(GInstanceInitFunc) init,	\
		};					\
		GType type = g_type_register_static (parent, #TypeName, &object_info, 0); \
		g_once_init_leave (&type_id__volatile, type);	\
	}						\
	return type_id__volatile;			\
}

#define EWS_MAKE_TYPE_WITH_IFACE(type_name,TypeName,class_init,init,parent,iface_init,iparent) \
GType type_name##_get_type(void)			\
{							\
	static volatile gsize type_id__volatile = 0;	\
	if (g_once_init_enter (&type_id__volatile)) {	\
		static GTypeInfo const object_info = {	\
			sizeof (TypeName##Class),	\
							\
			(GBaseInitFunc) NULL,		\
			(GBaseFinalizeFunc) NULL,	\
							\
			(GClassInitFunc) class_init,	\
			(GClassFinalizeFunc) NULL,	\
			NULL,	/* class_data */	\
							\
			sizeof (TypeName),		\
			0,	/* n_preallocs */	\
			(GInstanceInitFunc) init,	\
		};					\
		static GInterfaceInfo const iface_info = {	\
			(GInterfaceInitFunc) iface_init,	\
			NULL,					\
			NULL					\
		};						\
		GType type = g_type_register_static (parent, #TypeName, &object_info, 0);	\
		g_type_add_interface_static (type, iparent, &iface_info);		\
		g_once_init_leave (&type_id__volatile, type);	\
	}						\
	return type_id__volatile;					\
}

/* Put "EWS_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES;" on a line to
 * separate a _() from a comment that doesn't go with it.
 */
#define EWS_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES

#endif
