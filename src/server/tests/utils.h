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

#ifndef E_EWS_TEST_UTILS_H
#define E_EWS_TEST_UTILS_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

G_BEGIN_DECLS

void
util_get_email_from_env (const gchar **email);

void
util_get_login_info_from_env (const gchar **username, const gchar **password, const gchar **uri);

G_END_DECLS

#endif
