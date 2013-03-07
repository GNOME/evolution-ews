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

#ifndef E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H
#define E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H

#include <e-util/e-util.h>

void	e_ews_subscribe_foreign_folder	(GtkWindow *parent,
					 CamelSession *session,
					 CamelStore *store,
					 EClientCache *client_cache);

#endif /* E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H */
