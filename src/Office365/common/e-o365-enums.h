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

#ifndef E_O365_ENUMS_H
#define E_O365_ENUMS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	E_O365_FOLDER_KIND_UNKNOWN,
	E_O365_FOLDER_KIND_MAIL,
	E_O365_FOLDER_KIND_CALENDAR,
	E_O365_FOLDER_KIND_CONTACTS,
	E_O365_FOLDER_KIND_SEARCH,
	E_O365_FOLDER_KIND_TASKS,
	E_O365_FOLDER_KIND_MEMOS
} EO365FolderKind;

G_END_DECLS

#endif /* E_O365_ENUMS_H */