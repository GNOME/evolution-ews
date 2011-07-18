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

#include "e-ews-message.h"
#include "e-ews-connection.h"
#include "e-ews-item.h"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_ERROR_FAILED_STATUS(_code, _status) e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_ ## _code, "Failed with status 0x%x", _status)

G_DEFINE_TYPE (EBookBackendEws, e_book_backend_ews, E_TYPE_BOOK_BACKEND)

typedef struct {
	GCond *cond;
	GMutex *mutex;
	gboolean exit;
} SyncDelta;

struct _EBookBackendEwsPrivate {
	EEwsConnection *cnc;
	gchar *folder_id;

	EBookBackendSqliteDB *ebsdb;

	gboolean only_if_exists;
	gboolean is_writable;
	gboolean marked_for_offline;
	gboolean cache_ready;
	gint mode;

	GHashTable *ops;

	GStaticRecMutex rec_mutex;
	GThread *dthread;
	SyncDelta *dlock;
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
	const gchar *sur_name;

	cn = e_ews_item_get_complete_name (item);
	sur_name = e_ews_item_get_surname (item);

	e_contact_set (contact, E_CONTACT_FULL_NAME, cn->full_name);
	e_contact_set (contact, E_CONTACT_NICKNAME, cn->nick_name);
	e_contact_set (contact, E_CONTACT_FAMILY_NAME, sur_name);
}

static void
ebews_populate_birth_date	(EContact *contact, EEwsItem *item)
{
	time_t bdate;
	GDate date;
	EContactDate edate;

	bdate = e_ews_item_get_birthday (item);
	g_date_clear (&date, 1);
	g_date_set_time_t (&date, bdate);
	
	edate.year = date.year;
	edate.month = date.month;
	edate.day = date.day;

	if (g_date_valid (&date))
		e_contact_set (contact, E_CONTACT_BIRTH_DATE, &edate);
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
	gchar *val;
	
	if ((val = e_contact_get (contact, E_CONTACT_FULL_NAME)))
		e_ews_message_write_string_parameter(msg, "FullName", NULL, val);
	g_free (val);
	
	if ((val = e_contact_get (contact, E_CONTACT_GIVEN_NAME)))
		e_ews_message_write_string_parameter(msg, "GivenName", NULL, val);
	g_free (val);

	if ((val = e_contact_get (contact, E_CONTACT_NICKNAME)) && *val)
		e_ews_message_write_string_parameter(msg, "Nickname", NULL, val);
	g_free (val);
	
	if ((val = e_contact_get (contact, E_CONTACT_FAMILY_NAME)) && *val)
		e_ews_message_write_string_parameter(msg, "Surname", NULL, val);
	g_free (val);
}

static void
ebews_set_birth_date		(ESoapMessage *message, EContact *contact)
{
	
}

static void
add_entry (ESoapMessage *msg, EContact *contact, EContactField field, const gchar *entry_name)
{
	gchar *entry_val;

	entry_val = e_contact_get (contact, field);
	
	if (entry_val && *entry_val)
		e_ews_message_write_string_parameter_with_attribute(msg, "Entry", NULL, entry_val, "Key", entry_name);

	g_free (entry_val);
}

static void
ebews_set_phone_numbers		(ESoapMessage *msg, EContact *contact)
{
	gint i;
	
	e_soap_message_start_element(msg, "PhoneNumbers", NULL, NULL);
	
	for (i = 0; i < G_N_ELEMENTS (phone_field_map); i++)
		add_entry (msg, contact, phone_field_map[i].field, phone_field_map[i].element);

	e_soap_message_end_element(msg);
}

static void
add_physical_address (ESoapMessage *msg, EContact *contact, EContactField field, const gchar *entry_name)
{
	EContactAddress *contact_addr;

	contact_addr = e_contact_get (contact, field);
	if (!contact_addr)
		return;

	e_soap_message_start_element (msg, "Entry", NULL, NULL);
	
	e_soap_message_add_attribute (msg, "Key", entry_name, NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Street", NULL, contact_addr->street);
	e_ews_message_write_string_parameter (msg, "City", NULL, contact_addr->locality);
	e_ews_message_write_string_parameter (msg, "State", NULL, contact_addr->region);
	e_ews_message_write_string_parameter (msg, "PostalCode", NULL, contact_addr->code);

	e_soap_message_end_element (msg);
	e_contact_address_free (contact_addr);
}

static void
ebews_set_address	(ESoapMessage *msg, EContact *contact)
{
	e_soap_message_start_element (msg, "PhysicalAddresses", NULL, NULL);

	add_physical_address (msg, contact, E_CONTACT_ADDRESS_WORK, "Business");
	add_physical_address (msg, contact, E_CONTACT_ADDRESS_HOME, "Home");
	add_physical_address (msg, contact, E_CONTACT_ADDRESS_OTHER, "Other");

	e_soap_message_end_element(msg);
}

static void
ebews_set_ims			(ESoapMessage *message, EContact *contact)
{
	
}

static void
ebews_set_emails		(ESoapMessage *msg, EContact *contact)
{
	e_soap_message_start_element(msg, "EmailAddresses", NULL, NULL);

	add_entry (msg, contact, E_CONTACT_EMAIL_1, "EMailAddress1");
	add_entry (msg, contact, E_CONTACT_EMAIL_2, "EMailAddress2");
	add_entry (msg, contact, E_CONTACT_EMAIL_3, "EMailAddress3");

	e_soap_message_end_element(msg);
}

static void
ebews_set_full_name_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_birth_date_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_phone_number_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_address_changes	(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_im_changes		(ESoapMessage *message, EContact *new, EContact *old)
{
	
}

static void
ebews_set_email_changes		(ESoapMessage *message, EContact *new, EContact *old)
{
	
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
	{ E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "FileAs", e_ews_item_get_fileas},
	{ E_CONTACT_HOMEPAGE_URL, ELEMENT_TYPE_SIMPLE, "BusinessHomePage", e_ews_item_get_business_homepage},
	{ E_CONTACT_ORG, ELEMENT_TYPE_SIMPLE, "CompanyName", e_ews_item_get_company_name},
	{ E_CONTACT_ORG_UNIT, ELEMENT_TYPE_SIMPLE, "Department", e_ews_item_get_department},
	{ E_CONTACT_TITLE, ELEMENT_TYPE_SIMPLE, "JobTitle", e_ews_item_get_job_title},
	{ E_CONTACT_ASSISTANT, ELEMENT_TYPE_SIMPLE, "AssistantName", e_ews_item_get_assistant_name},
	{ E_CONTACT_MANAGER, ELEMENT_TYPE_SIMPLE, "Manager", e_ews_item_get_manager},
	{ E_CONTACT_SPOUSE, ELEMENT_TYPE_SIMPLE, "SpouseName", e_ews_item_get_spouse_name},

	/* Should take of uid and changekey (REV) */
	{ E_CONTACT_UID, ELEMENT_TYPE_COMPLEX, "ItemId", NULL,  ebews_populate_uid, ebews_set_item_id},
	/* Should handle all name parts */
	{ E_CONTACT_FULL_NAME, ELEMENT_TYPE_COMPLEX, "CompleteName", NULL, ebews_populate_full_name, ebews_set_full_name, ebews_set_full_name_changes},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "Birthday", NULL,  ebews_populate_birth_date, ebews_set_birth_date, ebews_set_birth_date_changes },
	/* should take care of all phone number fields */
	{ E_CONTACT_PHONE_PRIMARY, ELEMENT_TYPE_COMPLEX , "PhoneNumbers", NULL, ebews_populate_phone_numbers, ebews_set_phone_numbers, ebews_set_phone_number_changes},
	/* should take care of home, work and other adresss fields */
	{ E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "PhysicalAddresses", NULL, ebews_populate_address, ebews_set_address, ebews_set_address_changes },
	/* should take care of all im fields */
	{ E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "ImAddresses", NULL, ebews_populate_ims, ebews_set_ims, ebews_set_im_changes },
	/* should take care of all email adresss fields */
	{ E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "EmailAddresses", NULL, ebews_populate_emails, ebews_set_emails, ebews_set_email_changes }
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

		e_contact_set (create_contact->contact, E_CONTACT_UID, item_id);
		e_book_backend_sqlitedb_add_contact (ebews->priv->ebsdb, ebews->priv->folder_id, create_contact->contact, FALSE, &error);

		if (error == NULL)
			e_data_book_respond_create (create_contact->book, create_contact->opid, EDB_ERROR (SUCCESS), create_contact->contact);

		g_object_unref (item);
		g_slist_free (items);
	}
	
	if (error) {
		g_warning("Error while Creating contact: %s", error->message);
		e_data_book_respond_create (create_contact->book, create_contact->opid, EDB_ERROR_EX (OTHER_ERROR, error->message), create_contact->contact);
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
					 const gchar *vcard )
{
	EContact *contact = NULL;
	EBookBackendEws *ebews;
	EwsCreateContact *create_contact;
	EBookBackendEwsPrivate *priv;
	GCancellable *cancellable = NULL;
 
	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;

	switch (ebews->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_create (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case  E_DATA_BOOK_MODE_REMOTE :

		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_create (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		if (!ebews->priv->is_writable) {
			e_data_book_respond_create (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		contact = e_contact_new_from_vcard (vcard);

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

		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR (SUCCESS),  dl_ids);
		g_list_free (dl_ids);
	} else {
		e_data_book_respond_remove_contacts (remove_contact->book, remove_contact->opid, EDB_ERROR_EX (OTHER_ERROR, error->message), NULL);
		
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
					 GList *id_list)
{
	EBookBackendEws *ebews;
	EwsRemoveContact *remove_contact;
	EBookBackendEwsPrivate *priv;
	GSList *deleted_ids = NULL;
	GList *dl;
 
	ebews = E_BOOK_BACKEND_EWS (backend);
 
	priv = ebews->priv;

	switch (ebews->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		if (!ebews->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		for (dl = id_list; dl != NULL; dl = g_list_next (dl))
			deleted_ids = g_slist_prepend (NULL, g_strdup (dl->data));

		remove_contact = g_new0(EwsRemoveContact, 1);
		remove_contact->ebews = g_object_ref(ebews);
		remove_contact->book = g_object_ref(book);
		remove_contact->opid = opid;
		remove_contact->sl_ids = deleted_ids;

		e_ews_connection_delete_items_start (priv->cnc, EWS_PRIORITY_MEDIUM, deleted_ids,
						     EWS_HARD_DELETE, 0 , FALSE,
						     ews_book_remove_contact_cb, NULL,
						     remove_contact);
		return;
	default :
		break;
	}
}


static void
e_book_backend_ews_modify_contact	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *vcard)
{
	EContact *contact = NULL;
	EBookBackendEws *egwb;

	egwb = E_BOOK_BACKEND_EWS (backend);

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_modify (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	case E_DATA_BOOK_MODE_REMOTE :

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}
		if (!egwb->priv->is_writable) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		e_data_book_respond_modify (book, opid, EDB_ERROR (SUCCESS), contact);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact	(EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 const gchar   *id)
{
	EBookBackendEws *gwb;

	gwb =  E_BOOK_BACKEND_EWS (backend);

	switch (gwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (gwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "Not connected"), NULL);
			return;
		}
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact_list	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *query )
{
	GList *vcard_list;
	EBookBackendEws *egwb;

	egwb = E_BOOK_BACKEND_EWS (backend);
	vcard_list = NULL;

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		return;

	case E_DATA_BOOK_MODE_REMOTE:

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
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
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_and_or (ESExp *f, gint argc, ESExpResult **argv, gpointer and)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

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
	r->value.boolean = FALSE;

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
	r->value.boolean = FALSE;

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
	r->value.boolean = FALSE;

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

				if (element_type == ELEMENT_TYPE_SIMPLE) {
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
	gchar *sync_state;
	gboolean includes_last_item;
	GError *error = NULL;

	ebews = (EBookBackendEws *) data;
	priv = ebews->priv;

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

	if (!error)
		e_book_backend_sqlitedb_set_is_populated (priv->ebsdb, priv->folder_id, TRUE, &error);

	g_free (sync_state);

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
		gboolean succeeded = ebews_start_sync (ebews);

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

	if	(priv->mode == E_DATA_BOOK_MODE_REMOTE &&
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

	contacts = e_book_backend_sqlitedb_search (priv->ebsdb, priv->folder_id, query, NULL, &error);
	for (l = contacts; l != NULL; l = g_slist_next (l)) {
		EbSdbSearchData *s_data = (EbSdbSearchData *) l->data;

		/* reset vcard to NULL as it would be free'ed in prefiltered_vcard function */
		e_data_book_view_notify_update_prefiltered_vcard (book_view, s_data->uid, s_data->vcard);
		s_data->vcard = NULL;

		e_book_backend_sqlitedb_search_data_free (s_data);
	}

	g_slist_free (contacts);
	e_data_book_view_notify_complete (book_view, error);
	e_data_book_view_unref (book_view);
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
	GError *error = NULL;
	gboolean includes_last_item;
	ESource *source;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;
	query = e_data_book_view_get_card_query (book_view);

	e_data_book_view_ref (book_view);
	e_data_book_view_notify_status_message (book_view, _("Searching..."));

	switch (priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL:
		if (e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}

		error = EDB_ERROR (OFFLINE_UNAVAILABLE);
		e_data_book_view_notify_complete (book_view, error);
		g_error_free (error);
		return;
	case E_DATA_BOOK_MODE_REMOTE:
		if (!priv->cnc) {
			error = EDB_ERROR (AUTHENTICATION_REQUIRED);
			e_book_backend_notify_auth_required (backend);
			e_data_book_view_notify_complete (book_view, error);
			e_data_book_view_unref (book_view);
			g_error_free (error);
			return;
		}

		ebews_start_refreshing (ebews);

		if (e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->folder_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}

		e_book_backend_ews_build_restriction (query, &is_autocompletion, &auto_comp_str);
		if (!is_autocompletion || !auto_comp_str) {
			g_free (auto_comp_str);
			e_data_book_view_notify_complete (book_view, error);
			e_data_book_view_unref (book_view);
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
			e_data_book_view_notify_complete (book_view, error);
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
		e_data_book_view_notify_complete (book_view, error);
		e_data_book_view_unref (book_view);
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

static void
e_book_backend_ews_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar *change_id)
{
}

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
	case E_DATA_BOOK_MODE_LOCAL:
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		return;

	case E_DATA_BOOK_MODE_REMOTE:
		if (priv->cnc) {
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
			return;
		}

		esource = e_book_backend_get_source (backend);
		host_url = e_source_get_property (esource, "hosturl");
		read_only = e_source_get_property (esource, "read_only");

		priv->cnc = e_ews_connection_new (host_url, user, passwd,
						  NULL, NULL, &error);

		if (read_only && !strcmp (read_only, "true")) {
			priv->is_writable = FALSE;
		} else 
			priv->is_writable = TRUE;

		/* FIXME: Do some dummy request to ensure that the password is actually
		   correct; don't just blindly return success */
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		e_book_backend_notify_writable (backend, priv->is_writable);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_required_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;

	fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (SUCCESS),
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
	
	for (i = 0; i < G_N_ELEMENTS (mappings) && mappings [i].element_type == ELEMENT_TYPE_SIMPLE; i++)
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
						  EDB_ERROR (SUCCESS),
						  fields);
	g_list_free (fields);
}

static void
e_book_backend_ews_cancel_operation (EBookBackend *backend, EDataBook *book, GError **perror)
{

}

static void
e_book_backend_ews_load_source 	(EBookBackend           *backend,
				 ESource                *source,
				 gboolean                only_if_exists,
				 GError                **perror)
{
	EBookBackendEws *cbews;
	EBookBackendEwsPrivate *priv;
	const gchar *cache_dir, *email;
	const gchar *folder_name;
	const gchar *offline;
	GError *err = NULL;

	cbews = E_BOOK_BACKEND_EWS (backend);
	priv = cbews->priv;

	cache_dir = e_book_backend_get_cache_dir (backend);
	email = e_source_get_property (source, "email");
	priv->folder_id = e_source_get_duped_property (source, "folder-id");
	folder_name = e_source_peek_name (source);

	priv->ebsdb = e_book_backend_sqlitedb_new (cache_dir, email, priv->folder_id, folder_name, TRUE, &err);
	if (err) {
		g_propagate_error (perror, err);
		return;
	}

	offline = e_source_get_property (source, "offline_sync");
	if (offline  && g_str_equal (offline, "1"))
		priv->marked_for_offline = TRUE;
	
	e_book_backend_set_is_loaded (backend, TRUE);
}

static void
e_book_backend_ews_remove	(EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
	e_data_book_respond_remove (book,  opid, EDB_ERROR (SUCCESS));
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
							EDB_ERROR (SUCCESS),
							auth_methods);
	g_free (auth_method);
	g_list_free (auth_methods);
}

static void
e_book_backend_ews_set_mode (EBookBackend *backend,
                                   EDataBookMode mode)
{
	EBookBackendEws *ebews;
	EBookBackendEwsPrivate *priv;

	ebews = E_BOOK_BACKEND_EWS (backend);
	priv = ebews->priv;
	priv->mode = mode;

	if (e_book_backend_is_loaded (backend)) {
		if (mode == E_DATA_BOOK_MODE_LOCAL) {
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
		else if (mode == E_DATA_BOOK_MODE_REMOTE) {
			if (ebews->priv->is_writable)
				e_book_backend_notify_writable (backend, TRUE);
			else
				e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, TRUE);
			e_book_backend_notify_auth_required (backend);
		}
	}
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
	parent_class->load_source             = e_book_backend_ews_load_source;
	parent_class->get_static_capabilities = e_book_backend_ews_get_static_capabilities;
	parent_class->remove                  = e_book_backend_ews_remove;

	parent_class->set_mode                = e_book_backend_ews_set_mode;
	parent_class->get_required_fields     = e_book_backend_ews_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ews_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ews_get_supported_auth_methods;

	parent_class->authenticate_user       = e_book_backend_ews_authenticate_user;

	parent_class->start_book_view         = e_book_backend_ews_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ews_stop_book_view;
	parent_class->cancel_operation        = e_book_backend_ews_cancel_operation;

	parent_class->create_contact          = e_book_backend_ews_create_contact;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ews_modify_contact;
	parent_class->get_contact             = e_book_backend_ews_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list;

	parent_class->get_changes             = e_book_backend_ews_get_changes;

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
