/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Siviah Nallagatla <snallagatla@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
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

#ifndef CAMEL_EWS_PRIVATE_H
#define CAMEL_EWS_PRIVATE_H

/* need a way to configure and save this data, if this header is to
 * be installed.  For now, dont install it */

#include "evolution-ews-config.h"

#ifdef ENABLE_THREADS
#define CAMEL_EWS_FOLDER_LOCK(f, l) \
	(g_mutex_lock (&((CamelEwsFolder *) f)->priv->l))
#define CAMEL_EWS_FOLDER_UNLOCK(f, l) \
	(g_mutex_unlock (&((CamelEwsFolder *) f)->priv->l))
#define CAMEL_EWS_FOLDER_REC_LOCK(f, l) \
	(g_rec_mutex_lock (&((CamelEwsFolder *) f)->priv->l))
#define CAMEL_EWS_FOLDER_REC_UNLOCK(f, l) \
	(g_rec_mutex_unlock (&((CamelEwsFolder *) f)->priv->l))
#else
#define CAMEL_EWS_FOLDER_LOCK(f, l)
#define CAMEL_EWS_FOLDER_UNLOCK(f, l)
#define CAMEL_EWS_FOLDER_REC_LOCK(f, l)
#define CAMEL_EWS_FOLDER_REC_UNLOCK(f, l)
#endif

#endif /* CAMEL_EWS_PRIVATE_H */
