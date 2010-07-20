/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 * Sivaiah Nallagatla <snallagatla@novell.com>
 *
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

#ifndef E_EWS_FILTER_H
#define E_EWS_FILTER_H

#include "soup-soap-message.h"
#include "soup-soap-response.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_FILTER           (e_ews_filter_get_type ())
#define E_EWS_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_FILTER, EEwsFilter))
#define E_EWS_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_FILTER, EEwsFilterClass))
#define E_IS_GW_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_FILTER))
#define E_IS_GW_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_FILTER))

typedef struct _EEwsFilter        EEwsFilter;
typedef struct _EEwsFilterClass   EEwsFilterClass;
typedef struct _EEwsFilterPrivate EEwsFilterPrivate;

typedef enum {
	E_EWS_FILTER_OP_AND,
	E_EWS_FILTER_OP_OR,
	E_EWS_FILTER_OP_NOT,
	E_EWS_FILTER_OP_EQUAL,
	E_EWS_FILTER_OP_NOTEQUAL,
	E_EWS_FILTER_OP_GREATERTHAN,
	E_EWS_FILTER_OP_LESSTHAN,
	E_EWS_FILTER_OP_GREATERTHAN_OR_EQUAL,
	E_EWS_FILTER_OP_LESSTHAN_OR_EQUAL,
	E_EWS_FILTER_OP_CONTAINS,
	E_EWS_FILTER_OP_CONTAINSWORD,
	E_EWS_FILTER_OP_BEGINS,
	E_EWS_FILTER_OP_EXISTS,
	E_EWS_FILTER_OP_NOTEXISTS

} EEwsFilterOpType;

struct _EEwsFilter {
	GObject parent;
	EEwsFilterPrivate *priv;
};

struct _EEwsFilterClass {
	GObjectClass parent_class;
};

GType       e_ews_filter_get_type (void);
EEwsFilter*  e_ews_filter_new(void);
void        e_ews_filter_add_filter_component (EEwsFilter *filter, EEwsFilterOpType operation, const gchar *field_name, const gchar *field_value);
void        e_ews_filter_append_to_soap_message (EEwsFilter *filter, SoupSoapMessage *msg);
void        e_ews_filter_group_conditions (EEwsFilter *filter, EEwsFilterOpType operation, gint num_of_condtions);

G_END_DECLS

#endif
