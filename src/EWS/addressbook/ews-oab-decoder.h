/*
 * SPDX-FileCopyrightText: (C) 1999-2011 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Chenthill Palanisamy <pchenthill@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EWS_OAB_DECODER_H
#define EWS_OAB_DECODER_H

#include <libebook/libebook.h>

/* Standard GObject macros */
#define EWS_TYPE_OAB_DECODER \
	(ews_oab_decoder_get_type ())
#define EWS_OAB_DECODER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EWS_TYPE_OAB_DECODER, EwsOabDecoder))
#define EWS_OAB_DECODER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EWS_TYPE_OAB_DECODER, EwsOabDecoderClass))
#define EWS_IS_OAB_DECODER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EWS_TYPE_OAB_DECODER))
#define EWS_IS_OAB_DECODER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EWS_TYPE_OAB_DECODER))
#define EWS_OAB_DECODER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EWS_TYPE_OAB_DECODER, EwsOabDecoderClass))

G_BEGIN_DECLS

typedef struct _EwsOabDecoder EwsOabDecoder;
typedef struct _EwsOabDecoderClass EwsOabDecoderClass;
typedef struct _EwsOabDecoderPrivate EwsOabDecoderPrivate;

struct _EwsOabDecoder {
	GObject parent;
	EwsOabDecoderPrivate *priv;
};

struct _EwsOabDecoderClass {
	GObjectClass parent_class;
};

typedef void	(*EwsOabContactAddedCb)		(EContact *contact,
						 goffset offset,
						 const gchar *sha1,
						 guint percent_complete,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
typedef gboolean (*EwsOabContactFilterCb)	(goffset offset,
						 const gchar *sha1,
						 gpointer user_data,
						 GError **error);

GType		ews_oab_decoder_get_type	(void);
EwsOabDecoder *	ews_oab_decoder_new		(const gchar *oab_filename,
						 const gchar *cache_dir,
						 GError **error);
gboolean	ews_oab_decoder_decode		(EwsOabDecoder *eod,
						 EwsOabContactFilterCb filter_cb,
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

#endif /* EWS_OAB_DECODER_H */

