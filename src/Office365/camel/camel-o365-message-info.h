/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CAMEL_O365_MESSAGE_INFO_H
#define CAMEL_O365_MESSAGE_INFO_H

#include <glib-object.h>

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_O365_MESSAGE_INFO \
	(camel_o365_message_info_get_type ())
#define CAMEL_O365_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_MESSAGE_INFO, CamelO365MessageInfo))
#define CAMEL_O365_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_MESSAGE_INFO, CamelO365MessageInfoClass))
#define CAMEL_IS_O365_MESSAGE_INFO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_MESSAGE_INFO))
#define CAMEL_IS_O365_MESSAGE_INFO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_MESSAGE_INFO))
#define CAMEL_O365_MESSAGE_INFO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_MESSAGE_INFO, CamelO365MessageInfoClass))

G_BEGIN_DECLS

/* extra summary flags*/
enum {
	CAMEL_O365_MESSAGE_MSGFLAG_RN_PENDING = CAMEL_MESSAGE_FOLDER_FLAGGED << 1
};

typedef struct _CamelO365MessageInfo CamelO365MessageInfo;
typedef struct _CamelO365MessageInfoClass CamelO365MessageInfoClass;
typedef struct _CamelO365MessageInfoPrivate CamelO365MessageInfoPrivate;

struct _CamelO365MessageInfo {
	CamelMessageInfoBase parent;
	CamelO365MessageInfoPrivate *priv;
};

struct _CamelO365MessageInfoClass {
	CamelMessageInfoBaseClass parent_class;
};

GType		camel_o365_message_info_get_type	(void);

guint32		camel_o365_message_info_get_server_flags(const CamelO365MessageInfo *omi);
gboolean	camel_o365_message_info_set_server_flags(CamelO365MessageInfo *omi,
							 guint32 server_flags);
gint32		camel_o365_message_info_get_item_type	(const CamelO365MessageInfo *omi);
gboolean	camel_o365_message_info_set_item_type	(CamelO365MessageInfo *omi,
							 gint32 item_type);
const gchar *	camel_o365_message_info_get_change_key	(const CamelO365MessageInfo *omi);
gchar *		camel_o365_message_info_dup_change_key	(const CamelO365MessageInfo *omi);
gboolean	camel_o365_message_info_set_change_key	(CamelO365MessageInfo *omi,
							 const gchar *change_key);
gboolean	camel_o365_message_info_take_change_key	(CamelO365MessageInfo *omi,
							 gchar *change_key);

G_END_DECLS

#endif /* CAMEL_O365_MESSAGE_INFO_H */
