/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Siviah Nallagatla <snallagatla@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_PRIVATE_H
#define CAMEL_EWS_PRIVATE_H

/* need a way to configure and save this data, if this header is to
 * be installed.  For now, don't install it */

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
