/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib.h>

#include "e-o365-message.h"

struct _EO365MessagePrivate {
	gboolean dummy;
};

G_DEFINE_TYPE_WITH_PRIVATE (EO365Message, e_o365_message, G_TYPE_OBJECT)

static void
o365_message_finalize (GObject *object)
{
	EO365Message *msg = E_O365_MESSAGE (object);

	msg->priv->dummy = FALSE;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_message_parent_class)->finalize (object);
}

static void
e_o365_message_class_init (EO365MessageClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = o365_message_finalize;
}

static void
e_o365_message_init (EO365Message *msg)
{
	msg->priv = e_o365_message_get_instance_private (msg);
}

EO365Message *
e_o365_message_new (void)
{
	return g_object_new (E_TYPE_O365_MESSAGE, NULL);
}
