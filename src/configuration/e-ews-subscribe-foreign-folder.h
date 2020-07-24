/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H
#define E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H

#include <e-util/e-util.h>

void	e_ews_subscribe_foreign_folder	(GtkWindow *parent,
					 CamelSession *session,
					 CamelStore *store,
					 EClientCache *client_cache);

#endif /* E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H */
