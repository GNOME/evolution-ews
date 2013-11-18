/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_EWS_NOTIFICATION_H
#define E_EWS_NOTIFICATION_H

#include <glib-object.h>
#include "e-ews-connection.h"

#define E_TYPE_EWS_NOTIFICATION \
	(e_ews_notification_get_type ())
#define E_EWS_NOTIFICATION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_NOTIFICATION, EEwsNotification))
#define E_EWS_NOTIFICATION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST \
	 ((klass), E_TYPE_EWS_NOTIFICATION, EEwsNotificationClass))
#define E_IS_EWS_NOTIFICATION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_NOTIFICATION))
#define E_IS_EWS_NOTIFICATION_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((klass), E_TYPE_EWS_NOTIFICATION))

G_BEGIN_DECLS

typedef struct _EEwsNotification EEwsNotification;
typedef struct _EEwsNotificationClass EEwsNotificationClass;
typedef struct _EEwsNotificationPrivate EEwsNotificationPrivate;

struct _EEwsNotification {
	GObject parent;
	EEwsNotificationPrivate *priv;
};

struct _EEwsNotificationClass {
	GObjectClass parent_class;
};

GType		e_ews_notification_get_type	(void);
EEwsNotification *
		e_ews_notification_new		(EEwsConnection *connection);

void		e_ews_notification_start_listening_sync
						(EEwsNotification *notification,
						 GSList *folders);
void		e_ews_notification_stop_listening_sync
						(EEwsNotification *notification);

G_END_DECLS

#endif /* E_EWS_NOTIFICATION_H */
