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

#include <gio/gio.h>
#include <libebook/e-contact.h>

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

typedef void (*EwsOabContactAddedCb) (EContact *contact, goffset offset, guint percent_complete, gpointer user_data, GError **error);

EwsOabDecoder *	ews_oab_decoder_new		(const gchar *oab_filename,
						 const gchar *cache_dir,
						 GError **error);
gboolean	ews_oab_decoder_decode		(EwsOabDecoder *eod,
						 EwsOabContactAddedCb cb,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
EContact *	ews_oab_decoder_get_contact_from_offset
						(EwsOabDecoder *eod,
						 goffset offset,
						 GSList *oab_props,
						 GCancellable *cancellable,
						 GError **error);
gchar *		ews_oab_decoder_get_oab_prop_string
						(EwsOabDecoder *eod,
						 GError **error);
gboolean	ews_oab_decoder_set_oab_prop_string
						(EwsOabDecoder *eod,
						 const gchar *prop_str,
						 GError **error);

G_END_DECLS

#endif /* _EWS_OAB_DECODER */
