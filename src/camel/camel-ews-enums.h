/*
 * camel-ews-enums.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
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
