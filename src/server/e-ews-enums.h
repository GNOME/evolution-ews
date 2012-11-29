/*
 * e-ews-enums.h
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

#ifndef E_EWS_ENUMS_H
#define E_EWS_ENUMS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	E_EWS_EXTERNAL_AUDIENCE_NONE,
	E_EWS_EXTERNAL_AUDIENCE_KNOWN,
	E_EWS_EXTERNAL_AUDIENCE_ALL
} EEwsExternalAudience;

typedef enum {
	E_EWS_FOLDER_TYPE_UNKNOWN,
	E_EWS_FOLDER_TYPE_MAILBOX,
	E_EWS_FOLDER_TYPE_CALENDAR,
	E_EWS_FOLDER_TYPE_CONTACTS,
	E_EWS_FOLDER_TYPE_SEARCH,
	E_EWS_FOLDER_TYPE_TASKS,
	E_EWS_FOLDER_TYPE_MEMOS
} EEwsFolderType;

typedef enum {
	E_EWS_OOF_STATE_DISABLED,
	E_EWS_OOF_STATE_ENABLED,
	E_EWS_OOF_STATE_SCHEDULED
} EEwsOofState;

G_END_DECLS

#endif /* E_EWS_ENUMS_H */

