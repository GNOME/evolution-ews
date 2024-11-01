/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_MESSAGE_INFO_H
#define CAMEL_M365_MESSAGE_INFO_H

#include <glib-object.h>

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_M365_MESSAGE_INFO \
	(camel_m365_message_info_get_type ())
#define CAMEL_M365_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_MESSAGE_INFO, CamelM365MessageInfo))
#define CAMEL_M365_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_MESSAGE_INFO, CamelM365MessageInfoClass))
#define CAMEL_IS_M365_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_MESSAGE_INFO))
#define CAMEL_IS_M365_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_MESSAGE_INFO))
#define CAMEL_M365_MESSAGE_INFO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_MESSAGE_INFO, CamelM365MessageInfoClass))

G_BEGIN_DECLS

/* extra summary flags*/
enum {
	CAMEL_M365_MESSAGE_MSGFLAG_RN_PENDING = CAMEL_MESSAGE_FOLDER_FLAGGED << 1
};

typedef struct _CamelM365MessageInfo CamelM365MessageInfo;
typedef struct _CamelM365MessageInfoClass CamelM365MessageInfoClass;
typedef struct _CamelM365MessageInfoPrivate CamelM365MessageInfoPrivate;

struct _CamelM365MessageInfo {
	CamelMessageInfoBase parent;
	CamelM365MessageInfoPrivate *priv;
};

struct _CamelM365MessageInfoClass {
	CamelMessageInfoBaseClass parent_class;
};

GType		camel_m365_message_info_get_type	(void);

guint32		camel_m365_message_info_get_server_flags(const CamelM365MessageInfo *omi);
gboolean	camel_m365_message_info_set_server_flags(CamelM365MessageInfo *omi,
							 guint32 server_flags);
gint32		camel_m365_message_info_get_item_type	(const CamelM365MessageInfo *omi);
gboolean	camel_m365_message_info_set_item_type	(CamelM365MessageInfo *omi,
							 gint32 item_type);
const gchar *	camel_m365_message_info_get_change_key	(const CamelM365MessageInfo *omi);
gchar *		camel_m365_message_info_dup_change_key	(const CamelM365MessageInfo *omi);
gboolean	camel_m365_message_info_set_change_key	(CamelM365MessageInfo *omi,
							 const gchar *change_key);
gboolean	camel_m365_message_info_take_change_key	(CamelM365MessageInfo *omi,
							 gchar *change_key);

G_END_DECLS

#endif /* CAMEL_M365_MESSAGE_INFO_H */
