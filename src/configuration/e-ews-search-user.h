/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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
 *
 * Authors:
 *    Milan Crha <mcrha@redhat.com>
 *
 * Copyright (C) 2012 Red Hat, Inc. (www.redhat.com)
 *
 */

#ifndef E_EWS_SEARCH_USER_H
#define E_EWS_SEARCH_USER_H

#include <gtk/gtk.h>
#include "server/e-ews-connection.h"
#include "server/e-ews-item.h"

gboolean	e_ews_search_user_modal	(GtkWindow *parent,
					 EEwsConnection *conn,
					 const gchar *search_this,
					 gchar **display_name,
					 gchar **email);

#endif /* E_EWS_SEARCH_USER_H */
