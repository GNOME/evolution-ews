/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  David Woodhouse <dwmw2@infradead.org>
 *
 * Copyright © 2011 Intel Corporation
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

#ifndef E_EWS_COMPAT_H
#define E_EWS_COMPAT_H

/* Fugly as hell, but it does the job... */

#include <libedataserver/eds-version.h>
#if EDS_CHECK_VERSION(2,33,0)
#define EVO2(...)
#define EVO3(...) __VA_ARGS__
#define EVO3_sync(x) x ## _sync
#else
#define EVO2(...) __VA_ARGS__
#define EVO3(...)
#define EVO3_sync(x) x
#endif


#endif /* E_EWS_COMPAT_H */
