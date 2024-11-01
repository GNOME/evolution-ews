/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_MESSAGE_INFO_H
#define CAMEL_EWS_MESSAGE_INFO_H

#include <glib-object.h>

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_MESSAGE_INFO \
	(camel_ews_message_info_get_type ())
#define CAMEL_EWS_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_MESSAGE_INFO, CamelEwsMessageInfo))
#define CAMEL_EWS_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_MESSAGE_INFO, CamelEwsMessageInfoClass))
#define CAMEL_IS_EWS_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_MESSAGE_INFO))
#define CAMEL_IS_EWS_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_MESSAGE_INFO))
#define CAMEL_EWS_MESSAGE_INFO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_MESSAGE_INFO, CamelEwsMessageInfoClass))

G_BEGIN_DECLS

/* extra summary flags*/
enum {
	CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING = CAMEL_MESSAGE_FOLDER_FLAGGED << 1
};

typedef struct _CamelEwsMessageInfo CamelEwsMessageInfo;
typedef struct _CamelEwsMessageInfoClass CamelEwsMessageInfoClass;
typedef struct _CamelEwsMessageInfoPrivate CamelEwsMessageInfoPrivate;

struct _CamelEwsMessageInfo {
	CamelMessageInfoBase parent;
	CamelEwsMessageInfoPrivate *priv;
};

struct _CamelEwsMessageInfoClass {
	CamelMessageInfoBaseClass parent_class;
};

GType		camel_ews_message_info_get_type	(void);

guint32		camel_ews_message_info_get_server_flags	(const CamelEwsMessageInfo *emi);
gboolean	camel_ews_message_info_set_server_flags	(CamelEwsMessageInfo *emi,
							 guint32 server_flags);
gint32		camel_ews_message_info_get_item_type	(const CamelEwsMessageInfo *emi);
gboolean	camel_ews_message_info_set_item_type	(CamelEwsMessageInfo *emi,
							 gint32 item_type);
const gchar *	camel_ews_message_info_get_change_key	(const CamelEwsMessageInfo *emi);
gchar *		camel_ews_message_info_dup_change_key	(const CamelEwsMessageInfo *emi);
gboolean	camel_ews_message_info_set_change_key	(CamelEwsMessageInfo *emi,
							 const gchar *change_key);
gboolean	camel_ews_message_info_take_change_key	(CamelEwsMessageInfo *emi,
							 gchar *change_key);

G_END_DECLS

#endif /* CAMEL_EWS_MESSAGE_INFO_H */
