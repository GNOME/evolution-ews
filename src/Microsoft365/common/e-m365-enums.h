/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_ENUMS_H
#define E_M365_ENUMS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	E_M365_FOLDER_KIND_UNKNOWN,
	E_M365_FOLDER_KIND_MAIL,
	E_M365_FOLDER_KIND_CALENDAR,
	E_M365_FOLDER_KIND_CONTACTS,
	E_M365_FOLDER_KIND_SEARCH,
	E_M365_FOLDER_KIND_TASKS,
	E_M365_FOLDER_KIND_MEMOS
} EM365FolderKind;

G_END_DECLS

#endif /* E_M365_ENUMS_H */