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

#include <libebook/e-vcard.h>

#include "ews-oab-decoder.h"
#include "ews-oab-props.h"

G_DEFINE_TYPE (EwsOabDecoder, ews_oab_decoder, G_TYPE_OBJECT)

#define d(x) x

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EWS_TYPE_OAB_DECODER, EwsOabDecoderPrivate))

#define EOD_ERROR \
	(ews_oab_decoder_error_quark ())

typedef struct _EwsOabDecoderPrivate EwsOabDecoderPrivate;

struct _EwsOabDecoderPrivate {
	gchar *cache_dir;
	gchar *folder_id;
	GFileInputStream *fis;

	EBookBackendSqliteDB *ebsdb;

	guint32 total_records;
	GSList *hdr_props;
	GSList *oab_props;

	GHashTable *prop_index_dict;
};

/* The of properties which will be accumulated and later set in EContact */
typedef struct {
	EContactAddress *addr;
} EwsDeferredSet;

static void
ews_populate_simple_string (EContact *contact, EContactField field, gpointer value, gpointer user_data)
{
	const gchar *str = (const gchar *) value;
	e_contact_set (contact, field, str);
}

static void
ews_deffered_populate_physical_address (EwsDeferredSet *dset, guint32 prop_id, gpointer value)
{
	gchar *val = g_strdup ((gchar *)value);

	if (!dset->addr)
		dset->addr = g_new0 (EContactAddress, 1);
	
	switch (prop_id) {
		case EWS_PT_STREET_ADDRESS:
			dset->addr->street = val;
			break;
		case EWS_PT_LOCALITY:
			dset->addr->locality = val;
			break;
		case EWS_PT_STATE_OR_PROVINCE:
			dset->addr->region = val;
			break;
		case EWS_PT_POSTAL_CODE:
			dset->addr->code = val;
			break;
		case EWS_PT_COUNTRY:
			dset->addr->country = val;
			break;
		default:
			g_free (val);
			break;
	}
}

static void
ews_populate_phone_numbers (EContact *contact, EContactField field, gpointer value, gpointer user_data)
{
	GSList *values = (GSList *) value;
	gint i, len;

	/* Just the two phone numbers from the list. it would be either business or home phone number. I don't
	   see a cleaner way than this at the moment */
	len = g_slist_length (values);
	for (i = 0; i < 2 && i < len; i++) {
		const gchar *val = g_slist_nth_data (values, i);
		e_contact_set (contact, field + i, val);
	}
}

static void
ews_populate_string_list (EContact *contact, EContactField field, gpointer value, gpointer user_data)
{
	GSList *sl_values = (GSList *)	value, *sl;
	GList *l_values = NULL;


	for (sl = sl_values; sl != NULL; sl = g_slist_next (sl)) {
		const gchar *val = (gchar *) sl_values->data;
		l_values = g_list_prepend (l_values, g_strdup (val));
	}

	l_values = g_list_reverse (l_values);
	e_contact_set (contact, field, l_values);

	g_list_free (l_values);
}

static void
ews_populate_photo (EContact *contact, EContactField field, gpointer value, gpointer user_data)
{
	EwsOabDecoder *eod = EWS_OAB_DECODER (user_data);
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	const gchar *val = (gchar *) value;
	EContactPhoto *photo = g_new0 (EContactPhoto, 1);
	gchar *fullname = e_contact_get (contact, E_CONTACT_FULL_NAME);
	gchar *filename = NULL, *pic_name = NULL;

	/* Rename the binary file to fullname.png */
	if (fullname) {
		pic_name = g_strconcat (fullname, ".png", NULL);
		filename = g_build_filename (priv->cache_dir, pic_name, NULL);
		g_rename (val, filename);
	} else
		filename = g_strdup (val);

	photo->type = E_CONTACT_PHOTO_TYPE_URI;
	photo->data.uri = filename;

	e_contact_set (contact, field, (gpointer) photo);
	
	g_free (photo);
	g_free (fullname);
	g_free (pic_name);
	g_free (filename);
}

static const struct prop_field_mapping {
	guint32 prop_id;
	EContactField field;
	void (*populate_function) (EContact *contact, EContactField field, gpointer value, gpointer user_data);
	void (*defered_populate_function) (EwsDeferredSet *dset, guint32 prop_id, gpointer value);
} prop_map [] = {
	{EWS_PT_SMTP_ADDRESS, E_CONTACT_EMAIL_1, ews_populate_simple_string},
	{EWS_PT_DISPLAY_NAME, E_CONTACT_FULL_NAME, ews_populate_simple_string},
	{EWS_PT_ACCOUNT, E_CONTACT_NICKNAME, ews_populate_simple_string},
	{EWS_PT_SURNAME, E_CONTACT_FAMILY_NAME, ews_populate_simple_string},
	{EWS_PT_GIVEN_NAME, E_CONTACT_GIVEN_NAME, ews_populate_simple_string},
	{EWS_PT_BUS_TEL_NUMBER, E_CONTACT_PHONE_BUSINESS, ews_populate_simple_string},
	{EWS_PT_STREET_ADDRESS, E_CONTACT_ADDRESS_WORK, NULL, ews_deffered_populate_physical_address},
	{EWS_PT_LOCALITY, E_CONTACT_ADDRESS_WORK, NULL, ews_deffered_populate_physical_address},
	{EWS_PT_STATE_OR_PROVINCE, E_CONTACT_ADDRESS_WORK, NULL, ews_deffered_populate_physical_address},
	{EWS_PT_POSTAL_CODE, E_CONTACT_ADDRESS_WORK, NULL, ews_deffered_populate_physical_address},
	{EWS_PT_COUNTRY, E_CONTACT_ADDRESS_WORK, NULL, ews_deffered_populate_physical_address},
	{EWS_PT_TITLE, E_CONTACT_TITLE, ews_populate_simple_string},
	{EWS_PT_COMPANY_NAME, E_CONTACT_ORG, ews_populate_simple_string},
	{EWS_PT_ASSISTANT, E_CONTACT_ASSISTANT, ews_populate_simple_string},
	{EWS_PT_DEPARTMENT_NAME, E_CONTACT_ORG_UNIT, ews_populate_simple_string},
	{EWS_PT_HOME_TEL_NUMBER, E_CONTACT_PHONE_HOME, ews_populate_simple_string},
	{EWS_PT_BUS_TEL_NUMBERS, E_CONTACT_PHONE_BUSINESS, ews_populate_phone_numbers},
	{EWS_PT_HOME_TEL_NUMBERS, E_CONTACT_PHONE_HOME, ews_populate_phone_numbers},
	{EWS_PT_PRIMARY_FAX_NUMBER, E_CONTACT_PHONE_BUSINESS_FAX, ews_populate_simple_string},
	{EWS_PT_MOB_TEL_NUMBER, E_CONTACT_PHONE_MOBILE, ews_populate_simple_string},
	{EWS_PT_ASSISTANT_TEL_NUMBER, E_CONTACT_PHONE_ASSISTANT, ews_populate_simple_string},
	{EWS_PT_PAGER_NUMBER, E_CONTACT_PHONE_PAGER, ews_populate_simple_string},
	{EWS_PT_COMMENT, E_CONTACT_NOTE, ews_populate_simple_string},
	{EWS_PT_DL_MEMBERS, E_CONTACT_EMAIL, ews_populate_string_list},
	{EWS_PT_THUMBNAIL_PHOTO, E_CONTACT_PHOTO, ews_populate_photo}
};

static void
ews_oab_decoder_finalize (GObject *object)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (object);

	if (priv->cache_dir) {
		g_free (priv->cache_dir);
		priv->cache_dir = NULL;
	}

	if (priv->folder_id) {
		g_free (priv->folder_id);
		priv->folder_id = NULL;
	}

	if (priv->ebsdb) {
		g_object_unref (priv->ebsdb);
		priv->ebsdb = NULL;
	}

	if (priv->fis) {
		g_object_unref (priv->fis);
		priv->fis = NULL;
	}

	if (priv->prop_index_dict) {
		g_hash_table_destroy (priv->prop_index_dict);
		priv->prop_index_dict = NULL;
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
	gint i;

	priv->cache_dir = NULL;

	priv->prop_index_dict = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (i = 1; i <= G_N_ELEMENTS (prop_map); i++)
		g_hash_table_insert (priv->prop_index_dict, GINT_TO_POINTER (prop_map[i-1].prop_id), GINT_TO_POINTER (i));
}

EwsOabDecoder*
ews_oab_decoder_new	(const gchar *oab_filename,
			 const gchar *cache_dir,
			 const gchar *folder_id,
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
	
	priv->ebsdb = g_object_ref (ebsdb);
	priv->cache_dir = g_strdup (cache_dir);
	priv->folder_id = g_strdup (folder_id);

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

/* endian-neutral reading of little-endian data */
#define __egi32(a,n) ( ((((guchar *) a)[n+3]) << 24) | \
		       ((((guchar *) a)[n+2]) << 16) | \
		       ((((guchar *) a)[n+1]) <<  8) | \
		       ((((guchar *) a)[n+0])))
#define EndGetI64(a) ((((guint64) __egi32(a,4)) << 32) | \
		      ((guint) __egi32(a,0)))
#define EndGetI32(a) __egi32(a,0)
#define EndGetI16(a) ((((a)[1])<<8)|((a)[0]))

static guint32
ews_oab_read_uint32 (GInputStream *is, GCancellable *cancellable, GError **error)
{
	gchar *str = g_malloc0 (4);
	guint32 ret = 0;
	
	g_input_stream_read (is, str, 4, cancellable, error);
	if (!*error)
		ret = EndGetI32 (str);

	g_free (str);
	return ret;
}

static guint16
ews_oab_read_uint16 (GInputStream *is, GCancellable *cancellable, GError **error)
{
	gchar *str = g_malloc0 (2);
	guint16 ret = 0;
	
	g_input_stream_read (is, str, 2, cancellable, error);
	if (!*error)
		ret = EndGetI16 (str);

	g_free (str);
	return ret;
}

/* Read upto the stop char include the same */
static gchar *
ews_oab_read_upto (GInputStream *is, gchar stop, GCancellable *cancellable, GError **error)
{
	gchar c = -1;
	GString *str;

	str = g_string_new (NULL);
	do {
		g_input_stream_read (is, &c, 1, cancellable, error);
		if (c == stop)
			break;
		str = g_string_append_c (str, c);
	} while (!*error);

	return g_string_free (str, FALSE);
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

	o_hdr->version = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
	if (*error)
		goto exit;

	if (o_hdr->version != 0x00000020) {
		g_set_error_literal (error, EOD_ERROR, 1, "wrong version header");
		goto exit;
	}

	o_hdr->serial = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
	if (*error)
		goto exit;
	o_hdr->total_recs = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);

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
	num_props = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
	if (*error)
		return FALSE;

	if (oab_hdrs)
		props = &priv->oab_props;
	else
		props = &priv->hdr_props;

	for (i = 0; i < num_props; i++) {
		guint32 prop_id, flags;
		
		prop_id = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
		d(g_print ("%X \n", prop_id);)
		*props = g_slist_prepend (*props, GUINT_TO_POINTER (prop_id));
		
		if (*error)
			return FALSE;
		flags = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
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
	size = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
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
	guint32 ret = 0, num;
	
	g_input_stream_read (G_INPUT_STREAM (priv->fis), &first, 1, cancellable, error);
	if (*error)
		return ret;
	
	if (first & 0x80)
		num = first & 0x0F;
	else
		return (guint32) first;

	if (num == 1) {
		g_input_stream_read (G_INPUT_STREAM (priv->fis), &first, 1, cancellable, error);
		return (guint32) first;
	}

	if (num == 2)
		ret = ews_oab_read_uint16 (G_INPUT_STREAM (priv->fis), cancellable, error);
	if (num == 3) {
		gchar *tmp, *str = g_malloc0 (num + 1);
	
		g_input_stream_read (G_INPUT_STREAM (priv->fis), str, num, cancellable, error);
		/* not sure if its the right way to do, test it */
		tmp = g_strconcat ("0", str, NULL);

		sscanf (tmp, "%"G_GUINT32_FORMAT, &ret);
		ret = GUINT32_SWAP_LE_BE (ret);
		
		g_free (str);
		g_free (tmp);
	} else if (num == 4)
		ret = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);

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
	g_input_stream_read (G_INPUT_STREAM (priv->fis), binary, len, cancellable, error);
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
			
			d(g_print ("prop id %X prop type: int32 value %d \n", prop_id, val);)
		
			break;		
		}
		case EWS_PTYP_BOOLEAN:
		{
			guchar val;

			g_input_stream_read (G_INPUT_STREAM (priv->fis), &val, 1, cancellable, error);
			ret_val = GUINT_TO_POINTER ((guint) val);
			d(g_print ("prop id %X prop type: bool value %d \n", prop_id, val);)
			
			break;
		}
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
		{
			gchar *val;
		       	
			val = ews_oab_read_upto (G_INPUT_STREAM (priv->fis), '\0', cancellable, error);
			ret_val = (gpointer) val;
			
			d(g_print ("prop id %X prop type: string value %s \n", prop_id, val);)
			break;
		}
		case EWS_PTYP_BINARY:
		{
			ret_val = ews_decode_binary (eod, cancellable, error);
			d(g_print ("prop id %X prop type: binary value %s \n", prop_id, (gchar *) ret_val);)
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
			d(g_print ("prop id %X prop type: multi-num %d \n", prop_id, num);)

			for (i = 0; i < num; i++) {
				gpointer val;

				if (prop_type == EWS_PTYP_MULTIPLEINTEGER32) {
					guint32 v = 0;

					v = ews_decode_uint32 (eod, cancellable, error);
					val = GUINT_TO_POINTER (v);
					list = g_slist_prepend (list, val);
					
					d(g_print ("prop id %X prop type: multi-int32 %d \n", prop_id, v);)
					if (*error) {
						g_slist_free (list);
						return NULL;
					}
				} else {
					gchar *val;

					if (prop_type == EWS_PTYP_MULTIPLEBINARY) {
						val = ews_decode_binary (eod, cancellable, error);
						d(g_print ("prop id %X prop type: multi-string %s \n", prop_id, val);)
					} else {
						val = ews_oab_read_upto (G_INPUT_STREAM (priv->fis), '\0', cancellable, error);
						d(g_print ("prop id %X prop type: multi-string %s \n", prop_id, val);)
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
			if (delete_files)
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
 * @dset: used to collect multiple properties that needs to be combined and stored as one EContactField
 * @cancellable: 
 * @error: 
 * 
 * Decodes the address-book records starting from presence bit array.
 *
 *
 * Returns: 
 **/
static gboolean
ews_decode_addressbook_record (EwsOabDecoder *eod, EContact *contact, EwsDeferredSet *dset, GSList *props, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint bit_array_size, i, len;
	gchar *bit_str;
	gboolean ret = TRUE;

	len = g_slist_length (props);
	bit_array_size = (guint) ceil (len/8.0);
	bit_str = g_malloc0 (bit_array_size);
	g_input_stream_read (G_INPUT_STREAM (priv->fis), bit_str, bit_array_size, cancellable, error);
	if (*error) {
		ret = FALSE;	
		goto exit;
	}

	for (i = 0; i < len; i++) {
		gpointer val, index;
		guint32 prop_id;

		if (!ews_is_bit_set (bit_str, i))
			continue;
		
		val = g_slist_nth_data (props, i);
		prop_id = GPOINTER_TO_UINT (val);

		val = ews_decode_oab_prop (eod, prop_id, cancellable, error);

		/* Check the contact map and store the data in EContact */
		index = g_hash_table_lookup (priv->prop_index_dict, GINT_TO_POINTER (prop_id));
		if (contact && index) {
			gint i = GPOINTER_TO_INT (index);

			if (prop_map [i-1].populate_function)
				prop_map [i-1].populate_function (contact, prop_map[i-1].field, val, (gpointer) eod);
			else
				prop_map [i-1].defered_populate_function (dset, prop_id, val);
		}

		/* delete the binary file if we do not have the property in the index or if there
		   was an error */
		ews_destroy_oab_prop (prop_id, val, (*error || !index) ? TRUE : FALSE);
		if (*error)
			goto exit;
	}

exit:
	if (bit_str)
		g_free (bit_str);
	
	return ret;
}

/* Decodes the hdr and address-book records and stores the address-book records inside the db */
static gboolean
ews_decode_and_store_oab_records (EwsOabDecoder *eod, GCancellable *cancellable, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;
	guint32 size, i;

	size = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);
	ews_decode_addressbook_record (eod, NULL, NULL, priv->hdr_props, cancellable, error);
	if (*error) {
		ret = FALSE;
		goto exit;
	}

	for (i = 0; i < priv->total_records; i++) {
		EContact *contact;
		EwsDeferredSet *dset;
		gchar *uid = NULL, *bdata = NULL;
		goffset offset;

		contact = e_contact_new ();
		dset = g_new0 (EwsDeferredSet, 1);
		size = ews_oab_read_uint32 (G_INPUT_STREAM (priv->fis), cancellable, error);

		/* fetch the offset */
		offset = g_seekable_tell ((GSeekable *) priv->fis);

		ews_decode_addressbook_record (eod, contact, dset, priv->oab_props, cancellable, error);
		if (*error)
			goto error;
		
		if (dset->addr)
			e_contact_set (contact, E_CONTACT_ADDRESS_WORK, dset->addr);
	
		/* set the smtp address as contact's uid */
		uid = (gchar *) e_contact_get (contact, E_CONTACT_EMAIL_1);
		e_contact_set (contact, E_CONTACT_UID, uid);

		/* convert the offset to string */
		bdata = g_strdup_printf ("%"G_GOFFSET_FORMAT, offset);

		d(g_print ("%s \n", e_vcard_to_string ((EVCard *) contact, EVC_FORMAT_VCARD_30));)
			
		/* Store the contact inside the db with the offset */
		e_book_backend_sqlitedb_add_contact (priv->ebsdb, priv->folder_id, contact, FALSE, error);
		if (*error)
			goto error;

		e_book_backend_sqlitedb_set_contact_bdata (priv->ebsdb, priv->folder_id, uid, bdata, error);

error:		
		g_object_unref (contact);
		e_contact_address_free (dset->addr);
		g_free (dset);
		g_free (uid);
		g_free (bdata);

		if (*error) {
			ret = FALSE;	
			goto exit;
		}
	}

exit: 
	return ret;	
}

static gboolean
ews_store_oab_props (EwsOabDecoder *eod, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;
	GString *str = g_string_new (NULL);
	GSList *l;

	for (l = priv->oab_props; l != NULL; l = g_slist_next (l)) {
		guint32 prop_id = GPOINTER_TO_UINT (l->data);
		g_string_append_printf (str, "%"G_GUINT32_FORMAT, prop_id);
		g_string_append_c (str, ';');
	}
	g_string_erase (str, str->len - 1, 1);

	d(g_print ("Oab prop string: %s \n", str->str);)
	e_book_backend_sqlitedb_set_key_value (priv->ebsdb, priv->folder_id, "oab-props", str->str, error);
	if (*error)
		ret = TRUE;

	return ret;
}

static GSList *
ews_get_oab_props (EwsOabDecoder *eod, GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gchar *prop_str, **vals;
	guint32 len, i;
	GSList *props = NULL;

	prop_str = e_book_backend_sqlitedb_get_key_value (priv->ebsdb, priv->folder_id, "oab-props", error);
	if (*error)
		return NULL;

	vals = g_strsplit (prop_str, ";", -1);
	len = g_strv_length (vals);
	for (i = 0; i < len; i++) {
		guint32 prop_id;

		sscanf (vals[i],"%"G_GUINT32_FORMAT,&prop_id);
		props = g_slist_prepend (props, GUINT_TO_POINTER (prop_id));
		d(printf ("%X\n", prop_id);)
	}

	props = g_slist_reverse (props);

	g_strfreev (vals);
	g_free (prop_str);

	return props;
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
	if (!ret)
		goto exit;

	ret = ews_store_oab_props (eod, &err);
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
	EwsDeferredSet *dset;
	EContact *contact = NULL;
	GSList *oab_props;

	if (!g_seekable_seek ((GSeekable *) priv->fis, offset, G_SEEK_SET, cancellable, error))
		return NULL;

	oab_props = ews_get_oab_props (eod, error);

	contact = e_contact_new ();
	dset = g_new0 (EwsDeferredSet, 1);
	ews_decode_addressbook_record (eod, contact, dset, oab_props, cancellable, error);
	if (*error) {
		g_object_unref (contact);
		contact = NULL;
	}
	
	e_contact_address_free (dset->addr);
	g_free (dset);
	
	if (oab_props) {
		g_slist_free (oab_props);
		oab_props = NULL;
	}

	return contact;
}

/*
gint 
main (gint argc, gchar *argv [])
{
	EBookBackendSqliteDB *ebsdb;
	EwsOabDecoder *eod;
	GError *err = NULL;
	EContact *contact;
	gchar *val;
	goffset offset;

	g_type_init ();
	g_thread_init (NULL);

	if (argc != 3) {
		g_print ("Pass the oab filename  and cache dir as argument \n");
		return -1;
	}

	ebsdb = e_book_backend_sqlitedb_new (argv [2], "dum", "de", "dum", FALSE, NULL);
	eod = ews_oab_decoder_new (argv [1], argv [2], "de", ebsdb, &err);
	if (!ews_oab_decoder_decode (eod, NULL, &err)) {
		g_print ("Unable to decode %s \n", err->message);
	}

	val = e_book_backend_sqlitedb_get_contact_bdata (ebsdb, "de", "chen@g3.com", NULL);
	sscanf (val,"%"G_GOFFSET_FORMAT,&offset);
	contact = ews_oab_decoder_get_contact_from_offset (eod, offset, NULL, &err);
	if (!err)
		g_print ("%s \n", e_vcard_to_string ((EVCard *) contact, EVC_FORMAT_VCARD_30));

	if (err)
		g_clear_error (&err);

	g_free (val);
	g_object_unref (contact);
	g_object_unref (eod);
	g_object_unref (ebsdb);

	return 0;
} */
