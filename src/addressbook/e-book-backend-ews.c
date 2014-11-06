/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews.c - Ews contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
#include "server/e-source-ews-folder.h"

#include "utils/e-ews-query-to-restriction.h"

#include "e-book-backend-ews.h"
#include "ews-oab-decoder.h"
#include "ews-oab-decompress.h"


#define d(x) x

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

static gboolean
ebews_fetch_items (EBookBackendEws *ebews,  GSList *items, GSList **contacts,
		   GCancellable *cancellable, GError **error);

typedef struct {
	GCond cond;
	GMutex mutex;
	gboolean exit;
} SyncDelta;

struct _EBookBackendEwsPrivate {
	EEwsConnection *cnc;
	gchar *folder_id;
	gchar *oab_url;
	gchar *folder_name;

	EBookSqlite *summary;

	gboolean is_writable;
	gboolean marked_for_offline;
	gboolean cache_ready;
	gboolean is_gal;

	GHashTable *ops;

	/* used for storing attachments */
	gchar *attachment_dir;

	GRecMutex rec_mutex;
	GThread *dthread;
	SyncDelta *dlock;

	GCancellable *cancellable;

	guint subscription_key;
	gboolean listen_notifications;

	guint rev_counter;
	gchar *locale;
};

/* using this for backward compatibility with E_DATA_BOOK_MODE */
enum {
	MODE_LOCAL,
	MODE_REMOTE,
	MODE_ANY
};

#define EWS_MAX_FETCH_COUNT 500
#define REFRESH_INTERVAL 21600

#define ELEMENT_TYPE_SIMPLE 0x01 /* simple string fields */
#define ELEMENT_TYPE_COMPLEX 0x02 /* complex fields while require different get/set functions */

/* passing field uris for PhysicalAddress, PhoneNumbers causes error, so we use Default view to fetch them. Thus the summary props just have attachments  and 
 * some additional properties that are not return with Default view */
#define CONTACT_ITEM_PROPS "item:Attachments item:HasAttachments item:Body contacts:Manager contacts:Department contacts:SpouseName contacts:AssistantName contacts:BusinessHomePage contacts:Birthday"

/* NB: This is locked *outside* the EBookSqlite lock. Never lock it under e_book_sqlite_lock() */
#define PRIV_LOCK(p)   (g_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_rec_mutex_unlock (&(p)->rec_mutex))

/* Forward Declarations */
static void	e_book_backend_ews_authenticator_init
				(ESourceAuthenticatorInterface *iface);
static void	e_book_backend_ews_initable_init
				(GInitableIface *iface);
static gpointer ews_update_items_thread (gpointer data);


G_DEFINE_TYPE_WITH_CODE (
	EBookBackendEws,
	e_book_backend_ews,
	E_TYPE_BOOK_BACKEND,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_book_backend_ews_authenticator_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_book_backend_ews_initable_init))

static CamelEwsSettings *
book_backend_ews_get_collection_settings (EBookBackendEws *backend)
{
	ESource *source;
	ESource *collection;
	ESourceCamel *extension;
	ESourceRegistry *registry;
	CamelSettings *settings;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (backend));
	registry = e_book_backend_get_registry (E_BOOK_BACKEND (backend));

	extension_name = e_source_camel_get_extension_name ("ews");
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);

	/* The collection settings live in our parent data source. */
	collection = e_source_registry_find_extension (
		registry, source, extension_name);
	g_return_val_if_fail (collection != NULL, NULL);

	extension = e_source_get_extension (collection, extension_name);
	settings = e_source_camel_get_settings (extension);

	g_object_unref (collection);

	return CAMEL_EWS_SETTINGS (settings);
}

static void
convert_error_to_edb_error (GError **perror)
{
	GError *error = NULL;

	g_return_if_fail (perror != NULL);

	if (!*perror || (*perror)->domain == E_DATA_BOOK_ERROR)
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
		}
	}

	if (!error)
		error = EDB_ERROR_EX (OTHER_ERROR, (*perror)->message);

	g_error_free (*perror);
	*perror = error;
}

static gboolean ebews_bump_revision (EBookBackendEws *ebews, GError **error)
{
	gboolean ret;
	gchar *prop_value;
	time_t t = time (NULL);

	/* rev_counter is protected by the EBookSqlite lock. We only ever
	 * call ebews_bump_revision() under e_book_sqlite_lock() */
	prop_value = g_strdup_printf ("%ld(%d)", (long) t, ++ebews->priv->rev_counter);

	ret = e_book_sqlite_set_key_value (ebews->priv->summary, "revision",
					   prop_value, error);
	if (ret)
		e_book_backend_notify_property_changed (E_BOOK_BACKEND (ebews),
							BOOK_BACKEND_PROPERTY_REVISION,
							prop_value);
	g_free (prop_value);

	return ret;
}


static gboolean
book_backend_ews_ensure_connected (EBookBackendEws *bbews,
				   GCancellable *cancellable,
				   GError **perror)
{
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_EWS (bbews), FALSE);

	PRIV_LOCK (bbews->priv);

	if (bbews->priv->cnc) {
		PRIV_UNLOCK (bbews->priv);
		return TRUE;
	}

	PRIV_UNLOCK (bbews->priv);

	e_backend_authenticate_sync (
		E_BACKEND (bbews),
		E_SOURCE_AUTHENTICATOR (bbews),
		cancellable, &local_error);

	if (!local_error)
		return TRUE;

	g_propagate_error (perror, local_error);

	return FALSE;
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
ebews_populate_uid (EBookBackendEws *ebews,
		    EContact *contact,
                    EEwsItem *item,
		    GCancellable *cancellable,
		    GError **error)
{
	const EwsId *id;

	id = e_ews_item_get_id (item);
	if (id) {
		e_contact_set (contact, E_CONTACT_UID, id->id);
		e_contact_set (contact, E_CONTACT_REV, id->change_key);
	}
}

static void
ebews_populate_full_name (EBookBackendEws *ebews,
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
ebews_populate_nick_name (EBookBackendEws *ebews,
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
ebews_populate_birth_date (EBookBackendEws *ebews,
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
ebews_populate_anniversary (EBookBackendEws *ebews,
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
get_photo (EBookBackendEws *ebews,
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

	add_props = e_ews_additional_props_new ();
	add_props->field_uri = g_strdup ("item:Attachments");

	id = e_ews_item_get_id (item);
	contact_item_ids = g_slist_prepend (contact_item_ids, g_strdup (id->id));
	if (!e_ews_connection_get_items_sync (
			ebews->priv->cnc,
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
		ebews->priv->cnc,
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
ebews_populate_photo (EBookBackendEws *ebews,
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
	if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010_SP2))
		return;

	photo = get_photo (ebews, item, cancellable, error);
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
ebews_populate_phone_numbers (EBookBackendEws *ebews,
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
static void
set_address (EContact *contact,
             EContactField field,
             EEwsItem *item,
             const gchar *item_field)
{
	const EwsAddress *address;

	address = e_ews_item_get_physical_address (item, item_field);
	if (address) {
		EContactAddress *addr;

		addr = g_new0 (EContactAddress, 1);
		copy_ews_address_to_contact_address (addr, address);
		e_contact_set (contact, field, addr);
		e_contact_address_free (addr);
	}
}

static void
ebews_populate_address (EBookBackendEws *ebews,
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
ebews_populate_ims (EBookBackendEws *ebews,
		    EContact *contact,
		    EEwsItem *item,
		    GCancellable *cancellable,
		    GError **error)
{
	/* TODO : The fields returned by server does not match with the EContact fields
	 * for the IMS, handle it later */
}

static void
ebews_populate_notes (EBookBackendEws *ebews,
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
                   const gchar *item_field)
{
	const gchar *ea;

	ea = e_ews_item_get_email_address (item, item_field);
	if (ea && *ea)
		e_contact_set (contact, field, ea);
}

static void
ebews_populate_emails (EBookBackendEws *ebews,
		       EContact *contact,
		       EEwsItem *item,
		       GCancellable *cancellable,
		       GError **errror)
{
	set_email_address (contact, E_CONTACT_EMAIL_1, item, "EmailAddress1");
	set_email_address (contact, E_CONTACT_EMAIL_2, item, "EmailAddress2");
	set_email_address (contact, E_CONTACT_EMAIL_3, item, "EmailAddress3");
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
ebews_set_full_name_changes (EBookBackendEws *ebews,
			     ESoapMessage *message,
			     EContact *new,
			     EContact *old,
			     GCancellable *cancellable,
			     GError **error)
{
	EContactName *name, *old_name;

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
ebews_set_birth_date_changes (EBookBackendEws *ebews,
			      ESoapMessage *message,
			      EContact *new,
			      EContact *old,
			      GCancellable *cancellable,
			      GError **error)
{
	EContactDate *new_date, *old_date;
	gchar *birthday;

	new_date = e_contact_get (new, E_CONTACT_BIRTH_DATE);
	old_date = e_contact_get (old, E_CONTACT_BIRTH_DATE);

	if (e_contact_date_equal (new_date, old_date))
		return;

	birthday = g_strdup_printf (
		"%04d-%02d-%02dT00:00:00",
		new_date->year, new_date->month, new_date->day);

	convert_contact_property_to_updatexml (message, "Birthday", birthday, "contacts", NULL, NULL);
	g_free (birthday);
}

static void
ebews_set_anniversary_changes (EBookBackendEws *ebews,
			       ESoapMessage *message,
			       EContact *new,
			       EContact *old,
			       GCancellable *cancellable,
			       GError **error)
{

}

static void
set_photo (EBookBackendEws *ebews,
	   EContact *contact,
	   EContactPhoto *photo,
	   GCancellable *cancellable,
	   GError **error)
{
	EEwsAttachmentInfo *info;
	EwsId *id;
	GSList *files = NULL;
	const guchar *data;
	gsize len;

	id = g_new0 (EwsId, 1);
	id->id = e_contact_get (contact, E_CONTACT_UID);
	id->change_key = e_contact_get (contact, E_CONTACT_REV);

	data = e_contact_photo_get_inlined (photo, &len);

	info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_INLINED);
	e_ews_attachment_info_set_inlined_data (info, data, len);
	e_ews_attachment_info_set_mime_type (info, "image/jpeg");
	e_ews_attachment_info_set_filename (info, "ContactPicture.jpg");

	files = g_slist_append (files, info);

	e_ews_connection_create_attachments_sync (
			ebews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			id,
			files,
			TRUE,
			NULL,
			NULL,
			cancellable,
			error);

	g_free (id->change_key);
	g_free (id->id);
	g_free (id);

	g_slist_free_full (files, (GDestroyNotify) e_ews_attachment_info_free);
}

static gboolean
photos_equal (EContactPhoto *old,
		EContactPhoto *new)
{
	const guchar *old_content, *new_content;
	gsize old_len, new_len;

	if (!old && !new)
		return TRUE;

	if (!old || !new)
		return FALSE;

	old_content = e_contact_photo_get_inlined (old, &old_len);
	new_content = e_contact_photo_get_inlined (new, &new_len);

	if (old_len != new_len)
		return FALSE;

	if (memcmp (old_content, new_content, old_len) != 0)
		return FALSE;

	return TRUE;
}

static void
ebews_set_photo_changes (EBookBackendEws *ebews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 GCancellable *cancellable,
			 GError **error)
{
	EContactPhoto *old_photo, *new_photo;
	EEwsAdditionalProps *add_props = NULL;
	GSList *contact_item_ids = NULL, *new_items = NULL, *attachments_ids = NULL;
	gchar *id = e_contact_get (old, E_CONTACT_UID);
	const gchar *contact_photo_id;

	/*
	 * Support for ContactPhoto was added in Exchange 2010 SP2.
	 * We don't want to try to set/get this property if we are running in older version of the server.
	 */
	if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010_SP2))
		return;

	old_photo = e_contact_get (old, E_CONTACT_PHOTO);
	new_photo = e_contact_get (new, E_CONTACT_PHOTO);

	if (photos_equal (old_photo, new_photo))
		goto exit;

	add_props = e_ews_additional_props_new ();
	add_props->field_uri = g_strdup ("item:Attachments");

	contact_item_ids = g_slist_append (contact_item_ids, id);
	if (!e_ews_connection_get_items_sync (
			ebews->priv->cnc,
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
					ebews->priv->cnc,
					EWS_PRIORITY_MEDIUM,
					attachments_ids,
					NULL,
					cancellable,
					error))
			goto exit;
	}

	if (new_photo)
		set_photo (ebews, new, new_photo, cancellable, error);

exit:
	e_ews_additional_props_free (add_props);
	e_contact_photo_free (old_photo);
	e_contact_photo_free (new_photo);
	g_slist_free_full (contact_item_ids, g_free);
	g_slist_free_full (new_items, g_object_unref);
	g_slist_free_full (attachments_ids, g_free);
}

static void
ebews_set_phone_number_changes (EBookBackendEws *ebews,
				ESoapMessage *message,
				EContact *new,
				EContact *old,
				GCancellable *cancellable,
				GError **error)
{
	gint i;
	gchar *new_value, *old_value;

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
ebews_set_address_changes (EBookBackendEws *ebews,
			   ESoapMessage *message,
			   EContact *new,
			   EContact *old,
			   GCancellable *cancellable,
			   GError **error)
{
	compare_address (message, new, old, E_CONTACT_ADDRESS_WORK, "Business");
	compare_address (message, new, old, E_CONTACT_ADDRESS_HOME, "Home");
	compare_address (message, new, old, E_CONTACT_ADDRESS_OTHER, "Other");
}

static void
ebews_set_im_changes (EBookBackendEws *ebews,
		      ESoapMessage *message,
		      EContact *new,
		      EContact *old,
		      GCancellable *cancellable,
		      GError **error)
{

}

static void
ebews_set_notes_changes (EBookBackendEws *ebews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 GCancellable *cancellable,
			 GError **error)
{
	gchar *old_notes, *new_notes;

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
ebews_set_email_changes (EBookBackendEws *ebews,
			 ESoapMessage *message,
			 EContact *new,
			 EContact *old,
			 GCancellable *cancellable,
			 GError **error)
{
	gchar *new_value, *old_value;

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

static const struct field_element_mapping {
	EContactField field_id;
	gint element_type;
	const gchar *element_name;
	/* set function for simple string type values */
	const gchar * (*get_simple_prop_func) (EEwsItem *item);
	void (*populate_contact_func)(EBookBackendEws *ebews, EContact *contact, EEwsItem *item, GCancellable *cancellable, GError **error);
	void (*set_value_in_soap_message) (ESoapMessage *message, EContact *contact);
	void (*set_changes) (EBookBackendEws *ebews, ESoapMessage *message, EContact *new, EContact *old, GCancellable *cancellable, GError **error);

} mappings[] = {
	/* The order should be maintained for create contacts to work */
	{ E_CONTACT_NOTE, ELEMENT_TYPE_COMPLEX, "Notes", NULL, ebews_populate_notes, ebews_set_notes, ebews_set_notes_changes },
	{ E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "FileAs", e_ews_item_get_fileas},
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
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "WeddingAnniversary", NULL,  ebews_populate_anniversary, ebews_set_anniversary, ebews_set_anniversary_changes },
	{ E_CONTACT_PHOTO, ELEMENT_TYPE_COMPLEX, "Photo", NULL,  ebews_populate_photo, ebews_set_photo, ebews_set_photo_changes },

	/* Should take of uid and changekey (REV) */
	{ E_CONTACT_UID, ELEMENT_TYPE_COMPLEX, "ItemId", NULL,  ebews_populate_uid, ebews_set_item_id},
};

typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	EContact *contact;
	guint32 opid;
	GCancellable *cancellable;
	gboolean is_dl;
} EwsCreateContact;

static void
ews_write_dl_members (ESoapMessage *msg,
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
convert_dl_to_xml (ESoapMessage *msg,
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

	ews_write_dl_members (msg, contact);

	e_soap_message_end_element (msg); /* DistributionList */
}

static void
convert_contact_to_xml (ESoapMessage *msg,
                        gpointer user_data)
{
	EContact *contact = (EContact *) user_data;
	gint i, element_type;

	/* Prepare Contact node in the SOAP message */
	e_soap_message_start_element (msg, "Contact", NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE) {
			gchar *val = e_contact_get (contact, mappings[i].field_id);

			/* skip uid while creating contacts */
			if (mappings[i].field_id == E_CONTACT_UID)
				continue;

			if (val && *val)
				e_ews_message_write_string_parameter (msg, mappings[i].element_name, NULL, val);
			g_free (val);
		} else
			mappings[i].set_value_in_soap_message (msg, contact);
	}

	// end of "Contact"
	e_soap_message_end_element (msg);
}

static void
ews_create_contact_cb (GObject *object,
                       GAsyncResult *res,
                       gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCreateContact *create_contact = user_data;
	EBookBackendEws *ebews = create_contact->ebews;
	GError *error = NULL;
	GSList *items = NULL;
	const EwsId *item_id;

	/* get a list of ids from server (single item) */
	e_ews_connection_create_items_finish (cnc, res, &items, &error);

	g_return_if_fail (ebews->priv->summary != NULL);

	if (error == NULL) {
		EEwsItem *item = items->data;
		EContactPhoto *photo;
		EVCardAttribute *attr;

		attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
		e_vcard_add_attribute_with_value (
				E_VCARD (create_contact->contact),
				attr,
				create_contact->is_dl ? "DT_DISTLIST" : "DT_MAILUSER");

		/* set item id */
		item_id = e_ews_item_get_id (item);

		e_contact_set (create_contact->contact, E_CONTACT_UID, item_id->id);
		e_contact_set (create_contact->contact, E_CONTACT_REV, item_id->change_key);
		if (e_book_sqlite_lock (ebews->priv->summary, EBSQL_LOCK_WRITE, create_contact->cancellable, &error)) {
			if (e_book_sqlite_add_contact (ebews->priv->summary, create_contact->contact, NULL,
						       TRUE, create_contact->cancellable, &error) &&
			    ebews_bump_revision (ebews, &error))
				e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_COMMIT, &error);
			else
				e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_ROLLBACK, &error);
		}
		if (error == NULL) {
			GSList *contacts;

			contacts = g_slist_append (NULL, create_contact->contact);
			e_data_book_respond_create_contacts (create_contact->book, create_contact->opid, EDB_ERROR (SUCCESS), contacts);
			g_slist_free (contacts);
		}

		/*
		 * Support for ContactPhoto was added in Exchange 2010 SP2.
		 * We don't want to try to set/get this property if we are running in older version of the server.
		 */
		if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010_SP2)) {
			/*
			 * The contact photo is basically an attachment with a special name.
			 * Considering this, we only can set the contact photo after create the contact itself.
			 * Then we are able to attach the picture to the "Contact Item".
			 */
			photo = e_contact_get (create_contact->contact, E_CONTACT_PHOTO);
			if (photo) {
				set_photo (ebews, create_contact->contact, photo, create_contact->cancellable, &error);
				e_contact_photo_free (photo);
			}
		}

		g_object_unref (item);
		g_slist_free (items);
	}

	if (error) {
		g_warning ("Error while Creating contact: %s", error->message);
		e_data_book_respond_create_contacts (create_contact->book, create_contact->opid, EDB_ERROR_EX (OTHER_ERROR, error->message), NULL);
	}

	/* free memory allocated for create_contact & unref contained objects */
	g_object_unref (create_contact->ebews);
	g_object_unref (create_contact->contact);
	g_object_unref (create_contact->cancellable);
	g_free (create_contact);
	g_clear_error (&error);
}

static void
e_book_backend_ews_create_contacts (EBookBackend *backend,
                                    EDataBook *book,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const GSList *vcards)
{
	EContact *contact = NULL;
	EBookBackendEws *ebews;
	EwsCreateContact *create_contact;
	EwsFolderId *fid;
	EBookBackendEwsPrivate *priv;
	GError *error = NULL;
	gboolean is_dl = FALSE;

	if (vcards->next != NULL) {
		e_data_book_respond_create_contacts (
			book, opid,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk additions")),
			NULL);
		return;
	}

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (!priv->is_writable) {
			e_data_book_respond_create_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		e_data_book_respond_create_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	if (!book_backend_ews_ensure_connected (ebews, cancellable, &error)) {
		convert_error_to_edb_error (&error);
		e_data_book_respond_create_contacts (book, opid, error, NULL);
		return;
	}

	if (!ebews->priv->is_writable) {
		e_data_book_respond_create_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
		return;
	}

	contact = e_contact_new_from_vcard (vcards->data);

	if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
		if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010)) {
			g_object_unref (contact);
			e_data_book_respond_create_contacts (
				book,
				opid,
				EDB_ERROR_EX (
					NOT_SUPPORTED,
					_("Cannot save contact list, it's only supported on EWS Server 2010 or later")),
				NULL);
			return;
		}
		is_dl = TRUE;
	}

	create_contact = g_new0 (EwsCreateContact, 1);
	create_contact->ebews = g_object_ref (ebews);
	create_contact->book = g_object_ref (book);
	create_contact->opid = opid;
	create_contact->contact = g_object_ref (contact);
	create_contact->cancellable = g_object_ref (cancellable);
	create_contact->is_dl = is_dl;

	fid = e_ews_folder_id_new (priv->folder_id, NULL, FALSE);

	/* pass new contact component data to the exchange server and expect response in the callback */
	e_ews_connection_create_items (
		priv->cnc,
		EWS_PRIORITY_MEDIUM, NULL,
		NULL,
		fid,
		is_dl ? convert_dl_to_xml : convert_contact_to_xml,
		contact,
		cancellable,
		ews_create_contact_cb,
		create_contact);

	e_ews_folder_id_free (fid);
}

typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	guint32 opid;
	GSList *sl_ids;
	GCancellable *cancellable;
} EwsRemoveContact;

static void
ews_book_remove_contact_cb (GObject *object,
                            GAsyncResult *res,
                            gpointer user_data)
{
	EwsRemoveContact *remove_contact = user_data;
	EBookBackendEws *ebews = remove_contact->ebews;
	EBookBackendEwsPrivate *priv = ebews->priv;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (res);

	g_return_if_fail (priv->summary != NULL);

	if (!g_simple_async_result_propagate_error (simple, &error) &&
	    e_book_sqlite_lock (priv->summary, EBSQL_LOCK_WRITE, remove_contact->cancellable, &error)) {
		if (e_book_sqlite_remove_contacts (priv->summary, remove_contact->sl_ids, remove_contact->cancellable, &error) &&
		    ebews_bump_revision (ebews, &error))
			e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_COMMIT, &error);
		else
			e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);
	}

	if (error == NULL)
		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR (SUCCESS),  remove_contact->sl_ids);
	else {
		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR_EX (OTHER_ERROR, error->message), NULL);

		g_warning ("\nError removing contact %s \n", error->message);
	}

	g_slist_free_full (remove_contact->sl_ids, g_free);
	g_object_unref (remove_contact->ebews);
	g_object_unref (remove_contact->book);
	g_object_unref (remove_contact->cancellable);
	g_free (remove_contact);
	g_clear_error (&error);
}

static void
e_book_backend_ews_remove_contacts (EBookBackend *backend,
                                    EDataBook *book,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const GSList *id_list)
{
	EBookBackendEws *ebews;
	EwsRemoveContact *remove_contact;
	EBookBackendEwsPrivate *priv;
	GSList *l, *copy = NULL;
	GError *error = NULL;

	ebews = E_BOOK_BACKEND_EWS (backend);

	priv = ebews->priv;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (!priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	if (!book_backend_ews_ensure_connected (ebews, cancellable, &error)) {
		convert_error_to_edb_error (&error);
		e_data_book_respond_remove_contacts (book, opid, error, NULL);
		return;
	}

	if (!ebews->priv->is_writable) {
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
		return;
	}

	for (l = (GSList *) id_list; l != NULL; l = g_slist_next (l))
		copy = g_slist_prepend (copy, g_strdup ((gchar *) l->data));
	copy = g_slist_reverse (copy);

	remove_contact = g_new0 (EwsRemoveContact, 1);
	remove_contact->ebews = g_object_ref (ebews);
	remove_contact->book = g_object_ref (book);
	remove_contact->opid = opid;
	remove_contact->sl_ids = copy;
	remove_contact->cancellable = g_object_ref(cancellable);

	e_ews_connection_delete_items (
		priv->cnc, EWS_PRIORITY_MEDIUM, (GSList *) id_list,
		EWS_HARD_DELETE, 0 , FALSE,
		cancellable,
		ews_book_remove_contact_cb,
		remove_contact);
}

typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	EContact *new_contact;
	EContact *old_contact;
	guint32 opid;
	GCancellable *cancellable;
} EwsModifyContact;

static void
ews_modify_contact_cb (GObject *object,
                       GAsyncResult *res,
                       gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsModifyContact *modify_contact = user_data;
	EBookBackendEws *ebews = modify_contact->ebews;
	EBookBackendEwsPrivate *priv = ebews->priv;
	GError *error = NULL;
	GSList *items = NULL;
	gchar *id;
	const EwsId *item_id;

	g_object_ref (modify_contact->new_contact);
	g_object_ref (modify_contact->old_contact);

	e_ews_connection_update_items_finish (cnc, res, &items, &error);

	g_return_if_fail (priv->summary != NULL);

	if (error == NULL) {
		if (items != NULL) {
			EEwsItem *item = (EEwsItem *) items->data;

			/* set item id */
			item_id = e_ews_item_get_id (item);

			e_contact_set (modify_contact->new_contact, E_CONTACT_UID, item_id->id);
			e_contact_set (modify_contact->new_contact, E_CONTACT_REV, item_id->change_key);

			g_object_unref (item);
		}

		id = e_contact_get (modify_contact->old_contact, E_CONTACT_UID);

		if (e_book_sqlite_lock (ebews->priv->summary, EBSQL_LOCK_WRITE, modify_contact->cancellable, &error)) {
			if (e_book_sqlite_remove_contact (ebews->priv->summary, id, modify_contact->cancellable, &error) &&
			    e_book_sqlite_add_contact (ebews->priv->summary, modify_contact->new_contact, NULL,
						       TRUE, modify_contact->cancellable, &error) &&
			    ebews_bump_revision (ebews, &error))
				e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_COMMIT, &error);
			else
				e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);
		}

		if (error == NULL) {
			GSList *new_contacts;

			new_contacts = g_slist_append (NULL, modify_contact->new_contact);
			e_data_book_respond_modify_contacts (modify_contact->book, modify_contact->opid, EDB_ERROR (SUCCESS), new_contacts);
			g_slist_free (new_contacts);
		}

		g_slist_free (items);
	}

	if (error) {
		g_warning ("Error while Modifying contact: %s", error->message);

		e_data_book_respond_modify_contacts (modify_contact->book, modify_contact->opid, EDB_ERROR_EX (OTHER_ERROR, error->message), NULL);
	}

	/* free memory allocated for create_contact & unref contained objects */
	g_object_unref (modify_contact->ebews);
	g_object_unref (modify_contact->new_contact);
	g_object_unref (modify_contact->old_contact);
	g_object_unref (modify_contact->cancellable);
	g_free (modify_contact);
	g_clear_error (&error);
}

static void
convert_dl_to_updatexml (ESoapMessage *msg,
			 gpointer user_data)
{
	EwsModifyContact *modify_contact = user_data;
	EwsId *id;
	EContact *old_contact = modify_contact->old_contact;
	EContact *new_contact = modify_contact->new_contact;

	id = g_new0 (EwsId, 1);
	id->id = e_contact_get (old_contact, E_CONTACT_UID);
	id->change_key = e_contact_get (old_contact, E_CONTACT_REV);

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM, id->id, id->change_key, 0);
	e_ews_message_start_set_item_field (msg, "Members", "distributionlist", "DistributionList");
	ews_write_dl_members (msg, new_contact);
	e_ews_message_end_set_item_field (msg);
	e_ews_message_end_item_change (msg);
}

static void
convert_contact_to_updatexml (ESoapMessage *msg,
                              gpointer user_data)
{
	EwsModifyContact *modify_contact = user_data;
	EwsId *id;
	EContact *old_contact = modify_contact->old_contact;
	EContact *new_contact = modify_contact->new_contact;
	gchar *value = NULL, *old_value = NULL;
	gint i, element_type;
	GError *error = NULL;

	id = g_new0 (EwsId, 1);
	id->id = e_contact_get (old_contact, E_CONTACT_UID);
	id->change_key = e_contact_get (old_contact, E_CONTACT_REV);

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		id->id, id->change_key, 0);

	/*Iterate for each field in contact*/

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
			if (mappings[i].field_id == E_CONTACT_UID)
				continue;
			mappings[i].set_changes (
					modify_contact->ebews, msg,
					new_contact, old_contact,
					modify_contact->cancellable,
					&error);

			if (error != NULL) {
				e_data_book_respond_modify_contacts (
						modify_contact->book,
						modify_contact->opid,
						EDB_ERROR_EX (OTHER_ERROR, error->message),
						NULL);
				g_clear_error (&error);
			}
		}
	}

	e_ews_message_end_item_change (msg);
}

static void
e_book_backend_ews_modify_contacts (EBookBackend *backend,
                                    EDataBook *book,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const GSList *vcards)
{
	EContact *contact = NULL, *old_contact = NULL;
	EwsModifyContact *modify_contact;
	EBookBackendEws *ebews;
	EwsId *id;
	EBookBackendEwsPrivate *priv;
	GError *error;
	gboolean is_dl = FALSE;

	if (vcards->next != NULL) {
		e_data_book_respond_modify_contacts (book, opid,
			EDB_ERROR_EX (
			NOT_SUPPORTED,
			_("The backend does not support bulk modifications")),
			NULL);
		return;
	}

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (!priv->is_writable) {
			e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	if (!book_backend_ews_ensure_connected (ebews, cancellable, &error)) {
		convert_error_to_edb_error (&error);
		e_data_book_respond_modify_contacts (book, opid, error, NULL);
		return;
	}

	if (!priv->is_writable) {
		e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
		return;
	}

	g_return_if_fail (priv->summary != NULL);

	contact = e_contact_new_from_vcard (vcards->data);

	if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
		if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010)) {
			g_object_unref (contact);
			e_data_book_respond_create_contacts (
				book,
				opid,
				EDB_ERROR_EX (
					NOT_SUPPORTED,
					_("Cannot save contact list, it's only supported on EWS Server 2010 or later")),
				NULL);
			return;
		}
		is_dl = TRUE;
	}

	/*get item id and change key from contact and fetch old contact and assign.*/

	id = g_new0 (EwsId, 1);
	id->id = e_contact_get (contact, E_CONTACT_UID);
	id->change_key = e_contact_get (contact, E_CONTACT_REV);

	if (e_book_sqlite_lock (priv->summary, EBSQL_LOCK_READ, cancellable, &error)) {
		e_book_sqlite_get_contact (priv->summary, id->id, TRUE, &old_contact, &error);
		e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_NONE, error ? NULL : &error);
	}

	if (!old_contact) {
		g_object_unref (contact);
		g_clear_error (&error); // Shouldn't we be using this? NOT_SUPPORTED seems wrong
		e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (NOT_SUPPORTED), NULL);
		return;
	}

	/* TODO implement */
	modify_contact = g_new0 (EwsModifyContact, 1);
	modify_contact->ebews = g_object_ref (ebews);
	modify_contact->book = g_object_ref (book);
	modify_contact->opid = opid;
	modify_contact->old_contact = g_object_ref (old_contact);
	modify_contact->new_contact = g_object_ref (contact);
	modify_contact->cancellable = g_object_ref (cancellable);

	e_ews_connection_update_items (
		priv->cnc,
		EWS_PRIORITY_MEDIUM,
		"AlwaysOverwrite",
		"SendAndSaveCopy",
		"SendToAllAndSaveCopy",
		priv->folder_id,
		is_dl ? convert_dl_to_updatexml : convert_contact_to_updatexml,
		modify_contact,
		cancellable,
		ews_modify_contact_cb,
		modify_contact);
}

static void
e_book_backend_ews_get_contact (EBookBackend *backend,
                                EDataBook *book,
                                guint32 opid,
                                GCancellable *cancellable,
                                const gchar *id)
{
	EBookBackendEws *ebews;
	GError *error = NULL;

	ebews =  E_BOOK_BACKEND_EWS (backend);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	if (!book_backend_ews_ensure_connected (ebews, cancellable, &error)) {
		convert_error_to_edb_error (&error);
		e_data_book_respond_get_contact (book, opid, error, NULL);
		return;
	}

	e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
}

static void
e_book_backend_ews_get_contact_list (EBookBackend *backend,
                                     EDataBook *book,
                                     guint32 opid,
                                     GCancellable *cancellable,
                                     const gchar *query)
{
	GSList *vcard_list = NULL;
	GSList *list = NULL, *l;
	GError *error = NULL;
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;
	gint populated = 0;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	if (priv->summary)
		e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &populated, NULL);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (populated) {
		search_db:
			if (e_book_sqlite_lock (priv->summary, EBSQL_LOCK_READ, cancellable, &error)) {
				e_book_sqlite_search (priv->summary, query, FALSE, &list, cancellable, &error);

				l = list;
				while (l) {
					EbSqlSearchData *s_data = (EbSqlSearchData *) l->data;

					vcard_list = g_slist_append (vcard_list, g_strdup (s_data->vcard));
					e_book_sqlite_search_data_free (s_data);
					l = l->next;
				}
				e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_NONE, NULL);
			}
			convert_error_to_edb_error (&error);
			e_data_book_respond_get_contact_list (book, opid, error, vcard_list);

			g_slist_free (list);
			g_slist_free_full (vcard_list, g_free);
			return;
		} else
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), vcard_list);
			return;
	}

	if (!book_backend_ews_ensure_connected (ebews, cancellable, &error)) {
		convert_error_to_edb_error (&error);
		e_data_book_respond_get_contact_list (book, opid, error, NULL);
		return;
	}

	if (populated) {
		goto search_db;

	} else if (!priv->marked_for_offline) {
		GSList *items = NULL;
		EwsFolderId *fid = NULL;
		gboolean includes_last_item;

		fid = g_new0 (EwsFolderId, 1);
		fid->id = g_strdup (priv->folder_id);
		fid->is_distinguished_id = FALSE;

		e_ews_connection_find_folder_items_sync (
			priv->cnc, EWS_PRIORITY_MEDIUM,
			fid, "IdOnly", NULL, NULL, query,
			E_EWS_FOLDER_TYPE_CONTACTS,
			&includes_last_item,
			&items, (EwsConvertQueryCallback) (e_ews_query_to_restriction),
			cancellable, &error);

		/*we have got Id for items lets fetch them using getitem operation*/
		ebews_fetch_items (ebews, items, &list, cancellable, &error);

		while (list) {
			gchar *vcard_string;

			l = list;
			list = list->next;

			vcard_string = e_vcard_to_string (E_VCARD (l->data), EVC_FORMAT_VCARD_30);

			g_object_unref (l->data);
			g_slist_free_1 (l);

			vcard_list = g_slist_append (vcard_list, vcard_string);
		}

		convert_error_to_edb_error (&error);
		e_data_book_respond_get_contact_list (book, opid, error, vcard_list);

		e_ews_folder_id_free (fid);
		g_slist_free_full (vcard_list, g_free);
		return;
	} else
		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR_EX (OTHER_ERROR, _("Wait till syncing is done")), vcard_list);
		return;
}

typedef struct {
	/* For future use */
	gpointer restriction;

	gboolean is_query_handled;
	gboolean is_autocompletion;
	gchar *auto_comp_str;
} EBookBackendEwsSExpData;

static ESExpResult *
func_not (ESExp *f,
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
func_and_or (ESExp *f,
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
func_is (struct _ESExp *f,
         gint argc,
         struct _ESExpResult **argv,
         gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;
}

/* TODO implement */
static ESExpResult *
func_endswith (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* TODO implement */
static ESExpResult *
func_contains (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* We are just handling for autocompletion now. We need to support other fields after implementing
 * Restrictions and find_items request */
static ESExpResult *
func_beginswith (struct _ESExp *f,
                 gint argc,
                 struct _ESExpResult **argv,
                 gpointer data)
{
	ESExpResult *r;
	gchar *propname, *str;
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
	{ "and", func_and_or, 0 },
	{ "or", func_and_or, 0},
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0},
	{ "is", func_is, 0},
	{ "beginswith", func_beginswith, 0},
	{ "endswith", func_endswith, 0},
};

/* FIXME  build a complete filter from the query that can be used by find_items */
static gpointer
e_book_backend_ews_build_restriction (const gchar *query,
                                      gboolean *autocompletion,
                                      gchar **auto_comp_str)
{
	ESExpResult *r;
	ESExp *sexp;
	EBookBackendEwsSExpData *sdata;
	gint i;

	sexp = e_sexp_new ();
	sdata = g_new0 (EBookBackendEwsSExpData, 1);

	sdata->is_query_handled = TRUE;

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
		*autocompletion = sdata->is_autocompletion;
		*auto_comp_str = sdata->auto_comp_str;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);
	g_free (sdata);

	return NULL;
}

/************* GAL sync ***********************/

static gchar *
ews_download_gal_file (EBookBackendEws *cbews,
                       EwsOALDetails *full,
                       GCancellable *cancellable,
                       GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	EEwsConnection *oab_cnc;
	gchar *full_url, *oab_url;
	const gchar *cache_dir;
	gchar *download_path = NULL;
	gchar *password;
	CamelEwsSettings *ews_settings;

	ews_settings = book_backend_ews_get_collection_settings (cbews);

	/* oab url with oab.xml removed from the suffix */
	oab_url = g_strndup (priv->oab_url, strlen (priv->oab_url) - 7);
	full_url = g_strconcat (oab_url, full->filename, NULL);
	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (cbews));
	download_path = g_build_filename (cache_dir, full->filename, NULL);

	oab_cnc = e_ews_connection_new (full_url, ews_settings);

	g_object_bind_property (
		cbews, "proxy-resolver",
		oab_cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	password = e_ews_connection_dup_password (priv->cnc);
	e_ews_connection_set_password (oab_cnc, password);
	g_free (password);

	if (!e_ews_connection_download_oal_file_sync (oab_cnc, download_path,
						      NULL, NULL,
						      cancellable, error)) {
		g_free (download_path);
		download_path = NULL;
		goto exit;
	}

	d (g_print ("OAL file downloaded %s\n", download_path);)

 exit:
	g_object_unref (oab_cnc);
	g_free (oab_url);
	g_free (full_url);

	return download_path;
}

static gchar *
ews_download_full_gal (EBookBackendEws *cbews,
                       EwsOALDetails *full,
                       GCancellable *cancellable,
                       GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	const gchar *cache_dir;
	gchar *lzx_path, *oab_file, *oab_path;

	lzx_path = ews_download_gal_file (cbews, full, cancellable, error);
	if (!lzx_path)
		return NULL;

	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (cbews));
	oab_file = g_strdup_printf ("%s-%d.oab", priv->folder_name, full->seq);
	oab_path = g_build_filename (cache_dir, oab_file, NULL);
	if (!ews_oab_decompress_full (lzx_path, oab_path, error)) {
		g_free (oab_path);
		oab_path = NULL;
		goto exit;
	}

	d (g_print ("OAL file decompressed %s \n", oab_path);)

exit:
	if (lzx_path) {
		g_unlink (lzx_path);
		g_free (lzx_path);
	}
	g_free (oab_file);
	return oab_path;
}

static gchar *
ews_download_gal (EBookBackendEws *cbews, EwsOALDetails *full, GSList *deltas, guint32 seq,
		  GCancellable *cancellable, GError **error)
{
#ifdef USE_MSPACK
	EBookBackendEwsPrivate *priv = cbews->priv;
	GSList *p;
	gchar *thisoab = NULL;
	const gchar *cache_dir;

	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (cbews));

	if (!e_book_sqlite_get_key_value (priv->summary, "oab-filename", &thisoab, NULL)
	    || !thisoab)
		goto full;

	for (p = deltas; p; p = p->next) {
		EwsOALDetails *det = p->data;
		GError *local_error = NULL;
		gchar *oab_file, *lzx_path, *nextoab;

		seq++;
		if (det->seq != seq)
			break;

		lzx_path = ews_download_gal_file (cbews, det, cancellable, NULL);
		if (!lzx_path)
			break;

		oab_file = g_strdup_printf ("%s-%d.oab", priv->folder_name, seq);
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
			d (g_print ("Failed to apply incremental patch: %s\n",
				    local_error->message));
			g_error_free (local_error);
			break;
		}

		d (g_print ("Created %s from delta\n", thisoab));

		if (seq == full->seq)
			return thisoab;
	}

	if (thisoab) {
		g_unlink (thisoab);
		g_free (thisoab);
	}
 full:
#endif
	d (printf ("Ewsgal: Downloading full gal \n"));
	return ews_download_full_gal (cbews, full, cancellable, error);
}

static gboolean
ews_remove_old_gal_file (EBookBackendEws *cbews,
                         GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	gchar *filename = NULL;

	if (!priv->summary)
		return FALSE;

	if (!e_book_sqlite_get_key_value (priv->summary, "oab-filename", &filename, error))
		return FALSE;

	if (filename)
		g_unlink (filename);
	g_free (filename);

	return TRUE;
}

struct _db_data {
	GHashTable *uids;
	GHashTable *sha1s;
	GSList *contact_collector;
	GSList *sha1_collector;
	guint collected_length;
	EBookBackendEws *cbews;
	GCancellable *cancellable;
	gint unchanged;
	gint changed;
	gint added;
	gint percent;
};

static gboolean
ews_gal_filter_contact (goffset offset, const gchar *sha1,
                       gpointer user_data, GError **error)
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
ews_gal_store_contact (EContact *contact,
                       goffset offset,
		       const gchar *sha1,
                       guint percent,
                       gpointer user_data,
                       GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;
	EBookBackendEwsPrivate *priv = data->cbews->priv;

	g_return_if_fail (priv->summary != NULL);

	if (contact) {
		const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);
		if (g_hash_table_remove (data->uids, uid))
			data->changed++;
		else
			data->added++;

		data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
		data->sha1_collector = g_slist_prepend (data->sha1_collector, g_strdup (sha1));
		data->collected_length += 1;
	}

	if (data->collected_length == 1000 || percent >= 100) {
		GSList *l;

		data->contact_collector = g_slist_reverse (data->contact_collector);
		data->sha1_collector = g_slist_reverse (data->sha1_collector);
		if (e_book_sqlite_lock (priv->summary, EBSQL_LOCK_WRITE, data->cancellable, error)) {
			if (e_book_sqlite_add_contacts (priv->summary, data->contact_collector, data->sha1_collector,
						    TRUE, data->cancellable, error) &&
			    ebews_bump_revision (data->cbews, error)) {
				if (e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_COMMIT, error)) {
					for (l = data->contact_collector; l != NULL; l = g_slist_next (l))
						e_book_backend_notify_update (E_BOOK_BACKEND (data->cbews), E_CONTACT (l->data));
				}
			} else
				e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);
		}

		g_slist_free_full (data->contact_collector, g_object_unref);
		g_slist_free_full (data->sha1_collector, g_free);
		data->contact_collector = NULL;
		data->sha1_collector = NULL;
		data->collected_length = 0;
	}

	if (data->percent != percent) {
		gchar *status_message = NULL;
		GList *list, *link;

		data->percent = percent;

		d (g_print ("GAL processing contacts, %d%% complete (%d added, %d changed, %d unchanged\n",
			    percent, data->added, data->changed, data->unchanged);)

		status_message = g_strdup_printf (_("Downloading contacts in %s %d%% completed... "),
						  priv->folder_name, percent);
		list = e_book_backend_list_views (E_BOOK_BACKEND (data->cbews));
		for (link = list; link != NULL; link = g_list_next (link))
			e_data_book_view_notify_progress (E_DATA_BOOK_VIEW (link->data), -1, status_message);
		g_list_free_full (list, g_object_unref);
		g_free (status_message);
	}
}

static gint det_sort_func (gconstpointer _a, gconstpointer _b)
{
	const EwsOALDetails *a = _a, *b = _b;

	return a->seq - b->seq;
}

static void append_to_list (gpointer key, gpointer val, gpointer user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, key);
}

static gboolean
ews_replace_gal_in_db (EBookBackendEws *cbews,
                       const gchar *filename,
                       GCancellable *cancellable,
                       GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	EwsOabDecoder *eod;
	gboolean ret = TRUE;
	gint populated = 0;
	GSList *stale_uids = NULL;
	struct _db_data data;
	gint64 t1, t2;

	g_return_val_if_fail (priv->summary != NULL, FALSE);

	data.unchanged = data.changed = data.added = 0;
	data.percent = 0;
	data.uids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	data.sha1s = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	t1 = g_get_monotonic_time ();

	e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &populated, NULL);
	if (populated) {
		GSList *slist = NULL, *l;
		if (e_book_sqlite_lock (priv->summary, EBSQL_LOCK_READ, cancellable, NULL)) {
			e_book_sqlite_search (priv->summary, NULL, TRUE, &slist, cancellable, NULL);
			e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_NONE, NULL);
		}

		while (slist) {
			EbSqlSearchData *search_data = slist->data;

			l = slist;
			slist = slist->next;
			g_slist_free_1 (l);

			g_hash_table_insert (data.uids, search_data->uid, search_data->extra);
			if (search_data->extra)
				g_hash_table_insert (data.sha1s, search_data->extra, search_data->uid);

			/* We steal these */
			search_data->extra = search_data->uid = NULL;
			e_book_sqlite_search_data_free (search_data);
		}
	}

	eod = ews_oab_decoder_new (filename, priv->attachment_dir, error);
	if (*error)
		return FALSE;

	data.contact_collector = NULL;
	data.sha1_collector = NULL;
	data.collected_length = 0;
	data.cbews = cbews;
	data.cancellable = cancellable;

	ret = ews_oab_decoder_decode (eod, ews_gal_filter_contact, ews_gal_store_contact, &data, cancellable, error);
	/* Flush final entries if there are any */
	if (data.contact_collector)
		ews_gal_store_contact (NULL, 0, NULL, 100, &data, error);

	/* Remove any items which were not present in the new OAB */
	g_hash_table_foreach (data.uids, append_to_list, &stale_uids);
	d (g_print ("GAL removing %d contacts\n", g_slist_length (stale_uids)));

	/* Remove attachments. This will be easier once we add cursor support. */
	if (!e_book_sqlite_lock (priv->summary, EBSQL_LOCK_WRITE, cancellable, error))
		ret = FALSE;
	else {
		if ((stale_uids && !e_book_sqlite_remove_contacts (priv->summary, stale_uids, cancellable, error)) ||
		    !ebews_bump_revision (cbews, error) ||
		    !e_book_sqlite_set_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, TRUE, error)) {
			ret = FALSE;
			e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);
		} else {
			ret = e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_COMMIT, error);
		}
	}

	t2 = g_get_monotonic_time ();
	d (g_print("GAL update completed %ssuccessfully in %ld µs. Added: %d, Changed: %d, Unchanged %d, Removed: %d\n",
		   ret ? "" : "un", t2 - t1,
		   data.added, data.changed, data.unchanged, g_slist_length(stale_uids)));

	g_slist_free (stale_uids);
	g_hash_table_destroy (data.sha1s);
	g_hash_table_destroy (data.uids);
	/* always notify views as complete, to not left anything behind,
	   if the decode was cancelled before full completion */
	e_book_backend_notify_complete (E_BOOK_BACKEND (cbews));

	return ret;
}

static gboolean
ebews_start_gal_sync (gpointer data)
{
	EBookBackendEws *cbews;
	EBookBackendEwsPrivate *priv;
	EwsOALDetails *full = NULL;
	GError *error = NULL;
	EEwsConnection *oab_cnc;
	GSList *full_l = NULL;
	GSList *deltas = NULL;
	gboolean ret = TRUE;
	gint is_populated = 0;
	gchar *uncompressed_filename = NULL;
	gchar *password;
	gchar *old_etag = NULL, *etag = NULL;
	gchar *seq;
	guint32 old_seq = 0;
	guint32 delta_size = 0;
	CamelEwsSettings *ews_settings;
	GCancellable *cancellable;

	cbews = (EBookBackendEws *) data;
	ews_settings = book_backend_ews_get_collection_settings (cbews);
	priv = cbews->priv;

	g_return_val_if_fail (priv->summary != NULL, FALSE);

	cancellable = g_object_ref (priv->cancellable);

	oab_cnc = e_ews_connection_new (priv->oab_url, ews_settings);

	g_object_bind_property (
		cbews, "proxy-resolver",
		oab_cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	password = e_ews_connection_dup_password (priv->cnc);
	e_ews_connection_set_password (oab_cnc, password);
	g_free (password);

	d (printf ("Ewsgal: Fetching oal full details file \n");)

	e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &is_populated, NULL);
	if (is_populated) {
		gchar *tmp = NULL;
		e_book_sqlite_get_key_value (
			priv->summary, "etag", &old_etag, NULL);
		e_book_sqlite_get_key_value (
			priv->summary, "seq", &tmp, NULL);
		if (tmp)
			old_seq = strtoul(tmp, NULL, 10);
		else
			is_populated = FALSE;
		g_free (tmp);
	}

	if (!e_ews_connection_get_oal_detail_sync (
		oab_cnc, priv->folder_id, NULL, old_etag, &full_l, &etag,
		cancellable, &error)) {
		if (g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_NOT_MODIFIED)) {
			g_clear_error (&error);
		} else {
			ret = FALSE;
		}
		goto exit;
	}

	g_warn_if_fail (priv->summary != NULL);
	if (!priv->summary)
		goto exit;

	if (full_l == NULL)
		goto exit;

	while (full_l) {
		EwsOALDetails *det = full_l->data;

		/* Throw away anything older than we already have */
		if (det->seq <= old_seq) {
			ews_oal_details_free (det);
		} else if (!strcmp (det->type, "Full")) {
			if (full)
				ews_oal_details_free (full);
			full = det;
		} else if (is_populated && !strcmp (det->type, "Diff")) {
			delta_size += det->size;
			deltas = g_slist_insert_sorted (deltas, det, det_sort_func);
		} else {
			ews_oal_details_free (det);
		}
		full_l = g_slist_remove (full_l, det);
	}

	if (!full)
		goto exit;

	/* If the deltas would be bigger, just download the new full file */
	if (delta_size > full->size) {
		g_slist_free_full (deltas, (GDestroyNotify) ews_oal_details_free);
		deltas = NULL;
	}

	uncompressed_filename = ews_download_gal (cbews, full, deltas, old_seq, cancellable, &error);
	if (!uncompressed_filename) {
		ret = FALSE;
		goto exit;
	}

	d (printf ("Ewsgal: Removing old gal \n");)
	/* remove old_gal_file */
	ews_remove_old_gal_file (cbews, &error);

	d (printf ("Ewsgal: Replacing old gal with new gal contents in db \n");)
	ret = ews_replace_gal_in_db (cbews, uncompressed_filename, cancellable, &error);
	if (!ret)
		goto exit;

	e_book_sqlite_set_key_value (priv->summary, "etag", etag ? etag : "", NULL);
	if (e_book_sqlite_set_key_value (priv->summary, "oab-filename",
					 uncompressed_filename, NULL)) {
		/* Don't let it get deleted */
		g_free (uncompressed_filename);
		uncompressed_filename = NULL;
	}

	seq = g_strdup_printf ("%"G_GUINT32_FORMAT, full->seq);
	ret = e_book_sqlite_set_key_value (priv->summary, "seq", seq, &error);
	g_free (seq);

	if (!ret) {
		gchar *db_filename = g_build_filename (
						       e_book_backend_get_cache_dir (E_BOOK_BACKEND (cbews)),
						       "contacts.dn", NULL);
		g_object_unref (priv->summary);
		priv->summary = NULL;
		g_unlink (db_filename);
		g_free (db_filename);
		goto exit;
	}

	d (printf ("Ews gal: sync successful complete \n");)

exit:
	g_clear_object (&cancellable);

	if (error) {
		g_warning ("Unable to update gal : %s \n", error->message);
		g_clear_error (&error);
	}

	g_free (old_etag);
	g_free (etag);

	/* preserve  the oab file once we are able to decode the differential updates */
	if (uncompressed_filename) {
		g_unlink (uncompressed_filename);
		g_free (uncompressed_filename);
	}

	if (full)
		ews_oal_details_free (full);
	if (deltas)
		g_slist_free_full (deltas, (GDestroyNotify) ews_oal_details_free);
	if (full_l)
		g_slist_free_full (full_l, (GDestroyNotify) ews_oal_details_free);

	g_object_unref (oab_cnc);
	return ret;
}

/********** GAL sync **************************/

static EContact *
ebews_get_contact_info (EBookBackendEws *ebews,
			EEwsItem *item,
			GCancellable *cancellable,
			GError **error)
{
	EContact *contact;
	gint i, element_type;

	contact = e_contact_new ();

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE && !mappings[i].populate_contact_func) {
			const gchar *val = mappings[i].get_simple_prop_func (item);

			if (val != NULL)
				e_contact_set (contact, mappings[i].field_id, val);
		} else {
			mappings[i].populate_contact_func (ebews, contact, item, cancellable, error);
		}
	}

	return contact;
}

static void
ebews_get_contacts_list (EBookBackendEws *ebews, GSList *new_items,
			 GSList **contacts, GCancellable *cancellable,
			 GError **error)
{
	GSList *l;

	for (l = new_items; l != NULL; l = g_slist_next (l)) {
		EContact *contact;
		EEwsItem *item = l->data;
		EVCardAttribute *attr;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_object_unref (item);
			continue;
		}

		contact = ebews_get_contact_info (ebews, item, cancellable, error);

		attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
		e_vcard_add_attribute_with_value (E_VCARD (contact), attr, "DT_MAILUSER");

		*contacts = g_slist_prepend (*contacts, contact);

		g_object_unref (item);
	}
	g_slist_free (new_items);
}

static gboolean
ebews_traverse_dl (EBookBackendEws *ebews,
		   EContact **contact,
		   GHashTable *items,
		   GHashTable *values,
		   EwsMailbox *mb,
		   GError **error)
{
	if (g_strcmp0 (mb->mailbox_type, "PrivateDL") == 0) {
		GSList *members = NULL, *l;
		gboolean includes_last;
		gboolean ret = FALSE;

		if (!mb->item_id || !mb->item_id->id)
			return FALSE;

		if (g_hash_table_lookup (items, mb->item_id->id) != NULL)
			return TRUE;

		g_hash_table_insert (items, g_strdup (mb->item_id->id), GINT_TO_POINTER (1));

		if (!e_ews_connection_expand_dl_sync (
			ebews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			mb,
			&members,
			&includes_last,
			ebews->priv->cancellable,
			error))
			return FALSE;

		for (l = members; l; l = l->next) {
			ret = ebews_traverse_dl (ebews, contact, items, values, l->data, error);
			if (!ret)
				break;
		}

		g_slist_free_full (members, (GDestroyNotify) e_ews_mailbox_free);
		return ret;
	} else {
		EVCardAttribute *attr;
		CamelInternetAddress *addr;
		gchar *value = NULL;

		if (mb->name == NULL && mb->email == NULL)
			return TRUE;

		addr = camel_internet_address_new ();
		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);

		camel_internet_address_add (addr, mb->name, mb->email ? mb->email : "");
		value = camel_address_encode (CAMEL_ADDRESS (addr));

		if (value && g_hash_table_lookup (values, value) == NULL) {
			e_vcard_attribute_add_value (attr, value);
			e_vcard_add_attribute (E_VCARD (*contact), attr);

			g_hash_table_insert (values, g_strdup (value), GINT_TO_POINTER (1));
		}

		g_object_unref (addr);

		return TRUE;
	}
}

static EContact *
ebews_get_dl_info (EBookBackendEws *ebews,
		   const EwsId *id,
		   const gchar *d_name,
		   GSList *members,
		   GError **error)
{
	GHashTable *items, *values;
	GSList *l;
	EContact *contact;

	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_UID, id->id);
	e_contact_set (contact, E_CONTACT_REV, id->change_key);

	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_FULL_NAME, d_name);

	items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (l = members; l != NULL; l = l->next) {
		if (!ebews_traverse_dl (ebews, &contact, items, values, l->data, error)) {
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
ebews_contacts_append_dl (EBookBackendEws *ebews, const EwsId *id,
			  const gchar *d_name,GSList *members,
			  GSList **contacts, GError **error)
{
	EContact *contact;
	EVCardAttribute *attr;

	contact = ebews_get_dl_info (ebews, id, d_name, members, error);
	if (contact == NULL)
		return FALSE;

	attr = e_vcard_attribute_new (NULL, "X-EWS-KIND");
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, "DT_DISTLIST");

	*contacts = g_slist_prepend (*contacts, contact);

	return TRUE;
}

static gboolean
ebews_fetch_items (EBookBackendEws *ebews, GSList *items, GSList **contacts,
                   GCancellable *cancellable, GError **error)
{
	EBookBackendEwsPrivate *priv;
	EEwsConnection *cnc;
	GSList *l;
	GSList *contact_item_ids = NULL, *dl_ids = NULL;
	GSList *new_items = NULL;
	gboolean ret = FALSE;

	if (!book_backend_ews_ensure_connected (ebews, cancellable, error)) {
		g_slist_free_full (items, g_object_unref);
		return ret;
	}

	priv = ebews->priv;
	cnc = priv->cnc;

	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType type = e_ews_item_get_item_type (item);

		if (type == E_EWS_ITEM_TYPE_CONTACT)
			contact_item_ids = g_slist_prepend (contact_item_ids, g_strdup (id->id));
		else if (type == E_EWS_ITEM_TYPE_GROUP) {
			/* store a list of EwsMailBox's in case of distribution lists */
			dl_ids = g_slist_prepend (dl_ids, g_strdup (id->id));
		}

		g_object_unref (item);
	}
	g_slist_free (items);

	/* TODO fetch attachments */
	if (contact_item_ids) {
		EEwsAdditionalProps *add_props;
		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (CONTACT_ITEM_PROPS);

		ret = e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			contact_item_ids, "Default", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &new_items, NULL, NULL,
			cancellable, error);

		e_ews_additional_props_free (add_props);

		if (!ret)
			goto cleanup;
	}

	if (new_items)
		ebews_get_contacts_list (ebews, new_items, contacts, cancellable, error);
	new_items = NULL;

	/* Get the display names of the distribution lists */
	if (dl_ids)
		if (!e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			dl_ids, "Default", NULL,
			FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &new_items, NULL, NULL,
			cancellable, error))
			goto cleanup;

	for (l = new_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const gchar *d_name;
		const EwsId *id;
		EwsMailbox *mb;
		GSList *members = NULL;
		gboolean includes_last;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		id = e_ews_item_get_id (item);
		mb = g_new0 (EwsMailbox, 1);
		mb->item_id = (EwsId *) id;

		d_name = e_ews_item_get_subject (item);
		if (!e_ews_connection_expand_dl_sync (
			cnc, EWS_PRIORITY_MEDIUM, mb, &members,
			&includes_last, cancellable, error))
			goto cleanup;

		ret = ebews_contacts_append_dl (ebews, id, d_name, members, contacts, error);

		g_free (mb);
		g_slist_free_full (members, (GDestroyNotify) e_ews_mailbox_free);

		if (!ret)
			goto cleanup;
	}

cleanup:
	g_slist_free_full (new_items, g_object_unref);
	g_slist_free_full (dl_ids, g_free);
	g_slist_free_full (contact_item_ids, g_free);

	return ret;
}

static gboolean
ebews_start_sync (gpointer data)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;
	GList *list, *link;
	gchar *status_message = NULL;
	GCancellable *cancellable;
	GError *error = NULL;

	ebews = (EBookBackendEws *) data;
	priv = ebews->priv;

	g_return_val_if_fail (priv->summary != NULL, FALSE);

	/* Not connected? Try again later */
	if (!priv->cnc)
		return TRUE;

	cancellable = g_object_ref (priv->cancellable);

	status_message = g_strdup (_("Syncing contacts..."));
	list = e_book_backend_list_views (E_BOOK_BACKEND (ebews));
	for (link = list; link != NULL; link = g_list_next (link))
		e_data_book_view_notify_progress (E_DATA_BOOK_VIEW (link->data), -1, status_message);
	g_list_free_full (list, g_object_unref);
	g_free (status_message);

	ews_update_items_thread (g_object_ref (ebews));

	/* hide progress message when done */
	list = e_book_backend_list_views (E_BOOK_BACKEND (ebews));
	for (link = list; link != NULL; link = g_list_next (link))
		e_data_book_view_notify_progress (E_DATA_BOOK_VIEW (link->data), -1, NULL);
	g_list_free_full (list, g_object_unref);

	g_clear_object (&cancellable);

	if (error) {
		g_warning ("Error Syncing Contacts: Folder %s Error: %s", priv->folder_id, error->message);
		g_clear_error (&error);
		return FALSE;
	}

	return TRUE;
}

static gpointer
delta_thread (gpointer data)
{
	EBookBackendEws *ebews = data;
	EBookBackendEwsPrivate *priv = ebews->priv;
	gint64 end_time;

	g_mutex_lock (&priv->dlock->mutex);
	g_object_ref (ebews);
	g_mutex_unlock (&priv->dlock->mutex);

	while (TRUE)	{
		gboolean succeeded = TRUE;

		if (!priv->is_gal)
			succeeded = ebews_start_sync (ebews);
		else if (priv->summary)
			succeeded = ebews_start_gal_sync (ebews);

		g_mutex_lock (&priv->dlock->mutex);

		/* in case this is the last reference, then this cannot join
		   the itself thread in dispose */
		e_ews_connection_utils_unref_in_thread (ebews);

		if (!succeeded || priv->dlock->exit)
			break;

		end_time = g_get_monotonic_time () + REFRESH_INTERVAL * G_TIME_SPAN_SECOND;
		g_cond_wait_until (&priv->dlock->cond, &priv->dlock->mutex, end_time);

		if (priv->dlock->exit)
			break;

		g_object_ref (ebews);

		g_mutex_unlock (&priv->dlock->mutex);
	}

	g_mutex_unlock (&priv->dlock->mutex);
	priv->dthread = NULL;
	return NULL;
}

static gboolean
fetch_deltas (EBookBackendEws *ebews)
{
	EBookBackendEwsPrivate *priv = ebews->priv;
	GError *error = NULL;

	/* If the thread is already running just return back */
	if (priv->dthread)
		return FALSE;

	if (!priv->dlock) {
		priv->dlock = g_new0 (SyncDelta, 1);
		g_mutex_init (&priv->dlock->mutex);
		g_cond_init (&priv->dlock->cond);
	}

	priv->dlock->exit = FALSE;
	priv->dthread = g_thread_try_new (NULL, (GThreadFunc) delta_thread, ebews, &error);
	if (!priv->dthread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);
	}

	return TRUE;
}

static void
ebews_start_refreshing (EBookBackendEws *ebews)
{
	EBookBackendEwsPrivate *priv;

	priv = ebews->priv;

	PRIV_LOCK (priv);

	if (e_backend_get_online (E_BACKEND (ebews)) &&
	    priv->cnc != NULL && priv->marked_for_offline)
		fetch_deltas (ebews);

	PRIV_UNLOCK (priv);
}

static gboolean
fetch_from_offline (EBookBackendEws *ews,
		    EDataBookView *book_view,
		    const gchar *query,
		    GCancellable *cancellable,
		    GError **error)
{
	GSList *contacts = NULL, *l;
	EBookBackendEwsPrivate *priv;

	priv = ews->priv;

	/* GAL with folder_id means offline GAL */
	if (priv->is_gal && !priv->folder_id && !g_strcmp0 (query, "(contains \"x-evolution-any-field\" \"\")"))
		return TRUE;

	if (!e_book_sqlite_lock (priv->summary, EBSQL_LOCK_READ, cancellable, error))
		return FALSE;

	e_book_sqlite_search (priv->summary, query, FALSE, &contacts, cancellable, error);
	e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_NONE, NULL);
	for (l = contacts; l != NULL; l = g_slist_next (l)) {
		EbSqlSearchData *s_data = (EbSqlSearchData *) l->data;

		e_data_book_view_notify_update_prefiltered_vcard (book_view, s_data->uid, s_data->vcard);

		e_book_sqlite_search_data_free (s_data);
	}

	if (contacts)
		g_slist_free (contacts);

	return TRUE;
}

static void
e_book_backend_ews_start_view (EBookBackend *backend,
                               EDataBookView *book_view)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;
	EBookBackendSExp *sexp;
	const gchar *query;
	gboolean is_autocompletion = FALSE;
	gchar *auto_comp_str = NULL;
	GCancellable *cancellable;
	GSList *mailboxes = NULL, *l, *contacts = NULL, *c;
	EwsFolderId *fid;
	ESource *source;
	ESourceEwsFolder *extension;
	const gchar *extension_name;
	GError *error = NULL;
	gboolean includes_last_item;
	gint is_populated = 0;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	sexp = e_data_book_view_get_sexp (book_view);
	query = e_book_backend_sexp_text (sexp);

	source = e_backend_get_source (E_BACKEND (backend));

	g_object_ref (book_view);
	e_data_book_view_notify_progress (book_view, -1, _("Searching..."));

	PRIV_LOCK (priv);
	cancellable = g_cancellable_new ();
	g_hash_table_insert (priv->ops, book_view, cancellable);
	PRIV_UNLOCK (priv);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (priv->summary)
			e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &is_populated, NULL);
		if (is_populated) {
			fetch_from_offline (ebews, book_view, query, cancellable, &error);
			goto out;
		}

		error = EDB_ERROR (OFFLINE_UNAVAILABLE);
		goto out;
	}

	if (priv->cnc == NULL) {
		e_backend_authenticate_sync (
			E_BACKEND (backend),
			E_SOURCE_AUTHENTICATOR (backend),
			cancellable, &error);
		if (g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NORESPONSE)) {
			/* possibly server unreachable, try offline */
			if (priv->summary)
				e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &is_populated, NULL);
			if (is_populated) {
				g_clear_error (&error);
				fetch_from_offline (ebews, book_view, query, cancellable, &error);
				goto out;
			}
		}

		if (error != NULL)
			goto out;
	}

	g_return_if_fail (priv->cnc != NULL);

	ebews_start_refreshing (ebews);

	if (priv->summary)
		e_book_sqlite_get_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, &is_populated, NULL);
	if (is_populated) {
		fetch_from_offline (ebews, book_view, query, cancellable, &error);
		goto out;
	}

	e_book_backend_ews_build_restriction (query, &is_autocompletion, &auto_comp_str);
	if (!is_autocompletion || !auto_comp_str) {
		g_free (auto_comp_str);
		goto out;
	}

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (source, extension_name);

	/* FIXME Need to convert the Ids from EwsLegacyId format to EwsId format using
	 * convert_id operation before using it as the schema has changed between Exchange
	 * 2007 and 2007_SP1 */
	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (priv->folder_id);
	fid->change_key = e_source_ews_folder_dup_change_key (extension);

	/* We do not scan until we reach the last_item as it might be good enough to show first 100
	 * items during auto-completion. Change it if needed. TODO, Personal Address-book should start using
	 * find_items rather than resolve_names to support all queries */
	e_ews_connection_resolve_names_sync (
		priv->cnc, EWS_PRIORITY_MEDIUM, auto_comp_str,
		EWS_SEARCH_AD, NULL, TRUE, &mailboxes, &contacts,
		&includes_last_item, cancellable, &error);
	g_free (auto_comp_str);
	e_ews_folder_id_free (fid);
	if (error != NULL) {
		e_data_book_view_notify_complete (book_view, error);
		g_object_unref (book_view);
		g_clear_error (&error);
		return;
	}

	for (l = mailboxes, c = contacts; l != NULL; l = g_slist_next (l), c = c ? g_slist_next (c) : NULL) {
		EwsMailbox *mb = l->data;
		EwsResolveContact *rc = c ? c->data : NULL;
		EContact *contact;

		contact = e_contact_new ();

		/* We do not get an id from the server, so just using email_id as uid for now */
		e_contact_set (contact, E_CONTACT_UID, mb->email);

		if (rc && rc->display_name && *rc->display_name)
			e_contact_set (contact, E_CONTACT_FULL_NAME, rc->display_name);
		else
			e_contact_set (contact, E_CONTACT_FULL_NAME, mb->name);

		if (rc && g_hash_table_size (rc->email_addresses) > 0) {
			GList *emails = g_hash_table_get_values (rc->email_addresses), *iter;
			GList *use_emails = NULL;

			for (iter = emails; iter; iter = iter->next) {
				if (iter->data && g_str_has_prefix (iter->data, "SMTP:"))
					use_emails = g_list_prepend (use_emails, ((gchar *) iter->data) + 5);
			}

			if (!use_emails)
				use_emails = g_list_prepend (use_emails, mb->email);

			e_contact_set (contact, E_CONTACT_EMAIL, use_emails);

			g_list_free (use_emails);
			g_list_free (emails);
		} else
			e_contact_set (contact, E_CONTACT_EMAIL_1, mb->email);

		e_data_book_view_notify_update (book_view, contact);

		e_ews_mailbox_free (mb);
		e_ews_free_resolve_contact (rc);
		g_object_unref (contact);
	}

	g_slist_free (mailboxes);
	g_slist_free (contacts);
 out:
	e_data_book_view_notify_complete (book_view, error);
	g_clear_error (&error);
	PRIV_LOCK (priv);
	g_hash_table_remove (priv->ops, book_view);
	PRIV_UNLOCK (priv);
	g_object_unref (cancellable);
	g_object_unref (book_view);
}

static void
e_book_backend_ews_stop_view (EBookBackend *backend,
                              EDataBookView *book_view)
{
	EBookBackendEws *bews = E_BOOK_BACKEND_EWS (backend);
	EBookBackendEwsPrivate *priv = bews->priv;
	GCancellable *cancellable;

	PRIV_LOCK (priv);
	cancellable = g_hash_table_lookup (priv->ops, book_view);
	if (cancellable)
		g_cancellable_cancel (cancellable);
	PRIV_UNLOCK (priv);
}

static gboolean
book_backend_ews_initable_init (GInitable *initable,
				GCancellable *cancellable,
				GError **error)
{
	EBookBackend *backend = E_BOOK_BACKEND (initable);
	ESource *source = e_backend_get_source (E_BACKEND (backend));
	EBookBackendEws *cbews;
	EBookBackendEwsPrivate *priv;
	CamelEwsSettings *settings;
	ESourceExtension *extension;
	const gchar *cache_dir;
	const gchar *display_name;
	const gchar *extension_name;
	const gchar *gal_uid;
	const gchar *uid;
	gchar *db_filename;

	cbews = E_BOOK_BACKEND_EWS (backend);
	priv = cbews->priv;

	cache_dir = e_book_backend_get_cache_dir (backend);
	db_filename = g_build_filename (cache_dir, "contacts.db", NULL);
	settings = book_backend_ews_get_collection_settings (cbews);

	uid = e_source_get_uid (source);
	gal_uid = camel_ews_settings_get_gal_uid (settings);
	priv->is_gal = (g_strcmp0 (uid, gal_uid) == 0);

	display_name = e_source_get_display_name (source);

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (source, extension_name);

	priv->folder_id = e_source_ews_folder_dup_id (
		E_SOURCE_EWS_FOLDER (extension));

	priv->summary = e_book_sqlite_new (db_filename, cancellable, error);
	g_free (db_filename);
	if (priv->summary == NULL) {
		convert_error_to_edb_error (error);
		return FALSE;
	}

	if (!e_book_sqlite_get_locale (priv->summary, &priv->locale, error)) {
		convert_error_to_edb_error (error);
                g_object_unref (priv->summary);
                priv->summary = NULL;
		return FALSE;
	}

	priv->marked_for_offline = FALSE;
	priv->is_writable = FALSE;

	if (!priv->is_gal) {
		extension_name = E_SOURCE_EXTENSION_OFFLINE;
		extension = e_source_get_extension (source, extension_name);

		priv->marked_for_offline =
			e_source_offline_get_stay_synchronized (
			E_SOURCE_OFFLINE (extension));

	/* If folder_id is present it means the GAL is marked for
	 * offline usage, we do not check for offline_sync property */
	} else if (priv->folder_id != NULL) {
		priv->folder_name = g_strdup (display_name);
		priv->oab_url = camel_ews_settings_dup_oaburl (settings);

		/* setup stagging dir, remove any old files from there */
		priv->attachment_dir = g_build_filename (
			cache_dir, "attachments", NULL);
		g_mkdir_with_parents (priv->attachment_dir, 0777);

		priv->marked_for_offline = TRUE;
	}

	return TRUE;
}

static void
e_book_backend_ews_notify_online_cb (EBookBackend *backend,
                                     GParamSpec *spec)
{
	EBookBackendEws *ebews;

	ebews = E_BOOK_BACKEND_EWS (backend);

	if (e_book_backend_is_opened (backend)) {
		if (ebews->priv->cancellable) {
			g_cancellable_cancel (ebews->priv->cancellable);
			g_object_unref (ebews->priv->cancellable);
			ebews->priv->cancellable = NULL;
		}

		if (!e_backend_get_online (E_BACKEND (backend))) {
			e_book_backend_set_writable (backend, FALSE);
			if (ebews->priv->cnc) {
				g_object_unref (ebews->priv->cnc);
				ebews->priv->cnc = NULL;
			}
		} else {
			ebews->priv->cancellable = g_cancellable_new ();
			ebews->priv->is_writable = !ebews->priv->is_gal;

			e_book_backend_set_writable (backend, ebews->priv->is_writable);
		}
	}
}

static gchar *
e_book_backend_ews_get_backend_property (EBookBackend *backend,
                                         const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		EBookBackendEws *ebews;

		ebews = E_BOOK_BACKEND_EWS (backend);
		g_return_val_if_fail (ebews != NULL, NULL);

		/* GAL with folder_id is an offline GAL */
		if (ebews->priv->is_gal && !ebews->priv->folder_id) {
			return g_strdup ("net,bulk-removes,contact-lists");
		} else {
			/* do-initialy-query is enabled for system address book also, so that we get the
			 * book_view, which is needed for displaying cache update progress.
			 * and null query is handled for system address book.
			 */
			return g_strdup ("net,bulk-removes,do-initial-query,contact-lists");
		}
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
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REVISION)) {
		EBookBackendEws *ebews = E_BOOK_BACKEND_EWS (backend);
		gchar *prop_value = NULL;

		e_book_sqlite_get_key_value (ebews->priv->summary, "revision", &prop_value, NULL);
		return prop_value;
        }

	/* Chain up to parent's get_backend_property() method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_ews_parent_class)->
		get_backend_property (backend, prop_name);
}

static gpointer
handle_notifications_thread (gpointer data)
{
	EBookBackendEws *ebews = data;

	PRIV_LOCK (ebews->priv);
	if (ebews->priv->cnc == NULL)
		goto exit;

	if (ebews->priv->listen_notifications) {
		GSList *folders = NULL;

		if (ebews->priv->subscription_key != 0)
			goto exit;

		folders = g_slist_prepend (folders, ebews->priv->folder_id);

		e_ews_connection_enable_notifications_sync (
				ebews->priv->cnc,
				folders,
				&ebews->priv->subscription_key);

		g_slist_free (folders);
	} else {
		if (ebews->priv->subscription_key == 0)
			goto exit;

		e_ews_connection_disable_notifications_sync (
				ebews->priv->cnc,
				ebews->priv->subscription_key);

		ebews->priv->subscription_key = 0;
	}

exit:
	PRIV_UNLOCK (ebews->priv);
	g_object_unref (ebews);
	return NULL;
}

static void
ebews_listen_notifications_cb (EBookBackendEws *ebews,
			       GParamSpec *spec,
			       CamelEwsSettings *ews_settings)
{
	GThread *thread;

	PRIV_LOCK (ebews->priv);
	if (ebews->priv->cnc == NULL) {
		PRIV_UNLOCK (ebews->priv);
		return;
	}

	if (!e_ews_connection_satisfies_server_version (ebews->priv->cnc, E_EWS_EXCHANGE_2010_SP1)) {
		PRIV_UNLOCK (ebews->priv);
		return;
	}

	ebews->priv->listen_notifications = camel_ews_settings_get_listen_notifications (ews_settings);
	PRIV_UNLOCK (ebews->priv);

	thread = g_thread_new (NULL, handle_notifications_thread, g_object_ref (ebews));
	g_thread_unref (thread);
}

static gpointer
ews_update_items_thread (gpointer data)
{
	EBookBackendEws *ebews = data;
	EBookBackendEwsPrivate *priv;
	gchar *sync_state = NULL;
	GError *error = NULL;
	gboolean includes_last_item;
	GSList *items_created = NULL;
	GSList *items_updated = NULL;
	GSList *items_deleted = NULL;
	GSList *items_deleted_resync = NULL;
	GSList *contacts_created = NULL;
	GSList *contacts_updated = NULL;
	GSList *l;

	priv = ebews->priv;

	e_book_sqlite_get_key_value (priv->summary, E_BOOK_SQL_SYNC_DATA_KEY, &sync_state, NULL);
	do {
		gchar *old_sync_state = sync_state;

		sync_state = NULL;
		includes_last_item = TRUE;

		e_ews_connection_sync_folder_items_sync (
				priv->cnc,
				EWS_PRIORITY_MEDIUM,
				old_sync_state,
				priv->folder_id,
				"IdOnly",
				NULL,
				EWS_MAX_FETCH_COUNT,
				&sync_state,
				&includes_last_item,
				&items_created,
				&items_updated,
				&items_deleted,
				priv->cancellable,
				&error);

		g_free (old_sync_state);

		if (error != NULL) {
			if (g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
				g_clear_error (&error);

				if (!e_book_sqlite_search_uids (priv->summary, NULL, &items_deleted_resync,
								priv->cancellable, &error))
					break;

				/* This should be the case anyway, but make sure */
				sync_state = NULL;

				/* Ensure we go round the loop again */
				includes_last_item = FALSE;
				continue;
			}
			/* Other error */
			break;
		}

		if (items_created) {
			ebews_fetch_items (
					ebews,
					items_created, /* freed inside the function */
					&contacts_created,
					priv->cancellable,
					&error);
			items_created = NULL;
			if (error != NULL)
				break;
		}

		if (items_updated) {
			ebews_fetch_items (
					ebews,
					items_updated, /* freed inside the function */
					&contacts_updated,
					priv->cancellable,
					&error);
			items_updated = NULL;
			if (error != NULL)
				break;
		}

		/* Network traffic is done, and database access starts here */
		if (!e_book_sqlite_lock (priv->summary, EBSQL_LOCK_WRITE, priv->cancellable, &error))
			break;

		if ((items_deleted_resync && !e_book_sqlite_remove_contacts (priv->summary, items_deleted_resync, priv->cancellable, &error)) ||
		    (items_deleted && !e_book_sqlite_remove_contacts (priv->summary, items_deleted, priv->cancellable, &error)) ||
		    (contacts_created && !e_book_sqlite_add_contacts (priv->summary, contacts_created, NULL, TRUE, priv->cancellable, &error)) ||
		    (contacts_updated && !e_book_sqlite_add_contacts (priv->summary, contacts_updated, NULL, TRUE, priv->cancellable, &error)) ||
		    !e_book_sqlite_set_key_value (priv->summary, E_BOOK_SQL_SYNC_DATA_KEY, sync_state, &error) ||
		    (includes_last_item && !e_book_sqlite_set_key_value_int (priv->summary, E_BOOK_SQL_IS_POPULATED_KEY, TRUE, &error)) ||
		    !ebews_bump_revision (ebews, &error)) {
			e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);
			break;
		}
		if (!e_book_sqlite_unlock (priv->summary, EBSQL_UNLOCK_COMMIT, &error))
			break;

		while (items_deleted_resync || items_deleted) {
			if (items_deleted_resync) {
				l = items_deleted_resync;
				items_deleted_resync = l->next;
			} else {
				l = items_deleted;
				items_deleted = l->next;
			}

			e_book_backend_notify_remove (E_BOOK_BACKEND (ebews), l->data);
			g_free (l->data);
			g_slist_free_1 (l);
		}
		while (contacts_created || contacts_updated) {
			if (contacts_created) {
				l = contacts_created;
				contacts_created = l->next;
			} else {
				l = contacts_updated;
				contacts_updated = l->next;
			}
			e_book_backend_notify_update (E_BOOK_BACKEND (ebews), l->data);
			g_object_unref (l->data);
			g_slist_free_1 (l);
		}

	} while (!includes_last_item);

	g_slist_free_full (items_created, g_object_unref);
	g_slist_free_full (items_updated, g_object_unref);
	g_slist_free_full (items_deleted, g_free);
	g_slist_free_full (items_deleted_resync, g_free);
	g_slist_free_full (contacts_created, g_object_unref);
	g_slist_free_full (contacts_updated, g_object_unref);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	g_free (sync_state);
	g_object_unref (ebews);

	return NULL;
}

static void
ebews_server_notification_cb (EBookBackendEws *ebews,
			      GSList *events,
			      EEwsConnection *cnc)
{
	GSList *l;
	gboolean update_folder = FALSE;

	g_return_if_fail (ebews != NULL);
	g_return_if_fail (ebews->priv != NULL);

	for (l = events; l != NULL; l = l->next) {
		EEwsNotificationEvent *event = l->data;

		switch (event->type) {
			case E_EWS_NOTIFICATION_EVENT_CREATED:
			case E_EWS_NOTIFICATION_EVENT_DELETED:
			case E_EWS_NOTIFICATION_EVENT_MODIFIED:
				PRIV_LOCK (ebews->priv);
				if (g_strcmp0 (event->folder_id, ebews->priv->folder_id) == 0)
					update_folder = TRUE;
				PRIV_UNLOCK (ebews->priv);
				break;
			case E_EWS_NOTIFICATION_EVENT_MOVED:
			case E_EWS_NOTIFICATION_EVENT_COPIED:
				PRIV_LOCK (ebews->priv);
				if (g_strcmp0 (event->folder_id, ebews->priv->folder_id) == 0 ||
				    g_strcmp0 (event->old_folder_id, ebews->priv->folder_id) == 0)
					update_folder = TRUE;
				PRIV_UNLOCK (ebews->priv);
				break;
			default:
				return;
		}
	}

	if (update_folder) {
		GThread *thread;

		thread = g_thread_new (NULL, ews_update_items_thread, g_object_ref (ebews));
		g_thread_unref (thread);
	}
}

static gboolean
e_book_backend_ews_open_sync (EBookBackend *backend,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelEwsSettings *ews_settings;
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate * priv;
	gboolean need_to_authenticate;
	gchar *revision = NULL;

	if (e_book_backend_is_opened (backend))
		return TRUE;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	ews_settings = book_backend_ews_get_collection_settings (ebews);

	PRIV_LOCK (priv);
	need_to_authenticate = priv->cnc == NULL && e_backend_is_destination_reachable (E_BACKEND (backend), cancellable, NULL);
	PRIV_UNLOCK (priv);

	if (need_to_authenticate &&
	    !e_backend_authenticate_sync (E_BACKEND (backend),
					  E_SOURCE_AUTHENTICATOR (backend),
					  cancellable, error)) {
		convert_error_to_edb_error (error);
		return FALSE;
	}

	e_book_sqlite_get_key_value (priv->summary, "revision", &revision, NULL);
	if (revision) {
		e_book_backend_notify_property_changed (backend,
							BOOK_BACKEND_PROPERTY_REVISION,
							revision);
		g_free (revision);
	}

	if (ebews->priv->is_gal)
		return TRUE;

	PRIV_LOCK (priv);
	priv->listen_notifications = camel_ews_settings_get_listen_notifications (ews_settings);

	if (priv->listen_notifications)
		ebews_listen_notifications_cb (ebews, NULL, ews_settings);

	PRIV_UNLOCK (priv);

	g_signal_connect_swapped (
		ews_settings,
		"notify::listen-notifications",
		G_CALLBACK (ebews_listen_notifications_cb),
		ebews);

	return TRUE;
}

/**
 * e_book_backend_ews_new:
 */
EBookBackend *
e_book_backend_ews_new (void)
{
	EBookBackendEws *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_EWS, NULL);

	return E_BOOK_BACKEND (backend);
}

static gboolean
e_book_backend_ews_get_destination_address (EBackend *backend,
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

	ews_settings = book_backend_ews_get_collection_settings (E_BOOK_BACKEND_EWS (backend));
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
	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);
}

static void
e_book_backend_ews_dispose (GObject *object)
{
	EBookBackendEws *bews;
	EBookBackendEwsPrivate *priv;
	CamelEwsSettings *ews_settings;

	bews = E_BOOK_BACKEND_EWS (object);
	priv = bews->priv;

	ews_settings = book_backend_ews_get_collection_settings (bews);
	g_signal_handlers_disconnect_by_func (ews_settings, ebews_listen_notifications_cb, bews);

	if (priv->cancellable)
		g_cancellable_cancel (priv->cancellable);

	if (priv->dlock) {
		g_mutex_lock (&priv->dlock->mutex);
		priv->dlock->exit = TRUE;
		g_cond_signal (&priv->dlock->cond);
		g_mutex_unlock (&priv->dlock->mutex);

		if (priv->dthread)
			g_thread_join (priv->dthread);

		g_mutex_clear (&priv->dlock->mutex);
		g_cond_clear (&priv->dlock->cond);
		g_free (priv->dlock);
		priv->dthread = NULL;
		priv->dlock = NULL;
	}

	if (priv->cancellable) {
		g_object_unref (priv->cancellable);
		priv->cancellable = NULL;
	}

	if (priv->cnc) {
		g_signal_handlers_disconnect_by_func (priv->cnc, ebews_server_notification_cb, bews);

		if (priv->listen_notifications) {
			if (priv->subscription_key != 0) {
				e_ews_connection_disable_notifications_sync (
					priv->cnc,
					priv->subscription_key);
				priv->subscription_key = 0;
			}

			priv->listen_notifications = FALSE;
		}

		g_clear_object (&priv->cnc);
	}

	if (priv->ops)
		g_hash_table_destroy (priv->ops);

	g_free (priv->folder_id);
	priv->folder_id = NULL;

	g_free (priv->oab_url);
	priv->oab_url = NULL;

	g_free (priv->folder_name);
	priv->folder_name = NULL;

	g_free (priv->attachment_dir);
	priv->attachment_dir = NULL;

	if (priv->summary) {
		g_object_unref (priv->summary);
		priv->summary = NULL;
	}

	g_free (priv->locale);
	priv->locale = NULL;

	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->dispose (object);
}

static void
e_book_backend_ews_finalize (GObject *object)
{
	EBookBackendEws *bews;

	bews = E_BOOK_BACKEND_EWS (object);

	g_rec_mutex_clear (&bews->priv->rec_mutex);

	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->finalize (object);
}

static gboolean
book_backend_ews_get_without_password (ESourceAuthenticator *authenticator)
{
	EBookBackendEws *backend;
	CamelEwsSettings *ews_settings;

	backend = E_BOOK_BACKEND_EWS (authenticator);
	ews_settings = book_backend_ews_get_collection_settings (backend);

	return e_ews_connection_utils_get_without_password (ews_settings);
}

static ESourceAuthenticationResult
book_backend_ews_try_password_sync (ESourceAuthenticator *authenticator,
                                    const GString *password,
                                    GCancellable *cancellable,
                                    GError **error)
{
	EBookBackendEws *backend;
	EEwsConnection *connection;
	ESourceAuthenticationResult result;
	CamelEwsSettings *ews_settings;
	gchar *hosturl;

	backend = E_BOOK_BACKEND_EWS (authenticator);
	ews_settings = book_backend_ews_get_collection_settings (backend);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);

	connection = e_ews_connection_new (hosturl, ews_settings);

	g_object_bind_property (
		backend, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	result = e_source_authenticator_try_password_sync (
		E_SOURCE_AUTHENTICATOR (connection),
		password, cancellable, error);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {

		PRIV_LOCK (backend->priv);

		if (backend->priv->cnc != NULL)
			g_object_unref (backend->priv->cnc);
		backend->priv->cnc = g_object_ref (connection);
		backend->priv->is_writable = !backend->priv->is_gal;

		g_signal_connect_swapped (
			backend->priv->cnc,
			"server-notification",
			G_CALLBACK (ebews_server_notification_cb),
			backend);

		PRIV_UNLOCK (backend->priv);

		e_backend_set_online (E_BACKEND (backend), TRUE);
	} else {
		backend->priv->is_writable = FALSE;
		e_backend_set_online (E_BACKEND (backend), FALSE);
	}

	e_book_backend_set_writable (E_BOOK_BACKEND (backend), backend->priv->is_writable);

	g_object_unref (connection);

	g_free (hosturl);

	return result;
}

static gboolean
e_book_backend_ews_set_locale (EBookBackend *backend,
			       const gchar *locale,
			       GCancellable *cancellable,
			       GError **error)
{
	EBookBackendEws *ebews = E_BOOK_BACKEND_EWS (backend);
	gboolean success = FALSE;

	PRIV_LOCK (ebews->priv);

	if (!e_book_sqlite_lock (ebews->priv->summary, EBSQL_LOCK_WRITE, cancellable, error))
		return FALSE;

	if (e_book_sqlite_set_locale (ebews->priv->summary, locale, cancellable, error) &&
	    ebews_bump_revision (ebews, error))
		success = e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_COMMIT, error);
	else
		e_book_sqlite_unlock (ebews->priv->summary, EBSQL_UNLOCK_ROLLBACK, NULL);

	if (success) {
		g_free (ebews->priv->locale);
		ebews->priv->locale = g_strdup (locale);
	}

	PRIV_LOCK (ebews->priv);

	return success;
}

static gchar *
e_book_backend_ews_dup_locale (EBookBackend *backend)
{
	EBookBackendEws *ebews = E_BOOK_BACKEND_EWS (backend);
	EBookBackendEwsPrivate *priv = ebews->priv;
	gchar *locale;

	PRIV_LOCK (ebews->priv);
	locale = g_strdup (priv->locale);
	PRIV_UNLOCK (ebews->priv);

	return locale;
}

static void
e_book_backend_ews_class_init (EBookBackendEwsClass *klass)
{

	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBackendClass *backend_class;
	EBookBackendClass *parent_class;

	g_type_class_add_private (klass, sizeof (EBookBackendEwsPrivate));

	backend_class = E_BACKEND_CLASS (klass);
	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->open_sync		      = e_book_backend_ews_open_sync;
	parent_class->get_backend_property    = e_book_backend_ews_get_backend_property;

	parent_class->create_contacts         = e_book_backend_ews_create_contacts;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts;
	parent_class->modify_contacts         = e_book_backend_ews_modify_contacts;
	parent_class->get_contact             = e_book_backend_ews_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list;
	parent_class->start_view              = e_book_backend_ews_start_view;
	parent_class->stop_view               = e_book_backend_ews_stop_view;
	parent_class->set_locale              = e_book_backend_ews_set_locale;
	parent_class->dup_locale              = e_book_backend_ews_dup_locale;

	backend_class->get_destination_address = e_book_backend_ews_get_destination_address;

	object_class->constructed             = e_book_backend_ews_constructed;
	object_class->dispose                 = e_book_backend_ews_dispose;
	object_class->finalize                = e_book_backend_ews_finalize;
}

static void
e_book_backend_ews_authenticator_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password = book_backend_ews_get_without_password;
	iface->try_password_sync = book_backend_ews_try_password_sync;
}

static void
e_book_backend_ews_initable_init (GInitableIface *iface)
{
	iface->init = book_backend_ews_initable_init;
}

static void
e_book_backend_ews_init (EBookBackendEws *backend)
{
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (backend, E_TYPE_BOOK_BACKEND_EWS, EBookBackendEwsPrivate);
	backend->priv->ops = g_hash_table_new (NULL, NULL);

	g_rec_mutex_init (&backend->priv->rec_mutex);
	backend->priv->cancellable = g_cancellable_new ();

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_ews_notify_online_cb), NULL);
}
