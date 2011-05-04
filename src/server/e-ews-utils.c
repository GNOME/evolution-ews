/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Or Goshen, Pavel Ocheretny
 *
 * Copyright (C) 1999-2011 Intel, Inc. (www.intel.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "e-ews-utils.h"

void
e_ews_free_id (EwsId *id)
{
	if (id) {
		g_free (id->id);
		g_free (id->change_key);
		g_free (id);
	}
}

EwsId *
e_ews_parse_item_id (ESoapParameter *item_node, GError **error)
{
	ESoapParameter *subparam, *node;
	EwsId *item_id;

	node = e_soap_parameter_get_first_child (item_node);

	subparam = e_soap_parameter_get_first_child (node);

	item_id= g_new0 (EwsId, 1);
	item_id->id = e_soap_parameter_get_property (subparam, "Id");
	item_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");

	return item_id;
}
