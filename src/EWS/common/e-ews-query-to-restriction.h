/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Punit Jain <jpunit@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_QUERY_TO_RESTRICTION_H
#define E_EWS_QUERY_TO_RESTRICTION_H

#include "common/e-soap-message.h"
#include "common/e-ews-folder.h"

gboolean	e_ews_query_check_applicable	(const gchar *query,
						 EEwsFolderType type);

void		e_ews_query_to_restriction	(ESoapMessage *msg,
						 const gchar *query,
						 EEwsFolderType type);

#endif /* E_EWS_QUERY_TO_RESTRICTION_H */