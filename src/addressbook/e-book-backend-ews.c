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

#include <config.h>
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

#include <libedataserver/eds-version.h>
#include "libedataserver/e-sexp.h"
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-flag.h"
#include "libedataserver/e-url.h"
#include "libebook/e-contact.h"
#include "libebook/e-destination.h"
#include "libedata-book/e-book-backend-sexp.h"
#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"
#include "e-book-backend-ews.h"
#include "e-book-backend-sqlitedb.h"
#include "e-book-backend-ews-utils.h"
#include "lzx/ews-oal-decompress.h"
#include "ews-oab-decoder.h"
#include "e-ews-item-change.h"
#include "libedata-book-compat.h"

#include "e-ews-message.h"
#include "e-ews-connection.h"
#include "e-ews-item.h"

#define d(x) x
#define EDB_ERROR(_code) GNOME_Evolution_Addressbook_##_code

G_DEFINE_TYPE (EBookBackendEws, e_book_backend_ews, E_TYPE_BOOK_BACKEND)

typedef struct {
	GCond *cond;
	GMutex *mutex;
	gboolean exit;
} SyncDelta;

struct _EBookBackendEwsPrivate {
	EEwsConnection *cnc;
	gchar *folder_id;
	gchar *oab_url;
	gchar *folder_name;

	gchar *username;
	gchar *password;
	
	EBookBackendSqliteDB *ebsdb;

	gboolean only_if_exists;
	gboolean is_writable;
	gboolean marked_for_offline;
	gboolean cache_ready;
	gboolean is_gal;
	gint mode;

	GHashTable *ops;

	/* used for storing attachments */
	gchar *attachment_dir;

	GStaticRecMutex rec_mutex;
	GThread *dthread;
	SyncDelta *dlock;

#if EDS_CHECK_VERSION (3,1,0)
	ECredentials *credentials;
#endif
};

/* using this for backward compatibility with E_DATA_BOOK_MODE */
enum {
	MODE_LOCAL,
	MODE_REMOTE,
	MODE_ANY
};

#define EWS_MAX_FETCH_COUNT 500
#define REFRESH_INTERVAL 600000

#define ELEMENT_TYPE_SIMPLE 0x01 /* simple string fields */
#define ELEMENT_TYPE_COMPLEX 0x02 /* complex fields while require different get/set functions */

/* passing field uris for PhysicalAddress, PhoneNumbers causes error, so we use Default view to fetch them. Thus the summary props just have attachments  and 
   some additional properties that are not return with Default view */
#define CONTACT_ITEM_PROPS "item:Attachments item:HasAttachments contacts:Manager contacts:Department contacts:SpouseName contacts:AssistantName contacts:BusinessHomePage contacts:Birthday"

#define PRIV_LOCK(p)   (g_static_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_static_rec_mutex_unlock (&(p)->rec_mutex))

static gchar * ews_get_cache_dir (EBookBackend *backend, ESource *source);

static gboolean
ews_remove_attachments (const gchar *attachment_dir)
{
	GDir *dir;
	
	dir = g_dir_open (attachment_dir, 0, NULL);
	if (dir) {
		const gchar *fname;
		gchar *full_path;

		while ((fname = g_dir_read_name (dir))) {
			full_path = g_build_filename (attachment_dir, fname, NULL);
			if (g_unlink (full_path) != 0) {
				g_free (full_path);
				g_dir_close (dir);

				return FALSE;
			}

			g_free (full_path);
		}

		g_dir_close (dir);
	}

	return TRUE;
}

static const struct phone_field_mapping {
	EContactField field;
	const gchar *element;
} phone_field_map [] = {
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
ebews_populate_uid	(EContact *contact, EEwsItem *item)
{
	const EwsId *id;

	id = e_ews_item_get_id (item);
	e_contact_set (contact, E_CONTACT_UID, id->id);
	e_contact_set (contact, E_CONTACT_REV, id->change_key);
}

static void
ebews_populate_full_name	(EContact *contact, EEwsItem *item)
{
	const EwsCompleteName *cn;

	cn = e_ews_item_get_complete_name (item);
	e_contact_set (contact, E_CONTACT_FULL_NAME, cn->full_name);
}

static void
ebews_populate_nick_name	(EContact *contact, EEwsItem *item)
{
	const EwsCompleteName *cn;

	cn = e_ews_item_get_complete_name (item);
	e_contact_set (contact, E_CONTACT_NICKNAME, cn->nick_name);
}

static void
ebews_populate_birth_date	(EContact *contact, EEwsItem *item)
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
ebews_populate_anniversary	(EContact *contact, EEwsItem *item)
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

static void
set_phone_number (EContact *contact, EContactField field, EEwsItem *item, const gchar *item_field)
{
	const gchar *pn;
	
	pn = e_ews_item_get_phone_number (item, item_field);
	if (pn && *pn)
		e_contact_set (contact, field, pn);
}

static void
ebews_populate_phone_numbers	(EContact *contact, EEwsItem *item)
{
	gint i;
	
	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++)
		set_phone_number (contact, phone_field_map[i].field, item, phone_field_map[i].element);
}

static void
copy_ews_address_to_contact_address ( EContactAddress *contact_addr, const EwsAddress *address)
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
set_address (EContact *contact, EContactField field, EEwsItem *item, const gchar *item_field)
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
ebews_populate_address		(EContact *contact, EEwsItem *item)
{
	
	set_address (contact, E_CONTACT_ADDRESS_WORK, item, "Business");
	set_address (contact, E_CONTACT_ADDRESS_HOME, item, "Home");
	set_address (contact, E_CONTACT_ADDRESS_OTHER, item, "Other");
}

static void
ebews_populate_ims		(EContact *contact, EEwsItem *item)
{
	/* TODO : The fields returned by server does not match with the EContact fields
	   for the IMS, handle it later */
}

static void
set_email_address (EContact *contact, EContactField field, EEwsItem *item, const gchar *item_field)
{
	const gchar *ea;
	
	ea = e_ews_item_get_email_address (item, item_field);
	if (ea && *ea)
		e_contact_set (contact, field, ea);
}

static void
ebews_populate_emails		(EContact *contact, EEwsItem *item)
{
	set_email_address (contact, E_CONTACT_EMAIL_1, item, "EmailAddress1");
	set_email_address (contact, E_CONTACT_EMAIL_2, item, "EmailAddress2");
	set_email_address (contact, E_CONTACT_EMAIL_3, item, "EmailAddress3");
}

static void
ebews_set_item_id		(ESoapMessage *message, EContact *contact)
{
	
}

static void
ebews_set_full_name		(ESoapMessage *msg, EContact *contact)
{
	EContactName *name;

	name = e_contact_get (contact, E_CONTACT_NAME);
	if (!name)
		return;
	
	if (name->given)
		e_ews_message_write_string_parameter(msg, "GivenName", NULL, name->given);
	
	if (name->additional && *name->additional)
		e_ews_message_write_string_parameter(msg, "MiddleName", NULL, name->additional);

	e_contact_name_free (name);
}
/* TODO Set birth and anniversary dates */
static void
ebews_set_birth_date		(ESoapMessage *message, EContact *contact)
{
	
}

static void
ebews_set_anniversary		(ESoapMessage *message, EContact *contact)
{
	
}

static gboolean
add_entry (ESoapMessage *msg, EContact *contact, EContactField field, const gchar *entry_name, const gchar *include_hdr)
{
	gchar *entry_val;

	entry_val = e_contact_get (contact, field);
	
	if (entry_val && *entry_val){
		if (include_hdr)
			e_soap_message_start_element(msg, include_hdr, NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute(msg, "Entry", NULL, entry_val, "Key", entry_name);
		
		g_free (entry_val);
		return TRUE;
	}

	g_free (entry_val);
	return FALSE;
}

static void
ebews_set_phone_numbers		(ESoapMessage *msg, EContact *contact)
{
	gint i;
	const gchar *include_hdr = "PhoneNumbers";
	
	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++) {
		if (add_entry (msg, contact, phone_field_map[i].field, phone_field_map[i].element, include_hdr))
			include_hdr = NULL;
	}

	if (!include_hdr)
		e_soap_message_end_element(msg);
}

static gboolean
add_physical_address (ESoapMessage *msg, EContact *contact, EContactField field, const gchar *entry_name, gboolean include_start_hdr)
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
ebews_set_address	(ESoapMessage *msg, EContact *contact)
{
	gboolean include_hdr = TRUE;

	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_WORK, "Business", include_hdr))
		include_hdr = FALSE;
	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_HOME, "Home", include_hdr))
		include_hdr = FALSE;
	if (add_physical_address (msg, contact, E_CONTACT_ADDRESS_OTHER, "Other", include_hdr))
		include_hdr = FALSE;

	if (!include_hdr)
		e_soap_message_end_element(msg);
}

static void
ebews_set_ims			(ESoapMessage *message, EContact *contact)
{
	
}

static void
ebews_set_emails		(ESoapMessage *msg, EContact *contact)
{
	const gchar *include_hdr = "EmailAddresses";
	
	if (add_entry (msg, contact, E_CONTACT_EMAIL_1, "EmailAddress1", include_hdr))
		include_hdr = NULL;
	if (add_entry (msg, contact, E_CONTACT_EMAIL_2, "EmailAddress2", include_hdr))
		include_hdr = NULL;
	if (add_entry (msg, contact, E_CONTACT_EMAIL_3, "EmailAddress3", include_hdr))
		include_hdr = NULL;

	if (!include_hdr)
		e_soap_message_end_element(msg);
}

static void
convert_contact_property_to_updatexml (ESoapMessage *msg, const gchar *name, const gchar *value, const gchar * prefix, const gchar *attr_name, const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Contact");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_indexed_contact_property_to_updatexml (ESoapMessage *message, const gchar *name, const gchar *value, const gchar * prefix, const gchar *element_name, const gchar *key)
{
	gboolean delete_field = FALSE;

	if(!value || !g_strcmp0(value, ""))
		delete_field = TRUE;
	e_ews_message_start_set_indexed_item_field (message, name , prefix, "Contact", key, delete_field);
	
	if(!delete_field)
	{
		e_soap_message_start_element(message, element_name, NULL, NULL);
		e_ews_message_write_string_parameter_with_attribute(message, "Entry", NULL, value, "Key", key);
		e_soap_message_end_element(message);
	}
	e_ews_message_end_set_indexed_item_field (message, delete_field);
}


static void
ebews_set_full_name_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	EContactName *name, *old_name;

	name = e_contact_get (new, E_CONTACT_NAME);
	old_name = e_contact_get (old, E_CONTACT_NAME);
	if (!name && !old_name)
		return;
	
	if (g_ascii_strcasecmp(name->given, old_name->given))
		convert_contact_property_to_updatexml(message, "GivenName", name->given, "contacts", NULL, NULL);
	
	if (g_ascii_strcasecmp(name->additional, old_name->additional))
		convert_contact_property_to_updatexml(message, "MiddleName", name->additional, "contacts", NULL, NULL);

	e_contact_name_free (name);
	e_contact_name_free (old_name);
	
}

static void
ebews_set_birth_date_changes	(ESoapMessage *message, EContact *new, EContact *old)
{

}

static void
ebews_set_anniversary_changes	(ESoapMessage *message, EContact *new, EContact *old)
{

}

static void
ebews_set_phone_number_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	gint i;
	gchar *new_value, *old_value;
	
	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++) {
		new_value = e_contact_get (new, phone_field_map[i].field);
		old_value = e_contact_get (old, phone_field_map[i].field);
		if((new_value && !old_value) || (!new_value && old_value) ||(new_value && old_value && g_ascii_strcasecmp(new_value, old_value)))
			convert_indexed_contact_property_to_updatexml (message, "PhoneNumber", new_value, "contacts", "PhoneNumbers", phone_field_map[i].element);
		if(new_value)
			g_free(new_value);
		
		if(old_value)
			g_free(old_value);
	}
}

static void
convert_indexed_contact_property_to_updatexml_physical_address (ESoapMessage *message, const gchar *name, const gchar *uri_element, const gchar *value, const gchar * prefix, const gchar *element_name, const gchar *key)
{
	gchar * fielduri = NULL;
	gboolean delete_field = FALSE;

	if(!value || !g_strcmp0(value, ""))
		delete_field = TRUE;

	fielduri = g_strconcat (name, ":", uri_element, NULL);

	e_ews_message_start_set_indexed_item_field (message, fielduri , prefix, "Contact", key, delete_field);
	
	if(!delete_field)
	{
		e_soap_message_start_element(message, element_name, NULL, NULL);

		e_soap_message_start_element (message, "Entry", NULL, NULL);
		e_soap_message_add_attribute (message, "Key", key, NULL, NULL);
		e_ews_message_write_string_parameter (message, uri_element, NULL, value);
		e_soap_message_end_element(message);

		e_soap_message_end_element(message);
	}
	e_ews_message_end_set_indexed_item_field (message, delete_field);
}

static void compare_address(ESoapMessage *message, EContact *new, EContact *old, EContactField field, const char *key)
{
	EContactAddress *new_address, *old_address;
	gboolean set = FALSE;

	new_address = e_contact_get(new, field);
	old_address = e_contact_get(old, field);

	if(!new_address && !old_address)
		return;

	if(!old_address && new_address)
		set = TRUE;

	if(!new_address && old_address)
	{
		set = TRUE;
		new_address = g_new0(EContactAddress, 1);
	}

	if (set || g_ascii_strcasecmp(new_address->street, old_address->street))
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "Street", new_address->street, "contacts", "PhysicalAddresses", key);
	if (set || g_ascii_strcasecmp(new_address->locality, old_address->locality))
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "City", new_address->locality, "contacts", "PhysicalAddresses", key);
	if (set || g_ascii_strcasecmp(new_address->region, old_address->region))
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "State", new_address->region, "contacts", "PhysicalAddresses", key);
	if (set || g_ascii_strcasecmp(new_address->code, old_address->code))
		convert_indexed_contact_property_to_updatexml_physical_address (message, "PhysicalAddress", "PostalCode", new_address->code, "contacts", "PhysicalAddresses", key);

	e_contact_address_free(old_address);
	e_contact_address_free(new_address);
}

static void
ebews_set_address_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	compare_address(message, new, old, E_CONTACT_ADDRESS_WORK, "Business");
	compare_address(message, new, old, E_CONTACT_ADDRESS_HOME, "Home");
	compare_address(message, new, old, E_CONTACT_ADDRESS_OTHER, "Other");
}

static void
ebews_set_im_changes		(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_email_changes		(ESoapMessage *message, EContact *new, EContact *old)
{
	gchar *new_value, *old_value;
		
	new_value = e_contact_get (new, E_CONTACT_EMAIL_1);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_1);
	if((new_value && !old_value) || (!new_value && old_value) ||(new_value && old_value && g_ascii_strcasecmp(new_value, old_value)))
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress1");
	if(new_value)
		g_free(new_value);
	if (old_value)
		g_free(old_value);

	new_value = e_contact_get (new, E_CONTACT_EMAIL_2);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_2);
	if((new_value && !old_value) || (!new_value && old_value) ||(new_value && old_value && g_ascii_strcasecmp(new_value, old_value)))
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress2");
	if(new_value)
		g_free(new_value);
	if (old_value)
		g_free(old_value);

	new_value = e_contact_get (new, E_CONTACT_EMAIL_3);
	old_value = e_contact_get (old, E_CONTACT_EMAIL_3);
	if((new_value && !old_value) || (!new_value && old_value) ||(new_value && old_value && g_ascii_strcasecmp(new_value, old_value)))
		convert_indexed_contact_property_to_updatexml (message, "EmailAddress", new_value, "contacts", "EmailAddresses", "EmailAddress3");
	if(new_value)
		g_free(new_value);
	if (old_value)
		g_free(old_value);
}

static const struct field_element_mapping {
	EContactField field_id;
	gint element_type;
	const gchar *element_name;
	/* set function for simple string type values */
	const gchar * (*get_simple_prop_func) (EEwsItem *item);
	void (*populate_contact_func)(EContact *contact, EEwsItem *item);
	void (*set_value_in_soap_message) (ESoapMessage *message, EContact *contact);
	void (*set_changes) (ESoapMessage *message, EContact *new, EContact *old);

} mappings[] = {
	/* The order should be maintained for create contacts to work */
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
	{ E_CONTACT_MANAGER, ELEMENT_TYPE_SIMPLE, "OfficeLocation", e_ews_item_get_office_location},
	{ E_CONTACT_SPOUSE, ELEMENT_TYPE_SIMPLE, "Profession", e_ews_item_get_profession},
	{ E_CONTACT_SPOUSE, ELEMENT_TYPE_SIMPLE, "SpouseName", e_ews_item_get_spouse_name},
	{ E_CONTACT_FAMILY_NAME, ELEMENT_TYPE_SIMPLE, "Surname", e_ews_item_get_surname},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "WeddingAnniversary", NULL,  ebews_populate_anniversary, ebews_set_anniversary, ebews_set_anniversary_changes },

	/* Should take of uid and changekey (REV) */
	{ E_CONTACT_UID, ELEMENT_TYPE_COMPLEX, "ItemId", NULL,  ebews_populate_uid, ebews_set_item_id},
};


typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	EContact *contact;
	guint32 opid;
} EwsCreateContact;

static void
convert_contact_to_xml (ESoapMessage *msg, gpointer user_data)
{
	EContact *contact = (EContact*)user_data;
	gint i, element_type;

	/* Prepare Contact node in the SOAP message */
	e_soap_message_start_element(msg, "Contact", NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE) {
			char *val = e_contact_get (contact, mappings [i].field_id);

			/* skip uid while creating contacts */
			if (mappings [i].field_id == E_CONTACT_UID)
				continue;

			if (val && *val)
				e_ews_message_write_string_parameter(msg, mappings[i].element_name, NULL, val);
			g_free (val);
		} else
			mappings[i].set_value_in_soap_message (msg, contact);
	}

	// end of "Contact"
	e_soap_message_end_element(msg);
}

static void
ews_create_contact_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCreateContact *create_contact = user_data;
	EBookBackendEws *ebews = create_contact->ebews;
	GError *error = NULL;
	GSList *items = NULL;
	const EwsId *item_id;

	/* get a list of ids from server (single item) */
	e_ews_connection_create_items_finish(cnc, res, &items, &error);

	if (error == NULL) {
		EEwsItem *item = (EEwsItem *) items->data;

		/* set item id */
		item_id = e_ews_item_get_id((EEwsItem *)items->data);

		e_contact_set (create_contact->contact, E_CONTACT_UID, item_id->id);
		e_contact_set (create_contact->contact, E_CONTACT_REV, item_id->change_key);
		e_book_backend_sqlitedb_add_contact (ebews->priv->ebsdb, ebews->priv->folder_id, create_contact->contact, FALSE, &error);

		if (error == NULL)
			e_data_book_respond_create (create_contact->book, create_contact->opid, EDB_ERROR (Success), create_contact->contact);

		g_object_unref (item);
		g_slist_free (items);
	}
	
	if (error) {
		g_warning("Error while Creating contact: %s", error->message);
		e_data_book_respond_create (create_contact->book, create_contact->opid, EDB_ERROR (OtherError), create_contact->contact);
	}

	/* free memory allocated for create_contact & unref contained objects */
	g_object_unref(create_contact->ebews);
	g_object_unref(create_contact->contact);
	g_free(create_contact);
	g_clear_error (&error);
}

static void
e_book_backend_ews_create_contact	(EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 GCancellable *cancellable,
					 const gchar *vcard )
{
	EContact *contact = NULL;
	EBookBackendEws *ebews;
	EwsCreateContact *create_contact;
	EBookBackendEwsPrivate *priv;
 
	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	switch (ebews->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (!ebews->priv->is_writable) {
			e_data_book_respond_create (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}

		e_data_book_respond_create (book, opid, EDB_ERROR (RepositoryOffline), NULL);
		return;

	case  GNOME_Evolution_Addressbook_MODE_REMOTE :

		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_create (book, opid, EDB_ERROR (AuthenticationRequired), NULL);
			return;
		}

		if (!ebews->priv->is_writable) {
			e_data_book_respond_create (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}

		contact = e_contact_new_from_vcard (vcard);
		
		if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
			g_object_unref (contact);
			e_data_book_respond_create (book, opid, EDB_ERROR (OtherError), NULL);
			return;
		}

		create_contact = g_new0(EwsCreateContact, 1);
		create_contact->ebews = g_object_ref(ebews);
		create_contact->book = g_object_ref(book);
		create_contact->opid = opid;
		create_contact->contact = g_object_ref(contact);

		/* pass new contact component data to the exchange server and expect response in the callback */
		e_ews_connection_create_items_start (priv->cnc,
						     EWS_PRIORITY_MEDIUM, NULL,
						     NULL,
						     priv->folder_id,
						     convert_contact_to_xml,
						     contact,
						     ews_create_contact_cb,
						     cancellable,
						     create_contact);
		return;
	default:
		break;
	}
}

typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	guint32 opid;
	GSList *sl_ids;
} EwsRemoveContact;

static void
ews_book_remove_contact_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EwsRemoveContact *remove_contact = user_data;
	EBookBackendEws *ebews = remove_contact->ebews;
	EBookBackendEwsPrivate *priv = ebews->priv;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	gboolean deleted = FALSE;

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (!g_simple_async_result_propagate_error(simple, &error))
		deleted = e_book_backend_sqlitedb_remove_contacts (priv->ebsdb, priv->folder_id, remove_contact->sl_ids, &error);

	if (deleted) {
		GList *dl_ids = NULL;
		GSList *l;

		/* This is pretty ugly, but cant help */
		for (l = remove_contact->sl_ids; l != NULL; l = g_slist_next (l))
			dl_ids = g_list_prepend (dl_ids, l->data);

		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR (Success),  dl_ids);
		g_list_free (dl_ids);
	} else {
		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR (OtherError), NULL);
		g_warning ("\nError removing contact %s \n", error->message);
	}

	g_slist_foreach (remove_contact->sl_ids, (GFunc) g_free, NULL);
	g_slist_free (remove_contact->sl_ids);
	g_object_unref (remove_contact->ebews);
	g_object_unref (remove_contact->book);
	g_free (remove_contact);
	g_clear_error (&error);
}

static void
e_book_backend_ews_remove_contacts	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32 opid,
					 GCancellable *cancellable,
					 const GSList *id_list)
{
	EBookBackendEws *ebews;
	EwsRemoveContact *remove_contact;
	EBookBackendEwsPrivate *priv;
 
	ebews = E_BOOK_BACKEND_EWS (backend);
 
	priv = ebews->priv;

	switch (ebews->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (!ebews->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}
	
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (RepositoryOffline), NULL);
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (AuthenticationRequired), NULL);
			return;
		}

		if (!ebews->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}

		remove_contact = g_new0(EwsRemoveContact, 1);
		remove_contact->ebews = g_object_ref(ebews);
		remove_contact->book = g_object_ref(book);
		remove_contact->opid = opid;
		remove_contact->sl_ids = (GSList *) id_list;

		e_ews_connection_delete_items_start (priv->cnc, EWS_PRIORITY_MEDIUM, (GSList *) id_list,
						     EWS_HARD_DELETE, 0 , FALSE,
						     ews_book_remove_contact_cb, cancellable,
						     remove_contact);
		return;
	default :
		break;
	}
}

typedef struct {
	EBookBackendEws *ebews;
	EDataBook *book;
	EContact *new_contact;
	EContact *old_contact;
	guint32 opid;
} EwsModifyContact;

static void
ews_modify_contact_cb (GObject *object, GAsyncResult *res, gpointer user_data)
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


	e_ews_connection_update_items_finish(cnc, res, &items, &error);

	if (error == NULL) {
		EEwsItem *item = (EEwsItem *) items->data;

		/* set item id */
		item_id = e_ews_item_get_id((EEwsItem *)items->data);

		e_contact_set (modify_contact->new_contact, E_CONTACT_UID, item_id->id);
		e_contact_set (modify_contact->new_contact, E_CONTACT_REV, item_id->change_key);

		id = e_contact_get (modify_contact->old_contact, E_CONTACT_UID);

		e_book_backend_sqlitedb_remove_contact (priv->ebsdb, priv->folder_id, id, &error);
		e_book_backend_sqlitedb_add_contact (ebews->priv->ebsdb, ebews->priv->folder_id, modify_contact->new_contact, FALSE, &error);

		if (error == NULL)
			e_data_book_respond_modify (modify_contact->book, modify_contact->opid, EDB_ERROR (Success), modify_contact->new_contact);

		g_object_unref (item);
		g_slist_free (items);
	}
	
	if (error) {
		g_warning("Error while Creating contact: %s", error->message);
		e_data_book_respond_modify (modify_contact->book, modify_contact->opid, EDB_ERROR (OtherError), modify_contact->new_contact);
	}

	/* free memory allocated for create_contact & unref contained objects */
	g_object_unref(modify_contact->ebews);
	g_object_unref(modify_contact->new_contact);
	g_object_unref(modify_contact->old_contact);
	g_free(modify_contact);
	g_clear_error (&error);
}

static void
convert_contact_to_updatexml (ESoapMessage *msg, gpointer user_data)
{
	EwsModifyContact *modify_contact = user_data;
	EwsId *id;
	EContact *old_contact = modify_contact->old_contact;
	EContact *new_contact = modify_contact->new_contact;
	gchar *value = NULL, *old_value = NULL ;
	gint i, element_type;

	id = g_new0 (EwsId, 1);
	id->id = e_contact_get (old_contact, E_CONTACT_UID);
	id->change_key = e_contact_get (old_contact, E_CONTACT_REV);

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
                                         id->id, id->change_key, 0);

	/*Iterate for each field in contact*/

	for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;
		if (element_type == ELEMENT_TYPE_SIMPLE)  {
			value =  e_contact_get (new_contact, mappings[i].field_id);
			old_value =  e_contact_get (old_contact, mappings[i].field_id);
			if (value)
			{
				if(( *value && !old_value) || g_ascii_strcasecmp (value, old_value))
					convert_contact_property_to_updatexml(msg, mappings[i].element_name, value, "contacts", NULL, NULL);
				g_free(value);
			}
			if(old_value)
				g_free(old_value);
		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			if(mappings [i].field_id == E_CONTACT_UID)
				continue;
			mappings[i].set_changes (msg, new_contact, old_contact);
		}
	}
	
	e_ews_message_end_item_change (msg);
}

static void
e_book_backend_ews_modify_contact	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 GCancellable *cancellable,
					 const gchar   *vcard)
{
	EContact *contact = NULL, *old_contact;
	EwsModifyContact *modify_contact;
	EBookBackendEws *ebews;
	EwsId *id;
	EBookBackendEwsPrivate *priv;
	GError *error;


	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	switch (priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (!ebews->priv->is_writable) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}

		e_data_book_respond_modify (book, opid, EDB_ERROR (RepositoryOffline), NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE :

		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (AuthenticationRequired), NULL);
			return;
		}
		
		if (!ebews->priv->is_writable) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (PermissionDenied), NULL);
			return;
		}
		
		contact = e_contact_new_from_vcard (vcard);

		id = g_new0 (EwsId, 1);
		id->id = e_contact_get (contact, E_CONTACT_UID);
		id->change_key = e_contact_get (contact, E_CONTACT_REV);

		/*get item id and change key from contact and fetch old contact and assign.*/

		if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
			g_object_unref (contact);
			e_data_book_respond_modify (book, opid, EDB_ERROR (OtherError), NULL);
			return;
		}

		old_contact = e_book_backend_sqlitedb_get_contact ( priv->ebsdb, priv->folder_id,
					 id->id, NULL, NULL, &error); 
		if (!old_contact) {
			g_object_unref (contact);
			e_data_book_respond_modify (book, opid, EDB_ERROR (OtherError), NULL);
			return;
		}

		/* TODO implement */
		modify_contact = g_new0 (EwsModifyContact, 1);
		modify_contact->ebews = g_object_ref(ebews);
		modify_contact->book = g_object_ref(book);
		modify_contact->opid = opid;
		modify_contact->old_contact = g_object_ref(old_contact);
		modify_contact->new_contact = g_object_ref(contact);
		e_ews_connection_update_items_start (priv->cnc, EWS_PRIORITY_MEDIUM,
							"AlwaysOverwrite", "SendAndSaveCopy",
							"SendToAllAndSaveCopy", priv->folder_id,
							convert_contact_to_updatexml, modify_contact,
							ews_modify_contact_cb, cancellable,
							modify_contact);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact	(EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 GCancellable *cancellable,
				 const gchar   *id)
{
	EBookBackendEws *gwb;

	gwb =  E_BOOK_BACKEND_EWS (backend);

	switch (gwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (ContactNotFound), "");
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		if (gwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact (book, opid, EDB_ERROR (OtherError), NULL);
			return;
		}
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (ContactNotFound), "");
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact_list	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 GCancellable *cancellable,
					 const gchar   *query )
{
	GList *vcard_list;
	EBookBackendEws *egwb;

	egwb = E_BOOK_BACKEND_EWS (backend);
	vcard_list = NULL;

	switch (egwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (Success), vcard_list);
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (AuthenticationRequired), NULL);
			return;
		}

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (Success), vcard_list);
		return;
	default :
		break;

	}
}

typedef struct {
	/* For future use */
	gpointer restriction;

	gboolean is_query_handled;
	gboolean is_autocompletion;
	gchar *auto_comp_str;
} EBookBackendEwsSExpData;

static ESExpResult *
func_not (ESExp *f, gint argc, ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_UNDEFINED) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_and_or (ESExp *f, gint argc, ESExpResult **argv, gpointer and)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

/* TODO implement */
static ESExpResult *
func_is (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
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
	r->value.bool = FALSE;

	sdata->is_query_handled = FALSE;
	return r;
}

/* TODO implement */
static ESExpResult *
func_endswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
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
	r->value.bool = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* TODO implement */
static ESExpResult *
func_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
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
	r->value.bool = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* We are just handling for autocompletion now. We need to support other fields after implementing
   Restrictions and find_items request */
static ESExpResult *
func_beginswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
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
	r->value.bool = FALSE;
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
e_book_backend_ews_build_restriction (const gchar *query, gboolean *autocompletion, gchar **auto_comp_str)
{
	ESExpResult *r;
	ESExp *sexp;
	EBookBackendEwsSExpData *sdata;
	gint i;

	sexp = e_sexp_new ();
	sdata = g_new0 (EBookBackendEwsSExpData, 1);

	sdata->is_query_handled = TRUE;

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		e_sexp_add_function (sexp, 0, (gchar *) symbols[i].name,
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


static gboolean
ews_gal_needs_update (EBookBackendEws *cbews, EwsOALDetails *full, GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	guint32 seq;
	gboolean ret = FALSE;
	gchar *tmp;

	tmp = e_book_backend_sqlitedb_get_key_value (priv->ebsdb, priv->folder_id, "seq", error);
	if (error)
		goto exit;

	sscanf (tmp, "%"G_GUINT32_FORMAT, &seq);
	if (seq < full->seq)
		ret = TRUE;
	
	d(printf ("Gal needs update: %d \n", ret);)
exit:
	g_free (tmp);
	return ret;	
}

static gchar *
ews_download_full_gal (EBookBackendEws *cbews, EwsOALDetails *full, GCancellable *cancellable, GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	EEwsConnection *oab_cnc;
	gchar *full_url, *oab_url, *cache_file = NULL;
	gchar *cache_dir;
	gchar *comp_cache_file = NULL, *uncompress_file = NULL;
	ESource *source;

	/* oab url with oab.xml removed from the suffix */
	oab_url = g_strndup (priv->oab_url, strlen (priv->oab_url) - 7);
	full_url = g_strconcat (oab_url, full->filename, NULL);
	source = e_book_backend_get_source (E_BOOK_BACKEND (cbews));
	cache_dir = ews_get_cache_dir (E_BOOK_BACKEND (cbews), source);
	comp_cache_file = g_build_filename (cache_dir, full->filename, NULL);

	oab_cnc = e_ews_connection_new (full_url, priv->username, priv->password, NULL, NULL, NULL);
	if (!e_ews_connection_download_oal_file (oab_cnc, comp_cache_file, NULL, NULL, cancellable, error))
		goto exit;

	cache_file = g_strdup_printf ("%s-%d.oab", priv->folder_name, full->seq);
	uncompress_file = g_build_filename (cache_dir, cache_file, NULL);
	if (!oal_decompress_v4_full_detail_file (comp_cache_file, uncompress_file, error)) {
		g_free (uncompress_file);
		uncompress_file = NULL;
		goto exit;
	}

	d(g_print ("OAL file decompressed %s \n", uncompress_file);)

exit:	
	if (comp_cache_file)
		g_unlink (comp_cache_file);
	g_free (cache_dir);
	g_object_unref (oab_cnc);
	g_free (oab_url);
	g_free (full_url);
	g_free (comp_cache_file);
	g_free (cache_file);

	return uncompress_file;
}

static gboolean
ews_remove_old_gal_file (EBookBackendEws *cbews, GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	gchar *filename;

	filename = e_book_backend_sqlitedb_get_key_value (priv->ebsdb, priv->folder_id, "oab-filename", error);
	if (*error)
		return FALSE;

	if (filename)
		g_unlink (filename);
	
	return TRUE;
}

struct _db_data {
	GSList *contact_collector;
	guint collected_length;
	EBookBackendEws *cbews;
};

static void
ews_gal_store_contact (EContact *contact, goffset offset, guint percent, gpointer user_data, GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;
	EBookBackendEwsPrivate *priv = data->cbews->priv;

	data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
	data->collected_length += 1;

	if (data->collected_length == 1000 || percent >= 100) {
		GSList *l;
		gchar *status_message=NULL;
		EDataBookView *book_view = e_book_backend_ews_utils_get_book_view (E_BOOK_BACKEND (data->cbews));

		d(g_print ("GAL adding contacts, percent complete : %d \n", percent);)

		status_message = g_strdup_printf (_("Downloading contacts in %s %d%% completed... "), priv->folder_name, percent);
		if (book_view)
			e_data_book_view_notify_status_message (book_view, status_message);

		data->contact_collector = g_slist_reverse (data->contact_collector);
		e_book_backend_sqlitedb_add_contacts (priv->ebsdb, priv->folder_id, data->contact_collector, FALSE, error);

		for (l = data->contact_collector; l != NULL; l = g_slist_next (l))
			e_book_backend_notify_update (E_BOOK_BACKEND (data->cbews), E_CONTACT (l->data));

		/* reset data */
		if (book_view)
			e_data_book_view_unref (book_view);
		g_free (status_message);
		g_slist_foreach (data->contact_collector, (GFunc) g_object_unref, NULL);
		g_slist_free (data->contact_collector);
		data->contact_collector = NULL;
		data->collected_length = 0;
	}

	if (percent == 100)
		e_book_backend_notify_complete (E_BOOK_BACKEND (data->cbews));
}

static gboolean
ews_replace_gal_in_db (EBookBackendEws *cbews, const gchar *filename, GCancellable *cancellable, GError **error)
{
	EBookBackendEwsPrivate *priv = cbews->priv;
	EwsOabDecoder *eod;
	gboolean ret = TRUE;
	struct _db_data data;

	/* remove the old address-book and create a new one in db */
	if (e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL)) {
		ret = e_book_backend_sqlitedb_delete_addressbook (priv->ebsdb, priv->folder_id, error);
		ews_remove_attachments (priv->attachment_dir);
		if (ret)
			ret = e_book_backend_sqlitedb_create_addressbook (priv->ebsdb, priv->folder_id, priv->folder_name, TRUE, error);
	}

	if (!ret)
		return FALSE;

	eod = ews_oab_decoder_new (filename, priv->attachment_dir, error);
	if (*error)
		return FALSE;

	data.contact_collector = NULL;
	data.collected_length = 0;
	data.cbews = cbews;

	ret = ews_oab_decoder_decode (eod, ews_gal_store_contact, &data, cancellable, error);
	if (!ret)
	       return ret;

	/* mark the db as populated */
	ret = e_book_backend_sqlitedb_set_is_populated (priv->ebsdb, priv->folder_id, TRUE, error);

	return ret;
}

static gboolean
ebews_start_gal_sync	(gpointer data)
{
	EBookBackendEws *cbews;
	EBookBackendEwsPrivate *priv;
	EwsOALDetails *full = NULL;
	GError *error = NULL;
	EEwsConnection *oab_cnc;
	GSList *full_l = NULL;
	gboolean ret = TRUE;
	gchar *uncompressed_filename = NULL;
	GCancellable *cancellable;

	cbews = (EBookBackendEws *) data;
	priv = cbews->priv;

	cancellable = g_cancellable_new ();
	oab_cnc = e_ews_connection_new (priv->oab_url, priv->username, priv->password, NULL, NULL, NULL);

	d(printf ("Ewsgal: Fetching oal full details file \n");)

	if (!e_ews_connection_get_oal_detail (oab_cnc, priv->folder_id, "Full", &full_l, cancellable, &error)) {
		ret = FALSE;
		goto exit;
	}

	full = (EwsOALDetails *) full_l->data; 
	/* TODO fetch differential updates if available instead of downloading the whole GAL */
	if (!e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL) || ews_gal_needs_update (cbews, full, &error)) {
		gchar *seq;
		
		d(printf ("Ewsgal: Downloading full gal \n");)
		uncompressed_filename = ews_download_full_gal (cbews, full, cancellable, &error);
		if (error) {
			ret = FALSE;
			goto exit;
		}

		d(printf ("Ewsgal: Removing old gal \n");)
		/* remove old_gal_file */
		ret = ews_remove_old_gal_file (cbews, &error);
		if (!ret) {
			goto exit;
		}

		d(printf ("Ewsgal: Replacing old gal with new gal contents in db \n");)
		ret = ews_replace_gal_in_db (cbews, uncompressed_filename, cancellable, &error);
		if (!ret)
			goto exit;
	
		seq = g_strdup_printf ("%"G_GUINT32_FORMAT, full->seq);
		ret = e_book_backend_sqlitedb_set_key_value (priv->ebsdb, priv->folder_id, "seq", seq, &error);
		g_free (seq);
		
		if (!ret) {
			e_book_backend_sqlitedb_delete_addressbook (priv->ebsdb, priv->folder_id, &error);
			goto exit;
		}
	}
	
	d(printf ("Ews gal: sync successfull complete \n");)
	
exit:
	if (error) {
		g_warning ("Unable to update gal : %s \n", error->message);
		g_clear_error (&error);
	}

	/* preserve  the oab file once we are able to decode the differential updates */
	if (uncompressed_filename) {
		g_unlink (uncompressed_filename);
		g_free (uncompressed_filename);
	}

	if (full_l) {
		g_free (full->sha);
		g_free (full->filename);
		g_free (full);
		g_slist_free (full_l);
	}

	g_object_unref (oab_cnc);
	return ret;
}

/********** GAL sync **************************/

/**
 * ebews_sync_deleted_items 
 * @ebews: 
 * @deleted_items: 
 * @error: cannot be NULL 
 **/
static void
ebews_sync_deleted_items (EBookBackendEws *ebews, GSList *deleted_ids, GError **error)
{
	GSList *l;
	EBookBackendEwsPrivate *priv;

	priv = ebews->priv;

	for (l = deleted_ids; l != NULL; l = g_slist_next (l)) {
		gchar *id = (gchar *) l->data;
		gboolean partial_content;
		
		if (e_book_backend_sqlitedb_has_contact (priv->ebsdb, priv->folder_id, id, &partial_content, NULL))
			e_book_backend_sqlitedb_remove_contact (priv->ebsdb, priv->folder_id, id, error);
		e_book_backend_notify_remove (E_BOOK_BACKEND (ebews), id);
	}

	g_slist_foreach (deleted_ids, (GFunc) g_free, NULL);
	g_slist_free (deleted_ids);
}


static void
ebews_store_contact_items (EBookBackendEws *ebews, GSList *new_items, gboolean distribution_list, GError **error)
{
	EBookBackendEwsPrivate *priv;
	GSList *l;
	
	priv = ebews->priv;

	for (l = new_items; l != NULL; l = g_slist_next (l)) {
		EContact *contact;
		gint i, element_type;
		EEwsItem *item;
	
		item = (EEwsItem *) l->data;
		contact = e_contact_new ();

		if (!distribution_list) {
			for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
				element_type = mappings[i].element_type;

				if (element_type == ELEMENT_TYPE_SIMPLE && !mappings [i].populate_contact_func) {
					const char *val = mappings [i].get_simple_prop_func (item);

					if (val != NULL)
						e_contact_set (contact, mappings [i].field_id, val);
				} else
					mappings[i].populate_contact_func (contact, item);
			}
		} else {
			/* store display_name, fileas, item id */	
		}

		e_book_backend_sqlitedb_add_contact (priv->ebsdb, priv->folder_id, contact, FALSE, error);
		e_book_backend_notify_update (E_BOOK_BACKEND (ebews), contact);
		
		g_object_unref (item);
		g_object_unref (contact);
	}


	g_slist_free (new_items);
}

static void
ews_mb_free (EwsMailbox *mb)
{
	if (mb) {
		g_free (mb->name);
		g_free (mb->email);
	
		if (mb->item_id) {
			g_free (mb->item_id->id);
			g_free (mb->item_id->change_key);
			g_free (mb->item_id);
		}

		g_free (mb);
	}
}

static void
ebews_store_distribution_list_items (EBookBackendEws *ebews, const EwsId *id, const gchar *d_name, GSList *members, GError **error)
{
	GSList *l;
	EContact *contact;

	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_UID, id->id);
	e_contact_set (contact, E_CONTACT_REV, id->change_key);
	
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));
	e_contact_set (contact, E_CONTACT_FULL_NAME, d_name);

	for (l = members; l != NULL; l = g_slist_next (l)) {
		EwsMailbox *mb = (EwsMailbox *)	l->data;
		EVCardAttribute *attr;

		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
		if (mb->name) {
			gint len = strlen (mb->name);
			gchar *value;

			if (mb->name [0] == '\"' && mb->name [len - 1] == '\"')
				value = g_strdup_printf ("%s <%s>", mb->name, mb->email);
			else
				value = g_strdup_printf ("\"%s\" <%s>", mb->name, mb->email);

			e_vcard_attribute_add_value (attr, value);
			g_free (value);
		} else
			e_vcard_attribute_add_value (attr, mb->email);

		e_vcard_add_attribute (E_VCARD (contact), attr);
		ews_mb_free (mb);
	}
	
	g_slist_free (members);
	e_book_backend_sqlitedb_add_contact (ebews->priv->ebsdb, ebews->priv->folder_id, contact, FALSE, error);
	e_book_backend_notify_update (E_BOOK_BACKEND (ebews), contact);

	g_object_unref (contact);
}

static void
ebews_sync_items (EBookBackendEws *ebews, GSList *items, GError **error)
{
	EBookBackendEwsPrivate *priv;
	EEwsConnection *cnc;
	GSList *l;
	GSList *contact_item_ids = NULL, *dl_ids = NULL;
	GSList *new_items = NULL;

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
	if (contact_item_ids)
		e_ews_connection_get_items
			(cnc, EWS_PRIORITY_MEDIUM,
			 contact_item_ids, "Default", CONTACT_ITEM_PROPS,
			 FALSE, NULL, &new_items, NULL, NULL,
			 NULL, error);
	if (*error)
		goto cleanup;

	if (new_items)
		ebews_store_contact_items (ebews, new_items, FALSE, error);
	new_items = NULL;

	/* Get the display names of the distribution lists */
	if (dl_ids)
		e_ews_connection_get_items
			(cnc, EWS_PRIORITY_MEDIUM,
			 dl_ids, "Default", NULL,
			 FALSE, NULL, &new_items, NULL, NULL,
			 NULL, error);
	if (*error)
		goto cleanup;

	for (l = new_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const gchar *d_name;
		const EwsId *id;
		EwsMailbox *mb = g_new0 (EwsMailbox, 1);
		GSList *members = NULL;
		gboolean includes_last;

		id = e_ews_item_get_id (item);
		mb = g_new0 (EwsMailbox, 1);
		mb->item_id = (EwsId *) id;

		/* expand dl */
		if (*error)
			goto cleanup;
		
		d_name = e_ews_item_get_subject (item);
		e_ews_connection_expand_dl (cnc, EWS_PRIORITY_MEDIUM, mb, &members, &includes_last, NULL, error);
		if (*error)
			goto cleanup;
		
		ebews_store_distribution_list_items (ebews, id, d_name, members, error);
		g_free (mb);

		if (*error)
			goto cleanup;
	}

cleanup:
	if (new_items) {
		g_slist_foreach (new_items, (GFunc) g_object_unref, NULL);
		g_slist_free (new_items);
	}

	if (dl_ids) {
		g_slist_foreach (dl_ids, (GFunc) g_free, NULL);
		g_slist_free (dl_ids);
	}
	
	if (contact_item_ids) {
		g_slist_foreach (contact_item_ids, (GFunc) g_free, NULL);
		g_slist_free (contact_item_ids);
	}
}

static gboolean
ebews_start_sync	(gpointer data)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;
	gchar *sync_state, *status_message = NULL;
	gboolean includes_last_item;
	GError *error = NULL;
	EDataBookView *book_view;

	ebews = (EBookBackendEws *) data;
	priv = ebews->priv;

	status_message = g_strdup (_("Syncing contacts..."));
	book_view = e_book_backend_ews_utils_get_book_view (E_BOOK_BACKEND (ebews));
	if (book_view)
		e_data_book_view_notify_status_message (book_view, status_message);

	sync_state = e_book_backend_sqlitedb_get_sync_data (priv->ebsdb, priv->folder_id, NULL);
	do
	{
		GSList *items_created = NULL, *items_updated = NULL;
		GSList *items_deleted = NULL;

		e_ews_connection_sync_folder_items	(priv->cnc, EWS_PRIORITY_MEDIUM,
							 &sync_state, priv->folder_id,
							 "IdOnly", NULL,
							 EWS_MAX_FETCH_COUNT, &includes_last_item,
							 &items_created, &items_updated,
							 &items_deleted, NULL, &error);

		if (error)
			break;

		if (items_deleted)
			ebews_sync_deleted_items (ebews, items_deleted, &error);

		if (items_created)
			ebews_sync_items (ebews, items_created, &error);

		if (error) {
			if (items_updated) {
				g_slist_foreach (items_updated, (GFunc) g_object_unref, NULL);
				g_slist_free (items_updated);
			}

			break;
		}

		if (items_updated)
			ebews_sync_items (ebews, items_updated, &error);

		if (error)
			break;

		e_book_backend_sqlitedb_set_sync_data (priv->ebsdb, priv->folder_id, sync_state, &error);
	} while (!error && !includes_last_item);

	if (book_view) {
		e_data_book_view_notify_complete (book_view, error ? EDB_ERROR (OtherError) : 0);
		e_data_book_view_unref (book_view);
	}

	if (!error)
		e_book_backend_sqlitedb_set_is_populated (priv->ebsdb, priv->folder_id, TRUE, &error);

	g_free (sync_state);
	g_free (status_message);

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
	GTimeVal timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (TRUE)	{
		gboolean succeeded;
	       
		if (!priv->is_gal)
			succeeded = ebews_start_sync (ebews);
		else
			succeeded = ebews_start_gal_sync (ebews);

		g_mutex_lock (priv->dlock->mutex);

		if (!succeeded || priv->dlock->exit)
			break;

		g_get_current_time (&timeout);
		g_time_val_add (&timeout, REFRESH_INTERVAL * 1000);
		g_cond_timed_wait (priv->dlock->cond, priv->dlock->mutex, &timeout);

		if (priv->dlock->exit)
			break;

		g_mutex_unlock (priv->dlock->mutex);
	}

	g_mutex_unlock (priv->dlock->mutex);
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
		priv->dlock->mutex = g_mutex_new ();
		priv->dlock->cond = g_cond_new ();
	}

	priv->dlock->exit = FALSE;
	priv->dthread = g_thread_create ((GThreadFunc) delta_thread, ebews, TRUE, &error);
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

	if	(priv->mode == GNOME_Evolution_Addressbook_MODE_REMOTE &&
		 priv->cnc && priv->marked_for_offline)
				fetch_deltas (ebews);

	PRIV_UNLOCK (priv);
}

static void
fetch_from_offline (EBookBackendEws *ews, EDataBookView *book_view, const gchar *query, GError *error)
{
	GSList *contacts, *l;
	EBookBackendEwsPrivate *priv;

	priv = ews->priv;

	if (priv->is_gal && !g_strcmp0 (query, "(contains \"x-evolution-any-field\" \"\")")) {
		e_data_book_view_notify_complete (book_view, error ? EDB_ERROR (OtherError) : 0);
		e_data_book_view_unref (book_view);
		return;
	}

	contacts = e_book_backend_sqlitedb_search (priv->ebsdb, priv->folder_id, query, NULL, NULL, NULL, &error);
	for (l = contacts; l != NULL; l = g_slist_next (l)) {
		EbSdbSearchData *s_data = (EbSdbSearchData *) l->data;

		/* reset vcard to NULL as it would be free'ed in prefiltered_vcard function */
		e_data_book_view_notify_update_prefiltered_vcard (book_view, s_data->uid, s_data->vcard);
		s_data->vcard = NULL;

		e_book_backend_sqlitedb_search_data_free (s_data);
	}

	g_slist_free (contacts);
	e_data_book_view_notify_complete (book_view, error ? EDB_ERROR (OtherError) : 0);
	e_data_book_view_unref (book_view);
	g_clear_error (&error);
}

static void
e_book_backend_ews_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;
	const gchar *query;
	gboolean is_autocompletion = FALSE;
	gchar *auto_comp_str = NULL;
	GCancellable *cancellable;
	GSList *ids = NULL, *mailboxes = NULL, *l;
	EwsFolderId *fid;
	gboolean includes_last_item;
	ESource *source;
	GError *error = NULL;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;
	query = e_data_book_view_get_card_query (book_view);

	e_data_book_view_ref (book_view);
	e_data_book_view_notify_status_message (book_view, _("Searching..."));

	switch (priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}

		e_data_book_view_notify_complete (book_view, EDB_ERROR (OfflineUnavailable));
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		if (!priv->cnc) {
			e_book_backend_notify_auth_required (backend);
			e_data_book_view_notify_complete (book_view, EDB_ERROR (AuthenticationRequired));
			e_data_book_view_unref (book_view);
			return;
		}

		ebews_start_refreshing (ebews);

		if (priv->ebsdb &&
		    e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}

		e_book_backend_ews_build_restriction (query, &is_autocompletion, &auto_comp_str);
		if (!is_autocompletion || !auto_comp_str) {
			g_free (auto_comp_str);
			e_data_book_view_notify_complete (book_view, error ? EDB_ERROR (OtherError) : 0);
			e_data_book_view_unref (book_view);
			g_clear_error (&error);
			return;
		}

		source = e_book_backend_get_source (backend);
		cancellable = g_cancellable_new ();

		/* FIXME Need to convert the Ids from EwsLegacyId format to EwsId format using
		   convert_id operation before using it as the schema has changed between Exchange
		   2007 and 2007_SP1 */
		fid = g_new0 (EwsFolderId, 1);
		fid->id = g_strdup (priv->folder_id);
		fid->change_key = e_source_get_duped_property (source, "change-key");
		ids = g_slist_append (ids, fid);

		/* We do not scan until we reach the last_item as it might be good enough to show first 100
		   items during auto-completion. Change it if needed. TODO, Personal Address-book should start using
		   find_items rather than resolve_names to support all queries */
		g_hash_table_insert (priv->ops, book_view, cancellable);
		e_ews_connection_resolve_names	(priv->cnc, EWS_PRIORITY_MEDIUM, auto_comp_str,
						 EWS_SEARCH_AD, NULL, FALSE, &mailboxes, NULL,
						 &includes_last_item, cancellable, &error);
		g_free (auto_comp_str);
		g_hash_table_remove (priv->ops, book_view);
		e_ews_folder_free_fid (fid);
		if (error != NULL) {
			e_data_book_view_notify_complete (book_view, EDB_ERROR (OtherError));
			e_data_book_view_unref (book_view);
			g_clear_error (&error);
			return;
		}

		for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
			EwsMailbox *mb = l->data;
			EContact *contact;

			contact = e_contact_new ();

			/* We do not get an id from the server, so just using email_id as uid for now */
			e_contact_set (contact, E_CONTACT_UID, mb->email);
			e_contact_set (contact, E_CONTACT_FULL_NAME, mb->name);
			e_contact_set (contact, E_CONTACT_EMAIL_1, mb->email);

			e_data_book_view_notify_update (book_view, contact);

			g_free (mb->email);
			g_free (mb->name);
			g_free (mb);
			g_object_unref (contact);
		}

		g_slist_free (mailboxes);
		e_data_book_view_notify_complete (book_view, EDB_ERROR (OtherError));
		e_data_book_view_unref (book_view);
		g_clear_error (&error);
	default:
		break;
	}
}

static void
e_book_backend_ews_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	EBookBackendEws *bews = E_BOOK_BACKEND_EWS (backend);
	EBookBackendEwsPrivate *priv = bews->priv;
	GCancellable *cancellable;

	cancellable = g_hash_table_lookup (priv->ops, book_view);
	if (cancellable) {
		g_cancellable_cancel (cancellable);
		g_hash_table_remove (priv->ops, book_view);
	}
}

#if ! EDS_CHECK_VERSION (3,1,0)

static void
e_book_backend_ews_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const gchar *user,
					    const gchar *passwd,
					    const gchar *auth_method)
{
	EBookBackendEws *ebgw;
	EBookBackendEwsPrivate *priv;
	ESource *esource;
	GError *error = NULL;
	const gchar *host_url;
	const gchar *read_only;

	ebgw = E_BOOK_BACKEND_EWS (backend);
	priv = ebgw->priv;

	switch (ebgw->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (Success));
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		if (priv->cnc) {
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (Success));
			return;
		}

		esource = e_book_backend_get_source (backend);
		host_url = e_source_get_property (esource, "hosturl");
		read_only = e_source_get_property (esource, "read_only");

		priv->cnc = e_ews_connection_new (host_url, user, passwd,
						  NULL, NULL, &error);

		if ((read_only && !strcmp (read_only, "true")) || priv->is_gal) {
			priv->is_writable = FALSE;
		} else 
			priv->is_writable = TRUE;

		priv->username = e_source_get_duped_property (esource, "username");
		priv->password = g_strdup (passwd);
	
		/* FIXME: Do some dummy request to ensure that the password is actually
		   correct; don't just blindly return success */
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (Success));
		e_book_backend_notify_writable (backend, priv->is_writable);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar *change_id)
{
}


static void
e_book_backend_ews_get_required_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;

	fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (Success),
						  fields);
	g_list_free (fields);

}

static void
e_book_backend_ews_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;
	gint i;
	
	for (i = 0; i < G_N_ELEMENTS (mappings); i++)
		if (mappings [i].element_type == ELEMENT_TYPE_SIMPLE)
			fields = g_list_append (fields, g_strdup (e_contact_field_name (mappings[i].field_id)));
	
	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++)
		fields = g_list_append (fields, g_strdup (e_contact_field_name (phone_field_map[i].field)));
	
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FULL_NAME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_NICKNAME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FAMILY_NAME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_1)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_HOME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_OTHER)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_BIRTH_DATE)));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (Success),
						  fields);
	g_list_free (fields);
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_ews_cancel_operation (EBookBackend *backend, EDataBook *book)
{

	return EDB_ERROR (CouldNotCancel);
}


static gchar *
ews_get_cache_dir (EBookBackend *backend, ESource *source)
{
	gchar *filename, *mangled_uri;

	mangled_uri = g_strdelimit (e_source_get_uri (source), ":/", '_');

	filename = g_build_filename (
		g_get_home_dir (), ".evolution", "cache", "addressbook", mangled_uri, NULL);

	g_free (mangled_uri);

	return filename;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_ews_load_source 	(EBookBackend           *backend,
				 ESource                *source,
				 gboolean                only_if_exists)
{
	EBookBackendEws *cbews;
	EBookBackendEwsPrivate *priv;
	const gchar *email;
	const gchar *folder_name;
	const gchar *offline, *is_gal;
	gchar *cache_dir;
	GError *err = NULL;

	cbews = E_BOOK_BACKEND_EWS (backend);
	priv = cbews->priv;

	cache_dir = ews_get_cache_dir (backend, source);
	email = e_source_get_property (source, "email");
	is_gal = e_source_get_property (source, "gal");
	
	if (is_gal && !strcmp (is_gal, "1"))
		priv->is_gal = TRUE;

	if (!priv->is_gal) {
		priv->folder_id = e_source_get_duped_property (source, "folder-id");
		folder_name = e_source_peek_name (source);

		priv->ebsdb = e_book_backend_sqlitedb_new (cache_dir, email, priv->folder_id, folder_name, TRUE, &err);
		g_free (cache_dir);
		if (err) {
			g_clear_error (&err);
			return EDB_ERROR (OtherError);
		}

		offline = e_source_get_property (source, "offline_sync");
		if (offline  && g_str_equal (offline, "1"))
			priv->marked_for_offline = TRUE;
	} else {
		priv->folder_id = e_source_get_duped_property (source, "oal_id");
	
		/* If folder_id is present it means the GAL is marked for offline usage, we do not check for offline_sync property */
		if (priv->folder_id) {
			priv->folder_name = g_strdup (e_source_peek_name (source));
			priv->oab_url = e_source_get_duped_property (source, "oab_url");

			/* setup stagging dir, remove any old files from there */
			priv->attachment_dir = g_build_filename (cache_dir, "attachments", NULL);
			g_mkdir_with_parents (priv->attachment_dir, 0777);

			priv->ebsdb = e_book_backend_sqlitedb_new (cache_dir, email, priv->folder_id, priv->folder_name, TRUE, &err);
			if (err) {
				g_clear_error (&err);
				return EDB_ERROR (OtherError);
			}
			priv->marked_for_offline = TRUE;
			priv->is_writable = FALSE;
		}	
	}
	
	e_book_backend_set_is_loaded (backend, TRUE);
	
	if (priv->mode == GNOME_Evolution_Addressbook_MODE_REMOTE)
		e_book_backend_notify_connection_status (backend, TRUE);

	return EDB_ERROR (Success);
}

static void
e_book_backend_ews_remove	(EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
	e_data_book_respond_remove (book,  opid, EDB_ERROR (Success));
}

static gchar *
e_book_backend_ews_get_static_capabilities (EBookBackend *backend)
{
	/* do-initial-query is enabled for system address book also, so that we get the
	 * book_view, which is needed for displaying cache update progress.
	 * and null query is handled for system address book.
	 */
	return g_strdup ("net,bulk-removes,do-initial-query,contact-lists");
}

static void
e_book_backend_ews_get_supported_auth_methods (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	GList *auth_methods = NULL;
	gchar *auth_method;

	auth_method =  g_strdup_printf ("plain/password");
	auth_methods = g_list_append (auth_methods, auth_method);
	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							EDB_ERROR (Success),
							auth_methods);
	g_free (auth_method);
	g_list_free (auth_methods);
}

static void
e_book_backend_ews_set_mode (EBookBackend *backend,
                                   gint mode)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;
	priv->mode = mode;

	if (e_book_backend_is_loaded (backend)) {
		if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			
			if (priv->dlock) {
				g_mutex_lock (priv->dlock->mutex);
				priv->dlock->exit = TRUE;
				g_mutex_unlock (priv->dlock->mutex);

				g_cond_signal (priv->dlock->cond);
			}
					
			if (priv->cnc) {
				g_object_unref (priv->cnc);
				priv->cnc=NULL;
			}
		}
		else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
			if (ebews->priv->is_writable)
				e_book_backend_notify_writable (backend, TRUE);
			else
				e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, TRUE);
			e_book_backend_notify_auth_required (backend);
		}
	}
}

static void
e_book_backend_ews_create_contact_compat (EBookBackend *backend,
					  EDataBook *book,
					  guint32 opid,
					  const gchar *vcard )
{
	e_book_backend_ews_create_contact (backend, book, opid, NULL, vcard);
}

static void
e_book_backend_ews_remove_contacts_compat (EBookBackend *backend,
					   EDataBook    *book,
					   guint32 opid,
					   GList *id_list)
{
	GList *l;
	GSList *sl = NULL;

	for (l = id_list; l != NULL; l = g_list_next (l))
		sl = g_slist_prepend (sl, g_strdup (l->data));
	
	sl = g_slist_reverse (sl);
	e_book_backend_ews_remove_contacts (backend, book, opid, NULL, sl);
}

static void
e_book_backend_ews_modify_contact_compat (EBookBackend *backend,
					  EDataBook    *book,
					  guint32       opid,
					  const gchar   *vcard)
{
	e_book_backend_ews_modify_contact (backend, book, opid, NULL, vcard);
}

static void
e_book_backend_ews_get_contact_compat	(EBookBackend *backend,
				 	 EDataBook    *book,
				 	 guint32       opid,
				 	 const gchar   *id)
{
	e_book_backend_ews_get_contact (backend, book, opid, NULL, id);
}

static void
e_book_backend_ews_get_contact_list_compat(EBookBackend *backend,
					   EDataBook    *book,
					   guint32       opid,
					   const gchar   *query )
{
	e_book_backend_ews_get_contact_list (backend, book, opid, NULL, query);
}

static void
e_book_backend_ews_remove_compat (EBookBackend *backend,
				  EDataBook        *book,
				  guint32           opid)
{
	e_book_backend_ews_remove (backend, book, opid);
}

#else

static void
e_book_backend_ews_authenticate_user (EBookBackend *backend,
                                      GCancellable *cancellable,
                                      ECredentials *credentials)
{
	EBookBackendEws *ebgw;
	EBookBackendEwsPrivate *priv;
	ESource *esource;
	GError *error = NULL;
	const gchar *host_url;
	const gchar *read_only;

	ebgw = E_BOOK_BACKEND_EWS (backend);
	priv = ebgw->priv;

	switch (ebgw->priv->mode) {
	case MODE_LOCAL:
		e_book_backend_notify_opened (backend, EDB_ERROR (SUCCESS));
		return;

	case MODE_REMOTE:
		if (priv->cnc) {
			e_book_backend_notify_opened (backend, EDB_ERROR (SUCCESS));
			return;
		}

		esource = e_book_backend_get_source (backend);
		host_url = e_source_get_property (esource, "hosturl");
		read_only = e_source_get_property (esource, "read_only");

		priv->cnc = e_ews_connection_new (host_url, e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME), 
						  e_credentials_peek (credentials, E_CREDENTIALS_KEY_PASSWORD),
						  NULL, NULL, &error);

		if ((read_only && !strcmp (read_only, "true")) || priv->is_gal) {
			priv->is_writable = FALSE;
		} else 
			priv->is_writable = TRUE;

		priv->username = e_source_get_duped_property (esource, "username");
		priv->password = g_strdup (e_credentials_peek (credentials, E_CREDENTIALS_KEY_PASSWORD));
	
		/* FIXME: Do some dummy request to ensure that the password is actually
		   correct; don't just blindly return success */
		e_book_backend_notify_opened (backend, EDB_ERROR (SUCCESS));
		e_book_backend_notify_readonly (backend, !priv->is_writable);
		return;
	default :
		break;
	}
}	

static void
e_book_backend_ews_set_online (EBookBackend *backend,
                                     gboolean is_online)
{
	EBookBackendEws *ebews;

	ebews = E_BOOK_BACKEND_EWS (backend);
	
	if (is_online)
		ebews->priv->mode = MODE_REMOTE;
	else
		ebews->priv->mode = MODE_LOCAL;
	if (e_book_backend_is_opened (backend)) {
		if (!is_online) {
			e_book_backend_notify_readonly (backend, TRUE);
			e_book_backend_notify_online (backend, FALSE);
			if (ebews->priv->cnc) {
				g_object_unref (ebews->priv->cnc);
				ebews->priv->cnc = NULL;
			}
		} else {
			e_book_backend_notify_readonly (backend, !ebews->priv->is_writable);
			e_book_backend_notify_online (backend, TRUE);
			e_book_backend_notify_auth_required (backend, TRUE, NULL);
		}
	}
}

static void
e_book_backend_ews_get_backend_property	(EBookBackend *backend,
                                         EDataBook *book,
                                         guint32 opid,
                                         GCancellable *cancellable,
                                         const gchar *prop_name)
{
	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		/* do-initialy-query is enabled for system address book also, so that we get the
		 * book_view, which is needed for displaying cache update progress.
		 * and null query is handled for system address book.
		 */
		e_data_book_respond_get_backend_property (book, opid, NULL, "net,bulk-removes,do-initial-query,contact-lists");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		gchar *fields_str;
		GSList *fields = NULL;
		gint i;

		for (i = 0; i < G_N_ELEMENTS (mappings); i++)
			if (mappings [i].element_type == ELEMENT_TYPE_SIMPLE)
				fields = g_slist_append (fields, g_strdup (e_contact_field_name (mappings[i].field_id)));

		for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++)
			fields = g_slist_append (fields, g_strdup (e_contact_field_name (phone_field_map[i].field)));

		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FULL_NAME)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_NICKNAME)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FAMILY_NAME)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_1)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_HOME)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_OTHER)));
		fields = g_slist_append (fields, g_strdup (e_contact_field_name (E_CONTACT_BIRTH_DATE)));

		fields_str = e_data_book_string_slist_to_comma_string (fields);

		e_data_book_respond_get_backend_property (book, opid, NULL, fields_str);

		g_slist_free (fields);
		g_free (fields_str);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "plain/password");
	} else {
		E_BOOK_BACKEND_CLASS (e_book_backend_ews_parent_class)->get_backend_property (backend, book, opid, cancellable, prop_name);
	}
}

static void
e_book_backend_ews_open (EBookBackend *backend,
                         EDataBook *book,
                         guint opid,
                         GCancellable *cancellable,
                         gboolean only_if_exists)
{
	GError *error = NULL;
	ESource *source;

	source = e_book_backend_get_source (backend);
	e_book_backend_ews_load_source (backend, source, only_if_exists, &error);
	e_data_book_respond_open (book, opid, error);
}

#endif


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

static void
e_book_backend_ews_dispose (GObject *object)
{
	EBookBackendEws *bgw;
        EBookBackendEwsPrivate *priv;

	bgw = E_BOOK_BACKEND_EWS (object);
        priv = bgw->priv;

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	if (priv->folder_id) {
		g_free (priv->folder_id);
		priv->folder_id = NULL;
	}

	if (priv->oab_url) {
		g_free (priv->oab_url);
		priv->oab_url = NULL;
	}
	
	if (priv->folder_name) {
		g_free (priv->folder_name);
		priv->folder_name = NULL;
	}
	
	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}
	
	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->attachment_dir) {
		g_free (priv->attachment_dir);
		priv->attachment_dir = NULL;
	}

	if (priv->dlock) {
		g_mutex_lock (priv->dlock->mutex);
		priv->dlock->exit = TRUE;
		g_mutex_unlock (priv->dlock->mutex);

		g_cond_signal (priv->dlock->cond);

		if (priv->dthread)
			g_thread_join (priv->dthread);

		g_mutex_free (priv->dlock->mutex);
		g_cond_free (priv->dlock->cond);
		g_free (priv->dlock);
		priv->dthread = NULL;
	}

	if (priv->ebsdb) {
		g_object_unref (priv->ebsdb);
		priv->ebsdb = NULL;
	}

#if EDS_CHECK_VERSION (3,1,0)
	e_credentials_free (priv->credentials);
	priv->credentials = NULL;
#endif	
	g_static_rec_mutex_free (&priv->rec_mutex);

	g_free (priv);
	priv = NULL;

	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->dispose (object);
}

static void
e_book_backend_ews_class_init (EBookBackendEwsClass *klass)
{

	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;

	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
#if ! EDS_CHECK_VERSION (3,1,0)	
	parent_class->load_source             = e_book_backend_ews_load_source;
	parent_class->get_static_capabilities = e_book_backend_ews_get_static_capabilities;

	parent_class->set_mode                = e_book_backend_ews_set_mode;
	parent_class->get_required_fields     = e_book_backend_ews_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ews_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ews_get_supported_auth_methods;
	parent_class->cancel_operation        = e_book_backend_ews_cancel_operation;
	parent_class->get_changes             = e_book_backend_ews_get_changes;

	parent_class->create_contact          = e_book_backend_ews_create_contact_compat;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts_compat;
	parent_class->modify_contact          = e_book_backend_ews_modify_contact_compat;
	parent_class->get_contact             = e_book_backend_ews_get_contact_compat;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list_compat;
	parent_class->remove                  = e_book_backend_ews_remove_compat;
#else
	parent_class->open		      = e_book_backend_ews_open;
	parent_class->get_backend_property    = e_book_backend_ews_get_backend_property;
	parent_class->set_online	      = e_book_backend_ews_set_online;

	parent_class->create_contact          = e_book_backend_ews_create_contact;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ews_modify_contact;
	parent_class->get_contact             = e_book_backend_ews_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list;
	parent_class->remove                  = e_book_backend_ews_remove;
#endif	
	parent_class->authenticate_user       = e_book_backend_ews_authenticate_user;
	parent_class->start_book_view         = e_book_backend_ews_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ews_stop_book_view;

	object_class->dispose                 = e_book_backend_ews_dispose;
}

static void
e_book_backend_ews_init (EBookBackendEws *backend)
{
	EBookBackendEws *bews;
	EBookBackendEwsPrivate *priv;

	bews = E_BOOK_BACKEND_EWS (backend);

	priv = g_new0 (EBookBackendEwsPrivate, 1);
	priv->ops = g_hash_table_new (NULL, NULL);

	bews->priv = priv;
	g_static_rec_mutex_init (&priv->rec_mutex);
}
