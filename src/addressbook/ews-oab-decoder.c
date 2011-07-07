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

#include <math.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include "ews-oab-decoder.h"
#include "ews-oab-props.h"

G_DEFINE_TYPE (EwsOabDecoder, ews_oab_decoder, G_TYPE_OBJECT)

#define d(x)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EWS_TYPE_OAB_DECODER, EwsOabDecoderPrivate))

#define EOD_ERROR \
	(ews_oab_decoder_error_quark ())

typedef struct _EwsOabDecoderPrivate EwsOabDecoderPrivate;

struct _EwsOabDecoderPrivate {
	gchar *cache_dir;
	GFileInputStream *fis;
	GDataInputStream *dis;

	EBookBackendSqliteDB *ebsdb;

	guint32 total_records;
	GSList *hdr_props;
	GSList *oab_props;
};

static void
ews_oab_decoder_finalize (GObject *object)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (object);

	if (priv->cache_dir) {
		g_free (priv->cache_dir);
		priv->cache_dir = NULL;
	}
	
	if (priv->ebsdb) {
		g_object_unref (priv->ebsdb);
		priv->ebsdb = NULL;
	}

	if (priv->dis) {
		g_object_unref (priv->dis);
		priv->dis = NULL;
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

	priv->cache_dir = NULL;
}

EwsOabDecoder*
ews_oab_decoder_new	(const gchar *oab_filename,
			 const gchar *cache_dir,
			 EBookBackendSqliteDB *ebsdb,
		   	 GError **error)
{
	EwsOabDecoder *eod;
	EwsOabDecoderPrivate *priv;
	GError *err = NULL;
	GFile *gf = NULL;

	eod = g_object_new (EWS_TYPE_OAB_DECODER, NULL);
	priv = GET_PRIVATE (eod);

	gf = g_file_new_for_path (oab_filename);
	priv->fis = g_file_read (gf, NULL, &err);
	if (err)
		goto exit;
	
	priv->dis = g_data_input_stream_new ((GInputStream *) priv->fis);
	priv->ebsdb = g_object_ref (ebsdb);
	priv->cache_dir = g_strdup (cache_dir);

	g_data_input_stream_set_byte_order (priv->dis, G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);
exit:
	if (gf)
		g_object_unref (gf);
	if (err) {
		g_propagate_error (error, err);
		g_object_unref (eod);
		return NULL;
	}

	return eod;
}

static GQuark
ews_oab_decoder_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "ews-oab-decoder";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

typedef struct {
	guint32 version;
	guint32 serial;
	guint32 total_recs;
} EwsOabHdr;

static EwsOabHdr *
ews_read_oab_header (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabHdr *o_hdr;
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);

	o_hdr = g_new0 (EwsOabHdr, 1);

	o_hdr->version = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
	if (*error)
		goto exit;

	if (o_hdr->version != 0x00000020) {
		g_set_error_literal (error, EOD_ERROR, 1, "wrong version header");
		goto exit;
	}

	o_hdr->serial = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
	if (*error)
		goto exit;
	o_hdr->total_recs = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);

exit:
	if (*error) {
		g_free (o_hdr);
		return NULL;
	}

	return o_hdr;
}


static gboolean
ews_decode_hdr_props (EwsOabDecoder *eod, gboolean oab_hdrs, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint32 num_props, i;
	GSList **props;
	
	/* number of properties */
	num_props = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
	if (*error)
		return FALSE;

	if (oab_hdrs)
		props = &priv->oab_props;
	else
		props = &priv->hdr_props;

	for (i = 0; i < num_props; i++) {
		guint32 prop_id, flags;
		
		prop_id = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
		d(g_print ("%x \n", prop_id);)
		*props = g_slist_prepend (*props, GUINT_TO_POINTER (prop_id));
		
		if (*error)
			return FALSE;
		flags = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
		if (*error)
			return FALSE;

		/* skip store anr_index and primary key prop list as we will not be using it for online search,
		   store if required later */
	}
	
	*props = g_slist_reverse (*props);

	return TRUE;	
}

static gboolean
ews_decode_metadata (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;
	guint32 size;

	/* Size */
	size = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
	if (*error)
		return FALSE;

	ret = ews_decode_hdr_props (eod, FALSE, cancellable, error);
	if (!ret)
		return FALSE;

	ret = ews_decode_hdr_props (eod, TRUE, cancellable, error);

	return ret;
}

static gboolean
ews_is_bit_set (const gchar *str, guint32 pos)
{
	guint32 index, bit_pos;

	index = pos/8;
	bit_pos = pos & (8-1);

	if ((str[index] << bit_pos) & 0x80)
		return TRUE;
	else
		return FALSE;
}

static guint32
ews_decode_uint32 (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint8 first;
	guint32 ret = -1, num;
	
	first = g_data_input_stream_read_byte (priv->dis, cancellable, error);
	if (*error)
		goto exit;
	
	if (first & 0x80)
		num = first & 0x0F;
	else
		return (guint32) first;

	if (num == 1) {
		ret = g_data_input_stream_read_byte (priv->dis, cancellable, error);
		goto exit;
	}

	if (num == 2)
		ret = (guint16) g_data_input_stream_read_uint16 (priv->dis, cancellable, error);
	if (num == 3) {
		gchar *tmp, *str = g_malloc0 (num + 1);
	
		g_input_stream_read (G_INPUT_STREAM (priv->dis), str, num, cancellable, error);
		/* not sure if its the right way to do, test it */
		tmp = g_strconcat ("0000", str, NULL);

		ret = atoi (tmp);
		ret = GUINT32_SWAP_LE_BE (ret);
		
		g_free (str);
		g_free (tmp);
	} else if (num == 4)
		ret = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);

exit:
	return ret;	
}

static gchar *
ews_decode_binary (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint32 len;
	gchar *binary, *filename = NULL;
	gint fd;

	len = ews_decode_uint32 (eod, cancellable, error);
	if (*error)
		return NULL;
	
	binary = g_malloc (len);
	g_input_stream_read (G_INPUT_STREAM (priv->dis), binary, len, cancellable, error);
	if (*error)
		goto exit;

	filename = g_build_filename (priv->cache_dir, "XXXXXX", NULL);
	fd = g_mkstemp (filename);
	g_file_set_contents (filename, binary, len, error);

exit:
	if (binary)
		g_free (binary);
	close (fd);

	return filename;
}

static gpointer 
ews_decode_oab_prop (EwsOabDecoder *eod, guint32 prop_id, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint32 prop_type;
	gpointer ret_val = NULL;

	prop_type = prop_id & 0x0000FFFF;

	switch (prop_type) {
		case EWS_PTYP_INTEGER32:
		{
			guint32 val;
			
			val = ews_decode_uint32 (eod, cancellable, error);
			ret_val = GUINT_TO_POINTER (val);
			
			d(g_print ("prop id %x prop type: int32 value %d \n", prop_id, val);)
		
			break;		
		}
		case EWS_PTYP_BOOLEAN:
		{
			guchar val;

			val = g_data_input_stream_read_byte (priv->dis, cancellable, error);
			ret_val = GUINT_TO_POINTER ((guint) val);
			d(g_print ("prop id %x prop type: bool value %d \n", prop_id, val);)
			
			break;
		}
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
		{
			gsize len;
			gchar *val;
		       	
			val= g_data_input_stream_read_upto (priv->dis, "\0", 1, &len, cancellable, error);
			g_data_input_stream_read_byte (priv->dis, cancellable, error);
			ret_val = (gpointer) val;
			
			d(g_print ("prop id %x prop type: string value %s \n", prop_id, val);)
			break;
		}
		case EWS_PTYP_BINARY:
		{
			ret_val = ews_decode_binary (eod, cancellable, error);
			d(g_print ("prop id %x prop type: binary value %s \n", prop_id, (gchar *) ret_val);)
			break;
		}
		case EWS_PTYP_MULTIPLEINTEGER32:
		case EWS_PTYP_MULTIPLESTRING8:
		case EWS_PTYP_MULTIPLESTRING:
		case EWS_PTYP_MULTIPLEBINARY:
		{
			guint32 num, i;
			GSList *list = NULL;

			num = ews_decode_uint32 (eod, cancellable, error);
			if (*error)
				break;
			d(g_print ("prop id %x prop type: multi-num %d \n", prop_id, num);)

			for (i = 0; i < num; i++) {
				gpointer val;

				if (prop_type == EWS_PTYP_MULTIPLEINTEGER32) {
					guint32 v = 0;

					v = ews_decode_uint32 (eod, cancellable, error);
					val = GUINT_TO_POINTER (v);
					list = g_slist_prepend (list, val);
					
					d(g_print ("prop id %x prop type: multi-int32 %d \n", prop_id, v);)
					if (*error) {
						g_slist_free (list);
						return NULL;
					}
				} else {
					gchar *val;

					if (prop_type == EWS_PTYP_MULTIPLEBINARY) {
						val = ews_decode_binary (eod, cancellable, error);
						
						d(g_print ("prop id %x prop type: multi-string %s \n", prop_id, val);)
					} else {
						gsize len;

						val= g_data_input_stream_read_upto (priv->dis, "\0", 1, &len, cancellable, error);
						g_data_input_stream_read_byte (priv->dis, cancellable, error);
					
						d(g_print ("prop id %x prop type: multi-string %s \n", prop_id, val);)
					}

					if (*error) {
						g_slist_foreach (list, (GFunc) g_free, NULL);
						g_slist_free (list);
						return NULL;
					}
				}

			}

			break;	
		}
		default:
			g_assert_not_reached ();
			break;
	}

	return ret_val;
}


static void
ews_destroy_oab_prop (guint32 prop_id, gpointer val, gboolean delete_files)
{
	guint32 prop_type;

	prop_type = prop_id & 0x0000FFFF;
	
	switch (prop_type) {
		case EWS_PTYP_INTEGER32:
		case EWS_PTYP_BOOLEAN:
			break;
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
		case EWS_PTYP_BINARY:
			g_free ((gchar *) val);
			break;
		case EWS_PTYP_MULTIPLEBINARY:
			g_slist_foreach ((GSList *)val, (GFunc) g_unlink, NULL);
		case EWS_PTYP_MULTIPLESTRING8:
		case EWS_PTYP_MULTIPLESTRING:
			g_slist_foreach ((GSList *)val, (GFunc) g_free, NULL);
		case EWS_PTYP_MULTIPLEINTEGER32:
			g_slist_free ((GSList *) val);
			break;
		default:
			g_assert_not_reached ();
			break;
	}
}

/**
 * ews_decode_addressbook_record 
 * @eod: 
 * @contact: Pass a valid EContact for decoding the address-book record. NULL in case of header record.
 * @props: 
 * @cancellable: 
 * @error: 
 * 
 * Decodes the header and address-book records.
 * Returns: 
 **/
static gboolean
ews_decode_addressbook_record (EwsOabDecoder *eod, EContact *contact, GSList *props, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint bit_array_size, i, len;
	gchar *bit_str;
	gboolean ret = TRUE;
	goffset offset;

	/* fetch the offset */
	offset = g_seekable_tell ((GSeekable *) priv->fis);

	len = g_slist_length (props);
	bit_array_size = (guint) ceil (len/8.0);
	bit_str = g_malloc0 (bit_array_size);
	g_input_stream_read (G_INPUT_STREAM (priv->dis), bit_str, bit_array_size, cancellable, error);
	if (*error) {
		ret = FALSE;	
		goto exit;
	}

	for (i = 0; i < len; i++) {
		gpointer val;
		guint32 prop_id;

		if (!ews_is_bit_set (bit_str, i))
			continue;
		
		val = g_slist_nth_data (props, i);
		prop_id = GPOINTER_TO_UINT (val);

		val = ews_decode_oab_prop (eod, prop_id, cancellable, error);

		/* Check the contact map and store the data in EContact */
		if (contact) {
			
		}

		/* Store the contact summary into the db along with the offset */

		ews_destroy_oab_prop (prop_id, val, *error ? TRUE : FALSE);
		if (*error)
			goto exit;
	}

exit:
	if (bit_str)
		g_free (bit_str);
	
	return ret;
}

/* Decodes the hrd and address-book records and stores the address-book records inside the db */
static gboolean
ews_decode_and_store_oab_records (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;
	guint32 size, i;

	size = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
	ews_decode_addressbook_record (eod, NULL, priv->hdr_props, cancellable, error);
	if (*error) {
		ret = FALSE;
		goto exit;
	}

	for (i = 0; i < priv->total_records; i++) {
		EContact *contact;

		contact = e_contact_new ();
		size = g_data_input_stream_read_uint32 (priv->dis, cancellable, error);
		ews_decode_addressbook_record (eod, contact, priv->oab_props, cancellable, error);
		if (*error) {
			g_object_unref (contact);
			ret = FALSE;
			goto exit;
		}
		
		/* Store the contact summary into db with its offset */

		g_object_unref (contact);
	}

exit: 
	return ret;	
}

/**
 * ews_oab_decoder_decode 
 * @eod: 
 * @ebsdb: 
 * @cancellable: 
 * @error: 
 * 
 * Decodes the oab full details verions 4 file and stores
 * the summary in the sqlite db.
 * Returns: TRUE if successfully decoded and indexed in db 
 **/
gboolean	
ews_oab_decoder_decode	(EwsOabDecoder *eod,
			 GCancellable *cancellable,
			 GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	GError *err = NULL;
	EwsOabHdr *o_hdr;
	gboolean ret = TRUE;

	o_hdr = ews_read_oab_header (eod, cancellable, &err);
	if (!o_hdr) {
		ret = FALSE;
		goto exit;
	}
	
	priv->total_records = o_hdr->total_recs;

	ret = ews_decode_metadata (eod, cancellable, &err);
	if (!ret)
		goto exit;
	
	ret = ews_decode_and_store_oab_records (eod, cancellable, &err);

exit:
	if (o_hdr)
		g_free (o_hdr);

	if (priv->oab_props) {
		g_slist_free (priv->oab_props);
		priv->oab_props = NULL;
	}

	if (priv->hdr_props) {
		g_slist_free (priv->hdr_props);
		priv->hdr_props = NULL;
	}
	
	if (err)
		g_propagate_error (error, err);

	return ret;	
}

EContact *	
ews_oab_decoder_get_contact_from_offset	(EwsOabDecoder *eod,
					 goffset offset,
					 GCancellable *cancellable,
					 GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	EContact *contact = NULL;

	if (!g_seekable_seek ((GSeekable *) priv->dis, offset, G_SEEK_CUR, cancellable, error))
		return NULL;

	/* priv->oab_props = fetch from sqlite db */ 

	contact = e_contact_new ();
	ews_decode_addressbook_record (eod, contact, priv->oab_props, cancellable, error);
	if (*error) {
		g_object_unref (contact);
		contact = NULL;
	}
	
	if (priv->oab_props) {
		g_slist_free (priv->oab_props);
		priv->oab_props = NULL;
	}

	return contact;
}
