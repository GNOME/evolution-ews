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

#ifndef E_O365_JSON_UTILS_H
#define E_O365_JSON_UTILS_H

#include <json-glib/json-glib.h>

G_BEGIN_DECLS

const gchar *	e_o365_mail_folder_get_display_name	(JsonObject *object);
const gchar *	e_o365_mail_folder_get_id		(JsonObject *object);
const gchar *	e_o365_mail_folder_get_parent_folder_id	(JsonObject *object);
gint32		e_o365_mail_folder_get_child_folder_count
							(JsonObject *object);
gint32		e_o365_mail_folder_get_total_item_count	(JsonObject *object);
gint32		e_o365_mail_folder_get_unread_item_count(JsonObject *object);

G_END_DECLS

#endif /* E_O365_JSON_UTILS_H */
