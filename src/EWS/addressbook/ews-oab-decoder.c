/*
 * SPDX-FileCopyrightText: (C) 1999-2011 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Chenthill Palanisamy <pchenthill@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

#include "ews-oab-decoder.h"
#include "ews-oab-props.h"

#define d(x)

#define EOD_ERROR \
	(ews_oab_decoder_error_quark ())

struct _EwsOabDecoderPrivate {
	gchar *cache_dir;
	GInputStream *fis;

	guint32 total_records;
	GSList *hdr_props;
	GSList *oab_props;

	GHashTable *prop_index_dict;
};

G_DEFINE_TYPE_WITH_PRIVATE (EwsOabDecoder, ews_oab_decoder, G_TYPE_OBJECT)

/* The of properties which will be accumulated and later set in EContact */
typedef struct {
	EContactAddress *addr;
} EwsDeferredSet;

static void
ews_populate_string_sha1 (EContact *contact,
			  EContactField field,
			  gpointer value,
			  gpointer user_data)
{
	gchar *sum;

	sum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, (const gchar *) value, -1);
	e_contact_set (contact, field, sum);
	g_free (sum);
}

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
		dset->addr = e_contact_address_new ();

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
ews_populate_cert_data (EContact *contact,
			GBytes *bytes)
{
	EContactCert cert;

	if (!bytes || !g_bytes_get_size (bytes))
		return;

	cert.data = (gpointer) g_bytes_get_data (bytes, &cert.length);
	cert.length = g_bytes_get_size (bytes);

	e_contact_set (contact, E_CONTACT_X509_CERT, &cert);
}

static void
ews_populate_cert (EContact *contact,
                    EContactField field,
                    gpointer value,
                    gpointer user_data)
{
	GSList *link;

	for (link = value; link; link = g_slist_next (link)) {
		GBytes *bytes = link->data;

		ews_populate_cert_data (contact, bytes);
	}
}

static void
ews_populate_user_cert (EContact *contact,
			EContactField field,
			gpointer value,
			gpointer user_data)
{
	GBytes *bytes = value;

	ews_populate_cert_data (contact, bytes);
}

static void
ews_populate_user_x509_cert (EContact *contact,
			     EContactField field,
			     gpointer value,
			     gpointer user_data)
{
	GSList *link;

	for (link = value; link; link = g_slist_next (link)) {
		GBytes *bytes = link->data;

		ews_populate_cert_data (contact, bytes);
	}
}

static void
ews_populate_photo (EContact *contact,
                    EContactField field,
                    gpointer value,
                    gpointer user_data)
{
	EwsOabDecoder *eod = EWS_OAB_DECODER (user_data);
	const gchar *at;
	GBytes *bytes = value;
	gchar *email;
	gchar *filename = NULL, *pic_name = NULL, *name;
	gboolean success = TRUE;
	GError *local_error = NULL;

	if (!bytes)
		return;

	email = e_contact_get (contact, E_CONTACT_EMAIL_1);
	if (!email || !strchr (email, '@')) {
		g_free (email);
		return;
	}

	/* Rename the binary file to name.jpg */
	at = strchr (email, '@');
	name = g_strndup (email, at - email);

	pic_name = g_strconcat (name, ".jpg", NULL);
	filename = g_build_filename (eod->priv->cache_dir, pic_name, NULL);

	success = g_file_set_contents (filename, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), &local_error);

	if (success) {
		EContactPhoto photo;

		memset (&photo, 0, sizeof (EContactPhoto));
		photo.type = E_CONTACT_PHOTO_TYPE_URI;
		photo.data.uri = filename;

		e_contact_set (contact, field, &photo);
	} else {
		g_warning ("%s: Failed to store '%s': %s", G_STRFUNC, filename, local_error ? local_error->message : "Unknown error");
	}

	g_clear_error (&local_error);

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
	{EWS_PT_EMAIL_ADDRESS, E_CONTACT_UID, ews_populate_string_sha1},
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
	{EWS_PT_THUMBNAIL_PHOTO, E_CONTACT_PHOTO, ews_populate_photo},
	{EWS_PT_OFFICE_LOCATION, E_CONTACT_OFFICE, ews_populate_simple_string},
	{EWS_PT_X509_CERT, E_CONTACT_X509_CERT, ews_populate_cert},
	{EWS_PT_USER_CERTIFICATE, E_CONTACT_X509_CERT, ews_populate_user_cert},
	{EWS_PT_USER_X509_CERTIFICATE, E_CONTACT_X509_CERT, ews_populate_user_x509_cert},
	{EWS_PT_SEND_RICH_INFO, E_CONTACT_WANTS_HTML, ews_populate_simple_string},
};

static void
ews_oab_decoder_finalize (GObject *object)
{
	EwsOabDecoder *eod = EWS_OAB_DECODER (object);

	g_clear_pointer (&eod->priv->cache_dir, g_free);
	g_clear_object (&eod->priv->fis);
	g_clear_pointer (&eod->priv->prop_index_dict, g_hash_table_destroy);
	g_clear_pointer (&eod->priv->oab_props, g_slist_free);
	g_clear_pointer (&eod->priv->hdr_props, g_slist_free);

	G_OBJECT_CLASS (ews_oab_decoder_parent_class)->finalize (object);
}

static void
ews_oab_decoder_class_init (EwsOabDecoderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = ews_oab_decoder_finalize;
}

static void
ews_oab_decoder_init (EwsOabDecoder *self)
{
	gint i;

	self->priv = ews_oab_decoder_get_instance_private (self);

	self->priv->prop_index_dict = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (i = 1; i <= G_N_ELEMENTS (prop_map); i++)
		g_hash_table_insert (self->priv->prop_index_dict, GINT_TO_POINTER (prop_map[i - 1].prop_id), GINT_TO_POINTER (i));
}

EwsOabDecoder *
ews_oab_decoder_new (const gchar *oab_filename,
                     const gchar *cache_dir,
                     GError **error)
{
	EwsOabDecoder *eod;
	GError *err = NULL;
	GFile *gf = NULL;

	eod = g_object_new (EWS_TYPE_OAB_DECODER, NULL);

	gf = g_file_new_for_path (oab_filename);
	eod->priv->fis = (GInputStream *) g_file_read (gf, NULL, &err);
	if (err)
		goto exit;

	eod->priv->cache_dir = g_strdup (cache_dir);

 exit:
	g_clear_object (&gf);

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
	while (1) {
		gsize len;
		gsize bytes_read;
		gchar *c = g_malloc0 (size);

		if (!g_input_stream_read_all (is, c, size, &bytes_read,
					      cancellable, error)) {
			g_free (c);
			break;
		}

		if (bytes_read != size)
			size = bytes_read;

		len = get_pos (c, size, stop);
		if (len)
			g_string_append_len (str, c, len);

		g_free (c);

		if (len == 0 || len < size) {
			goffset seek = (goffset) len + 1 - (goffset) size;

			/* seek back */
			g_seekable_seek ((GSeekable *) is, seek, G_SEEK_CUR, cancellable, error);
			break;
		}

		size *= 2;
	}

	return g_string_free (str, FALSE);
}

typedef struct {
	guint32 version;
	guint32 serial;
	guint32 total_recs;
} EwsOabHdr;

static EwsOabHdr *
ews_read_oab_header (EwsOabDecoder *eod, GInputStream *stream,
                     GCancellable *cancellable,
                     GError **error)
{
	EwsOabHdr *o_hdr;

	o_hdr = g_new0 (EwsOabHdr, 1);

	o_hdr->version = ews_oab_read_uint32 (stream, cancellable, error);
	if (*error)
		goto exit;

	if (o_hdr->version != 0x00000020) {
		g_set_error_literal (error, EOD_ERROR, 1, "wrong version header");
		goto exit;
	}

	o_hdr->serial = ews_oab_read_uint32 (stream, cancellable, error);
	if (*error)
		goto exit;
	o_hdr->total_recs = ews_oab_read_uint32 (stream, cancellable, error);

exit:
	if (*error) {
		g_free (o_hdr);
		return NULL;
	}

	return o_hdr;
}

static gboolean
ews_decode_hdr_props (EwsOabDecoder *eod,
		      GInputStream *stream,
		      gboolean oab_hdrs,
		      GCancellable *cancellable,
		      GError **error)
{
	guint32 num_props, i;
	GSList **props;

	/* number of properties */
	num_props = ews_oab_read_uint32 (stream, cancellable, error);

	if (*error)
		return FALSE;

	if (oab_hdrs)
		props = &eod->priv->oab_props;
	else
		props = &eod->priv->hdr_props;

	if (*props) {
		g_slist_free (*props);
		*props = NULL;
	}

	for (i = 0; i < num_props; i++) {
		guint32 prop_id;

		prop_id = ews_oab_read_uint32 (stream, cancellable, error);

		*props = g_slist_prepend (*props, GUINT_TO_POINTER (prop_id));

		if (*error)
			return FALSE;

		/* eat the flags */
		ews_oab_read_uint32 (stream, cancellable, error);

		if (*error)
			return FALSE;

		/* skip store anr_index and primary key prop list as we will not be using it for online search,
		 * store if required later */
	}

	*props = g_slist_reverse (*props);

	return TRUE;
}

static gboolean
ews_decode_metadata (EwsOabDecoder *eod, GInputStream *stream,
                     GCancellable *cancellable,
                     GError **error)
{
	gboolean ret = TRUE;

	/* eat the size */
	ews_oab_read_uint32 (stream, cancellable, error);

	if (*error)
		return FALSE;

	ret = ews_decode_hdr_props (eod, stream, FALSE, cancellable, error);
	if (!ret)
		return FALSE;

	ret = ews_decode_hdr_props (eod, stream, TRUE, cancellable, error);

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
ews_decode_uint32 (EwsOabDecoder *eod, GInputStream *stream,
                   GCancellable *cancellable,
                   GError **error)
{
	guint8 first;
	guint32 ret = 0, num;

	g_input_stream_read (stream, &first, 1, cancellable, error);
	if (*error)
		return ret;

	if (first & 0x80)
		num = first & 0x0F;
	else
		return (guint32) first;

	if (num == 1) {
		g_input_stream_read (stream, &first, 1, cancellable, error);
		return (guint32) first;
	}

	if (num == 2)
		ret = ews_oab_read_uint16 (stream, cancellable, error);
	if (num == 3) {
		gchar *tmp, *str = g_malloc0 (num + 1);

		g_input_stream_read (stream, str, num, cancellable, error);
		/* not sure if its the right way to do, test it */
		tmp = g_strconcat ("0", str, NULL);

		sscanf (tmp, "%"G_GUINT32_FORMAT, &ret);
		ret = GUINT32_SWAP_LE_BE (ret);

		g_free (str);
		g_free (tmp);
	} else if (num == 4)
		ret = ews_oab_read_uint32 (stream, cancellable, error);

	return ret;
}

static GBytes *
ews_decode_binary (EwsOabDecoder *eod, GInputStream *stream,
                   GCancellable *cancellable,
                   GError **error)
{
	guint32 len;
	gchar *binary;
	GBytes *val = NULL;

	len = ews_decode_uint32 (eod, stream, cancellable, error);
	if (*error)
		return NULL;
	binary = g_malloc (len);
	g_input_stream_read (stream, binary, len, cancellable, error);
	if (*error) {
		g_free (binary);
		goto exit;
	}

	val = g_bytes_new_take (binary, len);
	binary = NULL;

exit:
	return val;
}

static gpointer
ews_decode_oab_prop (EwsOabDecoder *eod, GInputStream *stream,
                     guint32 prop_id,
                     GCancellable *cancellable,
                     GError **error)
{
	guint32 prop_type;
	gpointer ret_val = NULL;

	prop_type = prop_id & 0x0000FFFF;

	switch (prop_type) {
		case EWS_PTYP_INTEGER32:
		{
			guint32 val;

			val = ews_decode_uint32 (eod, stream, cancellable, error);
			ret_val = GUINT_TO_POINTER (val);

			d (g_print ("prop id %X prop type: int32 value %d \n", prop_id, val);)

			break;
		}
		case EWS_PTYP_BOOLEAN:
		{
			guchar val;

			g_input_stream_read (stream, &val, 1, cancellable, error);
			ret_val = GUINT_TO_POINTER ((guint) val);
			d (g_print ("prop id %X prop type: bool value %d \n", prop_id, val);)

			break;
		}
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
		{
			gchar *val;

			val = ews_oab_read_upto (stream, '\0', cancellable, error);
			ret_val = (gpointer) val;

			d (g_print ("prop id %X prop type: string value %s \n", prop_id, val);)
			break;
		}
		case EWS_PTYP_BINARY:
		{
			ret_val = ews_decode_binary (eod, stream, cancellable, error);
			d (g_print ("prop id %X prop type: binary size %zd \n", prop_id, g_bytes_get_size ((GBytes *)ret_val)));
			break;
		}
		case EWS_PTYP_MULTIPLEINTEGER32:
		case EWS_PTYP_MULTIPLESTRING8:
		case EWS_PTYP_MULTIPLESTRING:
		case EWS_PTYP_MULTIPLEBINARY:
		{
			guint32 num, i;
			GSList *list = NULL;

			num = ews_decode_uint32 (eod, stream, cancellable, error);
			if (*error)
				break;
			d (g_print ("prop id %X prop type: multi-num %d \n", prop_id, num);)

			for (i = 0; i < num; i++) {
				if (prop_type == EWS_PTYP_MULTIPLEINTEGER32) {
					gpointer val;
					guint32 v = 0;

					v = ews_decode_uint32 (eod, stream, cancellable, error);
					val = GUINT_TO_POINTER (v);
					list = g_slist_prepend (list, val);

					d (g_print ("prop id %X prop type: multi-int32 %d \n", prop_id, v);)
					if (*error) {
						g_slist_free (list);
						return NULL;
					}
				} else if (prop_type == EWS_PTYP_MULTIPLEBINARY) {
					GBytes *val;

					val = ews_decode_binary (eod, stream, cancellable, error);
					if (!val) {
						g_slist_foreach (list, (GFunc) g_bytes_unref, NULL);
						g_slist_free (list);
						return NULL;
					}

					d (g_print ("prop id %X prop type: multi-bin size %zd\n", prop_id, g_bytes_get_size (val)));

					list = g_slist_prepend (list, val);
				} else {
					gchar *val;

					val = ews_oab_read_upto (stream, '\0', cancellable, error);
					if (!val) {
						g_slist_foreach (list, (GFunc) g_free, NULL);
						g_slist_free (list);
						return NULL;
					}

					d (g_print ("prop id %X prop type: multi-str '%s'\n", prop_id, val));
					list = g_slist_prepend (list, val);
				}

			}
			ret_val = list;

			break;
		}
		default:
			g_error ("%s: Cannot decode property 0x%x", G_STRFUNC, prop_id);
			break;
	}

	return ret_val;
}

static void
ews_destroy_oab_prop (guint32 prop_id, gpointer val)
{
	guint32 prop_type;

	prop_type = prop_id & 0x0000FFFF;

	switch (prop_type) {
		case EWS_PTYP_INTEGER32:
		case EWS_PTYP_BOOLEAN:
			break;
		case EWS_PTYP_BINARY:
			g_bytes_unref (val);
			break;
		case EWS_PTYP_STRING8:
		case EWS_PTYP_STRING:
			g_free ((gchar *) val);
			break;
		case EWS_PTYP_MULTIPLEBINARY:
			g_slist_free_full ((GSList *) val, (GDestroyNotify) g_bytes_unref);
			break;
		case EWS_PTYP_MULTIPLESTRING8:
		case EWS_PTYP_MULTIPLESTRING:
			g_slist_free_full ((GSList *) val, g_free);
			break;
		case EWS_PTYP_MULTIPLEINTEGER32:
			g_slist_free ((GSList *) val);
			break;
		default:
			g_warn_if_reached ();
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
			       GInputStream *stream,
			       EContact *contact,
			       GSList *props,
			       GCancellable *cancellable,
			       GError **error)
{
	EwsDeferredSet *dset = NULL;
	guint bit_array_size, i, len;
	gchar *bit_str;
	gboolean ret = TRUE;

	len = g_slist_length (props);
	bit_array_size = (guint) ceil (len / 8.0);
	bit_str = g_malloc0 (bit_array_size);
	g_input_stream_read (stream, bit_str, bit_array_size, cancellable, error);
	if (*error) {
		ret = FALSE;
		goto exit;
	}

	if (contact)
		dset = g_new0 (EwsDeferredSet, 1);

	for (i = 0; i < len; i++) {
		gpointer val, index;
		guint32 prop_id;

		if (!ews_is_bit_set (bit_str, i))
			continue;

		val = g_slist_nth_data (props, i);
		prop_id = GPOINTER_TO_UINT (val);

		/* these are not encoded in the OAB, according to
		   http://msdn.microsoft.com/en-us/library/gg671985%28v=EXCHG.80%29.aspx
		 */
		if ((prop_id & 0xFFFF) == EWS_PTYP_OBJECT)
			continue;

		val = ews_decode_oab_prop (eod, stream, prop_id, cancellable, error);

		if (prop_id == EWS_PT_DISPLAY_TYPE)
			ews_decode_addressbook_write_display_type (&contact, GPOINTER_TO_UINT (val), FALSE);

		if (prop_id == EWS_PT_DISPLAY_TYPE_EX)
			ews_decode_addressbook_write_display_type (&contact, GPOINTER_TO_UINT (val), TRUE);

		/* Check the contact map and store the data in EContact */
		index = g_hash_table_lookup (eod->priv->prop_index_dict, GINT_TO_POINTER (prop_id));
		if (contact && index) {
			gint idx = GPOINTER_TO_INT (index);

			if (prop_map[idx - 1].populate_function)
				prop_map[idx - 1].populate_function (contact, prop_map[idx - 1].field, val, (gpointer) eod);
			else
				prop_map[idx - 1].defered_populate_function (dset, prop_id, val);
		}
		ews_destroy_oab_prop (prop_id, val);
		if (*error)
			goto exit;
	}

exit:
	if (bit_str)
		g_free (bit_str);

	if (!contact)
		return ret;

	if (dset && dset->addr) {
		e_contact_set (contact, E_CONTACT_ADDRESS_WORK, dset->addr);
		e_contact_address_free (dset->addr);
	}
	g_free (dset);

	/* set the smtp address as contact's uid */
	if (!e_contact_get_const(contact, E_CONTACT_UID)) {
		const gchar *uid = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
		if (uid && *uid)
			e_contact_set (contact, E_CONTACT_UID, uid);
		else
			ret = FALSE;
	}

	return ret;
}

/* Decodes the hdr and address-book records and stores the address-book records inside the db */
static gboolean
ews_decode_and_store_oab_records (EwsOabDecoder *eod,
				  EwsOabContactFilterCb filter_cb,
                                  EwsOabContactAddedCb cb,
                                  gpointer user_data,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gboolean ret = FALSE;
	guint32 i;
	int buf_len = 200;
	guchar *record_buf = g_malloc (buf_len);
	GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA1);

	if (!record_buf || !sum)
		goto exit;

	/* eat the size */
	ews_oab_read_uint32 (
		eod->priv->fis,
		cancellable, error);

	ews_decode_addressbook_record (eod, eod->priv->fis, NULL,
				       eod->priv->hdr_props, cancellable, error);

	if (*error)
		goto exit;


	for (i = 0; i < eod->priv->total_records; i++) {
		EContact *contact;
		goffset offset;
		guint32 rec_size;
		GInputStream *memstream;
		const gchar *sum_str;

		contact = e_contact_new ();

		/* eat the size */
		rec_size = ews_oab_read_uint32 (eod->priv->fis, cancellable, error);
		if (rec_size < 4)
			goto exit;

		rec_size -= 4;

		if (rec_size > buf_len) {
			g_free (record_buf);
			record_buf = g_malloc(rec_size);
			buf_len = rec_size;
			if (!record_buf)
				goto exit;
		}
		/* fetch the offset */
		offset = g_seekable_tell ((GSeekable *) eod->priv->fis);
		if (g_input_stream_read (eod->priv->fis, record_buf, rec_size, cancellable, error) != rec_size)
			goto exit;

		g_checksum_reset (sum);
		g_checksum_update (sum, record_buf, rec_size);
		sum_str = g_checksum_get_string (sum);

		memstream = g_memory_input_stream_new_from_data (record_buf, rec_size, NULL);

		if ((!filter_cb || filter_cb (offset, sum_str, user_data, error)) &&
		    ews_decode_addressbook_record (eod, memstream,
						   contact, eod->priv->oab_props,
						   cancellable, error))
			cb (contact, offset, sum_str,
			    ((gfloat) (i + 1) / eod->priv->total_records) * 100,
			    user_data, cancellable, error);

		g_object_unref (memstream);
		g_object_unref (contact);

		if (*error)
			goto exit;
	}

	ret = TRUE;
exit:
	g_checksum_free (sum);
	g_free (record_buf);
	return ret;
}

gchar *
ews_oab_decoder_get_oab_prop_string (EwsOabDecoder *eod,
                                     GError **error)
{
	GString *str;
	GSList *l;

	if (!eod->priv->oab_props) {
		g_set_error_literal (
			error, EOD_ERROR, 1,
			"Oab props not found");
		return NULL;
	}

	str = g_string_new (NULL);

	/* Ideally i would liked to store int as int instead of converting to
	 * string, but sqlite db doesn't yet support storing keys as blob. */
	for (l = eod->priv->oab_props; l != NULL; l = g_slist_next (l)) {
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

	g_clear_pointer (&eod->priv->oab_props, g_slist_free);

	for (i = 0; i < len; i++) {
		guint32 prop_id;

		sscanf (vals[i],"%"G_GUINT32_FORMAT,&prop_id);
		eod->priv->oab_props = g_slist_prepend (
			eod->priv->oab_props, GUINT_TO_POINTER (prop_id));
		d (printf ("%X\n", prop_id);)
	}

	eod->priv->oab_props = g_slist_reverse (eod->priv->oab_props);

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
                        EwsOabContactFilterCb filter_cb,
                        EwsOabContactAddedCb cb,
                        gpointer user_data,
                        GCancellable *cancellable,
                        GError **error)
{
	GError *err = NULL;
	EwsOabHdr *o_hdr;
	gboolean ret = TRUE;

	o_hdr = ews_read_oab_header (eod, eod->priv->fis, cancellable, &err);
	if (!o_hdr) {
		ret = FALSE;
		goto exit;
	}

	eod->priv->total_records = o_hdr->total_recs;
	d (g_print ("Total records is %d \n", eod->priv->total_records));

	ret = ews_decode_metadata (eod, eod->priv->fis, cancellable, &err);
	if (!ret)
		goto exit;

	ret = ews_decode_and_store_oab_records (
		eod, filter_cb, cb, user_data, cancellable, &err);
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
	EContact *contact = NULL;

	if (!g_seekable_seek ((GSeekable *) eod->priv->fis, offset, G_SEEK_SET, cancellable, error))
		return NULL;

	contact = e_contact_new ();
	if (!ews_decode_addressbook_record (eod, eod->priv->fis,
					    contact, oab_props, cancellable,
					    error)) {
		g_object_unref (contact);
		contact = NULL;
	}

	return contact;
}

