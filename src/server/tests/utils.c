/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Johnny Jacob <johnnyjacob@gmail.com>
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
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

/*Utility functions */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "utils.h"

void
util_get_email_from_env (const gchar **email)
{
	*email = g_getenv ("EWS_TEST_EMAIL");
}

void
util_get_login_info_from_env (const gchar **username, const gchar **password, const gchar **uri)
{
	*username = g_getenv ("EWS_TEST_USERNAME");
	*password = g_getenv ("EWS_TEST_PASSWORD");
	*uri = g_getenv ("EWS_TEST_URI");
}
