/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_ENUMS_H
#define CAMEL_EWS_ENUMS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN,
	CAMEL_EWS_STORE_OOO_ALERT_STATE_NOTIFIED,
	CAMEL_EWS_STORE_OOO_ALERT_STATE_CLOSED
} CamelEwsStoreOooAlertState;

G_END_DECLS

#endif /* CAMEL_EWS_ENUMS_H */
