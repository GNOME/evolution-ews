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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

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
ews_populate_simple_string (EContact *contact,
                            EContactField field,
                            gpointer value,
                            gpointer user_data)
{
	const gchar *str = (const gchar *) value;
	e_contact_set (contact, field, str);
}

static void
ews_deffered_populate_physical_address (EwsDeferredSet *dset,
                                        guint32 prop_id,
                                        gpointer value)
{
	gchar *val = g_strdup ((gchar *) value);

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
ews_populate_phone_numbers (EContact *contact,
                            EContactField field,
                            gpointer value,
                            gpointer user_data)
{
	GSList *values = (GSList *) value;
	gint i, len;

	/* Just the two phone numbers from the list. it would be either business or home phone number. I don't
	 * see a cleaner way than this at the moment */
	len = g_slist_length (values);
	for (i = 0; i < 2 && i < len; i++) {
		const gchar *val = g_slist_nth_data (values, i);
		e_contact_set (contact, field + i, val);
	}
}

static void
ews_populate_string_list (EContact *contact,
                          EContactField field,
                          gpointer value,
                          gpointer user_data)
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
ews_populate_photo (EContact *contact,
                    EContactField field,
                    gpointer value,
                    gpointer user_data)
{
	EwsOabDecoder *eod = EWS_OAB_DECODER (user_data);
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	const gchar *val = (gchar *) value, *at;
	EContactPhoto *photo = g_new0 (EContactPhoto, 1);
	gchar *email = e_contact_get (contact, E_CONTACT_EMAIL_1);
	gchar *filename = NULL, *pic_name = NULL, *name;
	gboolean success = TRUE;

	/* Rename the binary file to name.jpg */
	at = strchr (email, '@');
	name = g_strndup (email, at - email);

	pic_name = g_strconcat (name, ".jpg", NULL);
	filename = g_build_filename (priv->cache_dir, pic_name, NULL);

	if (g_file_test (filename, G_FILE_TEST_EXISTS))
		g_unlink (val);
	else {
		if (g_rename (val, filename))
			success = FALSE;
	}

	if (success) {
		photo->type = E_CONTACT_PHOTO_TYPE_URI;
		photo->data.uri = filename;

		e_contact_set (contact, field, (gpointer) photo);
	}

	g_free (photo);
	g_free (email);
	g_free (name);
	g_free (pic_name);
	g_free (filename);
}

/* Make sure that all the temp files are renamed while the fields are getting set in EContact */
static const struct prop_field_mapping {
	guint32 prop_id;
	EContactField field;
	void (*populate_function) (EContact *contact, EContactField field, gpointer value, gpointer user_data);
	void (*defered_populate_function) (EwsDeferredSet *dset, guint32 prop_id, gpointer value);
} prop_map[] = {
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

	if (priv->fis) {
		g_object_unref (priv->fis);
		priv->fis = NULL;
	}

	if (priv->prop_index_dict) {
		g_hash_table_destroy (priv->prop_index_dict);
		priv->prop_index_dict = NULL;
	}

	if (priv->oab_props) {
		g_slist_free (priv->oab_props);
		priv->oab_props = NULL;
	}

	if (priv->hdr_props) {
		g_slist_free (priv->hdr_props);
		priv->hdr_props = NULL;
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
		g_hash_table_insert (priv->prop_index_dict, GINT_TO_POINTER (prop_map[i - 1].prop_id), GINT_TO_POINTER (i));
}

EwsOabDecoder *
ews_oab_decoder_new (const gchar *oab_filename,
                     const gchar *cache_dir,
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

	priv->cache_dir = g_strdup (cache_dir);

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
		       ((((guchar *) a)[n + 2]) << 16) | \
		       ((((guchar *) a)[n + 1]) <<  8) | \
		       ((((guchar *) a)[n + 0])))
#define EndGetI64(a) ((((guint64) __egi32(a,4)) << 32) | \
		      ((guint) __egi32 (a,0)))
#define EndGetI32(a) __egi32(a,0)
#define EndGetI16(a) ((((a)[1])<<8)|((a)[0]))

static guint32
ews_oab_read_uint32 (GInputStream *is,
                     GCancellable *cancellable,
                     GError **error)
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
ews_oab_read_uint16 (GInputStream *is,
                     GCancellable *cancellable,
                     GError **error)
{
	guchar str[2];
	guint16 ret = 0;

	g_input_stream_read (is, str, 2, cancellable, error);
	if (!*error)
		ret = EndGetI16 (str);

	return ret;
}

static gint
get_pos (const gchar *str,
         gsize len,
         gchar stop)
{
	gsize i = 0;

	while (i < len && str[i] != stop)
		i++;
	return i;
}

/* Read upto the stop char include the same */
static gchar *
ews_oab_read_upto (GInputStream *is,
                   gchar stop,
                   GCancellable *cancellable,
                   GError **error)
{
	gsize size = 50;
	GString *str;

	str = g_string_sized_new (size);
	do {
		gsize len;
		gsize bytes_read;
		gchar *c = g_malloc0 (size);

		g_input_stream_read_all (is, c, size, &bytes_read, cancellable, error);
		if (*error)
			break;

		if (bytes_read != size)
			size = bytes_read;

		len = get_pos (c, size, stop);
		if (len)
			str = g_string_append_len (str, c, len);

		if (len == 0 || len < size) {
			goffset seek = (goffset) len + 1 - (goffset) size;

			/* seek back */
			g_seekable_seek ((GSeekable *) is, seek, G_SEEK_CUR, cancellable, error);
			break;
		}

		size *= 2;
		g_free (c);
	} while (!*error);

	return g_string_free (str, FALSE);
}

typedef struct {
	guint32 version;
	guint32 serial;
	guint32 total_recs;
} EwsOabHdr;

static EwsOabHdr *
ews_read_oab_header (EwsOabDecoder *eod,
                     GCancellable *cancellable,
                     GError **error)
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
ews_decode_hdr_props (EwsOabDecoder *eod,
                      gboolean oab_hdrs,
                      GCancellable *cancellable,
                      GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint32 num_props, i;
	GSList **props;

	/* number of properties */
	num_props = ews_oab_read_uint32 (
		G_INPUT_STREAM (priv->fis),
		cancellable, error);

	if (*error)
		return FALSE;

	if (oab_hdrs)
		props = &priv->oab_props;
	else
		props = &priv->hdr_props;

	if (*props) {
		g_slist_free (*props);
		*props = NULL;
	}

	for (i = 0; i < num_props; i++) {
		guint32 prop_id;

		prop_id = ews_oab_read_uint32 (
			G_INPUT_STREAM (priv->fis),
			cancellable, error);

		*props = g_slist_prepend (*props, GUINT_TO_POINTER (prop_id));

		if (*error)
			return FALSE;

		/* eat the flags */
		ews_oab_read_uint32 (
			G_INPUT_STREAM (priv->fis),
			cancellable, error);

		if (*error)
			return FALSE;

		/* skip store anr_index and primary key prop list as we will not be using it for online search,
		 * store if required later */
	}

	*props = g_slist_reverse (*props);

	return TRUE;
}

static gboolean
ews_decode_metadata (EwsOabDecoder *eod,
                     GCancellable *cancellable,
                     GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;

	/* eat the size */
	ews_oab_read_uint32 (
		G_INPUT_STREAM (priv->fis),
		cancellable, error);

	if (*error)
		return FALSE;

	ret = ews_decode_hdr_props (eod, FALSE, cancellable, error);
	if (!ret)
		return FALSE;

	ret = ews_decode_hdr_props (eod, TRUE, cancellable, error);

	return ret;
}

static gboolean
ews_is_bit_set (const gchar *str,
                guint32 pos)
{
	guint32 index, bit_pos;

	index = pos / 8;
	bit_pos = pos & (8 - 1);

	if ((str[index] << bit_pos) & 0x80)
		return TRUE;
	else
		return FALSE;
}

static guint32
ews_decode_uint32 (EwsOabDecoder *eod,
                   GCancellable *cancellable,
                   GError **error)
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
ews_decode_binary (EwsOabDecoder *eod,
                   GCancellable *cancellable,
                   GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint32 len;
	gchar *binary, *filename = NULL;
	gint fd = 0;

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
ews_decode_oab_prop (EwsOabDecoder *eod,
                     guint32 prop_id,
                     GCancellable *cancellable,
                     GError **error)
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

			d (g_print ("prop id %X prop type: int32 value %d \n", prop_id, val);)

			break;
		}
		case EWS_PTYP_BOOLEAN:
		{
			guchar val;

			g_input_stream_read (G_INPUT_STREAM (priv->fis), &val, 1, cancellable, error);
			ret_val = GUINT_TO_POINTER ((guint) val);
			d (g_print ("prop id %X prop type: bool value %d \n", prop_id, val);)

			break;
		}
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
		{
			gchar *val;

			val = ews_oab_read_upto (G_INPUT_STREAM (priv->fis), '\0', cancellable, error);
			ret_val = (gpointer) val;

			d (g_print ("prop id %X prop type: string value %s \n", prop_id, val);)
			break;
		}
		case EWS_PTYP_BINARY:
		{
			ret_val = ews_decode_binary (eod, cancellable, error);
			d (g_print ("prop id %X prop type: binary value %s \n", prop_id, (gchar *) ret_val);)
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
			d (g_print ("prop id %X prop type: multi-num %d \n", prop_id, num);)

			for (i = 0; i < num; i++) {
				gpointer val;

				if (prop_type == EWS_PTYP_MULTIPLEINTEGER32) {
					guint32 v = 0;

					v = ews_decode_uint32 (eod, cancellable, error);
					val = GUINT_TO_POINTER (v);
					list = g_slist_prepend (list, val);

					d (g_print ("prop id %X prop type: multi-int32 %d \n", prop_id, v);)
					if (*error) {
						g_slist_free (list);
						return NULL;
					}
				} else {
					gchar *val;

					if (prop_type == EWS_PTYP_MULTIPLEBINARY) {
						val = ews_decode_binary (eod, cancellable, error);
					} else {
						val = ews_oab_read_upto (G_INPUT_STREAM (priv->fis), '\0', cancellable, error);
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
ews_destroy_oab_prop (guint32 prop_id,
                      gpointer val,
                      gboolean delete_files)
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
				g_slist_foreach ((GSList *) val, (GFunc) g_unlink, NULL);
		case EWS_PTYP_MULTIPLESTRING8:
		case EWS_PTYP_MULTIPLESTRING:
			g_slist_foreach ((GSList *) val, (GFunc) g_free, NULL);
		case EWS_PTYP_MULTIPLEINTEGER32:
			g_slist_free ((GSList *) val);
			break;
		default:
			g_assert_not_reached ();
			break;
	}
}

static const gchar *
ews_decode_addressbook_get_display_type (guint32 value)
{
	const gchar *display_type = NULL;

	switch (value) {
		case EWS_DT_MAILUSER:
		/*
		 * DT_MAILUSER means that the display type could be, besides itself, one of
		 * these extended types: DT_ROOM, DT_EQUIPMENT, DT_SEC_DISTLIST. Considering
		 * this, we can just ignore this case here and treat it properly in the
		 * ews_decode_addressbook_get_display_type_extended() function.
		 */
			break;
		case EWS_DT_DISTLIST:
			display_type = "DT_DISTLIST";
			break;
		case EWS_DT_FORUM:
			display_type = "DT_FORUM";
			break;
		case EWS_DT_AGENT:
			display_type = "DT_AGENT";
			break;
		case EWS_DT_ORGANIZATION:
			display_type = "DT_ORGANIZATION";
			break;
		case EWS_DT_PRIVATE_DISTLIST:
			display_type = "DT_PRIVATE_DISTLIST";
			break;
		case EWS_DT_REMOTE_MAILUSER:
			display_type = "DT_PRIVATE_MAILUSER";
			break;
	}

	return display_type;
}

static const gchar *
ews_decode_addressbook_get_display_type_extended (guint32 value)
{
	const gchar *display_type = "DT_MAILUSER";

	switch (value) {
		case EWS_DT_ROOM:
			display_type = "DT_ROOM";
			break;
		case EWS_DT_EQUIPMENT:
			display_type = "DT_EQUIPMENT";
			break;
		case EWS_DT_SEC_DISTLIST:
			display_type = "DT_SEC_DISTLIST";
			break;
	}

	return display_type;
}

static void
ews_decode_addressbook_write_display_type (EContact **contact,
					   guint32 value,
					   gboolean extended)
{
	EVCardAttribute *attr;
	const gchar *display_type;

	if (extended)
		display_type = ews_decode_addressbook_get_display_type_extended (value);
	else
		display_type = ews_decode_addressbook_get_display_type (value);

	if (display_type != NULL) {
		attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
		e_vcard_add_attribute_with_value (E_VCARD (*contact), attr, display_type);
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
ews_decode_addressbook_record (EwsOabDecoder *eod,
                               EContact *contact,
                               EwsDeferredSet *dset,
                               GSList *props,
                               GCancellable *cancellable,
                               GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	guint bit_array_size, i, len;
	gchar *bit_str;
	gboolean ret = TRUE;

	len = g_slist_length (props);
	bit_array_size = (guint) ceil (len / 8.0);
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

		if (prop_id == EWS_PT_DISPLAY_TYPE)
			ews_decode_addressbook_write_display_type (&contact, GPOINTER_TO_UINT (val), FALSE);

		if (prop_id == EWS_PT_DISPLAY_TYPE_EX)
			ews_decode_addressbook_write_display_type (&contact, GPOINTER_TO_UINT (val), TRUE);

		/* Check the contact map and store the data in EContact */
		index = g_hash_table_lookup (priv->prop_index_dict, GINT_TO_POINTER (prop_id));
		if (contact && index) {
			gint i = GPOINTER_TO_INT (index);

			if (prop_map[i - 1].populate_function)
				prop_map[i - 1].populate_function (contact, prop_map[i - 1].field, val, (gpointer) eod);
			else
				prop_map[i - 1].defered_populate_function (dset, prop_id, val);
		}

		/* delete the binary file if we do not have the property in the index or if there
		 * was an error */
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
ews_decode_and_store_oab_records (EwsOabDecoder *eod,
                                  EwsOabContactAddedCb cb,
                                  gpointer user_data,
                                  GCancellable *cancellable,
                                  GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gboolean ret = TRUE;
	guint32 i;

	/* eat the size */
	ews_oab_read_uint32 (
		G_INPUT_STREAM (priv->fis),
		cancellable, error);

	ews_decode_addressbook_record (
		eod, NULL, NULL, priv->hdr_props, cancellable, error);

	if (*error) {
		ret = FALSE;
		goto exit;
	}

	for (i = 0; i < priv->total_records; i++) {
		EContact *contact;
		EwsDeferredSet *dset;
		gchar *uid = NULL;
		goffset offset;

		contact = e_contact_new ();
		dset = g_new0 (EwsDeferredSet, 1);

		/* eat the size */
		ews_oab_read_uint32 (
			G_INPUT_STREAM (priv->fis),
			cancellable, error);

		/* fetch the offset */
		offset = g_seekable_tell ((GSeekable *) priv->fis);

		ews_decode_addressbook_record (
			eod, contact, dset,
			priv->oab_props, cancellable, error);

		if (*error)
			goto error;

		if (dset->addr)
			e_contact_set (
				contact,
				E_CONTACT_ADDRESS_WORK,
				dset->addr);

		/* set the smtp address as contact's uid */
		uid = (gchar *) e_contact_get (contact, E_CONTACT_EMAIL_1);
		if (uid && *uid) {
			e_contact_set (contact, E_CONTACT_UID, uid);

			cb (
				contact, offset,
				((gfloat) (i + 1) / priv->total_records) * 100,
				user_data, error);
		}

error:
		g_object_unref (contact);
		e_contact_address_free (dset->addr);
		g_free (dset);
		g_free (uid);

		if (*error) {
			ret = FALSE;
			goto exit;
		}
	}

exit:
	return ret;
}

gchar *
ews_oab_decoder_get_oab_prop_string (EwsOabDecoder *eod,
                                     GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	GString *str = g_string_new (NULL);
	GSList *l;

	if (!priv->oab_props) {
		g_set_error_literal (
			error, EOD_ERROR, 1,
			"Oab props not found");
		return NULL;
	}

	/* Ideally i would liked to store int as int instead of converting to
	 * string, but sqlite db doesn't yet support storing keys as blob. */
	for (l = priv->oab_props; l != NULL; l = g_slist_next (l)) {
		guint32 prop_id = GPOINTER_TO_UINT (l->data);
		g_string_append_printf (str, "%"G_GUINT32_FORMAT, prop_id);
		g_string_append_c (str, ';');
	}
	if (str->len)
		g_string_erase (str, str->len - 1, 1);

	d (g_print ("Oab prop string: %s \n", str->str);)

	return g_string_free (str, FALSE);
}

gboolean
ews_oab_decoder_set_oab_prop_string (EwsOabDecoder *eod,
                                     const gchar *prop_str,
                                     GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	gchar **vals;
	guint32 len, i;

	vals = g_strsplit (prop_str, ";", -1);
	len = g_strv_length (vals);

	/* hmm is there a better way to check ? */
	if (len < 2) {
		g_set_error_literal (
			error, EOD_ERROR, 1,
			"Does not contain oab properties");
		return FALSE;
	}

	if (priv->oab_props) {
		g_slist_free (priv->oab_props);
		priv->oab_props = NULL;
	}

	for (i = 0; i < len; i++) {
		guint32 prop_id;

		sscanf (vals[i],"%"G_GUINT32_FORMAT,&prop_id);
		priv->oab_props = g_slist_prepend (
			priv->oab_props, GUINT_TO_POINTER (prop_id));
		d (printf ("%X\n", prop_id);)
	}

	priv->oab_props = g_slist_reverse (priv->oab_props);

	g_strfreev (vals);

	return TRUE;
}

/**
 * ews_oab_decoder_decode 
 * @eod: 
 * @cancellable: 
 * @error: 
 * 
 * Decodes the oab full details verions 4 file and stores
 * the properties in the sqlite db.
 * Returns: TRUE if successfully decoded and indexed in db 
 **/
gboolean
ews_oab_decoder_decode (EwsOabDecoder *eod,
                        EwsOabContactAddedCb cb,
                        gpointer user_data,
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
	g_print ("Total records is %d \n", priv->total_records);

	ret = ews_decode_metadata (eod, cancellable, &err);
	if (!ret)
		goto exit;

	ret = ews_decode_and_store_oab_records (
		eod, cb, user_data, cancellable, &err);
exit:
	if (o_hdr)
		g_free (o_hdr);

	if (err)
		g_propagate_error (error, err);

	return ret;
}

EContact *
ews_oab_decoder_get_contact_from_offset (EwsOabDecoder *eod,
                                         goffset offset,
                                         GSList *oab_props,
                                         GCancellable *cancellable,
                                         GError **error)
{
	EwsOabDecoderPrivate *priv = GET_PRIVATE (eod);
	EwsDeferredSet *dset;
	EContact *contact = NULL;

	if (!g_seekable_seek ((GSeekable *) priv->fis, offset, G_SEEK_SET, cancellable, error))
		return NULL;

	contact = e_contact_new ();
	dset = g_new0 (EwsDeferredSet, 1);
	ews_decode_addressbook_record (eod, contact, dset, oab_props, cancellable, error);
	if (*error) {
		g_object_unref (contact);
		contact = NULL;
	}

	e_contact_address_free (dset->addr);
	g_free (dset);

	return contact;
}

#ifdef TEST_BUILD

struct _db_data {
	GSList *contact_collector;
	guint collected_length;
	EBookBackendSqliteDB *summary;
	const gchar *folderid;
};

static void
ews_test_store_contact (EContact *contact,
                        goffset offset,
                        guint percent,
                        gpointer user_data,
                        GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;

	data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
	data->collected_length += 1;

	if (data->collected_length == 1000 || percent >= 100) {
		data->contact_collector = g_slist_reverse (data->contact_collector);
		e_book_backend_sqlitedb_add_contacts (data->summary, data->folderid, data->contact_collector, FALSE, error);
		g_print ("percent complete %d \n", percent);

		g_slist_foreach (data->contact_collector, (GFunc) g_object_unref, NULL);
		g_slist_free (data->contact_collector);
		data->contact_collector = NULL;
		data->collected_length = 0;
	}
}

gint
main (gint argc,
      gchar *argv[])
{
	EBookBackendSqliteDB *summary;
	EwsOabDecoder *eod;
	GError *err = NULL;
	GTimer *timer;
	struct _db_data data;

	g_type_init ();

	if (argc != 3) {
		g_print ("Pass the oab filename  and cache dir as argument \n");
		return -1;
	}

	summary = e_book_backend_sqlitedb_new (argv[2], "dum", "de", "dum", TRUE, NULL);
	eod = ews_oab_decoder_new (argv[1], argv[2], &err);

	data.contact_collector = NULL;
	data.collected_length = 0;
	data.summary = summary;
	data.folderid = "de";

	timer = g_timer_new ();
	g_timer_start (timer);
	if (!ews_oab_decoder_decode (eod, ews_test_store_contact, &data, NULL, &err)) {
		g_print ("Unable to decode %s \n", err->message);
	}
	g_timer_stop (timer);
	g_print ("Time elapsed %lf \n", g_timer_elapsed (timer, NULL));

	if (err)
		g_clear_error (&err);

	g_object_unref (eod);
	g_object_unref (summary);

	return 0;
}
#endif /* TEST_BUILD */
