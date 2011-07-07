/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ews-oab-decoder.c
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "ews-oab-decoder.h"
#include "ews-oab-props.h"

G_DEFINE_TYPE (EwsOabDecoder, ews_oab_decoder, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EWS_TYPE_OAB_DECODER, EwsOabDecoderPrivate))

typedef struct _EwsOabDecoderPrivate EwsOabDecoderPrivate;

struct _EwsOabDecoderPrivate {
	gchar *filename;
};

static void
ews_oab_decoder_finalize (GObject *object)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (object);

	if (priv->filename) {
		g_free (priv->filename);
		priv->filename = NULL;
	}

	G_OBJECT_CLASS (ews_oab_decoder_parent_class)->finalize (object);
}

static void
ews_oab_decoder_class_init (EwsOabDecoderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EwsOabDecoderPrivate));

	object_class->finalize = ews_oab_decoder_finalize;
}

static void
ews_oab_decoder_init (EwsOabDecoder *self)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (self);

	priv->filename = NULL;
}

EwsOabDecoder*
ews_oab_decoder_new (const gchar *oab_filename)
{
	EwsOabDecoder *eod;
	EwsOabDecoderPrivate *priv;

	eod = g_object_new (EWS_TYPE_OAB_DECODER, NULL);
	priv = GET_PRIVATE (eod);

	priv->filename = g_strdup (oab_filename);
}
