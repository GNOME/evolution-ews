/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ews-oab-decoder.h
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


#ifndef _EWS_OAB_DECODER
#define _EWS_OAB_DECODER

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define EWS_TYPE_OAB_DECODER ews_oab_decoder_get_type()

#define EWS_OAB_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EWS_TYPE_OAB_DECODER, EwsOabDecoder))

#define EWS_OAB_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EWS_TYPE_OAB_DECODER, EwsOabDecoderClass))

#define EWS_IS_OAB_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EWS_TYPE_OAB_DECODER))

#define EWS_IS_OAB_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EWS_TYPE_OAB_DECODER))

#define EWS_OAB_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EWS_TYPE_OAB_DECODER, EwsOabDecoderClass))

typedef struct {
  GObject parent;
} EwsOabDecoder;

typedef struct {
  GObjectClass parent_class;
} EwsOabDecoderClass;

GType ews_oab_decoder_get_type (void);

EwsOabDecoder* ews_oab_decoder_new (const gchar *oab_filename);

G_END_DECLS

#endif /* _EWS_OAB_DECODER */
