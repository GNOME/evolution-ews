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
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
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
#include "db.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "libebackend/e-db3-utils.h"

#include "libedataserver/e-sexp.h"
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-flag.h"
#include "libedataserver/e-url.h"
#include "libebook/e-contact.h"
#include "libebook/e-destination.h"
#include "libedata-book/e-book-backend-sexp.h"
#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"
#include "libedata-book/e-book-backend-db-cache.h"
#include "libedata-book/e-book-backend-summary.h"
#include "e-book-backend-ews.h"

#include "e-ews-connection.h"
#include "e-ews-item.h"
#include "e-ews-filter.h"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_ERROR_FAILED_STATUS(_code, _status) e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_ ## _code, "Failed with status 0x%x", _status)

G_DEFINE_TYPE (EBookBackendEws, e_book_backend_ews, E_TYPE_BOOK_BACKEND)

typedef struct {
        GCond *cond;
        GMutex *mutex;
        gboolean exit;
} SyncUpdate;

struct _EBookBackendEwsPrivate {
	EEwsConnection *cnc;
	gchar *uri;
	gchar *container_id;
	gchar *book_name;
	gchar *original_uri;
	gchar *summary_file_name;
	gboolean only_if_exists;
	GHashTable *categories_by_id;
	GHashTable *categories_by_name;
	gboolean is_writable;
	gboolean is_cache_ready;
	gboolean is_summary_ready;
	gboolean marked_for_offline;
	gchar *use_ssl;
	gint mode;
	EBookBackendSummary *summary;
	GMutex *update_cache_mutex;
	GMutex *update_mutex;
	DB     *file_db;
	DB_ENV *env;

	guint cache_timeout;
	GThread *dthread;
        SyncUpdate *dlock;
};

static GStaticMutex global_env_lock = G_STATIC_MUTEX_INIT;
static struct {
	gint ref_count;
	DB_ENV *env;
} global_env;

#define CACHE_REFRESH_INTERVAL 600000
#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02 /* fields which require explicit functions to set values into EContact and EEwsItem */
#define SUMMARY_FLUSH_TIMEOUT 5000

static gboolean enable_debug = FALSE;

static void populate_emails (EContact *contact, gpointer data);
static void set_emails_in_ews_item (EEwsItem *item, gpointer data);
static void set_emails_changes (EEwsItem *new_item, EEwsItem *old_item);
static void populate_full_name (EContact *contact, gpointer data);
static void set_full_name_in_ews_item (EEwsItem *item, gpointer data);
static void set_full_name_changes (EEwsItem *new_item, EEwsItem *old_item);
static void populate_contact_members (EContact *contact, gpointer data);
static void set_categories_changes (EEwsItem *new_item, EEwsItem *old_item);
static void populate_birth_date (EContact *contact, gpointer data);
static void set_birth_date_in_ews_item (EEwsItem *item, gpointer data);
static void set_birth_date_changes  (EEwsItem *new_item, EEwsItem *old_item);
static void populate_address (EContact *contact, gpointer data);
static void set_address_in_ews_item (EEwsItem *item, gpointer data);
static void set_address_changes (EEwsItem *new_item, EEwsItem *old_item);
static void populate_ims (EContact *contact, gpointer data);
static void set_ims_in_ews_item (EEwsItem *item, gpointer data);
static void set_im_changes (EEwsItem *new_item, EEwsItem *old_item);
static void fill_contact_from_ews_item (EContact *contact, EEwsItem *item, GHashTable *categories_by_ids);

static const struct field_element_mapping {
	EContactField field_id;
	gint element_type;
	const gchar *element_name;
	void (*populate_contact_func)(EContact *contact,    gpointer data);
	void (*set_value_in_ews_item) (EEwsItem *item, gpointer data);
	void (*set_changes) (EEwsItem *new_item, EEwsItem *old_item);

} mappings [] = {

	{ E_CONTACT_UID, ELEMENT_TYPE_SIMPLE, "id"},
	{ E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "name" },
	{ E_CONTACT_FULL_NAME, ELEMENT_TYPE_COMPLEX, "full_name", populate_full_name, set_full_name_in_ews_item, set_full_name_changes},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "birthday", populate_birth_date, set_birth_date_in_ews_item, set_birth_date_changes },
	{ E_CONTACT_HOMEPAGE_URL, ELEMENT_TYPE_SIMPLE, "website"},
	{ E_CONTACT_NOTE, ELEMENT_TYPE_SIMPLE, "comment"},
	{ E_CONTACT_PHONE_PRIMARY, ELEMENT_TYPE_SIMPLE , "default_phone"},
	{ E_CONTACT_PHONE_BUSINESS, ELEMENT_TYPE_SIMPLE, "phone_Office"},
	{ E_CONTACT_PHONE_HOME, ELEMENT_TYPE_SIMPLE, "phone_Home"},
	{ E_CONTACT_PHONE_MOBILE, ELEMENT_TYPE_SIMPLE, "phone_Mobile"},
	{ E_CONTACT_PHONE_BUSINESS_FAX, ELEMENT_TYPE_SIMPLE, "phone_Fax" },
	{ E_CONTACT_PHONE_PAGER, ELEMENT_TYPE_SIMPLE, "phone_Pager"},
	{ E_CONTACT_ORG, ELEMENT_TYPE_SIMPLE, "organization"},
	{ E_CONTACT_ORG_UNIT, ELEMENT_TYPE_SIMPLE, "department"},
	{ E_CONTACT_TITLE, ELEMENT_TYPE_SIMPLE, "title"},
	{ E_CONTACT_EMAIL, ELEMENT_TYPE_COMPLEX, "members", populate_contact_members, NULL, NULL},
	{ E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "Home", populate_address, set_address_in_ews_item, set_address_changes },
	{ E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "ims", populate_ims, set_ims_in_ews_item, set_im_changes },
	{ E_CONTACT_CATEGORIES, ELEMENT_TYPE_COMPLEX, "categories", NULL, NULL, set_categories_changes},
	{ E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "email", populate_emails, set_emails_in_ews_item, set_emails_changes },
	{ E_CONTACT_REV, ELEMENT_TYPE_SIMPLE, "modified_time"},
	{ E_CONTACT_BOOK_URI, ELEMENT_TYPE_SIMPLE, "book_uri"}
};

static void
free_attr_list (GList *attr_list)
{
        GList *l;

        for (l = attr_list; l; l = g_list_next (l)) {
                EVCardAttribute *attr = l->data;
                e_vcard_attribute_free (attr);
        }

        g_list_free (attr_list);
}

static void
populate_ims (EContact *contact, gpointer data)
{
	GList *im_list;
	GList *aim_list = NULL;
	GList *icq_list = NULL;
	GList *yahoo_list = NULL;
	GList *gadugadu_list = NULL;
	GList *msn_list = NULL;
	GList *skype_list = NULL;
	GList *jabber_list = NULL;
	GList *ews_list = NULL;
	IMAddress *address;
	EEwsItem *item;

	item = E_EWS_ITEM (data);
	im_list = e_ews_item_get_im_list (item);

	for (; im_list != NULL; im_list = g_list_next (im_list)) {
		EVCardAttribute *attr;
		GList **im_attr_list = NULL;
		gint im_field_id = -1;

		address = (IMAddress *) (im_list->data);
		if (address->service == NULL) {
			continue;
		}

		if (g_str_equal (address->service, "icq")) {
			im_field_id = E_CONTACT_IM_ICQ;
			im_attr_list = &icq_list;
		}
		else if (g_str_equal (address->service, "aim")) {
			im_field_id = E_CONTACT_IM_AIM;
			im_attr_list = &aim_list;
		}
		else if (g_str_equal (address->service, "msn")) {
			im_field_id = E_CONTACT_IM_MSN;
			im_attr_list = &msn_list;
		}
		else if (g_str_equal (address->service, "skype")) {
			im_field_id = E_CONTACT_IM_SKYPE;
			im_attr_list = &skype_list;
		}
		else if (g_str_equal (address->service, "yahoo")) {
			im_field_id = E_CONTACT_IM_YAHOO;
			im_attr_list = &yahoo_list;
		}
		else if (g_str_equal (address->service, "gadu-gadu")) {
			im_field_id = E_CONTACT_IM_GADUGADU;
			im_attr_list = &gadugadu_list;
		}
		else if (g_str_equal (address->service, "jabber")) {
			im_field_id = E_CONTACT_IM_JABBER;
			im_attr_list = &jabber_list;
		}

		else if (g_str_equal (address->service, "nov")) {
			im_field_id = E_CONTACT_IM_GROUPWISE;
			im_attr_list = &ews_list;
		}
		if (im_field_id == -1)
			continue;

		attr = e_vcard_attribute_new ("", e_contact_vcard_attribute(im_field_id));
		e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "WORK");
		e_vcard_attribute_add_value (attr, address->address);
		*im_attr_list = g_list_append (*im_attr_list, attr);
	}

	e_contact_set_attributes (contact, E_CONTACT_IM_AIM, aim_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_JABBER, jabber_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_ICQ, icq_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_YAHOO, yahoo_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_GADUGADU, gadugadu_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_MSN, msn_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_SKYPE, skype_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_GROUPWISE, ews_list);

	free_attr_list (aim_list);
	free_attr_list (jabber_list);
	free_attr_list (icq_list);
	free_attr_list (yahoo_list);
	free_attr_list (gadugadu_list);
	free_attr_list (msn_list);
	free_attr_list (skype_list);
	free_attr_list (ews_list);
}

static void
append_ims_to_list (GList **im_list, EContact *contact, const gchar *service_name, EContactField field_id)
{
	GList *list;
	IMAddress *address;
	list = e_contact_get (contact, field_id);
	for (; list != NULL; list =  g_list_next (list)) {
		address = g_new0 (IMAddress , 1);
		address->service = g_strdup (service_name);
		address->address = list->data;
		*im_list = g_list_append (*im_list, address);
	}
	g_list_free (list);

}

static void
set_ims_in_ews_item (EEwsItem *item, gpointer data)
{
	EContact *contact;
	GList *im_list = NULL;

	contact = E_CONTACT (data);

	append_ims_to_list (&im_list, contact, "aim", E_CONTACT_IM_AIM);
	append_ims_to_list (&im_list, contact, "yahoo", E_CONTACT_IM_YAHOO);
	append_ims_to_list (&im_list, contact, "gadu-gadu", E_CONTACT_IM_GADUGADU);
	append_ims_to_list (&im_list, contact, "icq", E_CONTACT_IM_ICQ);
	append_ims_to_list (&im_list, contact, "msn", E_CONTACT_IM_MSN);
	append_ims_to_list (&im_list, contact, "skype", E_CONTACT_IM_SKYPE);
	append_ims_to_list (&im_list, contact, "jabber", E_CONTACT_IM_JABBER);
	append_ims_to_list (&im_list, contact, "nov", E_CONTACT_IM_GROUPWISE);
	if (im_list)
		e_ews_item_set_im_list (item, im_list);
}

static void
set_im_changes (EEwsItem *new_item, EEwsItem *old_item)
{
	GList *old_ims;
	GList *new_ims;
	GList *added_ims = NULL;
	GList *old_ims_copy;
	GList *temp;
	gboolean ims_matched;
	IMAddress *im1, *im2;

	old_ims = e_ews_item_get_im_list (old_item);
	new_ims = e_ews_item_get_im_list (new_item);

	if (old_ims && new_ims) {

		old_ims_copy = g_list_copy (old_ims);
		for (; new_ims != NULL; new_ims = g_list_next (new_ims)) {

			im1 = new_ims->data;
			temp = old_ims;
			ims_matched = FALSE;
			for (; temp != NULL; temp = g_list_next (temp)) {
				im2 = temp->data;
				if (g_str_equal (im1->service, im2->service) && g_str_equal (im1->address, im2->address)) {
					ims_matched = TRUE;
					old_ims_copy = g_list_remove (old_ims_copy, im2);
					break;
				}

			}
			if (!ims_matched)
				added_ims = g_list_append (added_ims, im1);
		}

		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "ims", added_ims);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "ims", old_ims_copy);

	} else if (!new_ims && old_ims) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "ims", g_list_copy (old_ims));
	} else if (new_ims && !old_ims) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "ims", g_list_copy (new_ims));
	}

}

static void
copy_postal_address_to_contact_address ( EContactAddress *contact_addr, PostalAddress *address)
{
	contact_addr->address_format = NULL;
	contact_addr->po = NULL;
	contact_addr->street = g_strdup (address->street_address);
	contact_addr->ext = g_strdup (address->location);
	contact_addr->locality = g_strdup (address->city);
	contact_addr->region = g_strdup (address->state);
	contact_addr->code = g_strdup (address->postal_code);
	contact_addr->country = g_strdup (address->country);
}

static void
copy_contact_address_to_postal_address (PostalAddress *address, EContactAddress *contact_addr)
{
	/* ugh, contact addr has null terminated strings instead of NULLs*/
	address->street_address = (contact_addr->street && *contact_addr->street) ? g_strdup (contact_addr->street): NULL;
	address->location = (contact_addr->ext && *contact_addr->ext) ? g_strdup (contact_addr->ext) : NULL;
	address->city = (contact_addr->locality && *contact_addr->locality) ? g_strdup (contact_addr->locality) : NULL;
	address->state = (contact_addr->region && *contact_addr->region) ?  g_strdup (contact_addr->region) : NULL;
	address->postal_code = (contact_addr->code && *contact_addr->code ) ? g_strdup (contact_addr->code) : NULL;
	address->country = (contact_addr->country && *(contact_addr->country)) ? g_strdup (contact_addr->country) : NULL;
}

static void
populate_address (EContact *contact, gpointer data)
{
	PostalAddress *address;
	EEwsItem *item;
	EContactAddress *contact_addr;

	item = E_EWS_ITEM (data);

	address = e_ews_item_get_address (item, "Home");
	contact_addr = NULL;

	if (address) {
		contact_addr = g_new0(EContactAddress, 1);
		copy_postal_address_to_contact_address (contact_addr, address);
		e_contact_set (contact, E_CONTACT_ADDRESS_HOME, contact_addr);
		e_contact_address_free (contact_addr);
	}

	address = e_ews_item_get_address (item, "Office");
	if (address) {
		contact_addr = g_new0(EContactAddress, 1);
		copy_postal_address_to_contact_address (contact_addr, address);
		e_contact_set (contact, E_CONTACT_ADDRESS_WORK, contact_addr);
		e_contact_address_free (contact_addr);
	}
}

static void
set_address_in_ews_item (EEwsItem *item, gpointer data)
{
	EContact *contact;
	EContactAddress *contact_address;
	PostalAddress *address;

	contact = E_CONTACT (data);

	contact_address = e_contact_get (contact, E_CONTACT_ADDRESS_HOME);
	if (contact_address) {
		address = g_new0(PostalAddress, 1);
		copy_contact_address_to_postal_address (address, contact_address);
		e_ews_item_set_address (item, "Home", address);
		e_contact_address_free (contact_address);
	}

	contact_address = e_contact_get (contact, E_CONTACT_ADDRESS_WORK);
	if (contact_address) {
		address = g_new0(PostalAddress, 1);
		copy_contact_address_to_postal_address (address, contact_address);
		e_ews_item_set_address (item, "Office", address);
		e_contact_address_free (contact_address);
	}
}

static PostalAddress *
copy_postal_address (PostalAddress *address)
{
	PostalAddress *address_copy;

	address_copy = g_new0(PostalAddress, 1);

	address_copy->street_address = g_strdup (address->street_address);
	address_copy->location = g_strdup (address->location);
	address_copy->city = g_strdup (address->city);
	address_copy->state = g_strdup (address->state);
	address_copy->postal_code = g_strdup (address->postal_code);
	address_copy->country = g_strdup (address->country);
	return address_copy;
}

static void
set_postal_address_change (EEwsItem *new_item, EEwsItem *old_item, const gchar *address_type)
{
	PostalAddress *old_postal_address;
	PostalAddress *new_postal_address;
	PostalAddress *update_postal_address, *delete_postal_address;
	gchar *s1, *s2;
	update_postal_address = g_new0(PostalAddress, 1);
	delete_postal_address = g_new0 (PostalAddress, 1);

	new_postal_address = e_ews_item_get_address (new_item,  address_type);
	old_postal_address = e_ews_item_get_address (old_item, address_type);
	if (new_postal_address && old_postal_address) {
		s1 = new_postal_address->street_address;
		s2 = old_postal_address->street_address;
		if (!s1 && s2)
			delete_postal_address->street_address = g_strdup(s2);
		else if (s1)
			update_postal_address->street_address = g_strdup(s1);

		s1 =  new_postal_address->location;
		s2 = old_postal_address->location;
		if (!s1 && s2)
			delete_postal_address->location = g_strdup(s2);
		else if (s1)
			update_postal_address->location = g_strdup(s1);

		s1 = new_postal_address->city;
		s2 = old_postal_address->city;
		if (!s1 && s2)
			delete_postal_address->city = g_strdup(s2);
		else if (s1)
			update_postal_address->city = g_strdup(s1);

		s1 =  new_postal_address->state;
		s2 = old_postal_address->state;
		if (!s1 && s2)
			delete_postal_address->state = g_strdup(s2);
		else if (s1)
			update_postal_address->state = g_strdup(s1);
		s1 =  new_postal_address->postal_code;
		s2 = old_postal_address->postal_code;
		if (!s1 && s2)
			delete_postal_address->postal_code = g_strdup(s2);
		else if (s1)
			update_postal_address->postal_code = g_strdup(s1);

		s1 =  new_postal_address->country;
		s2 =  old_postal_address->country;
		if (!s1 && s2)
			delete_postal_address->country = g_strdup(s2);
		else if (s1)
			update_postal_address->country = g_strdup(s1);

		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE, address_type, update_postal_address);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, address_type, delete_postal_address);

	} else if (!new_postal_address && old_postal_address) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, address_type, copy_postal_address(old_postal_address));
	} else if (new_postal_address && !old_postal_address) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, address_type, copy_postal_address(new_postal_address));
	}
}

static void
set_address_changes (EEwsItem *new_item , EEwsItem *old_item)
{
	set_postal_address_change (new_item, old_item, "Home");
	set_postal_address_change (new_item, old_item, "Office");
}

static void
populate_birth_date (EContact *contact, gpointer data)
{
	EEwsItem *item;
	gchar *value;
	EContactDate *date;

	item = E_EWS_ITEM (data);
	value = e_ews_item_get_field_value (item, "birthday");
	if (value) {
		date =  e_contact_date_from_string (value);
		e_contact_set (contact, E_CONTACT_BIRTH_DATE, date);
		e_contact_date_free (date);
	}
}

static void
set_birth_date_in_ews_item (EEwsItem *item, gpointer data)
{
	EContact *contact;
	EContactDate *date;
	gchar *date_string;
	contact = E_CONTACT (data);
	date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (date) {
		date_string = e_contact_date_to_string (date);
		e_ews_item_set_field_value (item, "birthday", date_string);
		e_contact_date_free (date);
		g_free (date_string);
	}

}

static void
set_birth_date_changes (EEwsItem *new_item, EEwsItem *old_item)
{
	gchar *new_birthday;
	gchar *old_birthday;

	new_birthday = e_ews_item_get_field_value (new_item, "birthday");
	old_birthday = e_ews_item_get_field_value (old_item, "birthday");

	if (new_birthday && old_birthday) {
		if (!g_str_equal (new_birthday, old_birthday))
			e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE, "birthday", new_birthday);
	}
	else if (!new_birthday && old_birthday) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "birthday", old_birthday);
	}
	else if (new_birthday && !old_birthday) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "birthday", new_birthday);
	}
}

static const gint email_fields[3] = {
	E_CONTACT_EMAIL_1,
	E_CONTACT_EMAIL_2,
	E_CONTACT_EMAIL_3

};

static void
populate_emails (EContact *contact, gpointer data)
{
	GList *email_list;
	EEwsItem *item;
	gint i;

	item = E_EWS_ITEM (data);
	email_list = e_ews_item_get_email_list(item);

	for (i =0; i < 3 && email_list; i++, email_list = g_list_next (email_list)) {
		if (email_list->data)
			e_contact_set (contact, email_fields[i], email_list->data);
	}
}

static void
set_emails_in_ews_item (EEwsItem *item, gpointer data)
{
	GList *email_list;
	EContact *contact;
	gchar *email;
	gint i;

	contact = E_CONTACT (data);
	email_list = NULL;
	for (i =0; i < 3; i++) {
		email = e_contact_get (contact, email_fields[i]);
		if (email)
			email_list = g_list_append (email_list, g_strdup (email));
	}
	e_ews_item_set_email_list (item, email_list);
}

static void
compare_string_lists ( GList *old_list, GList *new_list, GList **additions, GList **deletions)
{
	GList *temp, *old_list_copy;
	gboolean strings_matched;
	gchar *string1, *string2;

	if (old_list && new_list) {
		old_list_copy = g_list_copy (old_list);
		for (; new_list != NULL; new_list = g_list_next (new_list)) {

			string1 = new_list->data;
			temp = old_list;
			strings_matched = FALSE;
			for (; temp != NULL; temp = g_list_next (temp)) {
				string2 = temp->data;
				if ( g_str_equal (string1, string2)) {
					strings_matched = TRUE;
					old_list_copy = g_list_remove (old_list_copy, string2);
					break;
				}
			}
			if (!strings_matched)
				*additions = g_list_append (*additions, string1);
		}
		*deletions = old_list_copy;
	}
	else if (!new_list && old_list)
		*deletions = g_list_copy (old_list);
	else if (new_list && !old_list)
		*additions = g_list_copy (new_list);
}

static void
set_emails_changes (EEwsItem *new_item, EEwsItem *old_item)
{
	GList *old_email_list;
	GList *new_email_list;
	GList  *added_emails = NULL, *deleted_emails = NULL;

	old_email_list = e_ews_item_get_email_list (old_item);
	new_email_list = e_ews_item_get_email_list (new_item);
	compare_string_lists (old_email_list, new_email_list, &added_emails, &deleted_emails);
	if (added_emails)
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "email", added_emails);
	if (deleted_emails)
		e_ews_item_set_change (new_item,  E_EWS_ITEM_CHANGE_TYPE_DELETE, "email", deleted_emails);
}

static void
populate_full_name (EContact *contact, gpointer data)
{
	EEwsItem *item;
	FullName  *full_name;
	gchar *full_name_string;

	item = E_EWS_ITEM(data);
	full_name = e_ews_item_get_full_name (item);
	if (full_name) {
		full_name_string = g_strconcat ( (full_name->first_name == NULL) ? "\0" :    full_name->first_name, " ",
			    (full_name->middle_name == NULL) ? "\0" : full_name->middle_name, " ",
			    full_name->last_name == NULL ? "\0" : full_name->last_name, " ",
			    (full_name->name_suffix == NULL ) ? "\0" : full_name->name_suffix, NULL);
		full_name_string = g_strstrip (full_name_string);
		if (!g_str_equal (full_name_string, "\0"))
			e_contact_set (contact, E_CONTACT_FULL_NAME, full_name_string);
		g_free (full_name_string);
	}
}

static void
set_full_name_in_ews_item (EEwsItem *item, gpointer data)
{
	EContact *contact;
	gchar   *name;
	EContactName *contact_name;
	FullName *full_name;

	contact = E_CONTACT (data);

	name = e_contact_get (contact, E_CONTACT_FULL_NAME);

	if (name) {
		contact_name = e_contact_name_from_string (name);
		full_name = g_new0 (FullName, 1);
		if (contact_name && full_name) {
			full_name->name_prefix =  g_strdup (contact_name->prefixes);
			full_name->first_name =  g_strdup (contact_name->given);
			full_name->middle_name =  g_strdup (contact_name->additional);
			full_name->last_name =  g_strdup (contact_name->family);
			full_name->name_suffix = g_strdup (contact_name->suffixes);
			e_contact_name_free (contact_name);
		}
		e_ews_item_set_full_name (item, full_name);
	}
}

static FullName *
copy_full_name (FullName *full_name)
{
	FullName *full_name_copy = g_new0(FullName, 1);
	full_name_copy->name_prefix = g_strdup (full_name->name_prefix);
	full_name_copy->first_name =  g_strdup (full_name->first_name);
	full_name_copy->middle_name = g_strdup (full_name->middle_name);
	full_name_copy->last_name = g_strdup (full_name->last_name);
	full_name_copy->name_suffix = g_strdup (full_name->name_suffix);
	return full_name_copy;
}

static void
set_full_name_changes (EEwsItem *new_item, EEwsItem *old_item)
{
	FullName *old_full_name;
	FullName *new_full_name;
	FullName  *update_full_name, *delete_full_name;
	gchar *s1, *s2;
	update_full_name = g_new0(FullName, 1);
	delete_full_name = g_new0 (FullName, 1);

	old_full_name = e_ews_item_get_full_name (old_item);
	new_full_name = e_ews_item_get_full_name (new_item);

	if (old_full_name && new_full_name) {
		s1 = new_full_name->name_prefix;
		s2 = old_full_name->name_prefix;
		if (!s1 && s2)
			delete_full_name->name_prefix = g_strdup(s2);
		else if (s1)
			update_full_name->name_prefix = g_strdup(s1);
		s1 = new_full_name->first_name;
		s2  = old_full_name->first_name;
		if (!s1 && s2)
			delete_full_name->first_name = g_strdup(s2);
		else if (s1)
			update_full_name->first_name = g_strdup(s1);
		s1 = new_full_name->middle_name;
		s2  = old_full_name->middle_name;
		if (!s1 && s2)
			delete_full_name->middle_name = g_strdup(s2);
		else if (s1)
			update_full_name->middle_name = g_strdup(s1);

		s1 = new_full_name->last_name;
		s2 = old_full_name->last_name;
		if (!s1 && s2)
			delete_full_name->last_name = g_strdup(s2);
		else if (s1)
			update_full_name->last_name = g_strdup(s1);
		s1 = new_full_name->name_suffix;
		s2  = old_full_name->name_suffix;
		if (!s1 && s2)
			delete_full_name->name_suffix = g_strdup(s2);
		else if (s1)
			update_full_name->name_suffix = g_strdup(s1);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE,"full_name",  update_full_name);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE,"full_name",  delete_full_name);

	} else if (!new_full_name && old_full_name) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "full_name", copy_full_name(old_full_name));
	} else if (new_full_name && !old_full_name) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "full_name", copy_full_name(new_full_name));
	}
}

static void
populate_contact_members (EContact *contact, gpointer data)
{
	EEwsItem *item;
	GList *member_list;

	item = E_EWS_ITEM(data);
	member_list = e_ews_item_get_member_list (item);

	for (; member_list != NULL; member_list = g_list_next (member_list)) {
		EVCardAttribute *attr;
		EGroupMember *member;
		member = (EGroupMember *) member_list->data;

		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
		e_vcard_attribute_add_param_with_value (attr,
                                                        e_vcard_attribute_param_new (EVC_X_DEST_CONTACT_UID),
							member->id);
		if (member->name) {
			gint len = strlen (member->name);
			gchar *value;

			if (member->name [0] == '\"' && member->name [len - 1] == '\"')
				value = g_strdup_printf ("%s <%s>", member->name, member->email);
			else
				value = g_strdup_printf ("\"%s\" <%s>", member->name, member->email);

			e_vcard_attribute_add_value (attr, value);
			g_free (value);
		} else {
			e_vcard_attribute_add_value (attr, member->email);
		}

		e_vcard_add_attribute (E_VCARD (contact), attr);
	}
}

static void
set_members_in_ews_item (EEwsItem  *item, EContact *contact, EBookBackendEws *egwb)
{
	GList  *members, *temp, *dtemp, *items, *p, *emails_without_ids, *dest_without_ids;
	GList *group_members;
	gchar *email;
	EEwsFilter *filter;
	gint status;
	gchar *id;
	EEwsItem *temp_item;
	gint count = 0;
	gint element_type;
	gint i;
	gchar *value;
	EGroupMember *member;

	members = e_contact_get_attributes (contact, E_CONTACT_EMAIL);
	temp = members;
	filter = e_ews_filter_new ();
	group_members = NULL;
	emails_without_ids = NULL;
	dest_without_ids = NULL;

	for (;temp != NULL; temp = g_list_next (temp)) {
		EVCardAttribute *attr = temp->data;
		EDestination *dest;
		id = email = NULL;

		dest = e_destination_new ();

		for (p = e_vcard_attribute_get_params (attr); p; p = p->next) {
			EVCardAttributeParam *param = p->data;
			const gchar *param_name = e_vcard_attribute_param_get_name (param);

			if (!g_ascii_strcasecmp (param_name,
						 EVC_X_DEST_CONTACT_UID)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				id = v ? v->data : NULL;
				if (id) {
					EEwsItem *ews_item = NULL;
					e_ews_connection_get_item (egwb->priv->cnc, egwb->priv->container_id,id, "name email", &ews_item);
					if (!ews_item) {
						/* The item corresponding to this id is not found. This happens in case of
						 * importing, in imported file the stored id is corresponding to the address
						 * book from which the contact list was exported.
						 */
						id = NULL;
					}
					else
						g_object_unref (ews_item);
				}
			} else if (!g_ascii_strcasecmp (param_name,
							EVC_X_DEST_EMAIL)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				email = v ? v->data : NULL;
			}
		}

		if (!id && !email)
			email = e_vcard_attribute_get_value (attr);
		if (id) {
			member = g_new0 (EGroupMember , 1);
			member->id = g_strdup (id);
			group_members = g_list_append (group_members, member);
		} else if (email) {
			e_destination_set_raw (dest, email);
			e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EQUAL, "emailList/@primary", email);
			emails_without_ids = g_list_append (emails_without_ids, g_strdup (email));
			dest_without_ids = g_list_append (dest_without_ids, dest);

			count++;
		}
	}
	e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, count);
	items = NULL;

	if (count)
		status = e_ews_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, "name email default members", filter, &items);

	for (; items != NULL; items = g_list_next (items )) {
		GList *emails;
		GList *ptr;

		temp_item = E_EWS_ITEM (items->data);
		emails = e_ews_item_get_email_list (temp_item);
		if (emails_without_ids && (ptr = g_list_find_custom (emails_without_ids, emails->data, (GCompareFunc)strcasecmp ))) {
			gint pos = g_list_position (emails_without_ids, ptr);
			emails_without_ids = g_list_remove_link (emails_without_ids, ptr);
			g_list_free (ptr);

			ptr = g_list_nth (dest_without_ids, pos);
			dest_without_ids = g_list_remove_link (dest_without_ids, ptr);
			g_object_unref (ptr->data);
			g_list_free (ptr);

			id = g_strdup (e_ews_item_get_id (temp_item));
			member = g_new0 (EGroupMember , 1);
			member->id = id;
			group_members = g_list_append (group_members, member);
		}
		g_object_unref (temp_item);
	}

	/* In ews there is no way to put arbitrary members into a group. There's no
	 * mechanism for a group to contain members that are not already present in a system
	 * or personal addressbook as a contact, and so they cant be saved and will be lost.
	 * In order to save them we first need to create ews based contacts for these
	 * arbitrary contacts and then add them as members to the group.
	 */

	temp = emails_without_ids;
	dtemp = dest_without_ids;
	for (; temp != NULL && dtemp != NULL; temp = g_list_next (temp), dtemp = g_list_next(dtemp)) {
		EContact *new_contact = e_contact_new ();
		EEwsItem *new_item = e_ews_item_new_empty ();
		FullName *full_name;
		EDestination *tdest = (EDestination *)dtemp->data;

		e_contact_set (new_contact,E_CONTACT_FULL_NAME, e_contact_name_from_string (strdup (e_destination_get_email(tdest))));
		e_contact_set (new_contact, E_CONTACT_EMAIL_1, strdup (e_destination_get_email(tdest)));
		e_contact_set (new_contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (FALSE));
		e_ews_item_set_item_type (new_item, E_EWS_ITEM_TYPE_CONTACT);
		e_ews_item_set_container_id (new_item, g_strdup(egwb->priv->container_id));
		full_name = g_new0 (FullName, 1);
		full_name->name_prefix = NULL;
		full_name->first_name = g_strdup(e_destination_get_name(tdest));
		full_name->middle_name = NULL;
		full_name->last_name = NULL;
		full_name->name_suffix = NULL;
		e_ews_item_set_full_name (new_item, full_name);

		for (i=0; i < G_N_ELEMENTS (mappings); i++) {
			element_type = mappings[i].element_type;
			if (element_type == ELEMENT_TYPE_SIMPLE) {
				value = e_contact_get (new_contact, mappings[i].field_id);
				if (value != NULL) {
					e_ews_item_set_field_value (new_item, mappings[i].element_name, value);
					g_free (value);
				}
			}
			else if	(element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES) {
					continue;
				}
				else if (mappings[i].field_id == E_CONTACT_EMAIL) {
					if (e_contact_get (contact, E_CONTACT_IS_LIST))
						continue;
				}
				else if (mappings[i].field_id == E_CONTACT_FULL_NAME) {
					continue;
				}
				else {
					mappings[i].set_value_in_ews_item (new_item, new_contact);
				}
			}

		}
		id = NULL;
		status = e_ews_connection_create_item (egwb->priv->cnc, new_item, &id);
		if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_ews_connection_create_item (egwb->priv->cnc, new_item, &id);

		if (status == E_EWS_CONNECTION_STATUS_OK && id) {
			e_contact_set (new_contact, E_CONTACT_UID, id);
			e_book_backend_db_cache_add_contact (egwb->priv->file_db, new_contact);
			e_book_backend_summary_add_contact (egwb->priv->summary, new_contact);
			member = g_new0 (EGroupMember, 1);
			member->id = g_strdup (id);
			group_members = g_list_append (group_members, member);
			g_free (id);
		}
		g_object_unref (new_item);
		g_object_unref (new_contact);
	}

	g_list_foreach (members, (GFunc) e_vcard_attribute_free, NULL);
	g_list_free (members);
	g_list_foreach (emails_without_ids, (GFunc) g_free, NULL);
	g_list_free (emails_without_ids);
	g_list_foreach (dest_without_ids, (GFunc) g_object_unref, NULL);
	g_list_free (dest_without_ids);

	g_list_free (items);
	e_ews_item_set_member_list (item, group_members);
}

static void
set_member_changes (EEwsItem *new_item, EEwsItem *old_item, EBookBackendEws *egwb)
{
	GList *old_members, *new_members;
	GList *old_ids,  *new_ids,  *additions, *deletions;

	old_ids = new_ids = additions = deletions = NULL;
	old_members = e_ews_item_get_member_list (old_item);
	new_members = e_ews_item_get_member_list (new_item);

	for (;old_members != NULL; old_members = g_list_next (old_members)) {
		EGroupMember *member;
		member = (EGroupMember *)old_members->data;
		old_ids = g_list_append (old_ids, member->id);
	}
	for (;new_members != NULL; new_members = g_list_next (new_members)) {
		EGroupMember *member;
		member = (EGroupMember *)new_members->data;
		new_ids = g_list_append (new_ids, member->id);
	}

	compare_string_lists (old_ids, new_ids, &additions, &deletions);
	if (additions)
		e_ews_connection_add_members (egwb->priv->cnc, e_ews_item_get_id (old_item), additions);
	if (deletions)
		e_ews_connection_remove_members (egwb->priv->cnc, e_ews_item_get_id (old_item), deletions);

	g_list_free (new_ids);
	g_list_free (old_ids);
	g_list_free (additions);
	g_list_free (deletions);
}

static void
set_organization_changes_in_ews_item (EEwsItem *new_item, EEwsItem *old_item)
{
	gchar *old_value;
	gchar *new_value;
	gchar *old_org_id;
	gchar *new_org_id;

	old_value = e_ews_item_get_field_value (old_item, "organization");
	new_value = e_ews_item_get_field_value (new_item, "organization");
	old_org_id = e_ews_item_get_field_value (old_item, "organization_id");
	new_org_id = e_ews_item_get_field_value (new_item, "organization_id");
	if (new_value && old_value) {
		if (!g_str_equal (new_value, old_value)) {
			e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE, "organization", new_value);
			e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE, "organization_id", new_org_id);
		}
	} else if (!new_value  && old_value) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE,"organization", old_value);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "organization_id", old_org_id);
	} else if (new_value && !old_value) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "organization", new_value);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "organization_id", new_org_id);
	}
}

static void
set_categories_in_ews_item (EEwsItem *item, EContact *contact, EBookBackendEws *egwb)
{
	GHashTable *categories_by_name;
	GList *category_names,  *category_ids;
	gchar *id;
	gint status;

	categories_by_name = egwb->priv->categories_by_name;
	category_names = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);
	category_ids = NULL;
	id = NULL;
	for (; category_names != NULL; category_names = g_list_next (category_names)) {
		if (!category_names->data || strlen(category_names->data) == 0 )
			continue;
		id = g_hash_table_lookup (categories_by_name, category_names->data);
		if (id)
			category_ids = g_list_append (category_ids, g_strdup (id));
		else {
			EEwsItem *category_item;

			category_item = e_ews_item_new_empty();
			e_ews_item_set_item_type (category_item,  E_EWS_ITEM_TYPE_CATEGORY);
			e_ews_item_set_category_name (category_item, category_names->data);
			status = e_ews_connection_create_item (egwb->priv->cnc, category_item, &id);
			if (status == E_EWS_CONNECTION_STATUS_OK && id != NULL) {
				gchar **components = g_strsplit (id, "@", -1);
				gchar *temp_id = components[0];

				g_hash_table_insert (categories_by_name, g_strdup (category_names->data), g_strdup(temp_id));
				g_hash_table_insert (egwb->priv->categories_by_id, g_strdup(temp_id), g_strdup (category_names->data));
				category_ids = g_list_append (category_ids, g_strdup(temp_id));
				g_free (id);
				g_strfreev(components);
			}
			g_object_unref (category_item);
		}
	}
	e_ews_item_set_categories (item, category_ids);
}

static void
set_categories_changes (EEwsItem *new_item, EEwsItem *old_item)
{
	GList *old_category_list;
	GList *new_category_list;
	GList *temp, *old_categories_copy, *added_categories = NULL;
	gboolean categories_matched;
	gchar *category1, *category2;

	old_category_list = e_ews_item_get_categories (old_item);
	new_category_list = e_ews_item_get_categories (new_item);

	if (old_category_list && new_category_list) {
		old_categories_copy = g_list_copy (old_category_list);

		for (; new_category_list != NULL; new_category_list = g_list_next (new_category_list)) {
			category1  = new_category_list->data;
			temp = old_category_list;
			categories_matched  = FALSE;

			for (; temp != NULL; temp = g_list_next (temp)) {
				category2 = temp->data;
				if ( g_str_equal (category1, category2)) {
					categories_matched = TRUE;
					old_categories_copy = g_list_remove (old_categories_copy, category2);
					break;
				}
			}
			if (!categories_matched)
				added_categories = g_list_append (added_categories, category1);
		}

		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "categories", added_categories);
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, "categories", old_categories_copy);
	} else if (!new_category_list && old_category_list) {
		e_ews_item_set_change (new_item,  E_EWS_ITEM_CHANGE_TYPE_DELETE, "categories", old_category_list);
	} else if (new_category_list && !old_category_list) {
		e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, "categories", new_category_list);
	}
}

static void
fill_contact_from_ews_item (EContact *contact, EEwsItem *item, GHashTable *categories_by_ids)
{
	gchar * value;
	gint element_type;
	gint i;
	gboolean is_contact_list;
	gboolean is_organization;

	is_organization = e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ORGANISATION ? TRUE: FALSE;
	is_contact_list = e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_GROUP ? TRUE: FALSE;

	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (is_contact_list));
	if (is_contact_list)
		e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));

	for ( i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;

		if (element_type == ELEMENT_TYPE_SIMPLE) {
			if (mappings[i].field_id != E_CONTACT_BOOK_URI) {
				if (!is_organization) {
					value = e_ews_item_get_field_value (item, mappings[i].element_name);
					if (value != NULL)
						e_contact_set (contact, mappings[i].field_id, value);
				}
			}
		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			if (mappings[i].field_id == E_CONTACT_CATEGORIES) {
				GList *category_ids, *category_names;
				gchar *name;

				category_names = NULL;
				category_ids = e_ews_item_get_categories (item);
				for (; category_ids; category_ids = g_list_next (category_ids)) {
					name = g_hash_table_lookup (categories_by_ids, category_ids->data);
					if (name)
						category_names = g_list_append (category_names, name);
				}
				if (category_names) {
					e_contact_set (contact, E_CONTACT_CATEGORY_LIST, category_names);
					g_list_free (category_names);
				}
			}
			else
				mappings[i].populate_contact_func(contact, item);
		}
	}
}

static void
e_book_backend_ews_create_contact (EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 const gchar *vcard )
{
	EContact *contact;
	EBookBackendEws *egwb;
	gchar *id;
	gint status;
	EEwsItem *item;
	gint element_type;
	gchar * value;
	gint i;

	if (enable_debug)
		printf("\ne_book_backend_ews_create_contact...\n");

	egwb = E_BOOK_BACKEND_GROUPWISE (backend);

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_create (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case  E_DATA_BOOK_MODE_REMOTE :

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_create (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}
		if (!egwb->priv->is_writable) {
			e_data_book_respond_create (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}
		contact = e_contact_new_from_vcard(vcard);
		item = e_ews_item_new_empty ();
		e_ews_item_set_item_type (item, e_contact_get (contact, E_CONTACT_IS_LIST) ? E_EWS_ITEM_TYPE_GROUP :E_EWS_ITEM_TYPE_CONTACT);
		e_ews_item_set_container_id (item, g_strdup(egwb->priv->container_id));

		for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
			element_type = mappings[i].element_type;
			if (element_type == ELEMENT_TYPE_SIMPLE)  {
				value =  e_contact_get(contact, mappings[i].field_id);
				if (value != NULL)
					e_ews_item_set_field_value (item, mappings[i].element_name, value);
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES) {
					set_categories_in_ews_item (item, contact, egwb);
				}
				else if (mappings[i].field_id == E_CONTACT_EMAIL) {
					if (e_contact_get (contact, E_CONTACT_IS_LIST))
						set_members_in_ews_item (item, contact, egwb);
				}
				else {
					mappings[i].set_value_in_ews_item (item, contact);
				}
			}
		}
		id = NULL;
		status = e_ews_connection_create_item (egwb->priv->cnc, item, &id);
		if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_ews_connection_create_item (egwb->priv->cnc, item, &id);

		/* Make sure server has returned  an id for the created contact */
		if (status == E_EWS_CONNECTION_STATUS_OK && id) {
			e_contact_set (contact, E_CONTACT_UID, id);
			g_free (id);
			e_book_backend_db_cache_add_contact (egwb->priv->file_db, contact);
			egwb->priv->file_db->sync(egwb->priv->file_db, 0);
			e_book_backend_summary_add_contact (egwb->priv->summary, contact);
			e_data_book_respond_create (book, opid, EDB_ERROR (SUCCESS), contact);

		}
		else {
			e_data_book_respond_create (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status), NULL);
		}
		g_object_unref (item);
		return;
	default:
		break;
	}
}

static void
e_book_backend_ews_remove_contacts (EBookBackend *backend,
					  EDataBook    *book,
					  guint32 opid,
					  GList *id_list)
{
	gchar *id;
	EBookBackendEws *ebgw;
	GList *deleted_ids = NULL;

	if (enable_debug)
		printf ("\ne_book_backend_ews_remove_contacts...\n");

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);

	switch (ebgw->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (ebgw->priv->cnc == NULL) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		if (!ebgw->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		for (; id_list != NULL; id_list = g_list_next (id_list)) {
			id = (gchar *) id_list->data;
			e_ews_connection_remove_item (ebgw->priv->cnc, ebgw->priv->container_id, id);
			deleted_ids =  g_list_append (deleted_ids, id);
			e_book_backend_db_cache_remove_contact (ebgw->priv->file_db, id);
			e_book_backend_summary_remove_contact (ebgw->priv->summary, id);
		}
		ebgw->priv->file_db->sync(ebgw->priv->file_db, 0);
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (SUCCESS),  deleted_ids);
		return;
	default :
		break;
	}
}

static void
set_changes_in_ews_item (EEwsItem *new_item, EEwsItem *old_item)
{
	gchar * new_value;
	gchar *old_value;
	gint element_type;
	gint i;

	g_return_if_fail (E_IS_GW_ITEM(new_item));
	g_return_if_fail (E_IS_GW_ITEM(old_item));

	for ( i = 0; i < G_N_ELEMENTS (mappings); i++) {
		element_type = mappings[i].element_type;
		if (element_type == ELEMENT_TYPE_SIMPLE) {
			if (mappings[i].field_id == E_CONTACT_ORG) {
				set_organization_changes_in_ews_item (new_item, old_item);
				continue;
			}

			new_value = e_ews_item_get_field_value (new_item, mappings[i].element_name);
			old_value = e_ews_item_get_field_value (old_item, mappings[i].element_name);
			if (new_value && old_value) {
				if (!g_str_equal (new_value, old_value))
					e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_UPDATE, mappings[i].element_name, new_value);
			} else if (!new_value  && old_value) {
				e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_DELETE, mappings[i].element_name, old_value);
			} else if (new_value && !old_value) {
				e_ews_item_set_change (new_item, E_EWS_ITEM_CHANGE_TYPE_ADD, mappings[i].element_name, new_value);
			}

		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			if (mappings[i].field_id != E_CONTACT_EMAIL)
				mappings[i].set_changes(new_item, old_item);
		}
	}
}

static void
e_book_backend_ews_modify_contact (EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *vcard)
{
	EContact *contact;
	EBookBackendEws *egwb;
	gchar *id;
	gint status;
	EEwsItem *new_item;
	EEwsItem *old_item;
	gint element_type;
	gchar * value;
	gint i;

	if (enable_debug)
		printf ("\ne_book_backend_ews_modify_contact...\n");
	egwb = E_BOOK_BACKEND_GROUPWISE (backend);

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
		contact = e_contact_new_from_vcard(vcard);
		new_item = e_ews_item_new_empty ();

		for (i = 0; i < G_N_ELEMENTS (mappings); i++) {
			element_type = mappings[i].element_type;
			if (element_type == ELEMENT_TYPE_SIMPLE)  {
				value =  e_contact_get(contact, mappings[i].field_id);
				if (value &&  *value)
					e_ews_item_set_field_value (new_item, mappings[i].element_name, value);
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES)
					set_categories_in_ews_item (new_item, contact, egwb);
				else if (mappings[i].field_id == E_CONTACT_EMAIL) {
					if (e_contact_get (contact, E_CONTACT_IS_LIST))
						set_members_in_ews_item (new_item, contact, egwb);
				}
				else
					mappings[i].set_value_in_ews_item (new_item, contact);
			}
		}

		id = e_contact_get (contact, E_CONTACT_UID);
		old_item = NULL;
		status = e_ews_connection_get_item (egwb->priv->cnc, egwb->priv->container_id, id, NULL,  &old_item);

		if (old_item == NULL) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
			return;
		}

		if (status != E_EWS_CONNECTION_STATUS_OK) {
			e_data_book_respond_modify (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status), NULL);
			return;
		}

		if (e_contact_get (contact, E_CONTACT_IS_LIST))
			set_member_changes (new_item, old_item, egwb);

		set_changes_in_ews_item (new_item, old_item);

		e_ews_item_set_item_type (new_item, e_ews_item_get_item_type (old_item));
		status = e_ews_connection_modify_item (egwb->priv->cnc, id, new_item);
		if (status == E_EWS_CONNECTION_STATUS_OK) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (SUCCESS), contact);
			e_book_backend_db_cache_remove_contact (egwb->priv->file_db, id);
			e_book_backend_summary_remove_contact (egwb->priv->summary, id);
			e_book_backend_db_cache_add_contact (egwb->priv->file_db, contact);
			egwb->priv->file_db->sync(egwb->priv->file_db, 0);
			e_book_backend_summary_add_contact (egwb->priv->summary, contact);
		}
		else
			e_data_book_respond_modify (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status), NULL);
		g_object_unref (new_item);
		g_object_ref (old_item);
		g_object_unref (contact);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar   *id)
{
	EBookBackendEws *gwb;
	gint status;
	EEwsItem *item;
	EContact *contact;
	gchar *vcard;

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_contact...\n");

	gwb =  E_BOOK_BACKEND_GROUPWISE (backend);

	switch (gwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		contact = e_book_backend_db_cache_get_contact (gwb->priv->file_db, id);
		vcard =  e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		if (contact) {
			e_data_book_respond_get_contact (book, opid, EDB_ERROR (SUCCESS), vcard);
			g_free (vcard);
			g_object_unref (contact);
		}
		else {
			e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		}
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (gwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "Not connected"), NULL);
			return;
		}
		status = e_ews_connection_get_item (gwb->priv->cnc, gwb->priv->container_id, id,
						   "name email default members", &item);
		if (status == E_EWS_CONNECTION_STATUS_OK) {
			if (item) {
				contact = e_contact_new ();
				fill_contact_from_ews_item (contact, item, gwb->priv->categories_by_id);
				e_contact_set (contact, E_CONTACT_BOOK_URI, gwb->priv->original_uri);
				vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
				e_data_book_respond_get_contact (book, opid, EDB_ERROR (SUCCESS), vcard);
				g_free (vcard);
				g_object_unref (contact);
				g_object_unref (item);
				return;
			}
		}
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		return;
	default :
		break;
	}
}

typedef struct {
	EEwsFilter *filter;
	gboolean is_filter_valid;
	gboolean is_personal_book;
	gint auto_completion;
	gchar *search_string;
} EBookBackendEwsSExpData;

static ESExpResult *
func_and(ESExp *f, gint argc, ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);
	if (argc > 0)
		e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_AND, argc);
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_or(ESExp *f, gint argc, ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);
	if (argc > 0)
		 e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, argc);
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_not(ESExp *f, gint argc, ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	sexp_data->is_filter_valid = FALSE;
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		const gchar *ews_field_name;

		if (g_str_equal (propname, "x-evolution-any-field")) {
			if (!sexp_data->is_personal_book && str && strlen(str) == 0) {
				/* ignore the NULL query */
				sexp_data->is_filter_valid = FALSE;
				r = e_sexp_result_new(f, ESEXP_RES_BOOL);
				r->value.boolean = FALSE;
				return r;
			}
		}
		ews_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			ews_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			ews_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			 ews_field_name = "name";

		if (ews_field_name) {
			if (g_str_equal (ews_field_name, "fullName")) {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_CONTAINS, "fullName/firstName", str);
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_CONTAINS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_CONTAINS, "fullName/displayName", str);
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 3);
				}
				else {
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 2);
				}
			}
			else {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_CONTAINS, ews_field_name, str);
			}
		}
		else {
		     sexp_data->is_filter_valid = FALSE;
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		const gchar *ews_field_name;

		ews_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			ews_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			ews_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			ews_field_name = "name";

		if (ews_field_name) {
			if (g_str_equal (ews_field_name, "fullName")) {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EQUAL, "fullName/firstName", str);
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EQUAL, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EQUAL, "fullName/displayName", str);
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 3);
				}
				else {
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 2);
				}
			}
			else {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EQUAL, ews_field_name, str);
			}
		}
		else {
		     sexp_data->is_filter_valid = FALSE;
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

#define BEGINS_WITH_NAME (1 << 0)
#define BEGINS_WITH_EMAIL (1 << 1)
#define BEGINS_WITH_FILE_AS (1 << 2)
#define BEGINS_WITH_NICK_NAME (1 << 3)
#define AUTO_COMPLETION_QUERY 15

static ESExpResult *
func_beginswith(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = argv[1]->value.string;
		const gchar *ews_field_name;

		if (!sexp_data->is_personal_book && str && strlen(str) == 0) {
			/* ignore the NULL query */
			sexp_data->is_filter_valid = FALSE;
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.boolean = FALSE;
			return r;
		}

		ews_field_name = NULL;
		if (g_str_equal (propname, "full_name")) {
			ews_field_name = "fullName";
			sexp_data->auto_completion |= BEGINS_WITH_NAME;
			sexp_data->search_string = g_strdup (str);
		}
		else if (g_str_equal (propname, "email")) {
			ews_field_name = "emailList/email";
			sexp_data->auto_completion |= BEGINS_WITH_EMAIL;
		}
		else if (g_str_equal (propname, "file_as")) {
			 ews_field_name = "name";
			 sexp_data->auto_completion |= BEGINS_WITH_FILE_AS;
		} else if (g_str_equal (propname, "nickname")) {
			 ews_field_name = "name";
			 sexp_data->auto_completion |= BEGINS_WITH_NICK_NAME;
		}

		if (ews_field_name) {

			if (g_str_equal (ews_field_name, "fullName")) {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS, "fullName/firstName", str);
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS, "fullName/displayName", str);
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 3);
				}
				else {
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 2);
				}
			}
			else {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS, ews_field_name, str);
			}
		}
		else {
			sexp_data->is_filter_valid = FALSE;
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_endswith(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	EBookBackendEwsSExpData *sexp_data;
	ESExpResult *r;

	sexp_data = (EBookBackendEwsSExpData *) data;
	sexp_data->is_filter_valid = FALSE;

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);

	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_exists(struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EEwsFilter *filter;
	EBookBackendEwsSExpData *sexp_data;

	sexp_data = (EBookBackendEwsSExpData *) data;
	filter = E_EWS_FILTER (sexp_data->filter);

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		const gchar *str = "";
		const gchar *ews_field_name;

		ews_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			ews_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			ews_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			 ews_field_name = "name";

		/* FIXME the whole function looks useless. Why does one need str argument to the filter here ?*/
		if (ews_field_name) {

			if (g_str_equal (ews_field_name, "fullName")) {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EXISTS, "fullName/firstName", str);
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EXISTS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EXISTS, "fullName/displayName", str);
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 3);
				}
				else {
					e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 2);
				}
			}
			else {
				e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_EXISTS, ews_field_name, str);
			}
		}
		else {
			sexp_data->is_filter_valid = FALSE;
		}
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

/* 'builtin' functions */
static const struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", func_and, 0 },
	{ "or", func_or, 0 },
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
	{ "exists", func_exists, 0 },
};

static EEwsFilter*
e_book_backend_ews_build_ews_filter (EBookBackendEws *ebgw, const gchar *query, gpointer is_auto_completion, gchar ** search_string)
{
	ESExp *sexp;
	ESExpResult *r;
	EBookBackendEwsSExpData *sexp_data;
	EEwsFilter *filter;
	gint i;

	sexp = e_sexp_new();
	filter = e_ews_filter_new ();

	sexp_data = g_new0 (EBookBackendEwsSExpData, 1);
	sexp_data->filter = filter;
	sexp_data->is_filter_valid = TRUE;
	sexp_data->is_personal_book = e_book_backend_is_writable ( E_BOOK_BACKEND (ebgw));
	sexp_data->auto_completion = 0;
	sexp_data->search_string = NULL;

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, sexp_data);
		} else {
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, sexp_data);
		}
	}

	e_sexp_input_text(sexp, query, strlen(query));
	e_sexp_parse(sexp);
	r = e_sexp_eval(sexp);
	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	if (sexp_data->is_filter_valid) {
		if (sexp_data->auto_completion == AUTO_COMPLETION_QUERY)
			*(gboolean *)is_auto_completion = TRUE;
		if (search_string)
			*search_string = sexp_data->search_string;
		g_free (sexp_data);
		return filter;
	}
	else {
		g_object_unref (filter);
		g_free (sexp_data);
		return NULL;
	}
}

static void
e_book_backend_ews_get_contact_list (EBookBackend *backend,
					   EDataBook    *book,
					   guint32       opid,
					   const gchar   *query )
{
	GList *vcard_list;
	gint status;
	GList *ews_items, *contacts = NULL, *temp;
	EContact *contact;
	EBookBackendEws *egwb;
	gboolean match_needed;
	EBookBackendSExp *card_sexp = NULL;
	EEwsFilter *filter = NULL;
	GPtrArray *ids;
	gboolean is_auto_completion;

	egwb = E_BOOK_BACKEND_GROUPWISE (backend);
	vcard_list = NULL;
	ews_items = NULL;

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_contact_list...\n");

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :

		if (!egwb->priv->file_db) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
			return;
		}

		if (egwb->priv->is_summary_ready &&
		    e_book_backend_summary_is_summary_query (egwb->priv->summary, query)) {
			gint i;
			ids = e_book_backend_summary_search (egwb->priv->summary, query);
			if (!ids)
				return;

			for (i = 0; i < ids->len; i++) {
				gchar *uid = g_ptr_array_index (ids, i);

				EContact *contact =
					e_book_backend_db_cache_get_contact (egwb->priv->file_db, uid);
				contacts = g_list_append (contacts, contact);
			}
			g_ptr_array_free (ids, TRUE);
		}
		else
			contacts = e_book_backend_db_cache_get_contacts (egwb->priv->file_db, query);

		temp = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) {
			vcard_list = g_list_append (vcard_list,
						    e_vcard_to_string (E_VCARD (contacts->data),
						    EVC_FORMAT_VCARD_30));
			g_object_unref (contacts->data);
		}
		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		if (temp)
			g_list_free (temp);
		return;

	case E_DATA_BOOK_MODE_REMOTE:

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		match_needed = TRUE;
		card_sexp = e_book_backend_sexp_new (query);
		if (!card_sexp) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (INVALID_QUERY), vcard_list);
		}

		status = E_EWS_CONNECTION_STATUS_OK;
		if (egwb->priv->is_cache_ready ) {
			if (egwb->priv->is_summary_ready &&
			    e_book_backend_summary_is_summary_query (egwb->priv->summary, query)) {
				ids = e_book_backend_summary_search (egwb->priv->summary, query);

				if (!egwb->priv->is_writable) {
					gint i;
					for (i = 0; i < ids->len; i++) {
						gchar *uid = g_ptr_array_index (ids, i);
						contact = e_book_backend_db_cache_get_contact (egwb->priv->file_db, uid);
						vcard_list = g_list_append (vcard_list,
                                                            e_vcard_to_string (E_VCARD (contact),
                                                            EVC_FORMAT_VCARD_30));
						g_object_unref (contact);
					}
					g_ptr_array_free (ids, TRUE);
					ids = NULL;
				}
			}
			else {
				ids = e_book_backend_db_cache_search (egwb->priv->file_db, query);
			}

			if (ids && ids->len > 0) {
				status = e_ews_connection_get_items_from_ids (egwb->priv->cnc,
									egwb->priv->container_id,
									"name email default members",
									ids, &ews_items);
				if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_ews_connection_get_items_from_ids (egwb->priv->cnc,
									egwb->priv->container_id,
									"name email default members",
									ids, &ews_items);
			}
			if (ids)
				g_ptr_array_free (ids, TRUE);
			match_needed = FALSE;
		} else {
			if (strcmp (query, "(contains \"x-evolution-any-field\" \"\")") != 0)
				filter = e_book_backend_ews_build_ews_filter (egwb,
										   query,
										   &is_auto_completion,
										   NULL);
			if (filter)
				match_needed = FALSE;
			status = e_ews_connection_get_items (egwb->priv->cnc,
							    egwb->priv->container_id,
							    "name email default members",
							    filter, &ews_items);
			if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_ews_connection_get_items (egwb->priv->cnc,
								    egwb->priv->container_id,
								    "name email default members",
								    filter, &ews_items);
		}

		if (status != E_EWS_CONNECTION_STATUS_OK) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status), NULL);
			return;
		}
		for (; ews_items != NULL; ews_items = g_list_next(ews_items)) {
			contact = e_contact_new ();
			fill_contact_from_ews_item (contact, E_EWS_ITEM (ews_items->data), egwb->priv->categories_by_id);
			e_contact_set (contact, E_CONTACT_BOOK_URI, egwb->priv->original_uri);
			if (match_needed &&  e_book_backend_sexp_match_contact (card_sexp, contact))
				vcard_list = g_list_append (vcard_list,
							    e_vcard_to_string (E_VCARD (contact),
							    EVC_FORMAT_VCARD_30));
			else
				vcard_list = g_list_append (vcard_list,
							    e_vcard_to_string (E_VCARD (contact),
							    EVC_FORMAT_VCARD_30));
			g_object_unref (contact);
			g_object_unref (ews_items->data);
		}
		if (ews_items)
			g_list_free (ews_items);
		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		if (filter)
			g_object_unref (filter);
		return;
	default :
		break;

	}
}

typedef struct {
	EBookBackendEws *bg;
	GThread *thread;
	EFlag *running;
} EwsBackendSearchClosure;

static void
closure_destroy (EwsBackendSearchClosure *closure)
{
	e_flag_free (closure->running);
	g_free (closure);
}

static EwsBackendSearchClosure*
init_closure (EDataBookView *book_view, EBookBackendEws *bg)
{
	EwsBackendSearchClosure *closure = g_new (EwsBackendSearchClosure, 1);

	closure->bg = bg;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (G_OBJECT (book_view), "EBookBackendEws.BookView::closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static EwsBackendSearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "EBookBackendEws.BookView::closure");
}

static void
get_contacts_from_cache (EBookBackendEws *ebgw,
			 const gchar *query,
			 GPtrArray *ids,
			 EDataBookView *book_view,
			 EwsBackendSearchClosure *closure)
{
	gint i;

	if (enable_debug)
		printf ("\nread contacts from cache for the ids found in summary\n");
	for (i = 0; i < ids->len; i++) {
		gchar *uid;
		EContact *contact;

                if (!e_flag_is_set (closure->running))
                        break;

		uid = g_ptr_array_index (ids, i);
		contact = e_book_backend_db_cache_get_contact (ebgw->priv->file_db, uid);
		if (contact) {
			e_data_book_view_notify_update (book_view, contact);
			g_object_unref (contact);
		}
	}
	if (e_flag_is_set (closure->running))
		e_data_book_view_notify_complete (book_view, NULL /* Success */);
}

static gpointer
book_view_thread (gpointer data)
{
	gint status, count = 0;
	GList *ews_items, *temp_list, *contacts;
	EContact *contact;
	EBookBackendEws *gwb;
	const gchar *query = NULL;
	EEwsFilter *filter = NULL;
	GPtrArray *ids = NULL;
	EDataBookView *book_view = data;
	EwsBackendSearchClosure *closure = get_closure (book_view);
	const gchar *view = NULL;
	gboolean is_auto_completion = FALSE;
	gchar *search_string = NULL;
	GTimeVal start, end;
	gulong diff;

	gwb  = closure->bg;
	ews_items = NULL;

	if (enable_debug)
		printf ("start book view for %s \n", gwb->priv->book_name);
	e_data_book_view_ref (book_view);
	e_flag_set (closure->running);

	query = e_data_book_view_get_card_query (book_view);
	if (enable_debug)
		printf ("get view for query %s \n", query);
	switch (gwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		if (!gwb->priv->file_db) {
			e_data_book_view_notify_complete (book_view, NULL /* Success */);
			return NULL;
		}

		if (gwb->priv->is_summary_ready &&
		    e_book_backend_summary_is_summary_query (gwb->priv->summary, query)) {
			if (enable_debug)
				printf ("reading the uids from summary \n");
			ids = e_book_backend_summary_search (gwb->priv->summary, query);
			if (ids && ids->len > 0) {
				get_contacts_from_cache (gwb, query, ids, book_view, closure);
			}
			if (ids)
				g_ptr_array_free (ids, TRUE);
			e_data_book_view_unref (book_view);
			return NULL;
		}

		/* fall back to cache */
		if (enable_debug)
			printf ("summary not found, reading the uids from cache\n");
		contacts = e_book_backend_db_cache_get_contacts (gwb->priv->file_db, query);
		temp_list = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) {
			if (!e_flag_is_set (closure->running)) {
				for (;contacts != NULL; contacts = g_list_next (contacts))
					g_object_unref (contacts->data);
				break;
			}
			e_data_book_view_notify_update (book_view, E_CONTACT(contacts->data));
			g_object_unref (contacts->data);
		}
		if (e_flag_is_set (closure->running))
			e_data_book_view_notify_complete (book_view, NULL /* Success */);
		if (temp_list)
			g_list_free (temp_list);
		e_data_book_view_unref (book_view);
		return NULL;

	case E_DATA_BOOK_MODE_REMOTE :

		if (gwb->priv->cnc == NULL) {
			GError *edb_err = EDB_ERROR (AUTHENTICATION_REQUIRED);

			e_data_book_view_notify_complete (book_view, edb_err);
			e_data_book_view_unref (book_view);
			g_error_free (edb_err);
			return NULL;
		}

		if (enable_debug)
			g_get_current_time(&start);

		filter = e_book_backend_ews_build_ews_filter (gwb, query, &is_auto_completion, &search_string);
		view = "name email default members";
		if (is_auto_completion && !g_getenv ("AUTOCOMPLETE_EXPAND_CL"))
			view = "name email";

		if (search_string) {
			if (filter)
				g_object_unref (filter);

			/* ews server supports only name, rebuild the filter */
			filter = e_ews_filter_new ();
			e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS,
							  "fullName/lastName", search_string);
			e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_BEGINS,
							  "fullName/firstName", search_string);
			e_ews_filter_group_conditions (filter, E_EWS_FILTER_OP_OR, 2);
			g_free (search_string);
		}

		if (!gwb->priv->is_writable && !filter && (g_getenv ("GW_HIDE_SYSBOOK") || (!gwb->priv->is_cache_ready))) {

				e_data_book_view_notify_complete (book_view, NULL /* Success */);
				e_data_book_view_unref (book_view);
				if (filter)
					g_object_unref (filter);
				return NULL;
		}
		else
			status =  E_EWS_CONNECTION_STATUS_OK;

		/* Check if the data is found on summary */
		if (gwb->priv->is_summary_ready &&
		    e_book_backend_summary_is_summary_query (gwb->priv->summary, query)) {
			if (enable_debug)
				printf("reading the uids from summary file\n");
			ids = e_book_backend_summary_search (gwb->priv->summary, query);
		}

		/*
		 * Search for contact in cache, if not found, read from server
		 */

		if (ids && ids->len > 0) {
			if (enable_debug)
				printf ("number of matches found in summary %d\n", ids->len);
			/* read from summary */
			if (gwb->priv->is_cache_ready && !gwb->priv->is_writable) {
				/* read from cache, only for system address book, as we refresh
				 * only system address book, periodically.
				 */
				if (enable_debug)
					printf ("reading contacts from cache for the uids in summary \n");
				if (!is_auto_completion)
					e_data_book_view_notify_status_message (book_view,
										_("Searching..."));
				get_contacts_from_cache (gwb, query, ids, book_view, closure);
				g_ptr_array_free (ids, TRUE);
				e_data_book_view_unref (book_view);
				if (enable_debug) {
					g_get_current_time(&end);
					diff = end.tv_sec * 1000 + end.tv_usec/1000;
					diff -= start.tv_sec * 1000 + start.tv_usec/1000;
					printf("reading contacts from cache took %ld.%03ld seconds\n",
						diff/1000,diff%1000);
				}
				if (filter)
					g_object_unref (filter);
				return NULL;
			}
			else {
				/* read from server for the ids */
				/* either autocompletion or search query and cache not ready */
				if (enable_debug)
					printf ("reading contacts from server for the uids in summary \n");
				if (!is_auto_completion)
					e_data_book_view_notify_status_message (book_view,
										_("Searching..."));
				status = e_ews_connection_get_items_from_ids (gwb->priv->cnc,
									     gwb->priv->container_id,
									     view, ids, &ews_items);
				if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_ews_connection_get_items_from_ids (gwb->priv->cnc,
										     gwb->priv->container_id,
										     view, ids, &ews_items);
				if (enable_debug && status == E_EWS_CONNECTION_STATUS_OK)
					printf ("read contacts from server \n");
			}
		}
		else {
			if (gwb->priv->is_cache_ready) {
				contacts = e_book_backend_db_cache_get_contacts (gwb->priv->file_db, query);
				temp_list = contacts;
				for (; contacts != NULL; contacts = g_list_next(contacts)) {
					if (!e_flag_is_set (closure->running)) {
						for (;contacts != NULL; contacts = g_list_next (contacts))
							g_object_unref (contacts->data);
						break;
					}
					e_data_book_view_notify_update (book_view, E_CONTACT(contacts->data));
					g_object_unref (contacts->data);
				}
				if (e_flag_is_set (closure->running))
					e_data_book_view_notify_complete (book_view, NULL /* Success */);
				if (temp_list)
					g_list_free (temp_list);
				e_data_book_view_unref (book_view);

				if (filter)
					g_object_unref (filter);

				if (ids)
					g_ptr_array_free (ids, TRUE);

				return NULL;
			}

			/* no summary information found, read from server */
			if (enable_debug)
				printf ("summary not found, reading the contacts from server\n");
			if (!is_auto_completion) {
				if (filter)
					e_data_book_view_notify_status_message (book_view, _("Searching..."));
				else
					e_data_book_view_notify_status_message (book_view, _("Loading..."));
			}
			status = e_ews_connection_get_items (gwb->priv->cnc,
							    gwb->priv->container_id,
							    view, filter, &ews_items);
			if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_ews_connection_get_items (gwb->priv->cnc,
								    gwb->priv->container_id,
								    view, filter, &ews_items);
		}

		if (ids)
			g_ptr_array_free (ids, TRUE);

		if (status != E_EWS_CONNECTION_STATUS_OK) {
			GError *edb_err = EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status);
			e_data_book_view_notify_complete (book_view, edb_err);
			e_data_book_view_unref (book_view);
			g_error_free (edb_err);
			if (filter)
				g_object_unref (filter);
			return NULL;
		}

		temp_list = ews_items;
		for (; ews_items != NULL; ews_items = g_list_next(ews_items)) {

			if (!e_flag_is_set (closure->running)) {
				for (;ews_items != NULL; ews_items = g_list_next (ews_items))
					g_object_unref (ews_items->data);
				break;
			}

			count++;
			contact = e_contact_new ();
			fill_contact_from_ews_item (contact,
						   E_EWS_ITEM (ews_items->data),
						   gwb->priv->categories_by_id);
			e_contact_set (contact, E_CONTACT_BOOK_URI, gwb->priv->original_uri);
			if (e_contact_get_const (contact, E_CONTACT_UID))
				e_data_book_view_notify_update (book_view, contact);
			else
				g_critical ("Id missing for item %s\n", (gchar *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
			g_object_unref(contact);
			g_object_unref (ews_items->data);
		}
		if (temp_list)
			g_list_free (temp_list);
		if (e_flag_is_set (closure->running))
			e_data_book_view_notify_complete (book_view, NULL /* Success */);
		if (filter)
			g_object_unref (filter);
		e_data_book_view_unref (book_view);

		if (enable_debug) {
			g_get_current_time(&end);
			diff = end.tv_sec * 1000 + end.tv_usec/1000;
			diff -= start.tv_sec * 1000 + start.tv_usec/1000;
			printf("reading %d contacts from server took %ld.%03ld seconds\n",
				count, diff/1000,diff%1000);
		}

		return NULL;
	default :
		break;
	}
	return NULL;
}

static void
e_book_backend_ews_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	EwsBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_GROUPWISE (backend));

	if (enable_debug)
		printf ("\ne_book_backend_ews_start_book_view...\n");
	closure->thread = g_thread_create (book_view_thread, book_view, FALSE, NULL);
	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
}

static void
e_book_backend_ews_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	EwsBackendSearchClosure *closure = get_closure (book_view);

	if (enable_debug)
		printf ("\ne_book_backend_ews_stop_book_view...\n");
	e_flag_clear (closure->running);
}

static void
e_book_backend_ews_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar *change_id  )
{
	if (enable_debug)
		printf ("\ne_book_backend_ews_get_changes...\n");

	/* FIXME : provide implmentation */

}

static void
book_view_notify_status (EDataBookView *view, const gchar *status)
{
	if (!view)
		return;
	e_data_book_view_notify_status_message (view, status);
}

static EDataBookView *
find_book_view (EBookBackendEws *ebgw)
{
	EList *views = e_book_backend_get_book_views (E_BOOK_BACKEND (ebgw));
	EIterator *iter;
	EDataBookView *rv = NULL;

	if (!views)
		return NULL;

	iter = e_list_get_iterator (views);

	if (!iter) {
		g_object_unref (views);
		return NULL;
	}

	if (e_iterator_is_valid (iter)) {
		/* just always use the first book view */
		EDataBookView *v = (EDataBookView*)e_iterator_get(iter);
		if (v)
			rv = v;
	}

	g_object_unref (iter);
	g_object_unref (views);

	return rv;
}

static void
get_sequence_from_cache (DB *db,
		gdouble *cache_first_sequence,
		gdouble *cache_last_sequence,
		gdouble *cache_last_po_rebuild_time)
{
	DBT uid_dbt, vcard_dbt;
	gint db_error;

	string_to_dbt ("firstSequence", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
	}
	else {
		*cache_first_sequence = strtod (g_strdup (vcard_dbt.data), NULL);
		g_free (vcard_dbt.data);
	}

	string_to_dbt ("lastSequence", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
	}
	else {
		*cache_last_sequence = strtod (g_strdup (vcard_dbt.data), NULL);
		g_free (vcard_dbt.data);
	}

	string_to_dbt ("lastTimePORebuild", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof(vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
	}
	else {
		*cache_last_po_rebuild_time = strtod (g_strdup (vcard_dbt.data), NULL);
		g_free (vcard_dbt.data);
	}

	if (enable_debug) {
		printf("Read sequences from cache\n");
		printf("firstSequence:%lf, lastSequence:%lf, lastPoRebuildTime:%lf\n", *cache_first_sequence, *cache_last_sequence, *cache_last_po_rebuild_time);
	}

}
static void
add_sequence_to_cache (DB *db,
		       gdouble first_sequence,
		       gdouble last_sequence,
		       gdouble last_po_rebuild_time)
{
		gchar *tmp;
		DBT   uid_dbt, vcard_dbt;
		gint db_error;

		if (enable_debug) {
			printf("Adding sequences to cache\n");
			printf("firstSequence:%lf, lastSequence:%lf, lastPoRebuildTime:%lf\n", first_sequence, last_sequence, last_po_rebuild_time);
		}

		string_to_dbt ("firstSequence",&uid_dbt );
		tmp = g_strdup_printf("%lf", first_sequence);
		string_to_dbt (tmp, &vcard_dbt);

		db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);

		g_free (tmp);

		if (db_error != 0) {
			g_warning ("db->put failed with %d", db_error);
		}

		string_to_dbt ("lastSequence",&uid_dbt );
		tmp = g_strdup_printf("%lf", last_sequence);
		string_to_dbt (tmp, &vcard_dbt);

		db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);

		g_free (tmp);

		if (db_error != 0) {
			g_warning ("db->put failed with %d", db_error);
		}

		string_to_dbt ("lastTimePORebuild",&uid_dbt );
		tmp = g_strdup_printf("%lf", last_po_rebuild_time);
		string_to_dbt (tmp, &vcard_dbt);

		db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);

		g_free (tmp);

		if (db_error != 0) {
			g_warning ("db->put failed with %d", db_error);
		}
}

#define CURSOR_ITEM_LIMIT 100
/*
static gpointer
build_cache (EBookBackendEws *ebgw)
{
	gint status, contact_num = 0;
	GList *ews_items = NULL;
	EContact *contact;
	EDataBookView *book_view;
	EBookBackendEwsPrivate *priv = ebgw->priv;
	gchar *status_msg;

	status = e_ews_connection_get_items (ebgw->priv->cnc, ebgw->priv->container_id, "name email default members", NULL, &ews_items);
	if (status != E_EWS_CONNECTION_STATUS_OK)
		return NULL;

	for (; ews_items != NULL; ews_items = g_list_next(ews_items)) {
		contact_num++;
		contact = e_contact_new ();
		fill_contact_from_ews_item (contact, E_EWS_ITEM (ews_items->data), ebgw->priv->categories_by_id);
		e_book_backend_cache_add_contact (ebgw->priv->cache, contact);
		if (book_view) {
			status_msg = g_strdup_printf (_("Downloading contacts (%d)... "),
							 contact_num);
			book_view_notify_status (book_view, status_msg);
			g_free (status_msg);
		}
		g_object_unref(contact);
		g_object_unref (ews_items->data);

	}

	e_book_backend_cache_set_populated (priv->cache);
	priv->is_cache_ready=TRUE;

	g_list_free (ews_items);

	return NULL;
}*/

/*FIXME using cursors for address book seems to be crashing server
till it gets fixed we will use get items. cursor implementation is below */

static gpointer
build_cache (EBookBackendEws *ebgw)
{
	gint status;
	GList *ews_items = NULL, *l;
	EContact *contact;
	gint cursor, contact_num = 0;
	gboolean done = FALSE;
	EBookBackendEwsPrivate *priv = ebgw->priv;
	const gchar *position = E_EWS_CURSOR_POSITION_START;
	EDataBookView *book_view;
	EwsBackendSearchClosure *closure;
	gchar *status_msg;
	GTimeVal start, end;
	GTimeVal tstart, tend;
	gulong diff;

	if (!ebgw)
		return NULL;

	if (enable_debug) {
		g_get_current_time(&start);
		printf("Building the cache for %s \n", ebgw->priv->book_name);
	}

	status = e_ews_connection_create_cursor (priv->cnc, priv->container_id,
						"default members", NULL, &cursor);
	if (status != E_EWS_CONNECTION_STATUS_OK) {
		if (enable_debug)
			printf("No connection with the server \n");
		return NULL;
	}

	book_view = find_book_view (ebgw);
	if (book_view) {
		closure = get_closure (book_view);
		e_data_book_view_ref (book_view);
		if (closure)
			e_flag_set (closure->running);
	}

	while (!done) {

		if (enable_debug)
			g_get_current_time(&tstart);
		status = e_ews_connection_read_cursor (priv->cnc, priv->container_id,
						      cursor, TRUE, CURSOR_ITEM_LIMIT,
						      position, &ews_items);
		if (enable_debug) {
			g_get_current_time(&tend);
			diff = tend.tv_sec * 1000 + tend.tv_usec/1000;
			diff -= tstart.tv_sec * 1000 + tstart.tv_usec/1000;
			printf("e_ews_connection_read_cursor took %ld.%03ld seconds for %d contacts\n", diff / 1000, diff % 1000, CURSOR_ITEM_LIMIT);
		}

		if (status != E_EWS_CONNECTION_STATUS_OK)
			 break;

		for (l = ews_items; l != NULL; l = g_list_next (l)) {
			contact_num++;

			contact = e_contact_new ();
			fill_contact_from_ews_item (contact, E_EWS_ITEM (l->data),
						   ebgw->priv->categories_by_id);
			e_contact_set (contact, E_CONTACT_BOOK_URI, priv->original_uri);
			e_book_backend_db_cache_add_contact (ebgw->priv->file_db, contact);
			e_book_backend_summary_add_contact (ebgw->priv->summary, contact);

			/* Since we get contacts incrementally, 100 at a time, we can not
			 * calculate the percentage of cache update.
			 * Also we should be using "percent" in notify_progress() instead of
			 * forming the message like this.
			 */
			if (book_view) {
				status_msg = g_strdup_printf (_("Downloading contacts (%d)... "),
								 contact_num);
				book_view_notify_status (book_view, status_msg);
				g_free (status_msg);
			}

			g_object_unref(contact);
			g_object_unref (l->data);

		}
		if (!ews_items) {
			e_book_backend_db_cache_set_populated (ebgw->priv->file_db);
			done = TRUE;
			priv->is_cache_ready=TRUE;
			priv->is_summary_ready = TRUE;
		}

		g_list_free (ews_items);
		ews_items = NULL;
		position = E_EWS_CURSOR_POSITION_CURRENT;
	}

	ebgw->priv->file_db->sync(ebgw->priv->file_db, 0);

	if (book_view) {
		e_data_book_view_notify_complete (book_view, NULL /* Success */);
		e_data_book_view_unref (book_view);
	}

	e_ews_connection_destroy_cursor (priv->cnc, priv->container_id, cursor);

	if (enable_debug) {
		g_get_current_time(&end);
		diff = end.tv_sec * 1000 + end.tv_usec/1000;
		diff -= start.tv_sec * 1000 + start.tv_usec/1000;
		printf("completed building cache for %s in %ld.%03ld seconds for %d contacts\n",
			priv->book_name, diff / 1000, diff % 1000, contact_num);
	}
	return NULL;
}

static void
build_summary (EBookBackendEws *ebgw)
{
	gchar *query_string;
	GList *contacts, *temp_list = NULL;
	GTimeVal start, end;
	gulong diff;

	if (enable_debug) {
		g_get_current_time(&start);
		printf ("summary file not found or not up-to-date, building summary for %s\n",
			ebgw->priv->book_name);
	}

	/* build summary from cache */
	query_string = g_strdup_printf ("(or (beginswith \"file_as\" \"\") "
					"    (beginswith \"full_name\" \"\") "
					"    (beginswith \"email\" \"\") "
					"    (beginswith \"nickname\" \"\"))");
	contacts = e_book_backend_db_cache_get_contacts (ebgw->priv->file_db, query_string);
	g_free (query_string);
	temp_list = contacts;
	for (; contacts != NULL; contacts = g_list_next(contacts)) {
		e_book_backend_summary_add_contact (ebgw->priv->summary, contacts->data);
		g_object_unref (contacts->data);
	}
	if (temp_list)
		g_list_free (temp_list);
	ebgw->priv->is_summary_ready = TRUE;

	if (enable_debug) {
		g_get_current_time(&end);
		diff = end.tv_sec * 1000 + end.tv_usec/1000;
		diff -= start.tv_sec * 1000 + start.tv_usec/1000;
		printf("building summary for %s took %ld.%03ld seconds \n",
			ebgw->priv->book_name, diff / 1000, diff % 1000);
	}
}

static gboolean
update_cache (EBookBackendEws *ebgw)
{
	gint status, contact_num = 0;
	GList *ews_items = NULL;
	EContact *contact;
	EEwsFilter *filter;
	time_t mod_time;
	gchar cache_time_string[100], *status_msg;
	const struct tm *tm;
	struct stat buf;
	gchar *cache_file_name;
	EDataBookView *book_view;
	EwsBackendSearchClosure *closure;
	GTimeVal start, end;
	gulong diff;

	if (!ebgw)
		return FALSE;

	g_mutex_lock (ebgw->priv->update_cache_mutex);

	if (enable_debug) {
		g_get_current_time(&start);
		printf("updating cache for %s\n", ebgw->priv->book_name);
	}

	book_view = find_book_view (ebgw);
	if (book_view) {
		closure = get_closure (book_view);
		e_data_book_view_ref (book_view);
		if (closure)
			e_flag_set (closure->running);
	}

	cache_file_name = e_book_backend_db_cache_get_filename(ebgw->priv->file_db);
	g_stat (cache_file_name, &buf);
	g_free (cache_file_name);
	mod_time = buf.st_mtime;
	tm = gmtime (&mod_time);
	strftime (cache_time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);

	filter = e_ews_filter_new ();
	e_ews_filter_add_filter_component (filter, E_EWS_FILTER_OP_GREATERTHAN,
					  "modified", cache_time_string);
	status = e_ews_connection_get_items (ebgw->priv->cnc, ebgw->priv->container_id,
					    "name email default members", filter, &ews_items);
	if (status != E_EWS_CONNECTION_STATUS_OK) {
		if (book_view)
			e_data_book_view_unref (book_view);
		if (enable_debug)
			printf("No connection with the server \n");
		g_mutex_unlock (ebgw->priv->update_cache_mutex);
		return FALSE;
	}

	for (; ews_items != NULL; ews_items = g_list_next(ews_items)) {
		const gchar *id;

		contact = e_contact_new ();
		fill_contact_from_ews_item (contact, E_EWS_ITEM (ews_items->data),
					   ebgw->priv->categories_by_id);

		e_contact_set (contact, E_CONTACT_BOOK_URI, ebgw->priv->original_uri);
		id =  e_contact_get_const (contact, E_CONTACT_UID);

		contact_num++;
		if (book_view) {
			status_msg = g_strdup_printf (_("Updating contacts cache (%d)... "),
							 contact_num);
			book_view_notify_status (book_view, status_msg);
			g_free (status_msg);
		}

		if (e_book_backend_db_cache_check_contact (ebgw->priv->file_db, id)) {
			e_book_backend_db_cache_add_contact (ebgw->priv->file_db, contact);
			e_book_backend_summary_remove_contact (ebgw->priv->summary, id);
			e_book_backend_summary_add_contact (ebgw->priv->summary, contact);
		} else {
			e_book_backend_db_cache_add_contact (ebgw->priv->file_db, contact);
			e_book_backend_summary_add_contact (ebgw->priv->summary, contact);
		}

		g_object_unref(contact);
		g_object_unref (ews_items->data);
	}
	ebgw->priv->is_cache_ready = TRUE;
	ebgw->priv->is_summary_ready = TRUE;

	ebgw->priv->file_db->sync(ebgw->priv->file_db, 0);

	if (book_view) {
		e_data_book_view_notify_complete (book_view, NULL /* Success */);
		e_data_book_view_unref (book_view);
	}
	g_object_unref (filter);
	g_list_free (ews_items);

	if (enable_debug) {
		g_get_current_time(&end);
		diff = end.tv_sec * 1000 + end.tv_usec/1000;
		diff -= start.tv_sec * 1000 + start.tv_usec/1000;
		printf("updating the cache for %s complated in %ld.%03ld seconds for %d contacts\n",
			ebgw->priv->book_name, diff / 1000, diff % 1000, contact_num);
	}
	g_mutex_unlock (ebgw->priv->update_cache_mutex);
	return FALSE;
}

static gboolean
update_address_book_deltas (EBookBackendEws *ebgw)
{
	gint status, contact_num = 0;
	gdouble server_first_sequence = -1, server_last_sequence = -1, server_last_po_rebuild_time = -1;
	gdouble cache_first_sequence = -1, cache_last_sequence = -1, cache_last_po_rebuild_time = -1;
	gchar *count, *sequence, *status_msg;
	gboolean sync_required = FALSE;
	GList *add_list = NULL, *delete_list = NULL;
	EContact *contact;
	EDataBookView *book_view;
	EwsBackendSearchClosure *closure;
	EEwsItem *item;
	EBookBackendEwsPrivate *priv;

	GTimeVal start, end;
	gulong diff;
	gchar *cache_file_name;
	struct stat buf;
	time_t mod_time;

	if (!ebgw)
		return FALSE;

	priv = ebgw->priv;

	g_mutex_lock (priv->update_mutex);

	if (enable_debug)
		printf("\nupdating GroupWise system address book cache \n");

	/* builds or updates the cache for system address book */
	status = e_ews_connection_get_items_delta_info (priv->cnc,
						       ebgw->priv->container_id,
						       &server_first_sequence,
						       &server_last_sequence,
						       &server_last_po_rebuild_time);
	if (status != E_EWS_CONNECTION_STATUS_OK) {
		if (enable_debug)
			printf("No connection with the server \n");
		g_mutex_unlock (priv->update_mutex);
		return FALSE;
	}

	/* Check whether the sequence has been reset or not */
	if (server_first_sequence <= 0 || server_last_sequence <= 0) {
		/* build the cache */
		if (enable_debug)
			printf ("sequence is reset, rebuilding cache...\n");
		build_cache (ebgw);
		add_sequence_to_cache (priv->file_db, server_first_sequence,
				       server_last_sequence, server_last_po_rebuild_time);
		ebgw->priv->file_db->sync (ebgw->priv->file_db, 0);
		g_mutex_unlock (priv->update_mutex);
		return TRUE;
	}

	/* Read the last sequence and last poa rebuild time from cache */
	get_sequence_from_cache(priv->file_db, &cache_first_sequence, &cache_last_sequence, &cache_last_po_rebuild_time);

	/* check whether the all the sequences are available and also whether the PO is rebuilt */
	if (server_first_sequence > cache_last_sequence || cache_last_sequence == -1 ||
	    server_last_po_rebuild_time != cache_last_po_rebuild_time) {
		/* build the cache again and update the cache with the sequence information */
		if (enable_debug)
			printf ("either the sequences missing or PO is rebuilt...rebuilding the cache\n");
		build_cache (ebgw);
		add_sequence_to_cache (priv->file_db, server_first_sequence,
				       server_last_sequence, server_last_po_rebuild_time);
		ebgw->priv->file_db->sync (ebgw->priv->file_db, 0);
		g_mutex_unlock (priv->update_mutex);
		return TRUE;
	}

	if (enable_debug)
		g_get_current_time(&start);

	book_view = find_book_view (ebgw);
	if (book_view) {
		closure = get_closure (book_view);
		e_data_book_view_ref (book_view);
		if (closure)
			e_flag_set (closure->running);
	}

	/* update the cache */
	sequence = g_strdup_printf ("%lf", cache_last_sequence +1);
	count = g_strdup_printf ("%d", CURSOR_ITEM_LIMIT);

	/* load summary file */
	cache_file_name = e_book_backend_db_cache_get_filename(ebgw->priv->file_db);
	g_stat (cache_file_name, &buf);
	g_free (cache_file_name);
	mod_time = buf.st_mtime;

	if (cache_last_sequence != server_last_sequence) {

			if (enable_debug) {
				printf("cache_last_sequence:%lf, server_last_sequence:%lf\n", cache_last_sequence, server_last_sequence);
				printf("Calling get_items_delta\n");
			}
			e_ews_connection_get_items_delta (priv->cnc,
							 ebgw->priv->container_id,
							 "name email sync", count,
							 sequence,
							 &add_list, &delete_list);

			if (add_list == NULL && delete_list == NULL) {
				if (enable_debug)
					printf("sequence differs but no changes found !!!\n");
				add_sequence_to_cache (priv->file_db, server_first_sequence,
				       server_last_sequence, server_last_po_rebuild_time);
				g_mutex_unlock (priv->update_mutex);
				g_free (sequence);
				g_free (count);
				return TRUE;
			}
			sync_required = TRUE;
			if (enable_debug) {
				printf("add_list size:%d\n", g_list_length(add_list));
				printf("delete_list size:%d\n", g_list_length(delete_list));
			}

			for (; delete_list != NULL; delete_list = g_list_next(delete_list)) {
				const gchar *id;

				/* deleted from the server */
				contact = e_contact_new ();
				fill_contact_from_ews_item (contact,
							   E_EWS_ITEM (delete_list->data),
							   ebgw->priv->categories_by_id);
				if (enable_debug)
					printf("contact email:%s, contact name:%s\n", (gchar *) e_contact_get(contact, E_CONTACT_EMAIL_1), (gchar *) e_contact_get(contact, E_CONTACT_GIVEN_NAME));
				e_contact_set (contact,
					       E_CONTACT_BOOK_URI,
					       priv->original_uri);
				id =  e_contact_get_const (contact, E_CONTACT_UID);

				if (e_book_backend_db_cache_check_contact (ebgw->priv->file_db, id)) {
					contact_num++;

					if (book_view) {
						status_msg = g_strdup_printf (_("Updating contacts cache (%d)... "),
										 contact_num);
						book_view_notify_status (book_view, status_msg);
						g_free (status_msg);
					}
					e_book_backend_db_cache_remove_contact (ebgw->priv->file_db, id);
					e_book_backend_summary_remove_contact (ebgw->priv->summary, id);
				}
				g_object_unref(contact);
				g_object_unref (delete_list->data);
			}

			for (; add_list != NULL; add_list = g_list_next(add_list)) {
				const gchar *id;

				/* newly added to server */
				contact = e_contact_new ();
				fill_contact_from_ews_item (contact,
							   E_EWS_ITEM (add_list->data),
							   ebgw->priv->categories_by_id);

				/* When a distribution list is modified the server sends me a delete and add response.
				But it doesnt send me the members, so i have to explicitly request the server for the members				     of the distribution list */

				if (e_contact_get (contact, E_CONTACT_IS_LIST)) {
					if (enable_debug)
						printf ("Contact List modified fetching the members of the contact list\n");

					status = e_ews_connection_get_item (ebgw->priv->cnc, ebgw->priv->container_id, e_contact_get (contact, E_CONTACT_UID), "name email default members", &item);
					g_object_unref (contact);
					contact = e_contact_new ();
					fill_contact_from_ews_item (contact, item, ebgw->priv->categories_by_id);
					g_object_unref (item);
				}

				if (enable_debug)
					printf("contact email:%s, contact name:%s\n", (gchar *)e_contact_get(contact, E_CONTACT_EMAIL_1),(gchar *) e_contact_get(contact, E_CONTACT_GIVEN_NAME));
				e_contact_set (contact,
					       E_CONTACT_BOOK_URI,
					       priv->original_uri);
				id =  e_contact_get_const (contact, E_CONTACT_UID);

				contact_num++;
				if (book_view) {
					status_msg = g_strdup_printf (_("Updating contacts cache (%d)... "),
									 contact_num);
					book_view_notify_status (book_view, status_msg);
					g_free (status_msg);
				}
				if (e_book_backend_db_cache_check_contact (ebgw->priv->file_db, id)) {
					if (enable_debug)
						printf("contact already there\n");
					e_book_backend_summary_remove_contact (ebgw->priv->summary, id);
					e_book_backend_db_cache_add_contact (ebgw->priv->file_db, contact);
					e_book_backend_summary_add_contact (ebgw->priv->summary, contact);
				} else {
					if (enable_debug)
						printf("contact not there\n");
					e_book_backend_db_cache_add_contact (ebgw->priv->file_db, contact);
					e_book_backend_summary_add_contact (ebgw->priv->summary, contact);
				}

				g_object_unref(contact);
				g_object_unref (add_list->data);
			}
			cache_last_sequence += contact_num;

		/* cache is updated, now adding the sequence information to the cache */

		add_sequence_to_cache (priv->file_db, server_first_sequence,
				       server_last_sequence, server_last_po_rebuild_time);

		g_list_free (add_list);
		g_list_free (delete_list);
	}

	g_free (sequence);
	g_free (count);
	ebgw->priv->is_cache_ready = TRUE;
	ebgw->priv->is_summary_ready = TRUE;

	if (sync_required)
		ebgw->priv->file_db->sync(ebgw->priv->file_db, 0);

	if (book_view) {
		e_data_book_view_notify_complete (book_view, NULL /* Success */);
		e_data_book_view_unref (book_view);
	}

	if (enable_debug) {
		g_get_current_time(&end);
		diff = end.tv_sec * 1000 + end.tv_usec/1000;
		diff -= start.tv_sec * 1000 + start.tv_usec/1000;
		printf("updating GroupWise system address book cache took %ld.%03ld seconds for %d changes\n",
			diff / 1000, diff % 1000, contact_num);
	}
	g_mutex_unlock(priv->update_mutex);

	return TRUE;
}

static gpointer
address_book_deltas_thread (gpointer data)
{
        EBookBackendEws *ebgw = data;
        EBookBackendEwsPrivate *priv = ebgw->priv;
        GTimeVal timeout;

        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        while (TRUE)    {
                gboolean succeeded = update_address_book_deltas (ebgw);

                g_mutex_lock (priv->dlock->mutex);

                if (!succeeded || priv->dlock->exit)
                        break;

                g_get_current_time (&timeout);
                g_time_val_add (&timeout, CACHE_REFRESH_INTERVAL * 1000);
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
fetch_address_book_deltas (EBookBackendEws *ebgw)
{
        EBookBackendEwsPrivate *priv = ebgw->priv;
        GError *error = NULL;

        /* If the thread is already running just return back */
        if (priv->dthread)
                return FALSE;

        priv->dlock->exit = FALSE;
        priv->dthread = g_thread_create ((GThreadFunc) address_book_deltas_thread, ebgw, TRUE, &error);
        if (!priv->dthread) {
                g_warning (G_STRLOC ": %s", error->message);
                g_error_free (error);
        }

        return TRUE;
}

static gboolean
update_address_book_cache (gpointer data)
{
	EBookBackendEws *ebgw = data;

	fetch_address_book_deltas (ebgw);

        ebgw->priv->cache_timeout = 0;
        return FALSE;
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
	EEwsConnectionErrors error;
	gchar *id, *tmpfile;
	gint status;
	gchar *http_uri;
	gboolean is_writable;
	const gchar *cache_refresh_interval_set;
	gint cache_refresh_interval = CACHE_REFRESH_INTERVAL;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;

	if (enable_debug) {
		printf ("authenticate user ............\n");
		if (priv->book_name)
			printf("book_name:%s\n", priv->book_name);
	}

	switch (ebgw->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL:
		/* load summary file for offline use */
		g_mkdir_with_parents (g_path_get_dirname (priv->summary_file_name), 0700);
		priv->summary = e_book_backend_summary_new (priv->summary_file_name,
						    SUMMARY_FLUSH_TIMEOUT);
		e_book_backend_summary_load (priv->summary);

		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		return;

	case E_DATA_BOOK_MODE_REMOTE:

		if (priv->cnc) { /*we have already authenticated to server */
			printf("already authenticated\n");
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
			return;
		}

		priv->cnc = e_ews_connection_new_with_error_handler (priv->uri, user, passwd, &error);
		if (!E_IS_GW_CONNECTION(priv->cnc) && priv->use_ssl && g_str_equal (priv->use_ssl, "when-possible")) {
			http_uri = g_strconcat ("http://", priv->uri + 8, NULL);
			priv->cnc = e_ews_connection_new (http_uri, user, passwd);
			g_free (http_uri);
		}

		if (!E_IS_GW_CONNECTION(priv->cnc)) {

			if (error.status == E_EWS_CONNECTION_STATUS_INVALID_PASSWORD)
				e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (AUTHENTICATION_FAILED));
			else
				e_data_book_respond_authenticate_user (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, error.status));
			return;
		}

		id = NULL;
		is_writable = FALSE;
		status = e_ews_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable);
		if (status == E_EWS_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_ews_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable);
		if (status == E_EWS_CONNECTION_STATUS_OK) {
			if ((id == NULL) && !priv->only_if_exists) {
				status = e_ews_connection_create_book (priv->cnc, priv->book_name,  &id);
				is_writable = TRUE;
				if (status != E_EWS_CONNECTION_STATUS_OK ) {
					e_data_book_respond_authenticate_user (book, opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status));
					return;
				}
			}
		}
		if (id != NULL) {
			priv->container_id = g_strdup (id);
			g_free(id);
			e_book_backend_set_is_writable (backend, is_writable);
			e_book_backend_notify_writable (backend, is_writable);
			e_book_backend_notify_connection_status (backend, TRUE);
			priv->is_writable = is_writable;
			e_ews_connection_get_categories (priv->cnc, &priv->categories_by_id, &priv->categories_by_name);
			if (!e_ews_connection_get_version(priv->cnc))
				e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (INVALID_SERVER_VERSION));
			else
				e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		} else {
			e_book_backend_set_is_loaded (backend, FALSE);
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (NO_SUCH_BOOK));
		}

		/* initialize summary file */
		tmpfile = g_path_get_dirname (priv->summary_file_name);
		g_mkdir_with_parents (tmpfile, 0700);
		g_free (tmpfile);
		priv->summary = e_book_backend_summary_new (priv->summary_file_name,
							    SUMMARY_FLUSH_TIMEOUT);

		if (!ebgw->priv->file_db) {
				e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (OTHER_ERROR));
				return;
		}
		if (e_book_backend_db_cache_is_populated (ebgw->priv->file_db)) {
			if (enable_debug)
				printf("cache is populated\n");

			if (!e_book_backend_summary_load (priv->summary))
				build_summary (ebgw);

			ebgw->priv->is_cache_ready = TRUE;
			ebgw->priv->is_summary_ready = TRUE;

			if (priv->is_writable) {
				if (enable_debug) {
					printf("is writable\n");
					printf("creating update_cache thread\n");
				}
				g_thread_create ((GThreadFunc) update_cache, ebgw, FALSE, NULL);
			}
			else if (priv->marked_for_offline) {
				GThread *t;
				if (enable_debug)
					printf("marked for offline\n");
				if (enable_debug)
					printf("creating update_address_book_deltas thread\n");

				t = g_thread_create ((GThreadFunc) update_address_book_deltas, ebgw, TRUE, NULL);

				/* spawn a thread to update the system address book cache
				 * at given intervals
				 */
				cache_refresh_interval_set = g_getenv ("BOOK_CACHE_REFRESH_INTERVAL");
				if (cache_refresh_interval_set) {
					cache_refresh_interval = g_ascii_strtod (cache_refresh_interval_set,
										NULL); /* use this */
					cache_refresh_interval *= (60*1000);
				}

				/* set the cache refresh time */
				g_thread_join (t);
				if (enable_debug)
					printf ("creating cache refresh thread for GW system book \n");
				priv->cache_timeout = g_timeout_add (cache_refresh_interval,
								     (GSourceFunc) update_address_book_cache,
								     (gpointer)ebgw);
			}
		}
		else if (priv->is_writable) {  /* for personal books we always cache */
			/* Personal address book and frequent contacts */
			if (enable_debug) {
				printf("else if is _writable");
				printf("build_cahe thread");
			}
			g_thread_create ((GThreadFunc) build_cache, ebgw, FALSE, NULL);
		}
		else if (priv->marked_for_offline) {
			GThread *t;
			if (enable_debug)
				printf("else if marked_for_offline\n");
			/* System address book */
			/* cache is not populated and book is not writable and marked for offline usage */
			if (enable_debug)
				printf("creating update_address_book_deltas thread\n");
			t = g_thread_create ((GThreadFunc) update_address_book_deltas, ebgw, TRUE, NULL);
			g_thread_join (t);
			/* set the cache refresh time */
			if (enable_debug)
				printf ("creating cache refresh thread for GW system book \n");
			priv->cache_timeout = g_timeout_add (cache_refresh_interval,
							     (GSourceFunc) update_address_book_cache,
							     (gpointer)ebgw);
		}
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

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_required_fields...\n");

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

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_supported_fields...\n");

	for (i = 0; i < G_N_ELEMENTS (mappings); i++)
		fields = g_list_append (fields, g_strdup (e_contact_field_name (mappings[i].field_id)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_ICQ)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_YAHOO)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_GADUGADU)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_MSN)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_SKYPE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_JABBER)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_GROUPWISE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (SUCCESS),
						  fields);
	g_list_free (fields);
}

static void
e_book_backend_ews_cancel_operation (EBookBackend *backend, EDataBook *book, GError **perror)
{
	if (enable_debug)
		printf ("\ne_book_backend_ews_cancel_operation...\n");
	g_propagate_error (perror, EDB_ERROR (COULD_NOT_CANCEL));
}

static void
#if DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 3
file_errcall (const DB_ENV *env, const gchar *buf1, const gchar *buf2)
#else
file_errcall (const gchar *buf1, gchar *buf2)
#endif
{
	g_warning ("libdb error: %s", buf2);
}

static void
e_book_backend_ews_load_source (EBookBackend           *backend,
				      ESource                *source,
				      gboolean                only_if_exists,
				      GError                **perror)
{
	EBookBackendEws *ebgw;
	EBookBackendEwsPrivate *priv;
	gchar *dirname, *filename, *tmp;
        gchar *book_name;
        gchar *uri;
	gchar **tokens;
	const gchar *port;
	gint db_error;
	DB *db;
	DB_ENV *env;
	EUri *parsed_uri;
	gint i;
	const gchar *use_ssl;
	const gchar *offline;

	if (enable_debug)
		printf("\ne_book_backend_ews_load_source.. \n");
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;
	g_object_ref (source);

	offline = e_source_get_property (source, "offline_sync");
	if (offline  && g_str_equal (offline, "1"))
		priv->marked_for_offline = TRUE;

	uri =  e_source_get_uri (source);
	priv->original_uri = g_strdup (uri);
	if (uri == NULL) {
		g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, "No URI given"));
		return;
	}

	tokens = g_strsplit (uri, ";", 2);
	g_free (uri);
	if (tokens[0])
		uri = g_strdup(tokens[0]);
	book_name = g_strdup (tokens[1]);
	if (book_name == NULL) {
		g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, "No book name recognized for a URI"));
		return;
	}
	g_strfreev (tokens);
	parsed_uri = e_uri_new (uri);
	port = e_source_get_property (source, "port");
	if (port == NULL)
		port = "7191";
	use_ssl = e_source_get_property (source, "use_ssl");
	if (use_ssl && !g_str_equal (use_ssl, "never"))
		priv->uri = g_strconcat ("https://", parsed_uri->host,":", port, "/soap", NULL );
	else
		priv->uri = g_strconcat ("http://", parsed_uri->host,":", port, "/soap", NULL );

	priv->book_name = book_name;

	for (i = 0; i < strlen (uri); i++) {
		switch (uri[i]) {
		case ':' :
		case '/' :
			uri[i] = '_';
		}
	}

	g_free (priv->summary_file_name);
	tmp = g_build_filename (
		e_get_user_data_dir (), "addressbook",
		uri, priv->book_name, NULL);
	priv->summary_file_name = g_strconcat (tmp, ".summary", NULL);
	g_free (tmp);

	dirname = g_build_filename (
		g_get_user_cache_dir (), "addressbook",
		uri, priv->book_name, NULL);
	filename = g_build_filename (dirname, "cache.db", NULL);

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error !=0) {
		g_warning ("db recovery failed with %d", db_error);
		g_free (dirname);
		g_free (filename);
		g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "DB recovery failed with code 0x%x", db_error));
		return;
	}

	g_static_mutex_lock(&global_env_lock);
	if (global_env.ref_count > 0) {
		env = global_env.env;
		global_env.ref_count++;
	}
	else {
		db_error = db_env_create (&env, 0);
		if (db_error != 0) {
			g_warning ("db_env_create failed with %d", db_error);
			g_static_mutex_unlock (&global_env_lock);
			g_free (dirname);
			g_free (filename);
			g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "db_env_create failed with code 0x%x", db_error));
			return;
		}

		db_error = env->open (env, NULL, DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_THREAD, 0);
		if (db_error != 0) {
			env->close(env, 0);
			g_warning ("db_env_open failed with %d", db_error);
			g_static_mutex_unlock(&global_env_lock);
			g_free(dirname);
			g_free(filename);
			g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "db_env_open failed with code 0x%x", db_error));
			return;
		}

		env->set_errcall (env, file_errcall);

		global_env.env = env;
		global_env.ref_count = 1;
	}
	g_static_mutex_unlock(&global_env_lock);

	ebgw->priv->env = env;

	db_error = db_create (&db, env, 0);
	if (db_error != 0) {
		g_warning ("db_create failed with %d", db_error);
		g_free(dirname);
		g_free(filename);
		g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "db_create failed with code 0x%x", db_error));
		return;
	}

	db_error = db->open (db, NULL, filename, NULL, DB_HASH, DB_THREAD, 0666);

	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0) {
			g_warning ("db format upgrade failed with %d", db_error);
			g_free(filename);
			g_free(dirname);
			g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "db format upgrade failed with code 0x%x", db_error));
			return;
		}

		db_error = db->open (db, NULL, filename, NULL, DB_HASH, DB_THREAD, 0666);
	}

	ebgw->priv->file_db = db;

	if (db_error != 0) {
		gint rv;

		/* the databade didn't exist, so we create the
		   directory then the .db */
		rv = g_mkdir_with_parents (dirname, 0700);
		if (rv == -1 && errno != EEXIST) {
			if (errno == EACCES || errno == EPERM)
				g_propagate_error (perror, EDB_ERROR (PERMISSION_DENIED));
			else
				g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "Failed to make directory %s: %s", dirname, g_strerror (errno)));
			g_free (dirname);
			g_free (filename);
			return;
		}

		db_error = db->open (db, NULL, filename, NULL, DB_HASH, DB_CREATE | DB_THREAD, 0666);
		if (db_error != 0) {
			g_warning ("db->open (...DB_CREATE...) failed with %d", db_error);
		}

	}

	ebgw->priv->file_db = db;

	if (db_error != 0 || ebgw->priv->file_db == NULL) {
		ebgw->priv->file_db = NULL;
		g_free(filename);
		g_free(dirname);
		g_propagate_error (perror, EDB_ERROR (OTHER_ERROR));
		return;
	}

	if (priv->mode ==  E_DATA_BOOK_MODE_LOCAL &&  !priv->marked_for_offline ) {
		g_propagate_error (perror, EDB_ERROR (OFFLINE_UNAVAILABLE));
		return;
	}

	priv->use_ssl = g_strdup (use_ssl);
	priv->only_if_exists = only_if_exists;

	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);
	if (priv->mode == E_DATA_BOOK_MODE_LOCAL) {
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
	}
	else {
		e_book_backend_notify_connection_status (backend, TRUE);
	}

	if (priv->mode == E_DATA_BOOK_MODE_LOCAL)
		if (!e_book_backend_db_cache_exists (priv->original_uri)) {
			g_free (uri);
			e_uri_free (parsed_uri);
			g_propagate_error (perror, EDB_ERROR (OFFLINE_UNAVAILABLE));
			return;
		}

	e_book_backend_db_cache_set_filename (ebgw->priv->file_db, filename);
	if (priv->marked_for_offline)
		ebgw->priv->is_cache_ready = TRUE;
	g_free(filename);
	g_free(dirname);
	g_free (uri);
	e_uri_free (parsed_uri);

	/*if (enable_debug) {
		printf ("summary file name = %s\ncache file name = %s \n",
			 priv->summary_file_name, e_file_cache_get_filename (E_FILE_CACHE(priv->cache)));
	}*/
}

static void
e_book_backend_ews_remove (EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
	EBookBackendEws *ebgw;
	gint status;

	if (enable_debug)
		printf ("\ne_book_backend_ews_remove...\n");
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	if (ebgw->priv->cnc == NULL) {
		e_data_book_respond_remove (book,  opid,  EDB_ERROR (AUTHENTICATION_REQUIRED));
		return;
	}
	if (!ebgw->priv->is_writable) {
		e_data_book_respond_remove (book,  opid,  EDB_ERROR (PERMISSION_DENIED));
		return;
	}
	status = e_ews_connection_remove_item (ebgw->priv->cnc, NULL, ebgw->priv->container_id);
	if (status == E_EWS_CONNECTION_STATUS_OK)
		e_data_book_respond_remove (book,  opid, EDB_ERROR (SUCCESS));
	else
		e_data_book_respond_remove (book,  opid, EDB_ERROR_FAILED_STATUS (OTHER_ERROR, status));
	g_unlink (e_book_backend_db_cache_get_filename(ebgw->priv->file_db));
}

static gchar *
e_book_backend_ews_get_static_capabilities (EBookBackend *backend)
{
	EBookBackendEws *ebgw;

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_static_capabilities...\n");

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);

	/* do-initialy-query is enabled for system address book also, so that we get the
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

	if (enable_debug)
		printf ("\ne_book_backend_ews_get_supported_auth_methods...\n");
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
	EBookBackendEws *bg;

	if (enable_debug)
		printf ("\ne_book_backend_ews_set_mode...\n");
	bg = E_BOOK_BACKEND_GROUPWISE (backend);
	bg->priv->mode = mode;
	if (e_book_backend_is_loaded (backend)) {
		if (mode == E_DATA_BOOK_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			if (bg->priv->cnc) {
				g_object_unref (bg->priv->cnc);
				bg->priv->cnc=NULL;
			}
		}
		else if (mode == E_DATA_BOOK_MODE_REMOTE) {
			if (bg->priv->is_writable)
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

	if (enable_debug)
		printf ("\ne_book_backend_ews_new...\n");

	backend = g_object_new (E_TYPE_BOOK_BACKEND_GROUPWISE, NULL);

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_ews_dispose (GObject *object)
{
	EBookBackendEws *bgw;

        EBookBackendEwsPrivate *priv;

	bgw = E_BOOK_BACKEND_GROUPWISE (object);
        priv = bgw->priv;

	if (enable_debug)
		printf ("\ne_book_backend_ews_dispose...\n");

	/* Clean up */

        if (priv->cache_timeout) {
                g_source_remove (priv->cache_timeout);
                priv->cache_timeout = 0;
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

	if (bgw->priv) {
		if (bgw->priv->file_db)
			bgw->priv->file_db->close (bgw->priv->file_db, 0);

		g_static_mutex_lock(&global_env_lock);
		global_env.ref_count--;
		if (global_env.ref_count == 0) {
			global_env.env->close (global_env.env, 0);
			global_env.env = NULL;
		}
		g_static_mutex_unlock(&global_env_lock);
		if (bgw->priv->uri) {
			g_free (bgw->priv->uri);
			bgw->priv->uri = NULL;
		}

		if (bgw->priv->original_uri) {
			g_free (bgw->priv->original_uri);
			bgw->priv->original_uri = NULL;
		}

		if (bgw->priv->cnc) {
			g_object_unref (bgw->priv->cnc);
			bgw->priv->cnc = NULL;
		}
		if (bgw->priv->container_id) {
			g_free (bgw->priv->container_id);
			bgw->priv->container_id = NULL;
		}
		if (bgw->priv->book_name) {
			g_free (bgw->priv->book_name);
			bgw->priv->book_name = NULL;
		}
		if (bgw->priv->summary_file_name) {
			g_free (bgw->priv->summary_file_name);
			bgw->priv->summary_file_name = NULL;
		}
		if (bgw->priv->summary) {
			e_book_backend_summary_save(bgw->priv->summary);
			g_object_unref (bgw->priv->summary);
			bgw->priv->summary = NULL;
		}
		if (bgw->priv->use_ssl) {
			g_free (bgw->priv->use_ssl);
		}
		if (bgw->priv->cache_timeout) {
			g_source_remove (bgw->priv->cache_timeout);
			bgw->priv->cache_timeout = 0;
		}
		if (bgw->priv->update_mutex)
			g_mutex_free (bgw->priv->update_mutex);
		if (bgw->priv->update_cache_mutex)
			g_mutex_free (bgw->priv->update_cache_mutex);

		g_free (bgw->priv);
		bgw->priv = NULL;
	}

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

	parent_class->create_contact          = e_book_backend_ews_create_contact;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ews_modify_contact;
	parent_class->get_contact             = e_book_backend_ews_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list;
	parent_class->start_book_view         = e_book_backend_ews_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ews_stop_book_view;
	parent_class->get_changes             = e_book_backend_ews_get_changes;
	parent_class->authenticate_user       = e_book_backend_ews_authenticate_user;
	parent_class->get_required_fields     = e_book_backend_ews_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ews_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ews_get_supported_auth_methods;
	parent_class->cancel_operation        = e_book_backend_ews_cancel_operation;
	parent_class->remove                  = e_book_backend_ews_remove;
	parent_class->set_mode                = e_book_backend_ews_set_mode;
	object_class->dispose                 = e_book_backend_ews_dispose;
}

static void
e_book_backend_ews_init (EBookBackendEws *backend)
{
	EBookBackendEwsPrivate *priv;

	priv= g_new0 (EBookBackendEwsPrivate, 1);
	priv->is_writable = TRUE;
	priv->is_cache_ready = FALSE;
	priv->is_summary_ready = FALSE;
	priv->marked_for_offline = FALSE;
	priv->use_ssl = NULL;
	priv->cnc = NULL;
	priv->original_uri = NULL;
	priv->cache_timeout = 0;
	priv->update_mutex = g_mutex_new();
	priv->update_cache_mutex = g_mutex_new();
	backend->priv = priv;

	if (!priv->dlock) {
                priv->dlock = g_new0 (SyncUpdate, 1);
                priv->dlock->mutex = g_mutex_new ();
                priv->dlock->cond = g_cond_new ();
        }

	if (g_getenv ("GROUPWISE_DEBUG")) {
		if (atoi (g_getenv ("GROUPWISE_DEBUG")) == 2)
			enable_debug = TRUE;
		else
			enable_debug = FALSE;
	}
}
