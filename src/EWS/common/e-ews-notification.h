/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
const gchar * const *
		e_ews_notification_get_event_names
						(void);
EEwsNotification *
		e_ews_notification_new		(EEwsConnection *connection,
						 gchar *last_subscription_id); /* assumes ownership of it */

void		e_ews_notification_start_listening_sync
						(EEwsNotification *notification,
						 GSList *folders);
void		e_ews_notification_stop_listening_sync
						(EEwsNotification *notification);

G_END_DECLS

#endif /* E_EWS_NOTIFICATION_H */
