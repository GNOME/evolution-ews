/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews.c - Ews contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "evolution-ews-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedata-book/libedata-book.h>

#include "server/e-ews-item-change.h"
#include "server/e-ews-message.h"
#include "server/e-ews-connection.h"
#include "server/e-ews-connection-utils.h"
#include "server/e-ews-item.h"
#include "server/e-ews-query-to-restriction.h"
#include "server/e-source-ews-folder.h"

#include "e-book-backend-ews.h"
#include "ews-oab-decoder.h"
#include "ews-oab-decompress.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define d(x)

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

#define X_EWS_ORIGINAL_VCARD "X-EWS-ORIGINAL-VCARD"
#define X_EWS_CHANGEKEY "X-EWS-CHANGEKEY"
#define X_EWS_GAL_SHA1 "X-EWS-GAL-SHA1"

#define EWS_MAX_FETCH_COUNT 500

#define ELEMENT_TYPE_SIMPLE 0x01 /* simple string fields */
#define ELEMENT_TYPE_COMPLEX 0x02 /* complex fields while require different get/set functions */

/* passing field uris for PhysicalAddress, PhoneNumbers causes error, so we
 * use Default view to fetch them. Thus the summary props just have attachments
 * and some additional properties that are not return with Default view */
#define CONTACT_ITEM_PROPS "item:Attachments item:HasAttachments item:Body item:LastModifiedTime contacts:Manager contacts:Department contacts:SpouseName contacts:AssistantName contacts:BusinessHomePage contacts:Birthday"

struct _EBookBackendEwsPrivate {
	GRecMutex cnc_lock;
	EEwsConnection *cnc;

	gchar *folder_id;
	gboolean is_gal;

	guint subscription_key;

	/* used for storing attachments */
	gchar *attachments_dir;
};

G_DEFINE_TYPE (EBookBackendEws, e_book_backend_ews, E_TYPE_BOOK_META_BACKEND)

static CamelEwsSettings *
ebb_ews_get_collection_settings (EBookBackendEws *bbews)
{
	ESource *source;
	ESource *collection;
	ESourceCamel *extension;
	ESourceRegistry *registry;
	CamelSettings *settings;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (bbews));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbews));

	extension_name = e_source_camel_get_extension_name ("ews");
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);

	/* The collection settings live in our parent data source. */
	collection = e_source_registry_find_extension (registry, source, extension_name);
	g_return_val_if_fail (collection != NULL, NULL);

	extension = e_source_get_extension (collection, extension_name);
	settings = e_source_camel_get_settings (extension);

	g_object_unref (collection);

	return CAMEL_EWS_SETTINGS (settings);
}

static void
ebb_ews_convert_error_to_edb_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror || (*perror)->domain == E_DATA_BOOK_ERROR)
		return;

	if ((*perror)->domain == EWS_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EDB_ERROR_EX (AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_FOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = EDB_ERROR_EX (NO_SUCH_BOOK, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_EVENTNOTFOUND:
		case EWS_CONNECTION_ERROR_ITEMNOTFOUND:
			error = EDB_ERROR_EX (CONTACT_NOT_FOUND, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EDB_ERROR_EX (OTHER_ERROR, (*perror)->message);
	}

	if (error) {
		g_error_free (*perror);
		*perror = error;
	}
}

static void
ebb_ews_maybe_disconnect_sync (EBookBackendEws *bbews,
			       GError **in_perror,
			       GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_EWS (bbews));

	if (in_perror && g_error_matches (*in_perror, E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_AUTHENTICATION_FAILED)) {
		e_book_meta_backend_disconnect_sync (E_BOOK_META_BACKEND (bbews), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (bbews), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static const struct phone_field_mapping {
	EContactField field;
	const gchar *element;
} phone_field_map[] = {
	{E_CONTACT_PHONE_ASSISTANT, "AssistantPhone"},
	{E_CONTACT_PHONE_BUSINESS_FAX, "BusinessFax"},
	{E_CONTACT_PHONE_BUSINESS, "BusinessPhone"},
	{E_CONTACT_PHONE_BUSINESS_2, "BusinessPhone2"},
	{E_CONTACT_PHONE_CAR, "CarPhone"},
	{E_CONTACT_PHONE_COMPANY, "CompanyMainPhone"},
	{E_CONTACT_PHONE_HOME_FAX, "HomeFax"},
	{E_CONTACT_PHONE_HOME, "HomePhone"},
	{E_CONTACT_PHONE_HOME_2, "HomePhone2"},
	{E_CONTACT_PHONE_ISDN, "Isdn"},
	{E_CONTACT_PHONE_MOBILE, "MobilePhone"},
	{E_CONTACT_PHONE_OTHER_FAX, "OtherFax"},
	{E_CONTACT_PHONE_OTHER, "OtherTelephone"},
	{E_CONTACT_PHONE_PAGER, "Pager"},
	{E_CONTACT_PHONE_PRIMARY, "PrimaryPhone"},
	{E_CONTACT_PHONE_RADIO, "RadioPhone"},
	{E_CONTACT_PHONE_TELEX, "Telex"},
	{E_CONTACT_PHONE_TTYTDD, "TtyTddPhone"}
};

static void
ebews_populate_rev (EContact *contact,
		    EEwsItem *item)
{
	struct tm stm;
	time_t tt = 0;
	gchar time_string[100] = { 0 };

	g_return_if_fail (E_IS_CONTACT (contact));

	if (item) {
		g_return_if_fail (E_IS_EWS_ITEM (item));

		tt = e_ews_item_get_last_modified_time (item);
	}

	if (tt <= 0)
		tt = time (NULL);

	gmtime_r (&tt, &stm);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", &stm);

	e_contact_set (contact, E_CONTACT_REV, time_string);
}

static void
ebews_populate_uid (EBookBackendEws *bbews,
		    EContact *contact,
                    EEwsItem *item,
		    GCancellable *cancellable,
		    GError **error)
{
	const EwsId *id;

	id = e_ews_item_get_id (item);
	if (id) {
		e_contact_set (contact, E_CONTACT_UID, id->id);
		ebews_populate_rev (contact, item);
		e_vcard_util_set_x_attribute (E_VCARD (contact), X_EWS_CHANGEKEY, id->change_key);
	}
}

static void
ebews_populate_full_name (EBookBackendEws *bbews,
			  EContact *contact,
			  EEwsItem *item,
			  GCancellable *cancellable,
			  GError **error)
{
	const EwsCompleteName *cn;

	cn = e_ews_item_get_complete_name (item);
	if (cn)
		e_contact_set (contact, E_CONTACT_FULL_NAME, cn->full_name);
}

static void
ebews_populate_nick_name (EBookBackendEws *bbews,
			  EContact *contact,
			  EEwsItem *item,
			  GCancellable *cancellable,
			  GError **error)
{
	const EwsCompleteName *cn;

	cn = e_ews_item_get_complete_name (item);
	if (cn)
		e_contact_set (contact, E_CONTACT_NICKNAME, cn->nick_name);
}

static void
ebews_populate_birth_date (EBookBackendEws *bbews,
			   EContact *contact,
			   EEwsItem *item,
			   GCancellable *cancellable,
			   GError **error)
{
	time_t bdate;
	GDate date;
	EContactDate edate;

	bdate = e_ews_item_get_birthday (item);

	if (bdate) {
		g_date_clear (&date, 1);
		g_date_set_time_t (&date, bdate);

		edate.year = date.year;
		edate.month = date.month;
		edate.day = date.day;

		if (g_date_valid (&date))
			e_contact_set (contact, E_CONTACT_BIRTH_DATE, &edate);
	}
}

static void
ebews_populate_anniversary (EBookBackendEws *bbews,
			    EContact *contact,
			    EEwsItem *item,
			    GCancellable *cancellable,
			    GError **error)
{
	time_t bdate;
	GDate date;
	EContactDate edate;

	bdate = e_ews_item_get_wedding_anniversary (item);

	if (bdate) {
		g_date_clear (&date, 1);
		g_date_set_time_t (&date, bdate);

		edate.year = date.year;
		edate.month = date.month;
		edate.day = date.day;

		if (g_date_valid (&date))
			e_contact_set (contact, E_CONTACT_ANNIVERSARY, &edate);
	}
}

static EContactPhoto *
get_photo (EBookBackendEws *bbews,
	   EEwsItem *item,
	   GCancellable *cancellable,
	   GError **error)
{
	EContactPhoto *photo = NULL;
	EEwsAdditionalProps *add_props = NULL;
	EEwsAttachmentInfo *info;
	GSList *contact_item_ids = NULL, *new_items = NULL;
	GSList *attachments = NULL,  *attachments_ids = NULL;
	const guchar *content;
	const gchar *contact_photo_id;
	const EwsId *id;
	gsize len;

	id = e_ews_item_get_id (item);
	if (!id)
		return NULL;

	add_props = e_ews_additional_props_new ();
	add_props->field_uri = g_strdup ("item:Attachments");

	contact_item_ids = g_slist_prepend (contact_item_ids, g_strdup (id->id));
	if (!e_ews_connection_get_items_sync (
			bbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			contact_item_ids,
			"IdOnly",
			add_props,
			FALSE,
			NULL,
			E_EWS_BODY_TYPE_TEXT,
			&new_items,
			NULL,
			NULL,
			cancellable,
			error))
		goto exit;

	contact_photo_id = e_ews_item_get_contact_photo_id (new_items->data);
	if (!contact_photo_id)
		goto exit;

	attachments_ids = g_slist_prepend (attachments_ids, g_strdup (contact_photo_id));
	if (!e_ews_connection_get_attachments_sync (
		bbews->priv->cnc,
		EWS_PRIORITY_MEDIUM,
		NULL,
		attachments_ids,
		NULL,
		FALSE,
		&attachments,
		NULL,
		NULL,
		cancellable,
		error))
		goto exit;

	info = attachments->data;
	content = (guchar *) e_ews_attachment_info_get_inlined_data (info, &len);

	photo = e_contact_photo_new ();
	photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
	e_contact_photo_set_inlined (photo, content, len);

exit:
	e_ews_additional_props_free (add_props);
	g_slist_free_full (contact_item_ids, g_free);
	g_slist_free_full (new_items, g_object_unref);
	g_slist_free_full (attachments_ids, g_free);
	g_slist_free_full (attachments, (GDestroyNotify) e_ews_attachment_info_free);

	return photo;
}

static void
ebews_populate_photo (EBookBackendEws *bbews,
		      EContact *contact,
		      EEwsItem *item,
		      GCancellable *cancellable,
		      GError **error)
{
	EContactPhoto *photo;

	/*
	 * Support for ContactPhoto was added in Exchange 2010 SP2.
	 * We don't want to try to set/get this property if we are running in older version of the server.
	 */
	if (!e_ews_connection_satisfies_server_version (bbews->priv->cnc, E_EWS_EXCHANGE_2010_SP2))
		return;

	photo = get_photo (bbews, item, cancellable, error);
	if (!photo) {
		return;
	}

	e_contact_set (contact, E_CONTACT_PHOTO, photo);
	e_contact_photo_free (photo);
}

static void
set_phone_number (EContact *contact,
                  EContactField field,
                  EEwsItem *item,
                  const gchar *item_field)
{
	const gchar *pn;

	pn = e_ews_item_get_phone_number (item, item_field);
	if (pn && *pn)
		e_contact_set (contact, field, pn);
}

static void
ebews_populate_phone_numbers (EBookBackendEws *bbews,
			      EContact *contact,
			      EEwsItem *item,
			      GCancellable *cancellable,
			      GError **error)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++)
		set_phone_number (contact, phone_field_map[i].field, item, phone_field_map[i].element);
}

static void
copy_ews_address_to_contact_address (EContactAddress *contact_addr,
                                     const EwsAddress *address)
{
	contact_addr->address_format = NULL;
	contact_addr->po = NULL;
	contact_addr->street = g_strdup (address->street);
	contact_addr->ext = NULL;
	contact_addr->locality = g_strdup (address->city);
	contact_addr->region = g_strdup (address->state);
	contact_addr->code = g_strdup (address->postal_code);
	contact_addr->country = g_strdup (address->country);
}

static gboolean
ews_address_is_empty (const EwsAddress *address)
{
	if (!address)
		return TRUE;

#define is_empty_str(x) (!(x) || (!*(x)))

	return is_empty_str (address->street) &&
		is_empty_str (address->city) &&
		is_empty_str (address->state) &&
		is_empty_str (address->postal_code) &&
		is_empty_str (address->country);

#undef is_empty_str
}

static void
set_address (EContact *contact,
             EContactField field,
             EEwsItem *item,
             const gchar *item_field)
{
	const EwsAddress *address;

	address = e_ews_item_get_physical_address (item, item_field);
	if (address && !ews_address_is_empty (address)) {
		EContactAddress *addr;

		addr = g_new0 (EContactAddress, 1);
		copy_ews_address_to_contact_address (addr, address);
		e_contact_set (contact, field, addr);
		e_contact_address_free (addr);
	}
}

static void
ebews_populate_address (EBookBackendEws *bbews,
			EContact *contact,
			EEwsItem *item,
			GCancellable *cancellable,
			GError **error)
{

	set_address (contact, E_CONTACT_ADDRESS_WORK, item, "Business");
	set_address (contact, E_CONTACT_ADDRESS_HOME, item, "Home");
	set_address (contact, E_CONTACT_ADDRESS_OTHER, item, "Other");
}

static void
ebews_populate_ims (EBookBackendEws *bbews,
		    EContact *contact,
		    EEwsItem *item,
		    GCancellable *cancellable,
		    GError **error)
{
	/* TODO : The fields returned by server does not match with the EContact fields
	 * for the IMS, handle it later */
}

static void
ebews_populate_notes (EBookBackendEws *bbews,
		      EContact *contact,
		      EEwsItem *item,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *notes = e_ews_item_get_notes (item);
	if (!notes)
		return;

	e_contact_set (contact, E_CONTACT_NOTE, notes);
}

static void
set_email_address (EContact *contact,
                   EContactField field,
                   EEwsItem *item,
                   const gchar *item_field,
		   gboolean require_smtp_prefix)
{
	const gchar *ea;

	ea = e_ews_item_get_email_address (item, item_field);
	if (ea && g_ascii_strncasecmp (ea, "SMTP:", 5) == 0)
		ea = ea + 5;
	else if (require_smtp_prefix)
		ea = NULL;

	if (ea && *ea)
		e_contact_set (contact, field, ea);
}

static void
ebews_populate_emails_ex (EBookBackendEws *bbews,
			  EContact *contact,
			  EEwsItem *item,
			  gboolean require_smtp_prefix)
{
	set_email_address (contact, E_CONTACT_EMAIL_1, item, "EmailAddress1", require_smtp_prefix);
	set_email_address (contact, E_CONTACT_EMAIL_2, item, "EmailAddress2", require_smtp_prefix);
	set_email_address (contact, E_CONTACT_EMAIL_3, item, "EmailAddress3", require_smtp_prefix);
}

static void
ebews_populate_emails (EBookBackendEws *bbews,
		       EContact *contact,
		       EEwsItem *item,
		       GCancellable *cancellable,
		       GError **errror)
{
	ebews_populate_emails_ex (bbews, contact, item, FALSE);
}

static void
ebews_set_item_id (ESoapMessage *message,
                   EContact *contact)
{

}

static void
ebews_set_full_name (ESoapMessage *msg,
                     EContact *contact)
{
	EContactName *name;

	name = e_contact_get (contact, E_CONTACT_NAME);
	if (!name)
		return;

	if (name->given)
		e_ews_message_write_string_parameter (msg, "GivenName", NULL, name->given);

	if (name->additional && *name->additional)
		e_ews_message_write_string_parameter (msg, "MiddleName", NULL, name->additional);

	e_contact_name_free (name);
}

/* TODO Set birth and anniversary dates */
static void
ebews_set_birth_date (ESoapMessage *message,
                      EContact *contact)
{
	EContactDate *date;
	gchar *birthday;

	date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);

	if (!date)
		return;

	birthday = g_strdup_printf (
		"%04d-%02d-%02dT00:00:00",
		date->year, date->month, date->day);

	e_ews_message_write_string_parameter (message, "Birthday", NULL, birthday);

	g_free (birthday);

}

static void
ebews_set_anniversary (ESoapMessage *message,
                       EContact *contact)
{

}

static void
ebews_set_photo (ESoapMessage *message,
                 EContact *contact)
{

}

static gboolean
add_entry (ESoapMessage *msg,
           EContact *contact,
           EContactField field,
           const gchar *entry_name,
           const gchar *include_hdr)
{
	gchar *entry_val;

	entry_val = e_contact_get (contact, field);

	if (entry_val && *entry_val) {
		if (include_hdr)
			e_soap_message_start_element (msg, include_hdr, NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (msg, "Entry", NULL, entry_val, "Key", entry_name);

		g_free (entry_val);
		return TRUE;
	}

	g_free (entry_val);
	return FALSE;
}

static void
ebews_set_phone_numbers (ESoapMessage *msg,
                         EContact *contact)
{
	gint i;
	const gchar *include_hdr = "PhoneNumbers";

	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++) {
		if (add_entry (msg, contact, phone_field_map[i].field, phone_field_map[i].element, include_hdr))
			include_hdr = NULL;
	}

	if (!include_hdr)
		e_soap_message_end_element (msg);
}

static gboolean
add_physical_address (ESoapMessage *msg,
                      EContact *contact,
                      EContactField field,
                      const gchar *entry_name,
                      gboolean include_start_hdr)
{
	EContactAddress *contact_addr;

	contact_addr = e_contact_get (contact, field);
	if (!contact_addr)
		return FALSE;

	if (include_start_hdr)
		e_soap_message_start_element (msg, "PhysicalAddresses", NULL, NULL);

	e_soap_message_start_element (msg, "Entry", NULL, NULL);

	e_soap_message_add_attribute (msg, "Key", entry_name, NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Street", NULL, contact_addr->street);
	e_ews_message_write_string_parameter (msg, "City", NULL, contact_addr->locality);
	e_ews_message_write_string_parameter (msg, "State", NULL, contact_addr->region);
	e_ews_message_write_string_parameter (msg, "PostalCode", NULL, contact_addr->code);

	e_soap_message_end_element (msg);
	e_contact_address_free (contact_addr);

	return TRUE;
}

static void
ebews_set_address (ESoapMessage *msg,
                   EContact *contact)
{
	gboolean include_hdr = TRUE;

	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_WORK, "Business", include_hdr))
		include_hdr = FALSE;
	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_HOME, "Home", include_hdr))
		include_hdr = FALSE;
	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_OTHER, "Other", include_hdr))
		include_hdr = FALSE;

	if (!include_hdr)
		e_soap_message_end_element (msg);
}

static void
ebews_set_ims (ESoapMessage *message,
               EContact *contact)
{

}

static void
ebews_set_notes (ESoapMessage *msg,
                 EContact *contact)
{
	gchar *notes = e_contact_get (contact, E_CONTACT_NOTE);
	if (!notes)
		return;

	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, notes, "BodyType", "Text");

	g_free (notes);
}

static void
ebews_set_emails (ESoapMessage *msg,
                  EContact *contact)
{
	const gchar *include_hdr = "EmailAddresses";

	if (add_entry (msg, contact, E_CONTACT_EMAIL_1, "EmailAddress1", include_hdr))
		include_hdr = NULL;
	if (add_entry (msg, contact, E_CONTACT_EMAIL_2, "EmailAddress2", include_hdr))
		include_hdr = NULL;
	if (add_entry (msg, contact, E_CONTACT_EMAIL_3, "EmailAddress3", include_hdr))
		include_hdr = NULL;

	if (!include_hdr)
		e_soap_message_end_element (msg);
}

static void
convert_contact_property_to_updatexml (ESoapMessage *msg,
                                       const gchar *name,
                                       const gchar *value,
                                       const gchar *prefix,
                                       const gchar *attr_name,
                                       const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Contact");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_indexed_contact_property_to_updatexml (ESoapMessage *message,
                                               const gchar *name,
                                               const gchar *value,
                                               const gchar *prefix,
                                               const gchar *element_name,
                                               const gchar *key)
{
	gboolean delete_field = FALSE;

	if (!value || g_strcmp0 (value, "") == 0)
		delete_field = TRUE;
	e_ews_message_start_set_indexed_item_field (message, name , prefix, "Contact", key, delete_field);

	if (!delete_field)
	{
		e_soap_message_start_element (message, element_name, NULL, NULL);
		e_ews_message_write_string_parameter_with_attribute (message, "Entry", NULL, value, "Key", key);
		e_soap_message_end_element (message);
	}
	e_ews_message_end_set_indexed_item_field (message, delete_field);
}

static void
ebews_set_full_name_changes (EBookBackendEws *bbews,
			     ESoapMessage *message,
			     EContact *new,
			     EContact *old,
			     gchar **out_new_change_key,
			     GCancellable *cancellable,
			     GError **error)
{
	EContactName *name, *old_name;

	if (!message)
		return;

	name = e_contact_get (new, E_CONTACT_NAME);
	old_name = e_contact_get (old, E_CONTACT_NAME);
	if (!old_name && !name)
		return;

	if (!old_name) {
		convert_contact_property_to_updatexml (message, "GivenName", name->given, "contacts", NULL, NULL);
		convert_contact_property_to_updatexml (message, "MiddleName", name->additional, "contacts", NULL, NULL);
	} else if (!name) {
		convert_contact_property_to_updatexml (message, "GivenName", "", "contacts", NULL, NULL);

		convert_contact_property_to_updatexml (message, "MiddleName", "", "contacts", NULL, NULL);
	} else {
		if (g_strcmp0 (name->given, old_name->given) != 0)
			convert_contact_property_to_updatexml (message, "GivenName", name->given, "contacts", NULL, NULL);
		if (g_strcmp0 (name->additional, old_name->additional) != 0)
			convert_contact_property_to_updatexml (message, "MiddleName", name->additional, "contacts", NULL, NULL);
	}

	e_contact_name_free (name);
	e_contact_name_free (old_name);

}

static void
ebews_set_birth_date_changes (EBookBackendEws *bbews,
			      ESoapMessage *message,
			      EContact *new,
			      EContact *old,
			      gchar **out_new_change_key,
			      GCancellable *cancellable,
			      GError **error)
{
	EContactDate *new_date, *old_date;
	gchar *birthday;

	if (!message)
		return;

	new_date = e_contact_get (new, E_CONTACT_BIRTH_DATE);
	old_date = e_contact_get (old, E_CONTACT_BIRTH_DATE);

	if (!e_contact_date_equal (new_date, old_date)) {
		birthday = g_strdup_printf (
			"%04d-%02d-%02dT00:00:00",
			new_date->year, new_date->month, new_date->day);

		convert_contact_property_to_updatexml (message, "Birthday", birthday, "contacts", NULL, NULL);
		g_free (birthday);
	}

	e_contact_date_free (new_date);
	e_contact_date_free (old_date);
}

static void
ebews_set_anniversary_changes (EBookBackendEws *bbews,
			       ESoapMessage *message,
			       EContact *new,
			       EContact *old,
			       gchar **out_new_change_key,
			       GCancellable *cancellable,
			       GError **error)
{

}

static void
set_photo (EBookBackendEws *bbews,
	   const EwsId *item_id,
	   EContact *contact,
	   EContactPhoto *photo,
	   gchar **new_change_key,
	   GCancellable *cancellable,
	   GError **error)
{
	EEwsAttachmentInfo *info;
	EwsId *id = NULL;
	GSList *files = NULL;
	const guchar *data;
	gsize len;

	if (!item_id) {
		id = g_new0 (EwsId, 1);
		id->id = e_contact_get (contact, E_CONTACT_UID);
		id->change_key = e_vcard_util_dup_x_attribute (E_VCARD (contact), X_EWS_CHANGEKEY);
		if (!id->change_key)
			id->change_key = e_contact_get (contact, E_CONTACT_REV);

		item_id = id;
	}

	data = e_contact_photo_get_inlined (photo, &len);

	info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_INLINED);
	e_ews_attachment_info_set_inlined_data (info, data, len);
	e_ews_attachment_info_set_mime_type (info, "image/jpeg");
	e_ews_attachment_info_set_filename (info, "ContactPicture.jpg");

	files = g_slist_append (files, info);

	e_ews_connection_create_attachments_sync (
			bbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			item_id,
			files,
			TRUE,
			new_change_key,
			NULL,
			cancellable,
			error);

	if (id) {
		g_free (id->change_key);
		g_free (id->id);
		g_free (id);
	}

	g_slist_free_full (files, (GDestroyNotify) e_ews_attachment_info_free);
}

static gboolean
ebb_ews_photo_changed (EBookMetaBackend *meta_backend,
		       EContact *old_contact,
		       EContact *new_contact,
		       GCancellable *cancellable)
{
	EContact *old_contact_copy = NULL;
	EContactPhoto *old_photo;
	EContactPhoto *new_photo;
	gboolean changed = FALSE;

	old_photo = e_contact_get (old_contact, E_CONTACT_PHOTO);
	new_photo = e_contact_get (new_contact, E_CONTACT_PHOTO);

	if (!old_photo && new_photo)
		changed = TRUE;

	if (old_photo && !new_photo)
		changed = TRUE;

	/* old_photo comes from cache, thus it's always URI (to local file or elsewhere),
	   while the new_photo is to be saved, which is always inlined. */
	if (!changed && old_photo && new_photo &&
	    old_photo->type == E_CONTACT_PHOTO_TYPE_URI &&
	    new_photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		e_contact_photo_free (old_photo);
		old_photo = NULL;

		old_contact_copy = e_contact_duplicate (old_contact);

		if (e_book_meta_backend_inline_local_photos_sync (meta_backend, old_contact_copy, cancellable, NULL))
			old_photo = e_contact_get (old_contact_copy, E_CONTACT_PHOTO);
	}

	if (old_photo && new_photo &&
	    old_photo->type == E_CONTACT_PHOTO_TYPE_INLINED &&
	    new_photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		guchar *old_data;
		guchar *new_data;
		gsize old_length;
		gsize new_length;

		old_data = old_photo->data.inlined.data;
		new_data = new_photo->data.inlined.data;

		old_length = old_photo->data.inlined.length;
		new_length = new_photo->data.inlined.length;

		changed =
			(old_length != new_length) ||
			(memcmp (old_data, new_data, old_length) != 0);
	}

	e_contact_photo_free (old_photo);
	e_contact_photo_free (new_photo);
	g_clear_object (&old_contact_copy);

	return changed;
}

static void
ebews_set_photo_changes (EBookBackendEws *bbews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 gchar **out_new_change_key,
			 GCancellable *cancellable,
			 GError **error)
{
	EContactPhoto *new_photo = NULL;
	EEwsAdditionalProps *add_props = NULL;
	GSList *contact_item_ids = NULL, *new_items = NULL, *attachments_ids = NULL;
	gchar *id = NULL;
	const gchar *contact_photo_id;
	gchar *new_change_key = NULL;

	/*
	 * Support for ContactPhoto was added in Exchange 2010 SP2.
	 * We don't want to try to set/get this property if we are running in older version of the server.
	 */
	if (!e_ews_connection_satisfies_server_version (bbews->priv->cnc, E_EWS_EXCHANGE_2010_SP2)) {
		return;
	}

	if (message) {
		/* Photo changes can be done only in pre-flight stage,
		   because it modifies ChangeKey */
		return;
	}

	if (!ebb_ews_photo_changed (E_BOOK_META_BACKEND (bbews), old, new, cancellable))
		return;

	new_photo = e_contact_get (new, E_CONTACT_PHOTO);
	id = e_contact_get (old, E_CONTACT_UID);

	add_props = e_ews_additional_props_new ();
	add_props->field_uri = g_strdup ("item:Attachments");

	contact_item_ids = g_slist_append (contact_item_ids, id);
	if (!e_ews_connection_get_items_sync (
			bbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			contact_item_ids,
			"IdOnly",
			add_props,
			FALSE,
			NULL,
			E_EWS_BODY_TYPE_TEXT,
			&new_items,
			NULL,
			NULL,
			cancellable,
			error))
		goto exit;

	contact_photo_id = e_ews_item_get_contact_photo_id (new_items->data);
	if (contact_photo_id) {
		attachments_ids = g_slist_prepend (attachments_ids, g_strdup (contact_photo_id));
		if (!e_ews_connection_delete_attachments_sync (
					bbews->priv->cnc,
					EWS_PRIORITY_MEDIUM,
					attachments_ids,
					&new_change_key,
					cancellable,
					error))
			goto exit;
	}

	if (new_photo) {
		EwsId *item_id = NULL;

		if (new_change_key) {
			item_id = g_new0 (EwsId, 1);
			item_id->id = e_contact_get (new, E_CONTACT_UID);
			item_id->change_key = new_change_key;

			new_change_key = NULL;
		}

		set_photo (bbews, item_id, new, new_photo, &new_change_key, cancellable, error);

		if (item_id) {
			if (!new_change_key) {
				new_change_key = item_id->change_key;
				item_id->change_key = NULL;
			}

			g_free (item_id->id);
			g_free (item_id->change_key);
			g_free (item_id);
		}
	}

 exit:
	e_ews_additional_props_free (add_props);
	e_contact_photo_free (new_photo);
	g_slist_free_full (contact_item_ids, g_free);
	g_slist_free_full (new_items, g_object_unref);
	g_slist_free_full (attachments_ids, g_free);

	if (new_change_key && out_new_change_key)
		*out_new_change_key = new_change_key;
	else
		g_free (new_change_key);
}

static void
ebews_set_phone_number_changes (EBookBackendEws *bbews,
				ESoapMessage *message,
				EContact *new,
				EContact *old,
				gchar **out_new_change_key,
				GCancellable *cancellable,
				GError **error)
{
	gint i;
	gchar *new_value, *old_value;

	if (!message)
		return;

	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++) {
		new_value = e_contact_get (new, phone_field_map[i].field);
		old_value = e_contact_get (old, phone_field_map[i].field);

		if (g_strcmp0 (new_value, old_value) != 0)
			convert_indexed_contact_property_to_updatexml (message, "PhoneNumber", new_value, "contacts", "PhoneNumbers", phone_field_map[i].element);

		g_free (new_value);
		g_free (old_value);
	}
}

static void
convert_indexed_contact_property_to_updatexml_physical_address (ESoapMessage *message,
                                                                const gchar *name,
                                                                const gchar *uri_element,
                                                                const gchar *value,
                                                                const gchar *prefix,
                                                                const gchar *element_name,
                                                                const gchar *key)
{
	gchar * fielduri = NULL;
	gboolean delete_field = FALSE;

	if (!value || g_strcmp0 (value, "") == 0)
		delete_field = TRUE;

	fielduri = g_strconcat (name, ":", uri_element, NULL);

	e_ews_message_start_set_indexed_item_field (message, fielduri , prefix, "Contact", key, delete_field);

	if (!delete_field)
	{
		e_soap_message_start_element (message, element_name, NULL, NULL);

		e_soap_message_start_element (message, "Entry", NULL, NULL);
		e_soap_message_add_attribute (message, "Key", key, NULL, NULL);
		e_ews_message_write_string_parameter (message, uri_element, NULL, value);
		e_soap_message_end_element (message);

		e_soap_message_end_element (message);
	}
	e_ews_message_end_set_indexed_item_field (message, delete_field);
}

static void
compare_address (ESoapMessage *message,
                 EContact *new,
                 EContact *old,
                 EContactField field,
                 const gchar *key)
{
	EContactAddress *new_address, *old_address;
	gboolean set = FALSE;

	new_address = e_contact_get (new, field);
	old_address = e_contact_get (old, field);

	if (!new_address && !old_address)
		return;

	if (!old_address && new_address)
		set = TRUE;

	if (!new_address && old_address)
	{
		set = TRUE;
		new_address = g_new0 (EContactAddress, 1);
	}

	if (set || g_strcmp0 (new_address->street, old_address->street) != 0)
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "Street", new_address->street, "contacts", "PhysicalAddresses", key);
	if (set || g_strcmp0 (new_address->locality, old_address->locality) != 0)
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "City", new_address->locality, "contacts", "PhysicalAddresses", key);
	if (set || g_strcmp0 (new_address->region, old_address->region) != 0)
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "State", new_address->region, "contacts", "PhysicalAddresses", key);
	if (set || g_strcmp0 (new_address->code, old_address->code) != 0)
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "PostalCode", new_address->code, "contacts", "PhysicalAddresses", key);

	e_contact_address_free (old_address);
	e_contact_address_free (new_address);
}

static void
ebews_set_address_changes (EBookBackendEws *bbews,
			   ESoapMessage *message,
			   EContact *new,
			   EContact *old,
			   gchar **out_new_change_key,
			   GCancellable *cancellable,
			   GError **error)
{
	if (!message)
		return;

	compare_address (message, new, old, E_CONTACT_ADDRESS_WORK, "Business");
	compare_address (message, new, old, E_CONTACT_ADDRESS_HOME, "Home");
	compare_address (message, new, old, E_CONTACT_ADDRESS_OTHER, "Other");
}

static void
ebews_set_im_changes (EBookBackendEws *bbews,
		      ESoapMessage *message,
		      EContact *new,
		      EContact *old,
		      gchar **out_new_change_key,
		      GCancellable *cancellable,
		      GError **error)
{

}

static void
ebews_set_notes_changes (EBookBackendEws *bbews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 gchar **out_new_change_key,
			 GCancellable *cancellable,
			 GError **error)
{
	gchar *old_notes, *new_notes;

	if (!message)
		return;

	old_notes = e_contact_get (old, E_CONTACT_NOTE);
	new_notes = e_contact_get (new, E_CONTACT_NOTE);

	if (g_strcmp0 (old_notes, new_notes) != 0) {
		convert_contact_property_to_updatexml (
				message, "Body", new_notes ? new_notes : "", "item", "BodyType", "Text");
	}

	g_free (old_notes);
	g_free (new_notes);
}

static void
ebews_set_email_changes (EBookBackendEws *bbews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 gchar **out_new_change_key,
			 GCancellable *cancellable,
			 GError **error)
{
	gchar *new_value, *old_value;

	if (!message)
		return;

	new_value = e_contact_get (new, E_CONTACT_EMAIL_1);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_1);
	if (g_strcmp0 (new_value, old_value) != 0)
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress1");
	g_free (new_value);
	g_free (old_value);

	new_value = e_contact_get (new, E_CONTACT_EMAIL_2);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_2);
	if (g_strcmp0 (new_value, old_value) != 0)
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress2");
	g_free (new_value);
	g_free (old_value);

	new_value = e_contact_get (new, E_CONTACT_EMAIL_3);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_3);
	if (g_strcmp0 (new_value, old_value) != 0)
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress3");
	g_free (new_value);
	g_free (old_value);
}

static void
ebews_populate_givenname (EBookBackendEws *bbews,
			  EContact *contact,
			  EEwsItem *item,
			  GCancellable *cancellable,
			  GError **error)
{
	const gchar *givenname;

	givenname = e_ews_item_get_givenname (item);
	if (givenname && *givenname)
		e_contact_set (contact, E_CONTACT_GIVEN_NAME, givenname);
}

static void
ebews_set_givenname (ESoapMessage *message,
		     EContact *contact)
{
	/* Does nothing, the "GivenName" is filled by the "FullName" code */
}

static void
ebews_set_givenname_changes (EBookBackendEws *bbews,
			     ESoapMessage *message,
			     EContact *new,
			     EContact *old,
			     gchar **out_new_change_key,
			     GCancellable *cancellable,
			     GError **error)
{
	/* Does nothing, the "GivenName" is filled by the "FullName" code */
}

static const gchar *
ebews_get_fileas_or_display_name (EEwsItem *item)
{
	const gchar *value;

	value = e_ews_item_get_fileas (item);
	if (!value || !*value)
		value = e_ews_item_get_display_name (item);

	return value;
}

static const struct field_element_mapping {
	EContactField field_id;
	gint element_type;
	const gchar *element_name;
	/* set function for simple string type values */
	const gchar * (*get_simple_prop_func) (EEwsItem *item);
	void (*populate_contact_func)(EBookBackendEws *bbews, EContact *contact, EEwsItem *item, GCancellable *cancellable, GError **error);
	void (*set_value_in_soap_message) (ESoapMessage *message, EContact *contact);
	void (*set_changes) (EBookBackendEws *bbews, ESoapMessage *message, EContact *new, EContact *old, gchar **out_new_change_key, GCancellable *cancellable, GError **error);

} mappings[] = {
	/* The order should be maintained for create contacts to work */
	{ E_CONTACT_NOTE, ELEMENT_TYPE_COMPLEX, "Notes", NULL, ebews_populate_notes, ebews_set_notes, ebews_set_notes_changes },
	{ E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "FileAs", ebews_get_fileas_or_display_name },
	{ E_CONTACT_FULL_NAME, ELEMENT_TYPE_COMPLEX, "CompleteName", NULL, ebews_populate_full_name, ebews_set_full_name, ebews_set_full_name_changes},
	{ E_CONTACT_NICKNAME, ELEMENT_TYPE_SIMPLE, "Nickname", NULL, ebews_populate_nick_name},
	{ E_CONTACT_ORG, ELEMENT_TYPE_SIMPLE, "CompanyName", e_ews_item_get_company_name},
	/* should take care of all email adresss fields */
	{ E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "EmailAddresses", NULL, ebews_populate_emails, ebews_set_emails, ebews_set_email_changes },
	/* should take care of home, work and other adresss fields */
	{ E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "PhysicalAddresses", NULL, ebews_populate_address, ebews_set_address, ebews_set_address_changes },
	/* should take care of all phone number fields */
	{ E_CONTACT_PHONE_PRIMARY, ELEMENT_TYPE_COMPLEX , "PhoneNumbers", NULL, ebews_populate_phone_numbers, ebews_set_phone_numbers, ebews_set_phone_number_changes},
	{ E_CONTACT_ASSISTANT, ELEMENT_TYPE_SIMPLE, "AssistantName", e_ews_item_get_assistant_name},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "Birthday", NULL,  ebews_populate_birth_date, ebews_set_birth_date, ebews_set_birth_date_changes },
	{ E_CONTACT_HOMEPAGE_URL, ELEMENT_TYPE_SIMPLE, "BusinessHomePage", e_ews_item_get_business_homepage},
	{ E_CONTACT_ORG_UNIT, ELEMENT_TYPE_SIMPLE, "Department", e_ews_item_get_department},
	/* should take care of all im fields */
	{ E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "ImAddresses", NULL, ebews_populate_ims, ebews_set_ims, ebews_set_im_changes },
	{ E_CONTACT_TITLE, ELEMENT_TYPE_SIMPLE, "JobTitle", e_ews_item_get_job_title},
	{ E_CONTACT_MANAGER, ELEMENT_TYPE_SIMPLE, "Manager", e_ews_item_get_manager},
	{ E_CONTACT_OFFICE, ELEMENT_TYPE_SIMPLE, "OfficeLocation", e_ews_item_get_office_location},
	{ E_CONTACT_SPOUSE, ELEMENT_TYPE_SIMPLE, "Profession", e_ews_item_get_profession},
	{ E_CONTACT_SPOUSE, ELEMENT_TYPE_SIMPLE, "SpouseName", e_ews_item_get_spouse_name},
	{ E_CONTACT_FAMILY_NAME, ELEMENT_TYPE_SIMPLE, "Surname", e_ews_item_get_surname},
	{ E_CONTACT_GIVEN_NAME, ELEMENT_TYPE_COMPLEX, "GivenName", NULL, ebews_populate_givenname, ebews_set_givenname, ebews_set_givenname_changes},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "WeddingAnniversary", NULL,  ebews_populate_anniversary, ebews_set_anniversary, ebews_set_anniversary_changes },
	{ E_CONTACT_PHOTO, ELEMENT_TYPE_COMPLEX, "Photo", NULL,  ebews_populate_photo, ebews_set_photo, ebews_set_photo_changes },

	/* Should take of uid and changekey (REV) */
	{ E_CONTACT_UID, ELEMENT_TYPE_COMPLEX, "ItemId", NULL,  ebews_populate_uid, ebews_set_item_id},
};

static void
ebb_ews_write_dl_members (ESoapMessage *msg,
			  EContact *contact)
{
	GSList *emails, *l;

	e_soap_message_start_element (msg, "Members", NULL, NULL);

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (l = emails; l; l = l->next) {
		CamelInternetAddress *addr;

		if (l->data == NULL)
			continue;

		addr = camel_internet_address_new ();
		if (camel_address_decode (CAMEL_ADDRESS (addr), l->data) > 0) {
			const gchar *name = NULL, *email = NULL;

			if (camel_internet_address_get (addr, 0, &name, &email) && email) {
				e_soap_message_start_element (msg, "Member", NULL, NULL);
				e_soap_message_start_element (msg, "Mailbox", NULL, NULL);
				e_ews_message_write_string_parameter (msg, "Name", NULL, name ? name : email);
				e_ews_message_write_string_parameter (msg, "EmailAddress", NULL, email);
				e_soap_message_end_element (msg); /* Mailbox */
				e_soap_message_end_element (msg); /* Member */
			}
		}
		g_object_unref (addr);
	}

	g_slist_free_full (emails, g_free);
	e_soap_message_end_element (msg); /* Members */
}

static void
ebb_ews_convert_dl_to_xml_cb (ESoapMessage *msg,
			      gpointer user_data)
{
	EContact *contact = user_data;
	EVCardAttribute *attribute;
	GList *values;

	/* Prepare DistributionList node in the SOAP message */
	e_soap_message_start_element (msg, "DistributionList", NULL, NULL);

	attribute = e_vcard_get_attribute (E_VCARD (contact), EVC_FN);
	values = e_vcard_attribute_get_values (attribute);
	e_ews_message_write_string_parameter (msg, "DisplayName", NULL, values->data);

	ebb_ews_write_dl_members (msg, contact);

	e_soap_message_end_element (msg); /* DistributionList */
}

static void
ebb_ews_convert_contact_to_xml_cb (ESoapMessage *msg,
				   gpointer user_data)
{
	EContact *contact = user_data;
	gint i, element_type;

	/* Prepare Contact node in the SOAP message */
	e_soap_message_start_element (msg, "Contact", NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE) {
			gchar *val;

			/* skip uid while creating contacts */
			if (mappings[i].field_id == E_CONTACT_UID)
				continue;

			val = e_contact_get (contact, mappings[i].field_id);
			if (val && *val)
				e_ews_message_write_string_parameter (msg, mappings[i].element_name, NULL, val);
			g_free (val);
		} else
			mappings[i].set_value_in_soap_message (msg, contact);
	}

	/* end of "Contact" */
	e_soap_message_end_element (msg);
}

typedef struct _ConvertData {
	EBookBackendEws *bbews;
	GCancellable *cancellable;
	GError **error;

	EContact *old_contact;
	EContact *new_contact;
	gchar *change_key;
} ConvertData;

static void
ebb_ews_convert_dl_to_updatexml_cb (ESoapMessage *msg,
				    gpointer user_data)
{
	ConvertData *cd = user_data;
	EContact *old_contact = cd->old_contact;
	EContact *new_contact = cd->new_contact;
	gchar *change_key = NULL;

	if (!cd->change_key) {
		change_key = e_vcard_util_dup_x_attribute (E_VCARD (old_contact), X_EWS_CHANGEKEY);
		if (!change_key)
			change_key = e_contact_get (old_contact, E_CONTACT_REV);
	}

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		e_contact_get_const (old_contact, E_CONTACT_UID),
		cd->change_key ? cd->change_key : change_key,
		0);
	e_ews_message_start_set_item_field (msg, "Members", "distributionlist", "DistributionList");
	ebb_ews_write_dl_members (msg, new_contact);
	e_ews_message_end_set_item_field (msg);
	e_ews_message_end_item_change (msg);

	g_free (change_key);
}

static void
ebb_ews_convert_contact_to_updatexml_cb (ESoapMessage *msg,
					 gpointer user_data)
{
	ConvertData *cd = user_data;
	EContact *old_contact = cd->old_contact;
	EContact *new_contact = cd->new_contact;
	gchar *value = NULL, *old_value = NULL;
	gchar *change_key = NULL;
	gint i, element_type;

	/* Pre-flight, to update the ChangeKey if needed */
	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_COMPLEX) {
			gchar *new_change_key = NULL;

			if (mappings[i].field_id == E_CONTACT_UID)
				continue;

			mappings[i].set_changes (cd->bbews, NULL, new_contact, old_contact, &new_change_key, cd->cancellable, cd->error);

			if (new_change_key) {
				g_free (cd->change_key);
				cd->change_key = new_change_key;
			}
		}
	}

	if (!cd->change_key) {
		change_key = e_vcard_util_dup_x_attribute (E_VCARD (old_contact), X_EWS_CHANGEKEY);
		if (!change_key)
			change_key = e_contact_get (old_contact, E_CONTACT_REV);
	}

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		e_contact_get_const (old_contact, E_CONTACT_UID),
		cd->change_key ? cd->change_key : change_key,
		0);

	/* Iterate for each field in contact */

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;
		if (element_type == ELEMENT_TYPE_SIMPLE)  {
			value =  e_contact_get (new_contact, mappings[i].field_id);
			old_value =  e_contact_get (old_contact, mappings[i].field_id);
			if (g_strcmp0 (value, old_value) != 0)
				convert_contact_property_to_updatexml (msg, mappings[i].element_name, value, "contacts", NULL, NULL);
			if (value)
				g_free (value);
			if (old_value)
				g_free (old_value);
		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			gchar *new_change_key = NULL;

			if (mappings[i].field_id == E_CONTACT_UID)
				continue;

			mappings[i].set_changes (cd->bbews, msg, new_contact, old_contact, &new_change_key, cd->cancellable, cd->error);

			if (new_change_key) {
				g_free (cd->change_key);
				cd->change_key = new_change_key;
			}
		}
	}

	e_ews_message_end_item_change (msg);

	g_free (change_key);
}

static EContact *
ebb_ews_item_to_contact (EBookBackendEws *bbews,
			 EEwsItem *item,
			 GCancellable *cancellable,
			 GError **error)
{
	EContact *contact;
	gint ii, element_type;

	contact = e_contact_new ();

	for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
		element_type = mappings[ii].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE && !mappings[ii].populate_contact_func) {
			const gchar *val = mappings[ii].get_simple_prop_func (item);

			if (val != NULL)
				e_contact_set (contact, mappings[ii].field_id, val);
		} else {
			mappings[ii].populate_contact_func (bbews, contact, item, cancellable, error);
		}
	}

	return contact;
}

static void
ebb_ews_items_to_contacts (EBookBackendEws *bbews,
			   const GSList *new_items,
			   GSList **contacts,
			   GCancellable *cancellable,
			   GError **error)
{
	GSList *link;

	for (link = (GSList *) new_items; link; link = g_slist_next (link)) {
		EContact *contact;
		EEwsItem *item = link->data;
		EVCardAttribute *attr;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		contact = ebb_ews_item_to_contact (bbews, item, cancellable, error);

		attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
		e_vcard_add_attribute_with_value (E_VCARD (contact), attr, "DT_MAILUSER");

		*contacts = g_slist_prepend (*contacts, contact);
	}
}

static void
ebb_ews_mailbox_to_contact (EBookBackendEws *bbews,
			    EContact **contact,
			    GHashTable *values,
			    const EwsMailbox *mb)
{
	CamelInternetAddress *addr;
	gchar *value;

	if (!mb->name && !mb->email)
		return;

	addr = camel_internet_address_new ();

	camel_internet_address_add (addr, mb->name, mb->email ? mb->email : "");
	value = camel_address_encode (CAMEL_ADDRESS (addr));

	if (value && (!values || g_hash_table_lookup (values, value) == NULL)) {
		EVCardAttribute *attr;

		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
		e_vcard_attribute_add_value (attr, value);
		e_vcard_append_attribute (E_VCARD (*contact), attr);

		if (values)
			g_hash_table_insert (values, g_strdup (value), GINT_TO_POINTER (1));
	} else {
		g_free (value);
	}

	g_object_unref (addr);
}

static gboolean
ebb_ews_traverse_dl (EBookBackendEws *bbews,
		     EContact **contact,
		     GHashTable *items,
		     GHashTable *values,
		     EwsMailbox *mb,
		     GCancellable *cancellable,
		     GError **error)
{
	if (g_strcmp0 (mb->mailbox_type, "PrivateDL") == 0 ||
	    g_strcmp0 (mb->mailbox_type, "PublicDL") == 0) {
		GSList *members = NULL, *l;
		gboolean includes_last;
		gboolean ret = FALSE;
		const gchar *ident;
		GError *local_error = NULL;

		if (mb->item_id && mb->item_id->id)
			ident = mb->item_id->id;
		else if (mb->email)
			ident = mb->email;
		else
			return FALSE;

		if (g_hash_table_lookup (items, ident) != NULL)
			return TRUE;

		g_hash_table_insert (items, g_strdup (ident), GINT_TO_POINTER (1));

		ret = e_ews_connection_expand_dl_sync (
			bbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			mb,
			&members,
			&includes_last,
			cancellable,
			&local_error);

		if (!ret) {
			if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NAMERESOLUTIONNORESULTS)) {
				g_clear_error (&local_error);
				if (mb->email && *mb->email)
					ebb_ews_mailbox_to_contact (bbews, contact, values, mb);

				ret = TRUE;
				members = NULL;
			} else {
				if (local_error)
					g_propagate_error (error, local_error);

				return FALSE;
			}
		}

		for (l = members; l; l = l->next) {
			ret = ebb_ews_traverse_dl (bbews, contact, items, values, l->data, cancellable, error);
			if (!ret)
				break;
		}

		g_slist_free_full (members, (GDestroyNotify) e_ews_mailbox_free);
		return ret;
	} else {
		ebb_ews_mailbox_to_contact (bbews, contact, values, mb);

		return TRUE;
	}
}

static EContact *
ebb_ews_get_dl_info (EBookBackendEws *bbews,
		     EEwsItem *item,
		     const EwsId *id,
		     const gchar *d_name,
		     GSList *members,
		     GCancellable *cancellable,
		     GError **error)
{
	GHashTable *items, *values;
	GSList *l;
	EContact *contact;

	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_UID, id->id);
	e_vcard_util_set_x_attribute (E_VCARD (contact), X_EWS_CHANGEKEY, id->change_key);
	ebews_populate_rev (contact, item);

	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_FULL_NAME, d_name);

	items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (l = members; l != NULL; l = l->next) {
		if (!ebb_ews_traverse_dl (bbews, &contact, items, values, l->data, cancellable, error)) {
			g_object_unref (contact);
			contact = NULL;
			goto exit;
		}
	}

 exit:
	g_hash_table_destroy (items);
	g_hash_table_destroy (values);

	return contact;
}

static gboolean
ebb_ews_get_dl_info_gal (EBookBackendEws *bbews,
			 EContact *contact,
			 EwsMailbox *mb,
			 GCancellable *cancellable,
			 GError **error)
{
	GHashTable *items, *values;
	gboolean success;

	items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	success = ebb_ews_traverse_dl (bbews, &contact, items, values, mb, cancellable, error);

	if (success) {
		e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
		e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));
	}

	g_hash_table_destroy (items);
	g_hash_table_destroy (values);

	return success;
}

static gboolean
ebb_ews_contacts_append_dl (EBookBackendEws *bbews,
			    EEwsItem *item,
			    const EwsId *id,
			    const gchar *d_name,
			    GSList *members,
			    GSList **contacts,
			    GCancellable *cancellable,
			    GError **error)
{
	EContact *contact;
	EVCardAttribute *attr;

	contact = ebb_ews_get_dl_info (bbews, item, id, d_name, members, cancellable, error);
	if (contact == NULL)
		return FALSE;

	attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, "DT_DISTLIST");

	*contacts = g_slist_prepend (*contacts, contact);

	return TRUE;
}

static gboolean
ebb_ews_fetch_items_sync (EBookBackendEws *bbews,
			  const GSList *items, /* EEwsItem * */
			  GSList **contacts,
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *contact_item_ids = NULL, *dl_ids = NULL, *link;
	GSList *new_items = NULL;
	gboolean ret = FALSE;

	for (link = (GSList *) items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType type = e_ews_item_get_item_type (item);

		if (type == E_EWS_ITEM_TYPE_CONTACT)
			contact_item_ids = g_slist_prepend (contact_item_ids, g_strdup (id->id));
		else if (type == E_EWS_ITEM_TYPE_GROUP) {
			/* store a list of EwsMailBox's in case of distribution lists */
			dl_ids = g_slist_prepend (dl_ids, g_strdup (id->id));
		}
	}

	/* TODO fetch attachments */
	if (contact_item_ids) {
		EEwsAdditionalProps *add_props;
		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (CONTACT_ITEM_PROPS);

		ret = e_ews_connection_get_items_sync (
			bbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			contact_item_ids, "Default", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &new_items, NULL, NULL,
			cancellable, error);

		e_ews_additional_props_free (add_props);

		if (!ret)
			goto cleanup;
	}

	if (new_items) {
		ebb_ews_items_to_contacts (bbews, new_items, contacts, cancellable, error);

		g_slist_free_full (new_items, g_object_unref);
		new_items = NULL;
	}

	/* Get the display names of the distribution lists */
	if (dl_ids) {
		if (!e_ews_connection_get_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM, dl_ids, "Default", NULL,
			FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &new_items, NULL, NULL, cancellable, error))
			goto cleanup;
	}

	for (link = new_items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const gchar *d_name;
		const EwsId *id;
		EwsMailbox *mb;
		GSList *members = NULL;
		gboolean includes_last;
		GError *local_error = NULL;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		id = e_ews_item_get_id (item);
		mb = g_new0 (EwsMailbox, 1);
		mb->item_id = (EwsId *) id;

		d_name = e_ews_item_get_subject (item);
		if (e_ews_connection_expand_dl_sync (
			bbews->priv->cnc, EWS_PRIORITY_MEDIUM, mb, &members,
			&includes_last, cancellable, &local_error)) {
			ret = ebb_ews_contacts_append_dl (bbews, item, id, d_name, members, contacts, cancellable, error);
			g_slist_free_full (members, (GDestroyNotify) e_ews_mailbox_free);
		} else {
			ret = g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NAMERESOLUTIONNORESULTS);
			if (ret) {
				g_clear_error (&local_error);
			} else if (local_error) {
				g_propagate_error (error, local_error);
			}
		}

		g_free (mb);

		if (!ret)
			goto cleanup;
	}

 cleanup:
	g_slist_free_full (new_items, g_object_unref);
	g_slist_free_full (contact_item_ids, g_free);
	g_slist_free_full (dl_ids, g_free);

	return ret;
}

static void
ebb_ews_server_notification_cb (EBookBackendEws *bbews,
				const GSList *events,
				EEwsConnection *cnc)
{
	GSList *link;
	gboolean update_folder = FALSE;

	g_return_if_fail (E_IS_BOOK_BACKEND_EWS (bbews));

	for (link = (GSList *) events; link && !update_folder; link = g_slist_next (link)) {
		EEwsNotificationEvent *event = link->data;

		switch (event->type) {
			case E_EWS_NOTIFICATION_EVENT_CREATED:
			case E_EWS_NOTIFICATION_EVENT_DELETED:
			case E_EWS_NOTIFICATION_EVENT_MODIFIED:
				g_rec_mutex_lock (&bbews->priv->cnc_lock);

				if (g_strcmp0 (event->folder_id, bbews->priv->folder_id) == 0)
					update_folder = TRUE;

				g_rec_mutex_unlock (&bbews->priv->cnc_lock);
				break;
			case E_EWS_NOTIFICATION_EVENT_MOVED:
			case E_EWS_NOTIFICATION_EVENT_COPIED:
				g_rec_mutex_lock (&bbews->priv->cnc_lock);

				if (g_strcmp0 (event->folder_id, bbews->priv->folder_id) == 0 ||
				    g_strcmp0 (event->old_folder_id, bbews->priv->folder_id) == 0)
					update_folder = TRUE;

				g_rec_mutex_unlock (&bbews->priv->cnc_lock);
				break;
			default:
				return;
		}
	}

	if (update_folder)
		e_book_meta_backend_schedule_refresh (E_BOOK_META_BACKEND (bbews));
}

static void
ebb_ews_unset_connection (EBookBackendEws *bbews)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_EWS (bbews));

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	if (bbews->priv->cnc) {
		e_ews_connection_set_disconnected_flag (bbews->priv->cnc, TRUE);

		g_signal_handlers_disconnect_by_func (bbews->priv->cnc, ebb_ews_server_notification_cb, bbews);

		if (bbews->priv->subscription_key != 0) {
			e_ews_connection_disable_notifications_sync (
				bbews->priv->cnc,
				bbews->priv->subscription_key);
			bbews->priv->subscription_key = 0;
		}
	}

	g_clear_object (&bbews->priv->cnc);

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);
}

static gint
det_sort_func (gconstpointer _a,
	       gconstpointer _b)
{
	const EwsOALDetails *a = _a, *b = _b;

	return a->seq - b->seq;
}

static gchar *
ebb_ews_download_gal_file (EBookBackendEws *bbews,
			   EwsOALDetails *full,
			   GCancellable *cancellable,
			   GError **error)
{
	EEwsConnection *oab_cnc;
	gchar *full_url, *oab_url;
	gchar *download_path = NULL;
	gchar *password;
	CamelEwsSettings *ews_settings;
	const gchar *cache_dir;

	ews_settings = ebb_ews_get_collection_settings (bbews);

	/* oab url with oab.xml removed from the suffix */
	oab_url = camel_ews_settings_dup_oaburl (ews_settings);
	if (!oab_url || !*oab_url) {
		g_free (oab_url);
		return NULL;
	}

	if (g_str_has_suffix (oab_url, "oab.xml"))
		oab_url [strlen (oab_url) - 7] = '\0';

	full_url = g_strconcat (oab_url, full->filename, NULL);
	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (bbews));
	download_path = g_build_filename (cache_dir, full->filename, NULL);

	oab_cnc = e_ews_connection_new_for_backend (E_BACKEND (bbews), e_book_backend_get_registry (E_BOOK_BACKEND (bbews)), full_url, ews_settings);

	e_binding_bind_property (
		bbews, "proxy-resolver",
		oab_cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	password = e_ews_connection_dup_password (bbews->priv->cnc);
	e_ews_connection_set_password (oab_cnc, password);
	g_free (password);

	if (!e_ews_connection_download_oal_file_sync (oab_cnc, download_path, NULL, NULL, cancellable, error)) {
		g_free (download_path);
		download_path = NULL;
	} else {
		d (printf ("OAL file downloaded %s\n", download_path));
	}

	g_object_unref (oab_cnc);
	g_free (oab_url);
	g_free (full_url);

	return download_path;
}

static gchar *
ebb_ews_download_full_gal (EBookBackendEws *bbews,
			   EwsOALDetails *full,
			   GCancellable *cancellable,
			   GError **error)
{
	ESource *source;
	const gchar *cache_dir;
	gchar *lzx_path, *oab_file, *oab_path;

	lzx_path = ebb_ews_download_gal_file (bbews, full, cancellable, error);
	if (!lzx_path)
		return NULL;

	source = e_backend_get_source (E_BACKEND (bbews));
	oab_file = g_strdup_printf ("%s-%d.oab", e_source_get_display_name (source), full->seq);
	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (bbews));
	oab_path = g_build_filename (cache_dir, oab_file, NULL);

	if (!ews_oab_decompress_full (lzx_path, oab_path, error)) {
		g_free (oab_path);
		oab_path = NULL;
	} else {
		d (printf ("OAL file decompressed %s\n", oab_path));
	}

	if (lzx_path) {
		g_unlink (lzx_path);
		g_free (lzx_path);
	}

	g_free (oab_file);

	return oab_path;
}

static gchar *
ebb_ews_download_gal (EBookBackendEws *bbews,
		      EBookCache *book_cache,
		      EwsOALDetails *full,
		      GSList *deltas,
		      guint32 seq,
		      GCancellable *cancellable,
		      GError **error)
{
#ifdef WITH_MSPACK
	GSList *link;
	gchar *thisoab;

	thisoab = e_cache_dup_key (E_CACHE (book_cache), "oab-filename", NULL);
	if (!thisoab)
		goto full;

	for (link = deltas; link; link = g_slist_next (link)) {
		EwsOALDetails *det = link->data;
		ESource *source;
		gchar *oab_file, *lzx_path, *nextoab;
		const gchar *cache_dir;
		GError *local_error = NULL;

		seq++;
		if (det->seq != seq)
			break;

		lzx_path = ebb_ews_download_gal_file (bbews, det, cancellable, NULL);
		if (!lzx_path)
			break;

		source = e_backend_get_source (E_BACKEND (bbews));
		oab_file = g_strdup_printf ("%s-%d.oab", e_source_get_display_name (source), seq);
		cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (bbews));
		nextoab = g_build_filename (cache_dir, oab_file, NULL);
		g_free (oab_file);

		ews_oab_decompress_patch (lzx_path, thisoab, nextoab, &local_error);

		/* Free the LZX file */
		g_unlink (lzx_path);
		g_free (lzx_path);

		/* Free the previous OAB file */
		g_unlink (thisoab);
		g_free (thisoab);

		thisoab = nextoab;

		/* For once we are *allowed* to use the error instead of having to
		 * check the return value of the function. It's our *own* error. */
		if (local_error) {
			d (printf ("Failed to apply incremental patch: %s\n", local_error->message));
			g_error_free (local_error);
			break;
		}

		d (printf ("Created %s from delta\n", thisoab));

		if (seq == full->seq)
			return thisoab;
	}

	if (thisoab) {
		g_unlink (thisoab);
		g_free (thisoab);
	}
 full:
#endif /* WITH_MSPACK */
	d (printf ("Ewsgal: Downloading full gal \n"));
	return ebb_ews_download_full_gal (bbews, full, cancellable, error);
}

static void
ebb_ews_remove_old_gal_file (EBookCache *book_cache)
{
	gchar *filename;

	g_return_if_fail (E_IS_BOOK_CACHE (book_cache));

	filename = e_cache_dup_key (E_CACHE (book_cache), "oab-filename", NULL);

	if (filename)
		g_unlink (filename);
	g_free (filename);
}

struct _db_data {
	GHashTable *uids;
	GHashTable *sha1s;
	gint unchanged;
	gint changed;
	gint added;
	gint percent;
	GSList *created_objects;
	GSList *modified_objects;
};

static gboolean
ebb_ews_gal_filter_contact (goffset offset,
			    const gchar *sha1,
			    gpointer user_data,
			    GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;
	gchar *uid;

	/* Is there an existing identical record, with the same SHA1? */
	uid = g_hash_table_lookup (data->sha1s, sha1);
	if (!uid)
		return TRUE;

	/* Remove it from the hash tables so it doesn't get deleted at the end. */
	g_hash_table_remove (data->sha1s, sha1);
	g_hash_table_remove (data->uids, uid);
	data->unchanged++;

	/* Don't bother to parse and process this record. */
	return FALSE;
}

static void
ebb_ews_gal_store_contact (EContact *contact,
			   goffset offset,
			   const gchar *sha1,
			   guint percent,
			   gpointer user_data,
			   GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;

	if (contact) {
		const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);
		EBookMetaBackendInfo *nfo;

		ebews_populate_rev (contact, NULL);
		e_vcard_util_set_x_attribute (E_VCARD (contact), X_EWS_GAL_SHA1, sha1);

		nfo = e_book_meta_backend_info_new (uid, e_contact_get_const (contact, E_CONTACT_REV), NULL, NULL);
		nfo->object = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		if (g_hash_table_remove (data->uids, uid)) {
			data->changed++;
			data->modified_objects = g_slist_prepend (data->modified_objects, nfo);
		} else {
			data->added++;
			data->created_objects = g_slist_prepend (data->created_objects, nfo);
		}
	}

	if (data->percent != percent) {
		data->percent = percent;

		d (printf ("GAL processing contacts, %d%% complete (%d added, %d changed, %d unchanged\n",
			   percent, data->added, data->changed, data->unchanged));
	}
}

static gboolean
ebb_ews_gather_existing_uids_cb (EBookCache *book_cache,
				 const gchar *uid,
				 const gchar *revision,
				 const gchar *object,
				 const gchar *extra,
				 EOfflineState offline_state,
				 gpointer user_data)
{
	struct _db_data *data = user_data;
	EVCard *vcard;
	gchar *dup_uid, *dup_sha1 = NULL;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data->uids != NULL, FALSE);
	g_return_val_if_fail (data->sha1s != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	vcard = e_vcard_new_from_string (object);
	if (vcard) {
		dup_sha1 = e_vcard_util_dup_x_attribute (vcard, X_EWS_GAL_SHA1);
		g_object_unref (vcard);
	}

	dup_uid = g_strdup (uid);
	if (!dup_sha1)
		dup_sha1 = g_strdup (revision);

	g_hash_table_insert (data->uids, dup_uid, dup_sha1);
	if (dup_sha1)
		g_hash_table_insert (data->sha1s, dup_sha1, dup_uid);

	return TRUE;
}

static gboolean
ebb_ews_check_gal_changes (EBookBackendEws *bbews,
			   EBookCache *book_cache,
			   const gchar *filename,
			   GSList **out_created_objects, /*EBookMetaBackendInfo * */
			   GSList **out_modified_objects, /*EBookMetaBackendInfo * */
			   GSList **out_removed_objects, /*EBookMetaBackendInfo * */
			   GCancellable *cancellable,
			   GError **error)
{
	EwsOabDecoder *eod;
	gboolean success = TRUE;
	struct _db_data data;
#if d(1) + 0
	gint64 t1, t2;
#endif
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (bbews), FALSE);
	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	data.created_objects = NULL;
	data.modified_objects = NULL;
	data.unchanged = data.changed = data.added = 0;
	data.percent = 0;
	data.uids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	data.sha1s = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	d (t1 = g_get_monotonic_time ());

	e_book_cache_search_with_callback (book_cache, NULL, ebb_ews_gather_existing_uids_cb, &data, cancellable, NULL);

	eod = ews_oab_decoder_new (filename, bbews->priv->attachments_dir, &local_error);
	if (!local_error) {
		GHashTableIter iter;
		gpointer key;

		success = ews_oab_decoder_decode (eod, ebb_ews_gal_filter_contact, ebb_ews_gal_store_contact, &data, cancellable, &local_error);

		if (success) {
			*out_created_objects = data.created_objects;
			*out_modified_objects = data.modified_objects;
			*out_removed_objects = NULL;

			g_hash_table_iter_init (&iter, data.uids);
			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				const gchar *uid = key;

				*out_removed_objects = g_slist_prepend (*out_removed_objects,
					e_book_meta_backend_info_new (uid, NULL, NULL, NULL));
			}
		} else {
			g_slist_free_full (data.created_objects, e_book_meta_backend_info_free);
			g_slist_free_full (data.modified_objects, e_book_meta_backend_info_free);
		}
	} else {
		success = FALSE;
	}

	d (t2 = g_get_monotonic_time ());
	d (printf ("GAL update completed %ssuccessfully in %" G_GINT64_FORMAT " µs. Added: %d, Changed: %d, Unchanged %d, Removed: %d (%s)\n",
		   success ? "" : "un", (gint64) (t2 - t1), data.added, data.changed, data.unchanged, g_hash_table_size (data.uids),
		   local_error ? local_error->message : "no error"));

	g_hash_table_destroy (data.sha1s);
	g_hash_table_destroy (data.uids);

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static void
ebb_ews_remove_original_vcard (EContact *contact)
{
	g_return_if_fail (E_IS_CONTACT (contact));

	e_vcard_remove_attributes (E_VCARD (contact), NULL, X_EWS_ORIGINAL_VCARD);
}

static void
ebb_ews_store_original_vcard (EContact *contact)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	gchar *vcard_str;

	g_return_if_fail (E_IS_CONTACT (contact));

	ebb_ews_remove_original_vcard (contact);

	vcard = E_VCARD (contact);

	vcard_str = e_vcard_to_string (vcard, EVC_FORMAT_VCARD_30);

	attr = e_vcard_attribute_new ("", X_EWS_ORIGINAL_VCARD);
	e_vcard_attribute_add_value (attr, vcard_str);
	e_vcard_add_attribute (vcard, attr);

	g_free (vcard_str);
}

static const gchar *
ebb_ews_get_original_vcard (EContact *contact)
{
	EVCardAttribute *attr;
	GList *values = NULL;
	const gchar *vcard;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	attr = e_vcard_get_attribute (E_VCARD (contact), X_EWS_ORIGINAL_VCARD);
	if (!attr)
		return NULL;

	values = e_vcard_attribute_get_values (attr);
	if (!values)
		return NULL;

	vcard = values->data;

	if (vcard && *vcard)
		return vcard;

	return NULL;
}

typedef struct {
	/* For future use */
	gpointer restriction;

	gboolean is_autocompletion;
	gchar *auto_comp_str;
} EBookBackendEwsSExpData;

static ESExpResult *
ebb_ews_func_not (ESExp *f,
		  gint argc,
		  ESExpResult **argv,
		  gpointer data)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_UNDEFINED) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
ebb_ews_func_and_or (ESExp *f,
		     gint argc,
		     ESExpResult **argv,
		     gpointer and)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

/* TODO implement */
static ESExpResult *
ebb_ews_func_is (struct _ESExp *f,
		 gint argc,
		 struct _ESExpResult **argv,
		 gpointer data)
{
	ESExpResult *r;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

/* TODO implement */
static ESExpResult *
ebb_ews_func_endswith (struct _ESExp *f,
		       gint argc,
		       struct _ESExpResult **argv,
		       gpointer data)
{
	ESExpResult *r;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;

}

/* TODO implement */
static ESExpResult *
ebb_ews_func_contains (struct _ESExp *f,
		       gint argc,
		       struct _ESExpResult **argv,
		       gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;
	const gchar *propname, *str;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp (propname, "full_name") || !strcmp (propname, "email")) {
		if (!sdata->auto_comp_str) {
			sdata->auto_comp_str = g_strdup (str);
			sdata->is_autocompletion = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;

}

/* We are just handling for autocompletion now. We need to support other fields after implementing
 * Restrictions and find_items request */
static ESExpResult *
ebb_ews_func_beginswith (struct _ESExp *f,
			 gint argc,
			 struct _ESExpResult **argv,
			 gpointer data)
{
	ESExpResult *r;
	const gchar *propname, *str;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp (propname, "full_name") || !strcmp (propname, "email")) {
		if (!sdata->auto_comp_str) {
			sdata->auto_comp_str = g_strdup (str);
			sdata->is_autocompletion = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;
	return r;
}

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint flags;
} symbols[] = {
	{ "and", ebb_ews_func_and_or, 0 },
	{ "or", ebb_ews_func_and_or, 0},
	{ "not", ebb_ews_func_not, 0 },
	{ "contains", ebb_ews_func_contains, 0},
	{ "is", ebb_ews_func_is, 0},
	{ "beginswith", ebb_ews_func_beginswith, 0},
	{ "endswith", ebb_ews_func_endswith, 0},
};

/* FIXME  build a complete filter from the query that can be used by find_items */
static gboolean
ebb_ews_build_restriction (const gchar *query,
			   gchar **auto_comp_str)
{
	ESExpResult *r;
	ESExp *sexp;
	EBookBackendEwsSExpData *sdata;
	gboolean autocompletion = FALSE;
	gint i;

	*auto_comp_str = NULL;

	sexp = e_sexp_new ();
	sdata = g_new0 (EBookBackendEwsSExpData, 1);
	sdata->is_autocompletion = FALSE;

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		e_sexp_add_function (
			sexp, 0, (gchar *) symbols[i].name,
			symbols[i].func,
			sdata);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (r) {
		autocompletion = sdata->is_autocompletion;
		if (autocompletion)
			*auto_comp_str = sdata->auto_comp_str;
		else
			g_free (sdata->auto_comp_str);
	}

	e_sexp_result_free (sexp, r);
	g_object_unref (sexp);
	g_free (sdata);

	return autocompletion && *auto_comp_str;
}

static gboolean
ebb_ews_update_cache_for_expression (EBookBackendEws *bbews,
				     const gchar *expr,
				     GCancellable *cancellable,
				     GError **error)
{
	EBookMetaBackend *meta_backend;
	CamelEwsSettings *ews_settings;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (bbews), FALSE);

	/* Resolve names in GAL only for GAL */
	if (!bbews->priv->is_gal)
		return TRUE;

	ews_settings = ebb_ews_get_collection_settings (bbews);

	if (camel_ews_settings_get_oab_offline (ews_settings))
		return TRUE;

	meta_backend = E_BOOK_META_BACKEND (bbews);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	/* Search only if not searching for everything */
	if (expr && *expr && g_ascii_strcasecmp (expr, "(contains \"x-evolution-any-field\" \"\")") != 0) {
		gchar *restriction_expr = NULL;
		GSList *mailboxes = NULL, *contacts = NULL, *found_infos = NULL;
		gboolean includes_last_item = TRUE;

		success = ebb_ews_build_restriction (expr, &restriction_expr) &&
			e_book_meta_backend_ensure_connected_sync (meta_backend, cancellable, error) &&
			e_ews_connection_resolve_names_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM, restriction_expr,
				EWS_SEARCH_AD, NULL, TRUE, &mailboxes, &contacts, &includes_last_item, cancellable, error);

		if (success) {
			GSList *mlink, *clink;

			for (mlink = mailboxes, clink = contacts; mlink; mlink = g_slist_next (mlink), clink = g_slist_next (clink)) {
				EwsMailbox *mb = mlink->data;
				EEwsItem *contact_item = clink ? clink->data : NULL;
				EBookMetaBackendInfo *nfo;
				EContact *contact = NULL;
				gboolean is_public_dl = FALSE;
				const gchar *str;

				if (g_strcmp0 (mb->mailbox_type, "PublicDL") == 0) {
					contact = e_contact_new ();

					if (!ebb_ews_get_dl_info_gal (bbews, contact, mb, cancellable, NULL)) {
						g_clear_object (&contact);
					} else {
						is_public_dl = TRUE;
					}
				}

				if (!contact && contact_item && e_ews_item_get_item_type (contact_item) == E_EWS_ITEM_TYPE_CONTACT)
					contact = ebb_ews_item_to_contact (bbews, contact_item, cancellable, NULL);

				if (!contact)
					contact = e_contact_new ();

				/* We do not get an id from the server, so just using email_id as uid for now */
				e_contact_set (contact, E_CONTACT_UID, mb->email);

				/* There is no ChangeKey provided either, thus make up some revision,
				   to have the contact always updated in the local cache. */
				ebews_populate_rev (contact, NULL);

				str = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
				if (!str || !*str)
					e_contact_set (contact, E_CONTACT_FULL_NAME, mb->name);

				str = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
				if (!str || !*str || (!is_public_dl && contact_item && e_ews_item_get_item_type (contact_item) == E_EWS_ITEM_TYPE_CONTACT)) {
					/* Cleanup first, then re-add only SMTP addresses */
					e_contact_set (contact, E_CONTACT_EMAIL_1, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_2, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_3, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_4, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL, NULL);

					ebews_populate_emails_ex (bbews, contact, contact_item, TRUE);
				}

				str = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
				if (!str || !*str) {
					e_contact_set (contact, E_CONTACT_EMAIL_1, mb->email);
				} else if (!is_public_dl && mb->email && (!mb->routing_type || g_ascii_strcasecmp (mb->routing_type, "SMTP") == 0)) {
					EContactField fields[3] = { E_CONTACT_EMAIL_2, E_CONTACT_EMAIL_3, E_CONTACT_EMAIL_4 };
					gchar *emails[3];
					gint ii, ff = 0;

					emails[0] = e_contact_get (contact, E_CONTACT_EMAIL_1);
					emails[1] = e_contact_get (contact, E_CONTACT_EMAIL_2);
					emails[2] = e_contact_get (contact, E_CONTACT_EMAIL_3);

					/* Make the mailbox email the primary email and skip duplicates */
					e_contact_set (contact, E_CONTACT_EMAIL_1, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_2, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_3, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL_4, NULL);
					e_contact_set (contact, E_CONTACT_EMAIL, NULL);

					e_contact_set (contact, E_CONTACT_EMAIL_1, mb->email);

					for (ii = 0; ii < 3; ii++) {
						if (emails[ii] && g_ascii_strcasecmp (emails[ii], mb->email) != 0) {
							e_contact_set (contact, fields[ff], emails[ii]);
							ff++;
						}

						g_free (emails[ii]);
					}
				}

				ebb_ews_store_original_vcard (contact);

				nfo = e_book_meta_backend_info_new (e_contact_get_const (contact, E_CONTACT_UID),
					e_contact_get_const (contact, E_CONTACT_REV), NULL, NULL);
				nfo->object = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

				found_infos = g_slist_prepend (found_infos, nfo);

				g_object_unref (contact);
			}
		}

		g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);
		e_util_free_nullable_object_slist (contacts);

		if (success) {
			GSList *created_objects = NULL, *modified_objects = NULL;

			success = e_book_meta_backend_split_changes_sync (meta_backend, found_infos, &created_objects,
				&modified_objects, NULL, cancellable, error);
			if (success)
				success = e_book_meta_backend_process_changes_sync (meta_backend, created_objects,
					modified_objects, NULL, cancellable, error);

			g_slist_free_full (created_objects, e_book_meta_backend_info_free);
			g_slist_free_full (modified_objects, e_book_meta_backend_info_free);
		}

		g_slist_free_full (found_infos, e_book_meta_backend_info_free);
		g_free (restriction_expr);
	}

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	ebb_ews_convert_error_to_edb_error (error);
	ebb_ews_maybe_disconnect_sync (bbews, error, cancellable);

	return success;
}

static GSList * /* the possibly modified 'in_items' */
ebb_ews_verify_changes (EBookCache *book_cache,
			GSList *in_items, /* EEwsItem * */
			GCancellable *cancellable)
{
	GSList *items = NULL, *link;

	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), in_items);

	for (link = in_items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType type = e_ews_item_get_item_type (item);

		if (!g_cancellable_is_cancelled (cancellable) && (
		    type == E_EWS_ITEM_TYPE_CONTACT ||
		    type == E_EWS_ITEM_TYPE_GROUP)) {
			EContact *existing = NULL;

			if (e_book_cache_get_contact (book_cache, id->id, TRUE, &existing, cancellable, NULL) &&
			    existing) {
				gchar *change_key;

				change_key = e_vcard_util_dup_x_attribute (E_VCARD (existing), X_EWS_CHANGEKEY);
				if (!change_key)
					change_key = e_contact_get (existing, E_CONTACT_REV);

				if (g_strcmp0 (change_key, id->change_key) == 0) {
					g_object_unref (item);
				} else {
					items = g_slist_prepend (items, item);
				}

				g_free (change_key);
			} else {
				items = g_slist_prepend (items, item);
			}

			g_clear_object (&existing);
		} else {
			items = g_slist_prepend (items, item);
		}
	}

	g_slist_free (in_items);

	return items;
}

static GSList * /* EBookMetaBackendInfo */
ebb_ews_contacts_to_infos (const GSList *contacts) /* EContact * */
{
	GSList *nfos = NULL, *link;

	for (link = (GSList *) contacts; link; link = g_slist_next (link)) {
		EContact *contact = link->data;
		EBookMetaBackendInfo *nfo;

		if (!E_IS_CONTACT (contact))
			continue;

		ebb_ews_store_original_vcard (contact);

		nfo = e_book_meta_backend_info_new (
			e_contact_get_const (contact, E_CONTACT_UID),
			e_contact_get_const (contact, E_CONTACT_REV),
			NULL, NULL);
		nfo->object = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		nfos = g_slist_prepend (nfos, nfo);
	}

	return nfos;
}

static gboolean
ebb_ews_connect_sync (EBookMetaBackend *meta_backend,
		      const ENamedParameters *credentials,
		      ESourceAuthenticationResult *out_auth_result,
		      gchar **out_certificate_pem,
		      GTlsCertificateFlags *out_certificate_errors,
		      GCancellable *cancellable,
		      GError **error)
{
	EBookBackendEws *bbews;
	CamelEwsSettings *ews_settings;
	gchar *hosturl;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	if (bbews->priv->cnc) {
		g_rec_mutex_unlock (&bbews->priv->cnc_lock);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	}

	ews_settings = ebb_ews_get_collection_settings (bbews);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);

	bbews->priv->cnc = e_ews_connection_new_for_backend (E_BACKEND (bbews), e_book_backend_get_registry (E_BOOK_BACKEND (bbews)), hosturl, ews_settings);

	e_binding_bind_property (
		bbews, "proxy-resolver",
		bbews->priv->cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	*out_auth_result = e_ews_connection_try_credentials_sync (bbews->priv->cnc, credentials, cancellable, error);

	if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		ESource *source = e_backend_get_source (E_BACKEND (bbews));
		ESourceEwsFolder *ews_folder;
		gchar *gal_uid;

		ews_folder = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);

		g_free (bbews->priv->folder_id);
		bbews->priv->folder_id = e_source_ews_folder_dup_id (ews_folder);

		gal_uid = camel_ews_settings_dup_gal_uid (ews_settings);
		bbews->priv->is_gal = g_strcmp0 (e_source_get_uid (source), gal_uid) == 0;

		g_free (gal_uid);

		g_signal_connect_swapped (bbews->priv->cnc, "server-notification",
			G_CALLBACK (ebb_ews_server_notification_cb), bbews);

		if (!bbews->priv->is_gal &&
		    camel_ews_settings_get_listen_notifications (ews_settings) &&
		    e_ews_connection_satisfies_server_version (bbews->priv->cnc, E_EWS_EXCHANGE_2010_SP1)) {
			GSList *folders = NULL;

			folders = g_slist_prepend (folders, bbews->priv->folder_id);

			e_ews_connection_enable_notifications_sync (bbews->priv->cnc,
				folders, &bbews->priv->subscription_key);

			g_slist_free (folders);
		}

		e_book_backend_set_writable (E_BOOK_BACKEND (bbews), !bbews->priv->is_gal);
		success = TRUE;
	} else {
		ebb_ews_convert_error_to_edb_error (error);
		g_clear_object (&bbews->priv->cnc);
	}

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	g_free (hosturl);

	return success;
}

static gboolean
ebb_ews_disconnect_sync (EBookMetaBackend *meta_backend,
			 GCancellable *cancellable,
			 GError **error)
{
	EBookBackendEws *bbews;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	ebb_ews_unset_connection (bbews);

	return TRUE;
}

static gboolean
ebb_ews_get_changes_sync (EBookMetaBackend *meta_backend,
			  const gchar *last_sync_tag,
			  gboolean is_repeat,
			  gchar **out_new_sync_tag,
			  gboolean *out_repeat,
			  GSList **out_created_objects,
			  GSList **out_modified_objects,
			  GSList **out_removed_objects,
			  GCancellable *cancellable,
			  GError **error)
{
	EBookBackendEws *bbews;
	EBookCache *book_cache;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), FALSE);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	if (bbews->priv->is_gal) {
		CamelEwsSettings *ews_settings;
		gchar *oab_url;

		ews_settings = ebb_ews_get_collection_settings (bbews);
		oab_url = camel_ews_settings_dup_oaburl (ews_settings);

		if (oab_url && *oab_url &&
		    camel_ews_settings_get_oab_offline (ews_settings)) {
			EEwsConnection *oab_cnc;
			GSList *full_l = NULL, *deltas = NULL, *link;
			EwsOALDetails *full = NULL;
			gchar *password, *etag = NULL;
			gint sequence;

			sequence = e_cache_get_key_int (E_CACHE (book_cache), "gal-sequence", NULL);
			if (sequence == -1)
				sequence = 0;

			oab_cnc = e_ews_connection_new_for_backend (E_BACKEND (bbews), e_book_backend_get_registry (E_BOOK_BACKEND (bbews)), oab_url, ews_settings);

			e_binding_bind_property (
				bbews, "proxy-resolver",
				oab_cnc, "proxy-resolver",
				G_BINDING_SYNC_CREATE);

			password = e_ews_connection_dup_password (bbews->priv->cnc);
			e_ews_connection_set_password (oab_cnc, password);
			e_util_safe_free_string (password);

			d (printf ("Ewsgal: Fetching oal full details file\n"));
			if (!e_ews_connection_get_oal_detail_sync (oab_cnc, bbews->priv->folder_id, NULL, last_sync_tag, &full_l, &etag, cancellable, &local_error)) {
				if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_NOT_MODIFIED)) {
					g_clear_error (&local_error);
				} else {
					success = FALSE;
				}
			}

			if (success && full_l) {
				guint32 delta_size = 0;

				for (link = full_l; link; link = g_slist_next (link)) {
					EwsOALDetails *det = link->data;

					/* Throw away anything older than we already have */
					if (det->seq <= sequence) {
						ews_oal_details_free (det);
					} else if (!g_strcmp0 (det->type, "Full")) {
						if (full)
							ews_oal_details_free (full);
						full = det;
					} else if (sequence > 0 && !g_strcmp0 (det->type, "Diff")) {
						delta_size += det->size;
						deltas = g_slist_insert_sorted (deltas, det, det_sort_func);
					} else {
						ews_oal_details_free (det);
					}
				}

				g_slist_free (full_l);
				full_l = NULL;

				/* If the deltas would be bigger, just download the new full file */
				if (full && delta_size > full->size) {
					g_slist_free_full (deltas, (GDestroyNotify) ews_oal_details_free);
					deltas = NULL;
				}
			}

			if (full) {
				gchar *uncompressed_filename;

				uncompressed_filename = ebb_ews_download_gal (bbews, book_cache, full, deltas, sequence, cancellable, &local_error);
				if (!uncompressed_filename) {
					success = FALSE;
				} else {
					d (printf ("Ewsgal: Removing old gal\n"));
					/* remove old_gal_file */
					ebb_ews_remove_old_gal_file (book_cache);

					d (printf ("Ewsgal: Check for changes in GAL\n"));
					success = ebb_ews_check_gal_changes (bbews, book_cache, uncompressed_filename,
						out_created_objects, out_modified_objects, out_removed_objects, cancellable, &local_error);

					if (success) {
						if (e_cache_set_key (E_CACHE (book_cache), "oab-filename", uncompressed_filename, NULL)) {
							/* Don't let it get deleted */
							g_free (uncompressed_filename);
							uncompressed_filename = NULL;
						}

						e_cache_set_key_int (E_CACHE (book_cache), "gal-sequence", full->seq, NULL);

						d (printf ("Ewsgal: sync successfully completed\n"));
					}

					ews_oal_details_free (full);
				}

				if (uncompressed_filename) {
					/* preserve  the oab file once we are able to decode the differential updates */
					g_unlink (uncompressed_filename);
					g_free (uncompressed_filename);
				}
			}

			g_slist_free_full (full_l, (GDestroyNotify) ews_oal_details_free);
			g_slist_free_full (deltas, (GDestroyNotify) ews_oal_details_free);
			g_clear_object (&oab_cnc);

			if (success)
				*out_new_sync_tag = etag;
			else
				g_free (etag);

			if (local_error) {
				g_prefix_error (&local_error, "%s", _("Failed to update GAL:"));
				g_propagate_error (error, local_error);
			}
		}

		g_free (oab_url);
	} else {
		GSList *items_created = NULL, *items_modified = NULL, *items_deleted = NULL, *link;
		gboolean includes_last_item = TRUE;

		success = e_ews_connection_sync_folder_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			last_sync_tag, bbews->priv->folder_id, "IdOnly", NULL, EWS_MAX_FETCH_COUNT,
			out_new_sync_tag, &includes_last_item, &items_created, &items_modified, &items_deleted,
			cancellable, &local_error);

		if (!success &&
		    g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
			g_clear_error (&local_error);

			e_book_meta_backend_empty_cache_sync (meta_backend, cancellable, NULL);

			success = e_ews_connection_sync_folder_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM,
				NULL, bbews->priv->folder_id, "IdOnly", NULL, EWS_MAX_FETCH_COUNT,
				out_new_sync_tag, &includes_last_item, &items_created, &items_modified, &items_deleted,
				cancellable, &local_error);
		}

		if (success) {
			GSList *contacts_created = NULL, *contacts_modified = NULL;

			/* The sync state doesn't cover changes made by save_contact_sync(),
			   thus verify the changes, instead of re-donwloading the contacts again */
			items_created = ebb_ews_verify_changes (book_cache, items_created, cancellable);
			items_modified = ebb_ews_verify_changes (book_cache, items_modified, cancellable);

			if (items_created) {
				success = ebb_ews_fetch_items_sync (bbews, items_created, &contacts_created, cancellable, error);
				if (success)
					*out_created_objects = ebb_ews_contacts_to_infos (contacts_created);
			}

			if (items_modified) {
				success = ebb_ews_fetch_items_sync (bbews, items_modified, &contacts_modified, cancellable, error);
				if (success)
					*out_modified_objects = ebb_ews_contacts_to_infos (contacts_modified);
			}

			for (link = items_deleted; link; link = g_slist_next (link)) {
				const gchar *uid = link->data;

				*out_removed_objects = g_slist_prepend (*out_removed_objects,
					e_book_meta_backend_info_new (uid, NULL, NULL, NULL));
			}

			g_slist_free_full (contacts_created, g_object_unref);
			g_slist_free_full (contacts_modified, g_object_unref);

			*out_repeat = !includes_last_item;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		g_slist_free_full (items_created, g_object_unref);
		g_slist_free_full (items_modified, g_object_unref);
		g_slist_free_full (items_deleted, g_free);
	}

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	ebb_ews_convert_error_to_edb_error (error);
	ebb_ews_maybe_disconnect_sync (bbews, error, cancellable);

	g_clear_object (&book_cache);

	return success;
}

static gboolean
ebb_ews_load_contact_sync (EBookMetaBackend *meta_backend,
			   const gchar *uid,
			   const gchar *extra,
			   EContact **out_contact,
			   gchar **out_extra,
			   GCancellable *cancellable,
			   GError **error)
{
	EBookBackendEws *bbews;
	GSList *ids, *items = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact, FALSE);

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	ids = g_slist_prepend (NULL, (gpointer) uid);

	success = e_ews_connection_get_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM, ids, "IdOnly",
		NULL, FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &items, NULL, NULL, cancellable, error);

	g_slist_free (ids);

	if (!items)
		success = FALSE;

	if (success) {
		GSList *contacts = NULL;

		success = ebb_ews_fetch_items_sync (bbews, items, &contacts, cancellable, error);
		if (success && contacts) {
			*out_contact = g_object_ref (contacts->data);

			ebb_ews_store_original_vcard (*out_contact);
		}

		g_slist_free_full (contacts, g_object_unref);
	}

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	g_slist_free_full (items, g_object_unref);

	ebb_ews_convert_error_to_edb_error (error);
	ebb_ews_maybe_disconnect_sync (bbews, error, cancellable);

	return success;
}

static gboolean
ebb_ews_save_contact_sync (EBookMetaBackend *meta_backend,
			   gboolean overwrite_existing,
			   EConflictResolution conflict_resolution,
			   /* const */ EContact *contact,
			   const gchar *extra,
			   gchar **out_new_uid,
			   gchar **out_new_extra,
			   GCancellable *cancellable,
			   GError **error)
{
	EBookBackendEws *bbews;
	EwsFolderId *fid;
	GSList *items = NULL;
	gboolean is_dl = FALSE;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
		if (!e_ews_connection_satisfies_server_version (bbews->priv->cnc, E_EWS_EXCHANGE_2010)) {
			g_rec_mutex_unlock (&bbews->priv->cnc_lock);
			g_propagate_error (error, EDB_ERROR_EX (NOT_SUPPORTED,
				_("Cannot save contact list, it’s only supported on EWS Server 2010 or later")));
			return FALSE;
		}

		is_dl = TRUE;
	}

	fid = e_ews_folder_id_new (bbews->priv->folder_id, NULL, FALSE);
	if (overwrite_existing) {
		EBookCache *book_cache;
		EContact *old_contact = NULL;

		book_cache = e_book_meta_backend_ref_cache (meta_backend);

		success = e_book_cache_get_contact (book_cache, e_contact_get_const (contact, E_CONTACT_UID), FALSE, &old_contact, cancellable, error);
		if (success) {
			const gchar *original_vcard;

			/* This is for offline changes, where the EContact in the cache
			   is already modified, while the original, the one on the server,
			   is different. Using the cached EContact in this case generates
			   empty UpdateItem request and nothing is saved. */
			original_vcard = ebb_ews_get_original_vcard (old_contact);
			if (original_vcard) {
				EContact *tmp;

				tmp = e_contact_new_from_vcard (original_vcard);
				if (tmp) {
					g_object_unref (old_contact);
					old_contact = tmp;
				}
			}
		}

		if (success) {
			ConvertData cd;
			const gchar *conflict_res = "AlwaysOverwrite";

			cd.bbews = bbews;
			cd.cancellable = cancellable;
			cd.error = error;
			cd.old_contact = old_contact;
			cd.new_contact = contact;
			cd.change_key = NULL;

			if (conflict_resolution == E_CONFLICT_RESOLUTION_FAIL)
				conflict_res = "NeverOverwrite";

			success = e_ews_connection_update_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM,
				conflict_res, "SendAndSaveCopy", "SendToAllAndSaveCopy",
				bbews->priv->folder_id, is_dl ? ebb_ews_convert_dl_to_updatexml_cb : ebb_ews_convert_contact_to_updatexml_cb,
				&cd, &items, cancellable, error);

			g_free (cd.change_key);
		}

		g_clear_object (&old_contact);
		g_clear_object (&book_cache);
	} else {
		success = e_ews_connection_create_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM, NULL, NULL,
			fid, is_dl ? ebb_ews_convert_dl_to_xml_cb : ebb_ews_convert_contact_to_xml_cb, contact,
			&items, cancellable, error);
	}

	if (success && items) {
		EEwsItem *item = items->data;
		const EwsId *item_id;

		item_id = e_ews_item_get_id (item);
		*out_new_uid = g_strdup (item_id->id);

		/*
		 * Support for ContactPhoto was added in Exchange 2010 SP2.
		 * We don't want to try to set/get this property if we are running in older version of the server.
		 */
		if (!overwrite_existing &&
		    e_ews_connection_satisfies_server_version (bbews->priv->cnc, E_EWS_EXCHANGE_2010_SP2)) {
			EContactPhoto *photo;

			/*
			 * The contact photo is basically an attachment with a special name.
			 * Considering this, we only can set the contact photo after create the contact itself.
			 * Then we are able to attach the picture to the "Contact Item".
			 */
			photo = e_contact_get (contact, E_CONTACT_PHOTO);
			if (photo) {
				GError *local_error = NULL;

				set_photo (bbews, item_id, contact, photo, NULL, cancellable, &local_error);
				e_contact_photo_free (photo);

				if (local_error) {
					g_propagate_error (error, local_error);
					g_prefix_error (error, "%s", _("Failed to set contact photo:"));
					success = FALSE;
				}
			}
		}
	}

	g_slist_free_full (items, g_object_unref);
	e_ews_folder_id_free (fid);

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	ebb_ews_convert_error_to_edb_error (error);
	ebb_ews_maybe_disconnect_sync (bbews, error, cancellable);

	return success;
}

static gboolean
ebb_ews_remove_contact_sync (EBookMetaBackend *meta_backend,
			     EConflictResolution conflict_resolution,
			     const gchar *uid,
			     const gchar *extra,
			     const gchar *object,
			     GCancellable *cancellable,
			     GError **error)
{
	EBookBackendEws *bbews;
	GSList *ids;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);

	bbews = E_BOOK_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	ids = g_slist_prepend (NULL, (gpointer) uid);

	success = e_ews_connection_delete_items_sync (bbews->priv->cnc, EWS_PRIORITY_MEDIUM, ids, EWS_HARD_DELETE, 0, FALSE, cancellable, error);

	g_slist_free (ids);

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	ebb_ews_convert_error_to_edb_error (error);
	ebb_ews_maybe_disconnect_sync (bbews, error, cancellable);

	return success;
}

static gboolean
ebb_ews_search_sync (EBookMetaBackend *meta_backend,
		     const gchar *expr,
		     gboolean meta_contact,
		     GSList **out_contacts,
		     GCancellable *cancellable,
		     GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	ebb_ews_update_cache_for_expression (E_BOOK_BACKEND_EWS (meta_backend), expr, cancellable, NULL);

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_ews_parent_class)->search_sync (meta_backend, expr, meta_contact,
		out_contacts, cancellable, error);
}

static gboolean
ebb_ews_search_uids_sync (EBookMetaBackend *meta_backend,
			  const gchar *expr,
			  GSList **out_uids,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	ebb_ews_update_cache_for_expression (E_BOOK_BACKEND_EWS (meta_backend), expr, cancellable, NULL);

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_ews_parent_class)->search_uids_sync (meta_backend, expr,
		out_uids, cancellable, error);
}

static gchar *
ebb_ews_get_backend_property (EBookBackend *book_backend,
			      const gchar *prop_name)
{
	EBookBackendEws *bbews;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	bbews = E_BOOK_BACKEND_EWS (book_backend);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		CamelEwsSettings *ews_settings;

		ews_settings = ebb_ews_get_collection_settings (bbews);

		return g_strjoin (",",
			"net",
			"contact-lists",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			(!bbews->priv->is_gal || camel_ews_settings_get_oab_offline (ews_settings)) ? "do-initial-query" : NULL,
			NULL);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GString *buffer;
		gchar *fields;
		gint ii;

		buffer = g_string_sized_new (1024);

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (mappings[ii].element_type != ELEMENT_TYPE_SIMPLE)
				continue;

			if (buffer->len > 0)
				g_string_append_c (buffer, ',');
			g_string_append (buffer, e_contact_field_name (mappings[ii].field_id));
		}

		for (ii = 0; ii < G_N_ELEMENTS (phone_field_map); ii++) {
			if (buffer->len > 0)
				g_string_append_c (buffer, ',');
			g_string_append (buffer, e_contact_field_name (phone_field_map[ii].field));
		}

		fields = g_strjoin (
			",",
			buffer->str,
			e_contact_field_name (E_CONTACT_FULL_NAME),
			e_contact_field_name (E_CONTACT_NICKNAME),
			e_contact_field_name (E_CONTACT_FAMILY_NAME),
			e_contact_field_name (E_CONTACT_EMAIL_1),
			e_contact_field_name (E_CONTACT_EMAIL_2),
			e_contact_field_name (E_CONTACT_EMAIL_3),
			e_contact_field_name (E_CONTACT_ADDRESS_WORK),
			e_contact_field_name (E_CONTACT_ADDRESS_HOME),
			e_contact_field_name (E_CONTACT_ADDRESS_OTHER),
			e_contact_field_name (E_CONTACT_BIRTH_DATE),
			e_contact_field_name (E_CONTACT_NOTE),
			e_contact_field_name (E_CONTACT_PHOTO),
			NULL);

		g_string_free (buffer, TRUE);

		return fields;
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_ews_parent_class)->get_backend_property (book_backend, prop_name);
}

static gboolean
ebb_ews_get_destination_address (EBackend *backend,
				 gchar **host,
				 guint16 *port)
{
	CamelEwsSettings *ews_settings;
	SoupURI *soup_uri;
	gchar *host_url;
	gboolean result = FALSE;

	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_book_backend_get_registry (E_BOOK_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	ews_settings = ebb_ews_get_collection_settings (E_BOOK_BACKEND_EWS (backend));
	g_return_val_if_fail (ews_settings != NULL, FALSE);

	host_url = camel_ews_settings_dup_hosturl (ews_settings);
	g_return_val_if_fail (host_url != NULL, FALSE);

	soup_uri = soup_uri_new (host_url);
	if (soup_uri) {
		*host = g_strdup (soup_uri_get_host (soup_uri));
		*port = soup_uri_get_port (soup_uri);

		result = *host && **host;
		if (!result) {
			g_free (*host);
			*host = NULL;
		}

		soup_uri_free (soup_uri);
	}

	g_free (host_url);

	return result;
}

static void
e_book_backend_ews_constructed (GObject *object)
{
	EBookBackendEws *bbews = E_BOOK_BACKEND_EWS (object);
	EBookCache *book_cache;
	gchar *cache_dirname;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->constructed (object);

	book_cache = e_book_meta_backend_ref_cache (E_BOOK_META_BACKEND (bbews));

	cache_dirname = g_path_get_dirname (e_cache_get_filename (E_CACHE (book_cache)));

	g_clear_object (&book_cache);

	bbews->priv->attachments_dir = g_build_filename (cache_dirname, "attachments", NULL);
	g_mkdir_with_parents (bbews->priv->attachments_dir, 0777);

	g_free (cache_dirname);
}

static void
e_book_backend_ews_dispose (GObject *object)
{
	EBookBackendEws *bbews = E_BOOK_BACKEND_EWS (object);

	g_rec_mutex_lock (&bbews->priv->cnc_lock);

	g_clear_object (&bbews->priv->cnc);

	g_rec_mutex_unlock (&bbews->priv->cnc_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->dispose (object);
}

static void
e_book_backend_ews_finalize (GObject *object)
{
	EBookBackendEws *bbews = E_BOOK_BACKEND_EWS (object);

	g_free (bbews->priv->folder_id);
	g_free (bbews->priv->attachments_dir);

	g_rec_mutex_clear (&bbews->priv->cnc_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->finalize (object);
}

static void
e_book_backend_ews_init (EBookBackendEws *bbews)
{
	bbews->priv = G_TYPE_INSTANCE_GET_PRIVATE (bbews, E_TYPE_BOOK_BACKEND_EWS, EBookBackendEwsPrivate);

	g_rec_mutex_init (&bbews->priv->cnc_lock);
}

static void
e_book_backend_ews_class_init (EBookBackendEwsClass *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;
	EBookMetaBackendClass *book_meta_backend_class;

	g_type_class_add_private (klass, sizeof (EBookBackendEwsPrivate));

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendews.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendEwsFactory";
	book_meta_backend_class->connect_sync = ebb_ews_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_ews_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_ews_get_changes_sync;
	book_meta_backend_class->load_contact_sync = ebb_ews_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_ews_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_ews_remove_contact_sync;
	book_meta_backend_class->search_sync = ebb_ews_search_sync;
	book_meta_backend_class->search_uids_sync = ebb_ews_search_uids_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->get_backend_property = ebb_ews_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ebb_ews_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_book_backend_ews_constructed;
	object_class->dispose = e_book_backend_ews_dispose;
	object_class->finalize = e_book_backend_ews_finalize;
}
