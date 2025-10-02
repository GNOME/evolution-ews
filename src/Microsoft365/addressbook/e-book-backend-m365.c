/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libedata-book/libedata-book.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-source-m365-folder.h"

#include "e-book-backend-m365.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define EC_ERROR_EX(_code,_msg) e_client_error_create (_code, _msg)
#define EBC_ERROR_EX(_code,_msg) e_book_client_error_create (_code, _msg)

#define LOCK(_bb) g_rec_mutex_lock (&_bb->priv->property_lock)
#define UNLOCK(_bb) g_rec_mutex_unlock (&_bb->priv->property_lock)

struct _EBookBackendM365Private {
	GRecMutex property_lock;
	EM365Connection *cnc;
	gchar *folder_id;
	GHashTable *view_cancellables; /* gpointer view ~> GCancellable; shares property_lock */
	EM365FolderKind folder_kind;
	gboolean cached_for_offline;
	guint max_people;
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendM365, e_book_backend_m365, E_TYPE_BOOK_META_BACKEND)

static void ebb_m365_check_source_properties (EBookBackendM365 *bbm365);

static void
ebb_m365_contact_get_string_attribute (EM365Contact *m365_contact,
				       EContact *inout_contact,
				       EContactField field_id,
				       const gchar * (*m365_get_func) (EM365Contact *contact))
{
	e_contact_set (inout_contact, field_id, m365_get_func (m365_contact));
}

static void
ebb_m365_contact_add_string_attribute (EContact *new_contact,
				       EContact *old_contact,
				       EContactField field_id,
				       JsonBuilder *builder,
				       void (* m365_add_func) (JsonBuilder *builder,
							       const gchar *value))
{
	const gchar *new_value, *old_value;

	g_return_if_fail (m365_add_func != NULL);

	new_value = e_contact_get_const (new_contact, field_id);
	old_value = old_contact ? e_contact_get_const (old_contact, field_id) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0)
		m365_add_func (builder, new_value);
}

static gboolean
ebb_m365_contact_get_rev (EBookBackendM365 *bbm365,
			  EM365Contact *m365_contact,
			  EContact *inout_contact,
			  EContactField field_id,
			  EM365Connection *cnc,
			  GCancellable *cancellable,
			  GError **error)
{
	gchar time_string[100] = { 0 };
	struct tm stm;
	time_t value;

	value = e_m365_contact_get_last_modified_date_time (m365_contact);

	if (value <= (time_t) 0)
		value = time (NULL);

	gmtime_r (&value, &stm);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", &stm);

	e_contact_set (inout_contact, field_id, time_string);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_birthday (EBookBackendM365 *bbm365,
			       EM365Contact *m365_contact,
			       EContact *inout_contact,
			       EContactField field_id,
			       EM365Connection *cnc,
			       GCancellable *cancellable,
			       GError **error)
{
	time_t value;
	gboolean exists = FALSE;

	value = e_m365_contact_get_birthday (m365_contact, &exists);

	if (exists) {
		EContactDate dt;
		struct tm stm;

		gmtime_r (&value, &stm);

		dt.year = stm.tm_year + 1900;
		dt.month = stm.tm_mon + 1;
		dt.day = stm.tm_mday;

		e_contact_set (inout_contact, field_id, &dt);
	}

	return TRUE;
}

static gboolean
ebb_m365_contact_add_birthday (EBookBackendM365 *bbm365,
			       EContact *new_contact,
			       EContact *old_contact,
			       EContactField field_id,
			       const gchar *m365_id,
			       JsonBuilder *builder,
			       GCancellable *cancellable,
			       GError **error)
{
	EContactDate *old_dt = NULL;
	EContactDate *new_dt = NULL;

	new_dt = e_contact_get (new_contact, field_id);
	old_dt = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!e_contact_date_equal (new_dt, old_dt)) {
		if (new_dt) {
			GDateTime *gdt;
			time_t value = (time_t) 0;

			gdt = g_date_time_new_local (new_dt->year, new_dt->month, new_dt->day, 11, 59, 0.0);

			if (gdt) {
				value = g_date_time_to_unix (gdt);
				value = value - (value % (24 * 60 * 60));
				value = value + (((12 * 60) - 1) * 60);

				g_date_time_unref (gdt);
			}

			e_m365_contact_add_birthday (builder, value);
		} else {
			e_m365_contact_add_birthday (builder, (time_t) 0);
		}
	}

	e_contact_date_free (new_dt);
	e_contact_date_free (old_dt);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_home_page (EBookBackendM365 *bbm365,
				EM365Contact *m365_contact,
				EContact *inout_contact,
				EContactField field_id,
				EM365Connection *cnc,
				GCancellable *cancellable,
				GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_get_business_home_page);
	else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_user_get_my_site);

	return TRUE;
}

static gboolean
ebb_m365_contact_add_home_page (EBookBackendM365 *bbm365,
				EContact *new_contact,
				EContact *old_contact,
				EContactField field_id,
				const gchar *m365_id,
				JsonBuilder *builder,
				GCancellable *cancellable,
				GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_add_string_attribute (new_contact, old_contact, field_id, builder, e_m365_contact_add_business_home_page);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_address (EBookBackendM365 *bbm365,
			      EM365Contact *m365_contact,
			      EContact *inout_contact,
			      EContactField field_id,
			      EM365Connection *cnc,
			      GCancellable *cancellable,
			      GError **error)
{
	EContactAddress addr;

	memset (&addr, 0, sizeof (EContactAddress));

	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS) {
		EM365PhysicalAddress *phys_address = NULL;

		if (field_id == E_CONTACT_ADDRESS_WORK)
			phys_address = e_m365_contact_get_business_address (m365_contact);
		else if (field_id == E_CONTACT_ADDRESS_HOME)
			phys_address = e_m365_contact_get_home_address (m365_contact);
		else if (field_id == E_CONTACT_ADDRESS_OTHER)
			phys_address = e_m365_contact_get_other_address (m365_contact);
		else
			g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));

		if (phys_address) {
			addr.locality = (gchar *) e_m365_physical_address_get_city (phys_address);
			addr.country = (gchar *) e_m365_physical_address_get_country_or_region (phys_address);
			addr.code = (gchar *) e_m365_physical_address_get_postal_code (phys_address);
			addr.region = (gchar *) e_m365_physical_address_get_state (phys_address);
			addr.street = (gchar *) e_m365_physical_address_get_street (phys_address);
		}
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS && field_id == E_CONTACT_ADDRESS_WORK) {
		addr.locality = (gchar *) e_m365_contact_user_get_city (m365_contact);
		addr.country = (gchar *) e_m365_contact_user_get_country (m365_contact);
		addr.code = (gchar *) e_m365_contact_user_get_postal_code (m365_contact);
		addr.region = (gchar *) e_m365_contact_user_get_state (m365_contact);
		addr.street = (gchar *) e_m365_contact_user_get_street_address (m365_contact);
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS && field_id == E_CONTACT_ADDRESS_WORK) {
		JsonArray *values = e_m365_contact_org_get_addresses (m365_contact);

		if (values) {
			guint ii, len;

			len = json_array_get_length (values);

			for (ii = 0; ii < len; ii++) {
				EM365PhysicalAddress *phys_address = json_array_get_object_element (values, ii);

				if (phys_address) {
					addr.locality = (gchar *) e_m365_physical_address_get_city (phys_address);
					addr.country = (gchar *) e_m365_physical_address_get_country_or_region (phys_address);
					addr.code = (gchar *) e_m365_physical_address_get_postal_code (phys_address);
					addr.region = (gchar *) e_m365_physical_address_get_state (phys_address);
					addr.street = (gchar *) e_m365_physical_address_get_street (phys_address);
					/* only one supported */
					break;
				}
			}
		}
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE) {
		JsonArray *values = e_m365_contact_person_get_postal_addresses (m365_contact);

		if (values) {
			guint ii, len;

			len = json_array_get_length (values);

			for (ii = 0; ii < len; ii++) {
				EM365Location *location = json_array_get_object_element (values, ii);
				EM365PhysicalAddress *phys_address;

				if (!location)
					continue;

				phys_address = e_m365_location_get_address (location);
				if (!phys_address)
					continue;

				if (field_id == E_CONTACT_ADDRESS_WORK) {
					if (e_m365_location_get_type (location) != E_M365_LOCATION_BUSINESS_ADDRESS)
						continue;
				} else if (field_id == E_CONTACT_ADDRESS_HOME) {
					if (e_m365_location_get_type (location) != E_M365_LOCATION_HOME_ADDRESS)
						continue;
				} else if (field_id == E_CONTACT_ADDRESS_OTHER) {
					if (e_m365_location_get_type (location) != E_M365_LOCATION_STREET_ADDRESS &&
					    e_m365_location_get_type (location) != E_M365_LOCATION_POSTAL_ADDRESS)
						continue;
				}

				addr.locality = (gchar *) e_m365_physical_address_get_city (phys_address);
				addr.country = (gchar *) e_m365_physical_address_get_country_or_region (phys_address);
				addr.code = (gchar *) e_m365_physical_address_get_postal_code (phys_address);
				addr.region = (gchar *) e_m365_physical_address_get_state (phys_address);
				addr.street = (gchar *) e_m365_physical_address_get_street (phys_address);
				/* only one supported */
				break;
			}
		}
	}

	if (addr.locality || addr.country || addr.code || addr.region || addr.street)
		e_contact_set (inout_contact, field_id, &addr);
	else
		e_contact_set (inout_contact, field_id, NULL);

	return TRUE;
}

static gboolean
ebb_m365_contact_address_equal (const EContactAddress *addr1,
				const EContactAddress *addr2)
{
	if (!addr1 && !addr2)
		return TRUE;

	if ((addr1 && !addr2) || (!addr1 && addr2))
		return FALSE;

	return /* g_strcmp0 (addr1->address_format, addr2->address_format) == 0 && */
		g_strcmp0 (addr1->po, addr2->po) == 0 &&
		g_strcmp0 (addr1->ext, addr2->ext) == 0 &&
		g_strcmp0 (addr1->street, addr2->street) == 0 &&
		g_strcmp0 (addr1->locality, addr2->locality) == 0 &&
		g_strcmp0 (addr1->region, addr2->region) == 0 &&
		g_strcmp0 (addr1->code, addr2->code) == 0 &&
		g_strcmp0 (addr1->country, addr2->country) == 0;
}

static gboolean
ebb_m365_contact_add_address (EBookBackendM365 *bbm365,
			      EContact *new_contact,
			      EContact *old_contact,
			      EContactField field_id,
			      const gchar *m365_id,
			      JsonBuilder *builder,
			      GCancellable *cancellable,
			      GError **error)
{
	EContactAddress *new_addr, *old_addr;

	new_addr = e_contact_get (new_contact, field_id);
	old_addr = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_m365_contact_address_equal (new_addr, old_addr)) {
		void (* add_func) (JsonBuilder *builder,
				   const gchar *city,
				   const gchar *country_or_region,
				   const gchar *postal_code,
				   const gchar *state,
				   const gchar *street) = NULL;

		if (field_id == E_CONTACT_ADDRESS_WORK)
			add_func = e_m365_contact_add_business_address;
		else if (field_id == E_CONTACT_ADDRESS_HOME)
			add_func = e_m365_contact_add_home_address;
		else if (field_id == E_CONTACT_ADDRESS_OTHER)
			add_func = e_m365_contact_add_other_address;
		else
			g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));

		if (add_func) {
			if (new_addr) {
				add_func (builder, new_addr->locality, new_addr->country, new_addr->code, new_addr->region, new_addr->street);
			} else {
				add_func (builder, NULL, NULL, NULL, NULL, NULL);
			}
		}
	}

	e_contact_address_free (new_addr);
	e_contact_address_free (old_addr);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_nick_name (EBookBackendM365 *bbm365,
				EM365Contact *m365_contact,
				EContact *inout_contact,
				EContactField field_id,
				EM365Connection *cnc,
				GCancellable *cancellable,
				GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_get_nick_name);
	else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_org_get_mail_nickname);
	else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_user_get_mail_nickname);

	return TRUE;
}

static gboolean
ebb_m365_contact_add_nick_name (EBookBackendM365 *bbm365,
				EContact *new_contact,
				EContact *old_contact,
				EContactField field_id,
				const gchar *m365_id,
				JsonBuilder *builder,
				GCancellable *cancellable,
				GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_add_string_attribute (new_contact, old_contact, field_id, builder, e_m365_contact_add_nick_name);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_note (EBookBackendM365 *bbm365,
			   EM365Contact *m365_contact,
			   EContact *inout_contact,
			   EContactField field_id,
			   EM365Connection *cnc,
			   GCancellable *cancellable,
			   GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_get_personal_notes);
	else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS)
		ebb_m365_contact_get_string_attribute (m365_contact, inout_contact, field_id, e_m365_contact_user_get_about_me);

	return TRUE;
}

static gboolean
ebb_m365_contact_add_note (EBookBackendM365 *bbm365,
			   EContact *new_contact,
			   EContact *old_contact,
			   EContactField field_id,
			   const gchar *m365_id,
			   JsonBuilder *builder,
			   GCancellable *cancellable,
			   GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		ebb_m365_contact_add_string_attribute (new_contact, old_contact, field_id, builder, e_m365_contact_add_personal_notes);

	return TRUE;
}

static gboolean
ebb_m365_string_values_equal (GSList *new_values, /* const gchar * */
			      GSList *old_values) /* const gchar * */
{
	GHashTable *values;
	GSList *link;
	gboolean equal = TRUE;

	if (g_slist_length (new_values) != g_slist_length (old_values))
		return FALSE;

	values = g_hash_table_new (g_str_hash, g_str_equal);

	for (link = new_values; link; link = g_slist_next (link)) {
		gchar *value = link->data;

		if (value)
			g_hash_table_add (values, value);
	}

	for (link = old_values; link && equal; link = g_slist_next (link)) {
		const gchar *value = link->data;

		if (value)
			equal = g_hash_table_remove (values, value);
	}

	equal = equal && !g_hash_table_size (values);

	g_hash_table_destroy (values);

	return equal;
}

static gboolean
ebb_m365_string_list_values_equal (GList *new_values, /* const gchar * */
				   GList *old_values) /* const gchar * */
{
	GHashTable *values;
	GList *link;
	gboolean equal = TRUE;

	if (g_list_length (new_values) != g_list_length (old_values))
		return FALSE;

	values = g_hash_table_new (g_str_hash, g_str_equal);

	for (link = new_values; link; link = g_list_next (link)) {
		gchar *value = link->data;

		if (value)
			g_hash_table_add (values, value);
	}

	for (link = old_values; link && equal; link = g_list_next (link)) {
		const gchar *value = link->data;

		if (value)
			equal = g_hash_table_remove (values, value);
	}

	equal = equal && !g_hash_table_size (values);

	g_hash_table_destroy (values);

	return equal;
}

static gboolean
ebb_m365_contact_get_phone (EBookBackendM365 *bbm365,
			    EM365Contact *m365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EM365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS ||
	    (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS && field_id == E_CONTACT_PHONE_BUSINESS)) {
		JsonArray *values = NULL;
		const gchar *type_val = NULL;

		if (field_id == E_CONTACT_PHONE_BUSINESS) {
			values = e_m365_contact_get_business_phones (m365_contact);
			type_val = "WORK";
		} else if (field_id == E_CONTACT_PHONE_HOME) {
			values = e_m365_contact_get_home_phones (m365_contact);
			type_val = "HOME";
		} else {
			g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));
		}

		if (values) {
			EVCard *vcard = E_VCARD (inout_contact);
			guint ii, len;

			len = json_array_get_length (values);

			for (ii = 0; ii < len; ii++) {
				const gchar *str = json_array_get_string_element (values, len - ii - 1);

				if (str && *str) {
					EVCardAttributeParam *param;
					EVCardAttribute *attr;

					attr = e_vcard_attribute_new (NULL, EVC_TEL);
					param = e_vcard_attribute_param_new (EVC_TYPE);

					e_vcard_attribute_add_param_with_value (attr, param, type_val);
					e_vcard_add_attribute_with_value (vcard, attr, str);
				}
			}
		}
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS && field_id == E_CONTACT_PHONE_HOME) {
		e_contact_set (inout_contact, E_CONTACT_PHONE_HOME_FAX, e_m365_contact_user_get_fax_number (m365_contact));
		e_contact_set (inout_contact, E_CONTACT_PHONE_MOBILE, e_m365_contact_user_get_mobile_phone (m365_contact));
	} else if ((bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS || bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE) &&
		   field_id == E_CONTACT_PHONE_BUSINESS) {
		JsonArray *values;

		if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS)
			values = e_m365_contact_org_get_phones (m365_contact);
		else
			values = e_m365_contact_person_get_phones (m365_contact);

		if (values) {
			gboolean had_business = FALSE;
			gboolean had_home = FALSE;
			guint ii, len;

			len = json_array_get_length (values);

			for (ii = 0; ii < len; ii++) {
				EM365Phone *phone = json_array_get_object_element (values, len - ii - 1);
				const gchar *number = phone ? e_m365_phone_get_number (phone) : NULL;

				if (number && *number) {
					EContactField fld = E_CONTACT_FIELD_LAST;

					switch (e_m365_phone_get_type (phone)) {
					case E_M365_PHONE_HOME:
						fld = had_home ? E_CONTACT_PHONE_HOME_2 : E_CONTACT_PHONE_HOME;
						had_home = TRUE;
						break;
					case E_M365_PHONE_BUSINESS:
						fld = had_business ? E_CONTACT_PHONE_BUSINESS_2 : E_CONTACT_PHONE_BUSINESS;
						had_business = TRUE;
						break;
					case E_M365_PHONE_MOBILE:
						fld = E_CONTACT_PHONE_MOBILE;
						break;
					case E_M365_PHONE_OTHER:
						fld = E_CONTACT_PHONE_OTHER;
						break;
					case E_M365_PHONE_ASSISTANT:
						fld = E_CONTACT_PHONE_ASSISTANT;
						break;
					case E_M365_PHONE_HOMEFAX:
						fld = E_CONTACT_PHONE_HOME_FAX;
						break;
					case E_M365_PHONE_BUSINESSFAX:
						fld = E_CONTACT_PHONE_BUSINESS_FAX;
						break;
					case E_M365_PHONE_OTHERFAX:
						fld = E_CONTACT_PHONE_OTHER_FAX;
						break;
					case E_M365_PHONE_PAGER:
						fld = E_CONTACT_PHONE_PAGER;
						break;
					case E_M365_PHONE_RADIO:
						fld = E_CONTACT_PHONE_RADIO;
						break;
					case E_M365_PHONE_NOT_SET:
					case E_M365_PHONE_UNKNOWN:
						break;
					}

					if (fld != E_CONTACT_FIELD_LAST)
						e_contact_set (inout_contact, fld, number);
				}
			}
		}
	}

	return TRUE;
}

static GSList * /* gchar * */
ebb_m365_extract_phones (EContact *contact,
			 const gchar *only_type) /* NULL for anything but known types */
{
	GSList *phones = NULL;
	GList *attrs, *link;

	if (!contact)
		return NULL;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (link = attrs; link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;
		gboolean use_it = FALSE;

		if (!attr || !e_vcard_attribute_get_name (attr) ||
		    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_TEL) != 0)
			continue;

		if (only_type) {
			use_it = e_vcard_attribute_has_type (attr, only_type);
		} else {
			use_it = !e_vcard_attribute_has_type (attr, "WORK") &&
				 !e_vcard_attribute_has_type (attr, "CELL");
		}

		if (use_it)
			phones = g_slist_prepend (phones, e_vcard_attribute_get_value (attr));
	}

	return g_slist_reverse (phones);
}

static gboolean
ebb_m365_contact_add_phone (EBookBackendM365 *bbm365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *m365_id,
			    JsonBuilder *builder,
			    GCancellable *cancellable,
			    GError **error)
{
	void (* begin_func) (JsonBuilder *builder) = NULL;
	void (* end_func) (JsonBuilder *builder) = NULL;
	void (* add_func) (JsonBuilder *builder, const gchar *value) = NULL;
	const gchar *type_val = NULL;
	GSList *new_values, *old_values;

	if (field_id == E_CONTACT_PHONE_BUSINESS) {
		begin_func = e_m365_contact_begin_business_phones;
		end_func = e_m365_contact_end_business_phones;
		add_func = e_m365_contact_add_business_phone;
		type_val = "WORK";
	} else if (field_id == E_CONTACT_PHONE_HOME) {
		begin_func = e_m365_contact_begin_home_phones;
		end_func = e_m365_contact_end_home_phones;
		add_func = e_m365_contact_add_home_phone;
		type_val = NULL; /* everything else is treated as "HOME" phone */
	} else {
		g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));
	}

	new_values = ebb_m365_extract_phones (new_contact, type_val);
	old_values = ebb_m365_extract_phones (old_contact, type_val);

	if (!ebb_m365_string_values_equal (new_values, old_values)) {
		GSList *link;

		begin_func (builder);

		for (link = new_values; link; link = g_slist_next (link)) {
			const gchar *value = link->data;

			add_func (builder, value);
		}

		end_func (builder);
	}

	g_slist_free_full (new_values, g_free);
	g_slist_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_categories (EBookBackendM365 *bbm365,
				 EM365Contact *m365_contact,
				 EContact *inout_contact,
				 EContactField field_id,
				 EM365Connection *cnc,
				 GCancellable *cancellable,
				 GError **error)
{
	JsonArray *values;

	values = e_m365_contact_get_categories (m365_contact);

	if (values) {
		GString *categories_str = NULL;
		guint ii, len;

		len = json_array_get_length (values);

		for (ii = 0; ii < len; ii++) {
			const gchar *str = json_array_get_string_element (values, ii);

			if (str && *str) {
				if (!categories_str) {
					categories_str = g_string_new (str);
				} else {
					g_string_append_c (categories_str,  ',');
					g_string_append (categories_str, str);
				}
			}
		}

		if (categories_str) {
			e_contact_set (inout_contact, field_id, categories_str->str);
			g_string_free (categories_str, TRUE);
		}
	}

	return TRUE;
}

static GSList *
ebb_m365_extract_categories (EContact *contact,
			     EContactField field_id)
{
	GSList *categories = NULL;
	const gchar *str;

	if (!contact)
		return NULL;

	str = e_contact_get_const (contact, field_id);

	if (str && *str) {
		gchar **split_str;
		gint ii;

		split_str = g_strsplit (str, ",", -1);

		for (ii = 0; split_str && split_str[ii]; ii++) {
			gchar *item = split_str[ii];

			if (item && *item)
				categories = g_slist_prepend (categories, item);
			else
				g_free (item);

			split_str[ii] = NULL;
		}

		g_free (split_str);
	}

	return g_slist_reverse (categories);
}

static gboolean
ebb_m365_contact_add_categories (EBookBackendM365 *bbm365,
				 EContact *new_contact,
				 EContact *old_contact,
				 EContactField field_id,
				 const gchar *m365_id,
				 JsonBuilder *builder,
				 GCancellable *cancellable,
				 GError **error)
{
	GSList *new_values, *old_values;

	new_values = ebb_m365_extract_categories (new_contact, field_id);
	old_values = ebb_m365_extract_categories (old_contact, field_id);

	if (!ebb_m365_string_values_equal (new_values, old_values)) {
		GSList *link;

		e_m365_contact_begin_categories (builder);

		for (link = new_values; link; link = g_slist_next (link)) {
			const gchar *value = link->data;

			e_m365_contact_add_category (builder, value);
		}

		e_m365_contact_end_categories (builder);
	}

	g_slist_free_full (new_values, g_free);
	g_slist_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_emails (EBookBackendM365 *bbm365,
			     EM365Contact *m365_contact,
			     EContact *inout_contact,
			     EContactField field_id,
			     EM365Connection *cnc,
			     GCancellable *cancellable,
			     GError **error)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS) {
		JsonArray *values;

		values = e_m365_contact_get_email_addresses (m365_contact);

		if (values) {
			EVCard *vcard = E_VCARD (inout_contact);
			guint ii, len;

			len = json_array_get_length (values);

			for (ii = 0; ii < len; ii++) {
				EM365EmailAddress *address = json_array_get_object_element (values, len - ii - 1);

				if (address) {
					EVCardAttribute *attr;

					attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "OTHER");

					/* ignore the address name, it's not meant to be there, but the server sometimes sets it and sends it back */
					e_vcard_add_attribute_with_value (vcard, attr, e_m365_email_address_get_address (address));
				}
			}
		}
	} else {
		GHashTable *known_mails = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
		EVCard *vcard = E_VCARD (inout_contact);
		EVCardAttribute *attr;
		JsonArray *proxy_addresses = NULL;
		JsonArray *scored_addresses = NULL;
		JsonArray *other_mails = NULL;
		const gchar *mail = NULL;
		guint ii, len;

		if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS) {
			proxy_addresses = e_m365_contact_org_get_proxy_addresses (m365_contact);
			mail = e_m365_contact_org_get_mail (m365_contact);
		} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS) {
			proxy_addresses = e_m365_contact_user_get_proxy_addresses (m365_contact);
			other_mails = e_m365_contact_user_get_other_mails (m365_contact);
			mail = e_m365_contact_user_get_mail (m365_contact);
		} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE) {
			scored_addresses = e_m365_contact_person_get_scored_email_addresses (m365_contact);
		}

		/* add it to the vCard as the last, because "add_attribute" prepends it */
		if (mail && *mail)
			g_hash_table_add (known_mails, (gpointer) mail);

		len = proxy_addresses ? json_array_get_length (proxy_addresses) :
		      (scored_addresses ? json_array_get_length (scored_addresses) : 0);

		for (ii = 0; ii < len; ii++) {
			const gchar *address = NULL;

			if (proxy_addresses) {
				address = json_array_get_string_element (proxy_addresses, len - ii - 1);
			} else if (scored_addresses) {
				EM365ScoredEmailAddress *scored_address;

				scored_address = json_array_get_object_element (scored_addresses, len - ii - 1);
				if (scored_address)
					address = e_m365_scored_email_address_get_address (scored_address);
			}

			if (address && *address) {
				/* accept only SMTP: addresses and skip any other */
				if (g_ascii_strncasecmp (address, "smtp:", 5) == 0)
					address += 5;
				else if (!scored_addresses)
					continue;

				if (g_hash_table_add (known_mails, (gpointer) address)) {
					attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "OTHER");
					e_vcard_add_attribute_with_value (vcard, attr, address);
				}
			}
		}

		len = other_mails ? json_array_get_length (other_mails) : 0;

		for (ii = 0; ii < len; ii++) {
			const gchar *address = json_array_get_string_element (other_mails, len - ii - 1);

			if (address && *address &&
			    g_hash_table_add (known_mails, (gpointer) address)) {
				attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
				e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "OTHER");
				e_vcard_add_attribute_with_value (vcard, attr, address);
			}
		}

		if (mail && *mail) {
			attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
			e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "OTHER");
			e_vcard_add_attribute_with_value (vcard, attr, mail);
		}

		g_hash_table_unref (known_mails);
	}

	return TRUE;
}

static gboolean
ebb_m365_parse_qp_email (const gchar *string,
			 gchar **email)
{
	struct _camel_header_address *address;
	gboolean res = FALSE;

	address = camel_header_address_decode (string, "UTF-8");

	if (address) {
		/* report success only when we have filled both name and email address */
		if (address->type == CAMEL_HEADER_ADDRESS_NAME && address->name && *address->name && address->v.addr && *address->v.addr) {
			*email = g_strdup (address->v.addr);
			res = TRUE;
		}

		camel_header_address_unref (address);
	}

	if (!res) {
		CamelInternetAddress *addr = camel_internet_address_new ();
		const gchar *const_name = NULL, *const_email = NULL;

		if (camel_address_unformat (CAMEL_ADDRESS (addr), string) == 1 &&
		    camel_internet_address_get (addr, 0, &const_name, &const_email) &&
		    const_name && *const_name && const_email && *const_email) {
			*email = g_strdup (const_email);
			res = TRUE;
		}

		g_clear_object (&addr);
	}

	return res;
}

static gboolean
ebb_m365_contact_add_emails (EBookBackendM365 *bbm365,
			     EContact *new_contact,
			     EContact *old_contact,
			     EContactField field_id,
			     const gchar *m365_id,
			     JsonBuilder *builder,
			     GCancellable *cancellable,
			     GError **error)
{
	GList *new_values, *old_values;

	new_values = e_contact_get (new_contact, field_id);
	old_values = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_m365_string_list_values_equal (new_values, old_values)) {
		GList *link;

		e_m365_contact_begin_email_addresses (builder);

		for (link = new_values; link; link = g_list_next (link)) {
			const gchar *value = link->data;
			gchar *address = NULL;

			if (ebb_m365_parse_qp_email (value, &address))
				e_m365_add_email_address (builder, NULL, NULL, address);
			else
				e_m365_add_email_address (builder, NULL, NULL, value);

			g_free (address);
		}

		e_m365_contact_end_email_addresses (builder);
	}

	g_list_free_full (new_values, g_free);
	g_list_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_m365_contact_add_file_as (EBookBackendM365 *bbm365,
			      EContact *new_contact,
			      EContact *old_contact,
			      EContactField field_id,
			      const gchar *m365_id,
			      JsonBuilder *builder,
			      GCancellable *cancellable,
			      GError **error)
{
	const gchar *new_value;

	ebb_m365_contact_add_string_attribute (new_contact, old_contact, field_id, builder, e_m365_contact_add_file_as);

	new_value = e_contact_get_const (new_contact, E_CONTACT_FILE_AS);

	/* Set it always, to not be overwritten by server re-calculations on other property changes */
	e_m365_contact_add_display_name (builder, new_value);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_generation (EBookBackendM365 *bbm365,
				 EM365Contact *m365_contact,
				 EContact *inout_contact,
				 EContactField field_id,
				 EM365Connection *cnc,
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *value;

	value = e_m365_contact_get_generation (m365_contact);

	if (value && *value) {
		EContactName *name = e_contact_get (inout_contact, field_id);
		gchar *prev;

		if (!name)
			name = e_contact_name_new ();

		prev = name->suffixes;
		name->suffixes = (gchar *) value;

		e_contact_set (inout_contact, field_id, name);

		name->suffixes = prev;
		e_contact_name_free (name);
	}

	return TRUE;
}

static gboolean
ebb_m365_contact_add_generation (EBookBackendM365 *bbm365,
				 EContact *new_contact,
				 EContact *old_contact,
				 EContactField field_id,
				 const gchar *m365_id,
				 JsonBuilder *builder,
				 GCancellable *cancellable,
				 GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->suffixes, old_value->suffixes) == 0))
		e_m365_contact_add_generation (builder, new_value ? new_value->suffixes : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_im_addresses (EBookBackendM365 *bbm365,
				   EM365Contact *m365_contact,
				   EContact *inout_contact,
				   EContactField field_id,
				   EM365Connection *cnc,
				   GCancellable *cancellable,
				   GError **error)
{
	JsonArray *values;

	values = e_m365_contact_get_im_addresses (m365_contact);

	if (values) {
		EVCard *vcard = E_VCARD (inout_contact);
		const gchar *field_name = e_contact_vcard_attribute (field_id);
		guint ii, len;

		len = json_array_get_length (values);

		for (ii = 0; ii < len; ii++) {
			const gchar *str = json_array_get_string_element (values, len - ii - 1);

			if (str && *str) {
				EVCardAttribute *attr;

				attr = e_vcard_attribute_new (NULL, field_name);

				e_vcard_add_attribute_with_value (vcard, attr, str);
			}
		}
	}

	return TRUE;
}

static GSList * /* gchar * */
ebb_m365_extract_im_addresses (EContact *contact)
{
	GSList *ims = NULL;
	GList *attrs, *link;

	if (!contact)
		return NULL;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (link = attrs; link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;
		const gchar *name;

		if (!attr)
			continue;

		name = e_vcard_attribute_get_name (attr);

		if (!name || (
		    g_ascii_strcasecmp (name, EVC_X_GOOGLE_TALK) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_SKYPE) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_GADUGADU) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_AIM) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_GROUPWISE) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_JABBER) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_YAHOO) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_MSN) != 0 &&
		    g_ascii_strcasecmp (name, EVC_X_ICQ) != 0))
			continue;

		ims = g_slist_prepend (ims, e_vcard_attribute_get_value (attr));
	}

	return g_slist_reverse (ims);
}

static gboolean
ebb_m365_contact_add_im_addresses (EBookBackendM365 *bbm365,
				   EContact *new_contact,
				   EContact *old_contact,
				   EContactField field_id,
				   const gchar *m365_id,
				   JsonBuilder *builder,
				   GCancellable *cancellable,
				   GError **error)
{
	GSList *new_values, *old_values;

	new_values = ebb_m365_extract_im_addresses (new_contact);
	old_values = ebb_m365_extract_im_addresses (old_contact);

	if (!ebb_m365_string_values_equal (new_values, old_values)) {
		GSList *link;

		e_m365_contact_begin_im_addresses (builder);

		for (link = new_values; link; link = g_slist_next (link)) {
			const gchar *value = link->data;

			if (value && *value)
				e_m365_contact_add_im_address (builder, value);
		}

		e_m365_contact_end_im_addresses (builder);
	}

	g_slist_free_full (new_values, g_free);
	g_slist_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_middle_name (EBookBackendM365 *bbm365,
				  EM365Contact *m365_contact,
				  EContact *inout_contact,
				  EContactField field_id,
				  EM365Connection *cnc,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *value;

	value = e_m365_contact_get_middle_name (m365_contact);

	if (value && *value) {
		EContactName *name = e_contact_get (inout_contact, field_id);
		gchar *prev;

		if (!name)
			name = e_contact_name_new ();

		prev = name->additional;
		name->additional = (gchar *) value;

		e_contact_set (inout_contact, field_id, name);

		name->additional = prev;
		e_contact_name_free (name);
	}

	return TRUE;
}

static gboolean
ebb_m365_contact_add_middle_name (EBookBackendM365 *bbm365,
				  EContact *new_contact,
				  EContact *old_contact,
				  EContactField field_id,
				  const gchar *m365_id,
				  JsonBuilder *builder,
				  GCancellable *cancellable,
				  GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->additional, old_value->additional) == 0))
		e_m365_contact_add_middle_name (builder, new_value ? new_value->additional : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_title (EBookBackendM365 *bbm365,
			    EM365Contact *m365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EM365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	const gchar *value;

	value = e_m365_contact_get_title (m365_contact);

	if (value && *value) {
		EContactName *name = e_contact_get (inout_contact, field_id);
		gchar *prev;

		if (!name)
			name = e_contact_name_new ();

		prev = name->prefixes;
		name->prefixes = (gchar *) value;

		e_contact_set (inout_contact, field_id, name);

		name->prefixes = prev;
		e_contact_name_free (name);
	}

	return TRUE;
}

static gboolean
ebb_m365_contact_add_title (EBookBackendM365 *bbm365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *m365_id,
			    JsonBuilder *builder,
			    GCancellable *cancellable,
			    GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->prefixes, old_value->prefixes) == 0))
		e_m365_contact_add_title (builder, new_value ? new_value->prefixes : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_m365_contact_get_photo (EBookBackendM365 *bbm365,
			    EM365Contact *m365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EM365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	GByteArray *photo_data = NULL;
	const gchar *contact_id;
	GError *local_error = NULL;

	LOCK (bbm365);

	if (bbm365->priv->folder_id)
		contact_id = e_m365_contact_get_id (m365_contact);
	else
		contact_id = e_m365_contact_org_get_mail (m365_contact);

	if (contact_id && *contact_id &&
	    e_m365_connection_get_contact_photo_sync (cnc, NULL, bbm365->priv->folder_id, contact_id, &photo_data, cancellable, &local_error) &&
	    photo_data && photo_data->len) {
		EContactPhoto *photo;

		photo = e_contact_photo_new ();
		e_contact_photo_set_inlined (photo, photo_data->data, photo_data->len);
		e_contact_photo_set_mime_type (photo, "image/jpeg");
		e_contact_set (inout_contact, field_id, photo);
		e_contact_photo_free (photo);
	}

	UNLOCK (bbm365);

	if (photo_data)
		g_byte_array_unref (photo_data);
	g_clear_error (&local_error);

	/* Even it could fail, ignore it and read as many contacts as possible, rather than stop on the first error */
	return TRUE;
}

static gboolean
ebb_m365_contact_photo_equal (EContactPhoto *photo1,
			      EContactPhoto *photo2)
{
	const guchar *data1, *data2;
	gsize len1 = 0, len2 = 0;

	if (!photo1 && !photo2)
		return TRUE;

	if ((photo1 && !photo2) || (!photo1 && photo2))
		return FALSE;

	data1 = e_contact_photo_get_inlined (photo1, &len1);
	data2 = e_contact_photo_get_inlined (photo2, &len2);

	if (!data1 && !data2)
		return TRUE;

	return len1 == len2 && data1 && data2 &&
		memcmp (data1, data2, len1) == 0;
}

static gboolean
ebb_m365_contact_add_photo (EBookBackendM365 *bbm365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *m365_id,
			    JsonBuilder *builder,
			    GCancellable *cancellable,
			    GError **error)
{
	EContactPhoto *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_m365_contact_photo_equal (new_value, old_value)) {
		GByteArray *jpeg_photo = NULL, tmp;
		GError *local_error = NULL;

		if (new_value) {
			gsize len = 0;

			tmp.data = (guchar *) e_contact_photo_get_inlined (new_value, &len);

			if (len && tmp.data) {
				tmp.len = len;
				jpeg_photo = &tmp;
			}
		}

		LOCK (bbm365);

		if (bbm365->priv->folder_id &&
		    !e_m365_connection_update_contact_photo_sync (bbm365->priv->cnc, NULL, bbm365->priv->folder_id,
			m365_id ? m365_id : e_contact_get_const (new_contact, E_CONTACT_UID), jpeg_photo, cancellable, &local_error)) {
			if (local_error) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}
		}

		UNLOCK (bbm365);

		g_clear_error (&local_error);
	}

	e_contact_photo_free (new_value);
	e_contact_photo_free (old_value);

	return TRUE;
}

#define STRING_FIELD(fldid, getfn, addfn) { fldid, FALSE, getfn, NULL, addfn, NULL }
#define COMPLEX_FIELD(fldid, getfn, addfn) { fldid, FALSE, NULL, getfn, NULL, addfn }
#define COMPLEX_FIELD_2(fldid, getfn, addfn) { fldid, TRUE, NULL, getfn, NULL, addfn }
#define COMPLEX_ADDFN(fldid, getfn, addfn) { fldid, FALSE, getfn, NULL, NULL, addfn }

struct _mappings {
	EContactField field_id;
	gboolean add_in_second_go;
	const gchar *	(* m365_get_func)	(EM365Contact *m365_contact);
	gboolean	(* get_func)		(EBookBackendM365 *bbm365,
						 EM365Contact *m365_contact,
						 EContact *inout_contact,
						 EContactField field_id,
						 EM365Connection *cnc,
						 GCancellable *cancellable,
						 GError **error);
	void		(* m365_add_func)	(JsonBuilder *builder,
						 const gchar *value);
	gboolean	(* add_func)		(EBookBackendM365 *bbm365,
						 EContact *new_contact,
						 EContact *old_contact, /* nullable */
						 EContactField field_id,
						 const gchar *m365_id,
						 JsonBuilder *builder,
						 GCancellable *cancellable,
						 GError **error);
} mappings[] = {
	STRING_FIELD	(E_CONTACT_UID,			e_m365_contact_get_id,			NULL),
	COMPLEX_FIELD	(E_CONTACT_REV,			ebb_m365_contact_get_rev,		NULL),
	STRING_FIELD	(E_CONTACT_ASSISTANT,		e_m365_contact_get_assistant_name,	e_m365_contact_add_assistant_name),
	COMPLEX_FIELD	(E_CONTACT_BIRTH_DATE,		ebb_m365_contact_get_birthday,		ebb_m365_contact_add_birthday),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_WORK,	ebb_m365_contact_get_address,		ebb_m365_contact_add_address),
	COMPLEX_FIELD	(E_CONTACT_HOMEPAGE_URL,	ebb_m365_contact_get_home_page,		ebb_m365_contact_add_home_page),
	COMPLEX_FIELD	(E_CONTACT_PHONE_BUSINESS,	ebb_m365_contact_get_phone,		ebb_m365_contact_add_phone),
	COMPLEX_FIELD	(E_CONTACT_CATEGORIES,		ebb_m365_contact_get_categories,	ebb_m365_contact_add_categories),
	STRING_FIELD	(E_CONTACT_ORG,			e_m365_contact_get_company_name,	e_m365_contact_add_company_name),
	STRING_FIELD	(E_CONTACT_ORG_UNIT,		e_m365_contact_get_department,		e_m365_contact_add_department),
	COMPLEX_FIELD	(E_CONTACT_EMAIL,		ebb_m365_contact_get_emails,		ebb_m365_contact_add_emails),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_m365_contact_get_generation,	ebb_m365_contact_add_generation),
	STRING_FIELD	(E_CONTACT_GIVEN_NAME,		e_m365_contact_get_given_name,		e_m365_contact_add_given_name),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_HOME,	ebb_m365_contact_get_address,		ebb_m365_contact_add_address),
	COMPLEX_FIELD	(E_CONTACT_PHONE_HOME,		ebb_m365_contact_get_phone,		ebb_m365_contact_add_phone),
	COMPLEX_FIELD	(E_CONTACT_IM_MSN,		ebb_m365_contact_get_im_addresses,	ebb_m365_contact_add_im_addresses),
	/* STRING_FIELD	(???,				e_m365_contact_get_initials,		e_m365_contact_add_initials), */
	STRING_FIELD	(E_CONTACT_TITLE,		e_m365_contact_get_job_title,		e_m365_contact_add_job_title),
	STRING_FIELD	(E_CONTACT_MANAGER,		e_m365_contact_get_manager,		e_m365_contact_add_manager),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_m365_contact_get_middle_name,	ebb_m365_contact_add_middle_name),
	STRING_FIELD	(E_CONTACT_PHONE_MOBILE,	e_m365_contact_get_mobile_phone,	e_m365_contact_add_mobile_phone),
	COMPLEX_FIELD	(E_CONTACT_NICKNAME,		ebb_m365_contact_get_nick_name,		ebb_m365_contact_add_nick_name),
	STRING_FIELD	(E_CONTACT_OFFICE,		e_m365_contact_get_office_location,	e_m365_contact_add_office_location),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_OTHER,	ebb_m365_contact_get_address,		ebb_m365_contact_add_address),
	COMPLEX_FIELD	(E_CONTACT_NOTE,		ebb_m365_contact_get_note,		ebb_m365_contact_add_note),
	STRING_FIELD 	(E_CONTACT_ROLE,		e_m365_contact_get_profession,		e_m365_contact_add_profession),
	STRING_FIELD 	(E_CONTACT_SPOUSE,		e_m365_contact_get_spouse_name,		e_m365_contact_add_spouse_name),
	STRING_FIELD	(E_CONTACT_FAMILY_NAME,		e_m365_contact_get_surname,		e_m365_contact_add_surname),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_m365_contact_get_title,		ebb_m365_contact_add_title),
	/* STRING_FIELD	(???,				e_m365_contact_get_yomi_company_name,	e_m365_contact_add_yomi_company_name), */
	/* STRING_FIELD	(???,				e_m365_contact_get_yomi_given_name,	e_m365_contact_add_yomi_given_name), */
	/* STRING_FIELD	(???,				e_m365_contact_get_yomi_surname,	e_m365_contact_add_yomi_surname), */
	COMPLEX_ADDFN	(E_CONTACT_FILE_AS,		e_m365_contact_get_file_as,		ebb_m365_contact_add_file_as),
	COMPLEX_FIELD_2	(E_CONTACT_PHOTO,		ebb_m365_contact_get_photo,		ebb_m365_contact_add_photo)
};

static EContact *
ebb_m365_json_contact_to_vcard (EBookBackendM365 *bbm365,
				EM365Contact *m365_contact,
				EM365Connection *cnc,
				gchar **out_object,
				GCancellable *cancellable,
				GError **error)
{
	EContact *contact;
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (m365_contact != NULL, NULL);
	g_return_val_if_fail (out_object != NULL, NULL);

	*out_object = NULL;

	contact = e_contact_new ();

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].m365_get_func) {
			ebb_m365_contact_get_string_attribute (m365_contact, contact, mappings[ii].field_id, mappings[ii].m365_get_func);
		} else if (mappings[ii].get_func) {
			success = mappings[ii].get_func (bbm365, m365_contact, contact, mappings[ii].field_id, cnc, cancellable, error);
		}
	}

	if (success)
		*out_object = e_vcard_to_string (E_VCARD (contact));
	else
		g_clear_object (&contact);

	return contact;
}

static JsonBuilder *
ebb_m365_contact_to_json_locked (EBookBackendM365 *bbm365,
				 EContact *new_contact,
				 EContact *old_contact, /* nullable */
				 GCancellable *cancellable,
				 GError **error)
{
	JsonBuilder *builder;
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (new_contact != NULL, NULL);

	builder = json_builder_new_immutable ();
	e_m365_json_begin_object_member (builder, NULL);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].m365_add_func) {
			ebb_m365_contact_add_string_attribute (new_contact, old_contact, mappings[ii].field_id, builder, mappings[ii].m365_add_func);
		} else if (!mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (bbm365, new_contact, old_contact, mappings[ii].field_id, NULL, builder, cancellable, error);
		}
	}

	e_m365_json_end_object_member (builder);

	if (!success)
		g_clear_object (&builder);

	return builder;
}

static gboolean
ebb_m365_contact_to_json_2nd_go_locked (EBookBackendM365 *bbm365,
					EContact *new_contact,
					EContact *old_contact, /* nullable */
					const gchar *m365_id,
					GCancellable *cancellable,
					GError **error)
{
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (new_contact != NULL, FALSE);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (bbm365, new_contact, old_contact, mappings[ii].field_id, m365_id, NULL, cancellable, error);
		}
	}

	return success;
}

typedef struct {
	gboolean is_autocompletion;
	gchar *auto_comp_str;
} EM365SExpData;

static ESExpResult *
ebb_m365_func_not (ESExp *f,
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
ebb_m365_func_and_or (ESExp *f,
		      gint argc,
		      ESExpResult **argv,
		      gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
ebb_m365_func_is (struct _ESExp *f,
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

static ESExpResult *
ebb_m365_func_endswith (struct _ESExp *f,
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

static ESExpResult *
ebb_m365_func_contains (struct _ESExp *f,
			gint argc,
			struct _ESExpResult **argv,
			gpointer data)
{
	ESExpResult *r;
	EM365SExpData *sdata = data;
	const gchar *propname, *str;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!g_ascii_strcasecmp (propname, "full_name") || !g_ascii_strcasecmp (propname, "email") ||
	    (str && *str && !g_ascii_strcasecmp (propname, "x-evolution-any-field"))) {
		if (!sdata->auto_comp_str) {
			sdata->auto_comp_str = g_strdup (str);
			sdata->is_autocompletion = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;

}

/* We are just handling for autocompletion now. */
static ESExpResult *
ebb_m365_func_beginswith (struct _ESExp *f,
			  gint argc,
			  struct _ESExpResult **argv,
			  gpointer data)
{
	ESExpResult *r;
	const gchar *propname, *str;
	EM365SExpData *sdata = data;

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!g_ascii_strcasecmp (propname, "full_name") || !g_ascii_strcasecmp (propname, "email") ||
	    (str && *str && !g_ascii_strcasecmp (propname, "x-evolution-any-field"))) {
		if (!sdata->auto_comp_str) {
			sdata->auto_comp_str = g_strdup (str);
			sdata->is_autocompletion = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;
	return r;
}

static ESExpResult *
ebb_m365_func_exists (struct _ESExp *f,
		      gint argc,
		      struct _ESExpResult **argv,
		      gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint flags;
} symbols[] = {
	{ "and", ebb_m365_func_and_or, 0 },
	{ "or", ebb_m365_func_and_or, 0},
	{ "not", ebb_m365_func_not, 0 },
	{ "contains", ebb_m365_func_contains, 0},
	{ "is", ebb_m365_func_is, 0},
	{ "beginswith", ebb_m365_func_beginswith, 0},
	{ "endswith", ebb_m365_func_endswith, 0},
	{ "exists", ebb_m365_func_exists, 0}
};

static gchar *
ebb_m365_expr_to_search_text (const gchar *query)
{
	ESExp *sexp;
	gchar *text = NULL;
	EM365SExpData sdata;
	guint ii;

	sexp = e_sexp_new ();
	memset (&sdata, 0, sizeof (EM365SExpData));
	sdata.is_autocompletion = FALSE;

	for (ii = 0; ii < G_N_ELEMENTS (symbols); ii++) {
		e_sexp_add_function (
			sexp, 0, (gchar *) symbols[ii].name,
			symbols[ii].func,
			&sdata);
	}

	e_sexp_input_text (sexp, query, strlen (query));

	if (e_sexp_parse (sexp) == -1) {
		const gchar *errstr = e_sexp_get_error (sexp);

		g_printerr ("%s: Failed to parse query '%s': %s\n", G_STRFUNC, query, errstr ? errstr : "Unknown error");
	} else {
		ESExpResult *r;

		r = e_sexp_eval (sexp);
		if (r) {
			if (sdata.is_autocompletion && sdata.auto_comp_str && *sdata.auto_comp_str)
				text = sdata.auto_comp_str;
			else
				g_free (sdata.auto_comp_str);
		}

		e_sexp_result_free (sexp, r);
	}

	g_object_unref (sexp);

	return text;
}

static void
ebb_m365_convert_error_to_client_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror || (*perror)->domain == E_CLIENT_ERROR || (*perror)->domain == E_BOOK_CLIENT_ERROR)
		return;

	/*if ((*perror)->domain == EWS_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_FOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = EBC_ERROR_EX (E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_EVENTNOTFOUND:
		case EWS_CONNECTION_ERROR_ITEMNOTFOUND:
			error = EBC_ERROR_EX (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, (*perror)->message);
	}*/

	if (error) {
		g_error_free (*perror);
		*perror = error;
	}
}

static gboolean
ebb_m365_enum_cached_for_offline (EBookBackendM365 *bbm365)
{
	if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS ||
	    bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS) {
		ESourceOffline *offline_ext;

		offline_ext = e_source_get_extension (e_backend_get_source (E_BACKEND (bbm365)), E_SOURCE_EXTENSION_OFFLINE);
		return e_source_offline_get_stay_synchronized (offline_ext);
	}

	return TRUE;
}

static gboolean
ebb_m365_update_cache_for_expression_sync (EBookBackendM365 *bbm365,
					   const gchar *expr,
					   GCancellable *cancellable,
					   GError **error)
{
	EBookCache *book_cache;
	GSList *contacts = NULL; /* EM365Contact * */
	gchar *text;
	gboolean success = TRUE;

	if (bbm365->priv->cached_for_offline ||
	    bbm365->priv->folder_kind == E_M365_FOLDER_KIND_CONTACTS ||
	    bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE)
		return success;

	if (!expr || !*expr || g_ascii_strcasecmp (expr, "(contains \"x-evolution-any-field\" \"\")") == 0)
		return success;

	text = ebb_m365_expr_to_search_text (expr);
	if (!text || !*text) {
		g_free (text);
		return success;
	}

	book_cache = e_book_meta_backend_ref_cache (E_BOOK_META_BACKEND (bbm365));
	if (!E_IS_BOOK_CACHE (book_cache)) {
		g_warn_if_fail (E_IS_BOOK_CACHE (book_cache));
		g_free (text);
		return FALSE;
	}

	LOCK (bbm365);

	success = e_book_meta_backend_ensure_connected_sync (E_BOOK_META_BACKEND (bbm365), cancellable, error) &&
		e_m365_connection_search_contacts_sync (bbm365->priv->cnc, NULL, bbm365->priv->folder_kind, bbm365->priv->folder_id, text, &contacts, cancellable, error);

	if (success && contacts) {
		EBookBackend *book_backend = E_BOOK_BACKEND (bbm365);
		GSList *link;

		for (link = contacts; link && success; link = g_slist_next (link)) {
			EM365Contact *contact = link->data;
			EContact *vcard;
			gchar *object;
			const gchar *id;

			if (!contact)
				continue;

			id = e_m365_contact_get_id (contact);
			if (!id)
				continue;

			vcard = ebb_m365_json_contact_to_vcard (bbm365, contact, bbm365->priv->cnc, &object, cancellable, error);

			if (!g_cancellable_is_cancelled (cancellable))
				g_warn_if_fail (object != NULL);

			if (object && vcard) {
				success = e_book_cache_put_contact (book_cache, vcard, object, 0, E_CACHE_IS_ONLINE, cancellable, error);

				if (success)
					e_book_backend_notify_update (book_backend, vcard);
			}

			g_clear_object (&vcard);
			g_free (object);
		}
	}

	UNLOCK (bbm365);

	g_slist_free_full (contacts, (GDestroyNotify) json_object_unref);
	g_clear_object (&book_cache);
	g_free (text);

	return success;
}

static void
ebb_m365_maybe_disconnect_sync (EBookBackendM365 *bbm365,
			        GError **in_perror,
			        GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_M365 (bbm365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_book_meta_backend_disconnect_sync (E_BOOK_META_BACKEND (bbm365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (bbm365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gboolean
ebb_m365_unset_connection_sync (EBookBackendM365 *bbm365,
				gboolean is_disconnect,
				GCancellable *cancellable,
				GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (bbm365), FALSE);

	LOCK (bbm365);

	if (bbm365->priv->cnc) {
		if (is_disconnect)
			success = e_m365_connection_disconnect_sync (bbm365->priv->cnc, cancellable, error);
	}

	g_clear_object (&bbm365->priv->cnc);
	g_clear_pointer (&bbm365->priv->folder_id, g_free);

	UNLOCK (bbm365);

	return success;
}

static gboolean
ebb_m365_connect_sync (EBookMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	EBookBackendM365 *bbm365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbm365 = E_BOOK_BACKEND_M365 (meta_backend);

	LOCK (bbm365);

	if (bbm365->priv->cnc) {
		UNLOCK (bbm365);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	} else {
		EBackend *backend;
		ESourceRegistry *registry;
		ESource *source;
		EM365Connection *cnc = NULL;
		ESourceM365Folder *m365_folder_extension;
		CamelM365Settings *m365_settings;
		gchar *folder_id;

		backend = E_BACKEND (bbm365);
		source = e_backend_get_source (backend);
		registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbm365));
		m365_settings = camel_m365_settings_get_from_backend (backend, registry);
		g_warn_if_fail (m365_settings != NULL);

		m365_folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);
		folder_id = e_source_m365_folder_dup_id (m365_folder_extension);

		if (folder_id) {
			cnc = e_m365_connection_new_for_backend (backend, registry, source, m365_settings);

			e_binding_bind_property (
				backend, "proxy-resolver",
				cnc, "proxy-resolver",
				G_BINDING_SYNC_CREATE);

			if (g_ascii_strcasecmp (folder_id, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS) == 0) {
				*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_ORG_CONTACTS, NULL, NULL,
					out_certificate_pem, out_certificate_errors, cancellable, error);

				bbm365->priv->folder_kind = E_M365_FOLDER_KIND_ORG_CONTACTS;

				/* do not store artificial folder id */
				g_clear_pointer (&folder_id, g_free);
			} else if (g_ascii_strcasecmp (folder_id, E_M365_ARTIFICIAL_FOLDER_ID_USERS) == 0) {
				*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_USERS, NULL, NULL,
					out_certificate_pem, out_certificate_errors, cancellable, error);

				bbm365->priv->folder_kind = E_M365_FOLDER_KIND_USERS;

				/* do not store artificial folder id */
				g_clear_pointer (&folder_id, g_free);
			} else if (g_ascii_strcasecmp (folder_id, E_M365_ARTIFICIAL_FOLDER_ID_PEOPLE) == 0) {
				*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_PEOPLE, NULL, NULL,
					out_certificate_pem, out_certificate_errors, cancellable, error);

				bbm365->priv->folder_kind = E_M365_FOLDER_KIND_PEOPLE;

				/* do not store artificial folder id */
				g_clear_pointer (&folder_id, g_free);
			} else {
				*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_CONTACTS, NULL, folder_id,
					out_certificate_pem, out_certificate_errors, cancellable, error);

				bbm365->priv->folder_kind = E_M365_FOLDER_KIND_CONTACTS;
			}

			if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
				bbm365->priv->cnc = g_object_ref (cnc);

				g_warn_if_fail (bbm365->priv->folder_id == NULL);

				g_free (bbm365->priv->folder_id);
				bbm365->priv->folder_id = folder_id;

				folder_id = NULL;
				success = TRUE;

				ebb_m365_check_source_properties (bbm365);
			}
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Folder ID is not set")));
		}

		g_clear_object (&cnc);
		g_free (folder_id);

		e_book_backend_set_writable (E_BOOK_BACKEND (bbm365), bbm365->priv->folder_id != NULL);
	}

	UNLOCK (bbm365);

	ebb_m365_convert_error_to_client_error (error);

	return success;
}

static gboolean
ebb_m365_disconnect_sync (EBookMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);

	return ebb_m365_unset_connection_sync (E_BOOK_BACKEND_M365 (meta_backend), TRUE, cancellable, error);
}

typedef struct _ObjectsDeltaData {
	EBookBackendM365 *bbm365;
	ECache *cache;
	GPtrArray *ids;
	GSList **out_created_objects;
	GSList **out_modified_objects;
	GSList **out_removed_objects;
} ObjectsDeltaData;

static gboolean
ebb_m365_get_objects_delta_cb (EM365Connection *cnc,
			       const GSList *results, /* JsonObject * - the returned objects from the server */
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	ObjectsDeltaData *odd = user_data;
	GSList *link;

	g_return_val_if_fail (odd != NULL, FALSE);

	for (link = (GSList *) results; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
		EM365Contact *contact = link->data;
		const gchar *id;

		if (!contact)
			continue;

		id = e_m365_contact_get_id (contact);

		if (!id)
			continue;

		if (e_m365_delta_is_removed_object (contact)) {
			*(odd->out_removed_objects) = g_slist_prepend (*(odd->out_removed_objects),
				e_book_meta_backend_info_new (id, NULL, NULL, NULL));
		} else {
			g_ptr_array_add (odd->ids, g_strdup (id));
		}
	}

	return TRUE;
}

static gboolean
ebb_m365_get_changes_sync (EBookMetaBackend *meta_backend,
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
	EBookBackendM365 *bbm365;
	EBookCache *book_cache;
	ObjectsDeltaData odd;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	bbm365 = E_BOOK_BACKEND_M365 (meta_backend);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), FALSE);

	odd.bbm365 = bbm365;
	odd.cache = E_CACHE (book_cache);
	odd.ids = g_ptr_array_new_with_free_func (g_free);
	odd.out_created_objects = out_created_objects;
	odd.out_modified_objects = out_modified_objects;
	odd.out_removed_objects = out_removed_objects;

	LOCK (bbm365);

	if (bbm365->priv->cached_for_offline && bbm365->priv->folder_kind != E_M365_FOLDER_KIND_PEOPLE) {
		success = e_m365_connection_get_objects_delta_sync (bbm365->priv->cnc, NULL,
			bbm365->priv->folder_kind, bbm365->priv->folder_id, "id", last_sync_tag, 0,
			ebb_m365_get_objects_delta_cb, &odd,
			out_new_sync_tag, cancellable, &local_error);
	} else {
		success = TRUE;
	}

	if (e_m365_connection_util_delta_token_failed (local_error)) {
		GSList *known_uids = NULL, *link;

		g_clear_error (&local_error);

		if (e_book_cache_search_uids (book_cache, NULL, &known_uids, cancellable, error)) {
			for (link = known_uids; link; link = g_slist_next (link)) {
				const gchar *uid = link->data;

				if (uid) {
					*out_removed_objects = g_slist_prepend (*out_removed_objects,
						e_book_meta_backend_info_new (uid, NULL, NULL, NULL));
				}
			}
		}

		e_cache_remove_all (E_CACHE (book_cache), cancellable, NULL);

		g_slist_free_full (known_uids, g_free);

		success = e_m365_connection_get_objects_delta_sync (bbm365->priv->cnc, NULL,
			bbm365->priv->folder_kind, bbm365->priv->folder_id, "id", NULL, 0,
			ebb_m365_get_objects_delta_cb, &odd,
			out_new_sync_tag, cancellable, &local_error);
	} else if (local_error) {
		g_propagate_error (error, local_error);
	}

	if (success && (odd.ids->len || bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE)) {
		GPtrArray *contacts = NULL;

		switch (bbm365->priv->folder_kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			success = e_m365_connection_get_contacts_sync (bbm365->priv->cnc, NULL,
				bbm365->priv->folder_id, odd.ids, &contacts, cancellable, error);
			break;
		case E_M365_FOLDER_KIND_ORG_CONTACTS:
			success = e_m365_connection_get_org_contacts_sync (bbm365->priv->cnc, NULL,
				odd.ids, &contacts, cancellable, error);
			break;
		case E_M365_FOLDER_KIND_USERS:
			success = e_m365_connection_get_users_sync (bbm365->priv->cnc, NULL,
				odd.ids, &contacts, cancellable, error);
			break;
		case E_M365_FOLDER_KIND_PEOPLE:
			success = e_m365_connection_get_people_sync (bbm365->priv->cnc, NULL,
				bbm365->priv->max_people, &contacts, cancellable, error);
			break;
		default:
			break;
		}

		/* process them also on failure, because it could fail in following batch requests */
		if (contacts != NULL) {
			guint ii;

			for (ii = 0; ii < contacts->len; ii++) {
				EM365Contact *contact = g_ptr_array_index (contacts, ii);
				GSList **out_slist;
				EContact *vcard;
				gchar *object;
				const gchar *id = e_m365_contact_get_id (contact);

				if (!id)
					continue;

				if (e_cache_contains (odd.cache, id, E_CACHE_INCLUDE_DELETED))
					out_slist = odd.out_modified_objects;
				else
					out_slist = odd.out_created_objects;

				vcard = ebb_m365_json_contact_to_vcard (bbm365, contact, bbm365->priv->cnc, &object, cancellable, error);

				g_clear_object (&vcard);

				if (!g_cancellable_is_cancelled (cancellable))
					g_warn_if_fail (object != NULL);

				if (object) {
					EBookMetaBackendInfo *nfo;

					nfo = e_book_meta_backend_info_new (id,
						e_m365_contact_get_change_key (contact),
						object, NULL);

					nfo->extra = object; /* assumes ownership, to avoid unnecessary re-allocation */

					*out_slist = g_slist_prepend (*out_slist, nfo);
				}
			}

			g_ptr_array_unref (contacts);
		}
	}

	UNLOCK (bbm365);

	ebb_m365_convert_error_to_client_error (error);
	ebb_m365_maybe_disconnect_sync (bbm365, error, cancellable);

	g_clear_pointer (&odd.ids, g_ptr_array_unref);
	g_clear_object (&book_cache);

	return success;
}

static gboolean
ebb_m365_load_contact_sync (EBookMetaBackend *meta_backend,
			    const gchar *uid,
			    const gchar *extra,
			    EContact **out_contact,
			    gchar **out_extra,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendM365 *bbm365;
	EM365Contact *contact = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	bbm365 = E_BOOK_BACKEND_M365 (meta_backend);

	LOCK (bbm365);

	if (bbm365->priv->folder_id) {
		success = e_m365_connection_get_contact_sync (bbm365->priv->cnc, NULL,
			bbm365->priv->folder_id, uid, &contact, cancellable, error);
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS) {
		success = e_m365_connection_get_org_contact_sync (bbm365->priv->cnc, NULL,
			uid, &contact, cancellable, error);
	} else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS) {
		success = e_m365_connection_get_user_sync (bbm365->priv->cnc, NULL,
			uid, &contact, cancellable, error);
	} else {
		g_warn_if_reached ();
	}

	if (success) {
		*out_contact = ebb_m365_json_contact_to_vcard (bbm365, contact, bbm365->priv->cnc, out_extra, cancellable, error);

		if (contact)
			json_object_unref (contact);
	}

	UNLOCK (bbm365);

	ebb_m365_convert_error_to_client_error (error);
	ebb_m365_maybe_disconnect_sync (bbm365, error, cancellable);

	return success;
}

static gboolean
ebb_m365_save_contact_sync (EBookMetaBackend *meta_backend,
			    gboolean overwrite_existing,
			    EConflictResolution conflict_resolution,
			    /* const */ EContact *contact,
			    const gchar *extra,
			    guint32 opflags,
			    gchar **out_new_uid,
			    gchar **out_new_extra,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendM365 *bbm365;
	EContact *tmp_contact = NULL, *old_contact = NULL;
	JsonBuilder *builder;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	if (GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_IS_LIST))) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED, _("Cannot save contact list into a Microsoft 365 address book")));
		return FALSE;
	}

	bbm365 = E_BOOK_BACKEND_M365 (meta_backend);

	LOCK (bbm365);

	if (!bbm365->priv->folder_id) {
		const gchar *text = "Cannot modify contact"; /* this should not happen, thus not localized */

		if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS)
			text = _("Cannot modify organizational contact");
		else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS)
			text = _("Cannot modify user contact");
		else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE)
			text = _("Cannot modify recent contact");

		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, text));

		UNLOCK (bbm365);

		return FALSE;
	}

	if (e_vcard_get_attribute (E_VCARD (contact), EVC_PHOTO)) {
		tmp_contact = e_contact_duplicate (contact);
		contact = tmp_contact;

		e_contact_inline_local_photos (contact, NULL);
	}

	if (extra && *extra)
		old_contact = e_contact_new_from_vcard (extra);

	builder = ebb_m365_contact_to_json_locked (bbm365, contact, old_contact, cancellable, error);

	if (builder) {
		if (overwrite_existing) {
			const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);

			success = e_m365_connection_update_contact_sync (bbm365->priv->cnc, NULL, bbm365->priv->folder_id,
				uid, builder, cancellable, error);

			if (success)
				success = ebb_m365_contact_to_json_2nd_go_locked (bbm365, contact, old_contact, uid, cancellable, error);

			if (success)
				*out_new_extra = e_vcard_to_string (E_VCARD (contact));
		} else {
			EM365Contact *created_contact = NULL;

			success = e_m365_connection_create_contact_sync (bbm365->priv->cnc, NULL, bbm365->priv->folder_id,
				builder, &created_contact, cancellable, error);

			if (success && created_contact) {
				const gchar *m365_id = e_m365_contact_get_id (created_contact);

				success = ebb_m365_contact_to_json_2nd_go_locked (bbm365, contact, old_contact, m365_id, cancellable, error);
			}

			if (success && created_contact) {
				EContact *vcard;

				*out_new_uid = g_strdup (e_m365_contact_get_id (created_contact));

				vcard = ebb_m365_json_contact_to_vcard (bbm365, created_contact, bbm365->priv->cnc, out_new_extra, cancellable, error);

				if (!vcard)
					success = FALSE;

				g_clear_object (&vcard);
			}

			if (created_contact)
				json_object_unref (created_contact);
		}

		g_clear_object (&builder);
	}

	UNLOCK (bbm365);

	ebb_m365_convert_error_to_client_error (error);
	ebb_m365_maybe_disconnect_sync (bbm365, error, cancellable);

	g_clear_object (&old_contact);
	g_clear_object (&tmp_contact);

	return success;
}

static gboolean
ebb_m365_remove_contact_sync (EBookMetaBackend *meta_backend,
			      EConflictResolution conflict_resolution,
			      const gchar *uid,
			      const gchar *extra,
			      const gchar *object,
			      guint32 opflags,
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendM365 *bbm365;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);

	bbm365 = E_BOOK_BACKEND_M365 (meta_backend);

	LOCK (bbm365);

	if (bbm365->priv->folder_id) {
		GError *local_error = NULL;

		success = e_m365_connection_delete_contact_sync (bbm365->priv->cnc, NULL,
			bbm365->priv->folder_id, uid, cancellable, &local_error);

		if (g_error_matches (local_error, E_M365_ERROR, E_M365_ERROR_ITEM_NOT_FOUND)) {
			g_clear_error (&local_error);
			success = TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}
	} else {
		const gchar *text = "Cannot remove contact"; /* this should not happen, thus not localized */

		if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS)
			text = _("Cannot remove organizational contact");
		else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_USERS)
			text = _("Cannot remove user contact");
		else if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE)
			text = _("Cannot remove recent contact");

		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, text));
		success = FALSE;
	}

	UNLOCK (bbm365);

	ebb_m365_convert_error_to_client_error (error);
	ebb_m365_maybe_disconnect_sync (bbm365, error, cancellable);

	return success;
}

static gboolean
ebb_m365_search_sync (EBookMetaBackend *meta_backend,
		      const gchar *expr,
		      gboolean meta_contact,
		      GSList **out_contacts,
		      GCancellable *cancellable,
		      GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	ebb_m365_update_cache_for_expression_sync (E_BOOK_BACKEND_M365 (meta_backend), expr, cancellable, NULL);

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_m365_parent_class)->search_sync (meta_backend, expr, meta_contact, out_contacts, cancellable, error);
}

static gboolean
ebb_m365_search_uids_sync (EBookMetaBackend *meta_backend,
			   const gchar *expr,
			   GSList **out_uids,
			   GCancellable *cancellable,
			   GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	ebb_m365_update_cache_for_expression_sync (E_BOOK_BACKEND_M365 (meta_backend), expr, cancellable, NULL);

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_m365_parent_class)->search_uids_sync (meta_backend, expr,
		out_uids, cancellable, error);
}

static gchar *
ebb_m365_get_backend_property (EBookBackend *book_backend,
			       const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_M365 (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		gboolean cached_for_offline = ebb_m365_enum_cached_for_offline (E_BOOK_BACKEND_M365 (book_backend));

		return g_strjoin (",",
			"net",
			"contact-lists",
			cached_for_offline ? "do-initial-query" : "",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			NULL);
	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GString *buffer;
		gchar *fields;
		gint ii;

		buffer = g_string_sized_new (1024);

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (buffer->len > 0)
				g_string_append_c (buffer, ',');

			g_string_append (buffer, e_contact_field_name (mappings[ii].field_id));
		}

		fields = g_strjoin (
			",",
			buffer->str,
			e_contact_field_name (E_CONTACT_FULL_NAME),
			e_contact_field_name (E_CONTACT_EMAIL_1),
			e_contact_field_name (E_CONTACT_EMAIL_2),
			e_contact_field_name (E_CONTACT_EMAIL_3),
			e_contact_field_name (E_CONTACT_EMAIL_4),
			NULL);

		g_string_free (buffer, TRUE);

		return fields;
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_m365_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static void
ebb_m365_start_view_search_thread_func (EBookBackend *book_backend,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error)
{
	EDataBookView *view = user_data;
	EBookBackendM365 *bbm365;

	g_return_if_fail (E_IS_BOOK_BACKEND_M365 (book_backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	bbm365 = E_BOOK_BACKEND_M365 (book_backend);

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		EBookBackendSExp *sexp;
		GSList *contacts = NULL;
		const gchar *expr = NULL;

		sexp = e_data_book_view_get_sexp (view);
		if (sexp)
			expr = e_book_backend_sexp_text (sexp);

		/* this is called only for not-cached-for-offline books and it's enough to search for the contacts,
		   the rest is done by the notifications when added/discovered a new contact */
		if (e_book_meta_backend_search_sync (E_BOOK_META_BACKEND (book_backend), expr, TRUE, &contacts, cancellable, NULL))
			g_slist_free_full (contacts, g_object_unref);
	}

	LOCK (bbm365);
	g_hash_table_remove (bbm365->priv->view_cancellables, view);
	UNLOCK (bbm365);
}

static void
ebb_m365_start_view (EBookBackend *book_backend,
		     EDataBookView *view)
{
	if ((e_data_book_view_get_flags (view) & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		EBookBackendM365 *bbm365 = E_BOOK_BACKEND_M365 (book_backend);

		if (!bbm365->priv->cached_for_offline) {
			GCancellable *cancellable;

			cancellable = g_cancellable_new ();

			LOCK (bbm365);
			g_hash_table_insert (bbm365->priv->view_cancellables, view, g_object_ref (cancellable));
			UNLOCK (bbm365);

			e_book_backend_schedule_custom_operation (book_backend, cancellable,
				ebb_m365_start_view_search_thread_func, g_object_ref (view), g_object_unref);

			g_clear_object (&cancellable);
		}
	}

	/* Chain up to parent's method. */
	E_BOOK_BACKEND_CLASS (e_book_backend_m365_parent_class)->impl_start_view (book_backend, view);
}

static void
ebb_m365_stop_view (EBookBackend *book_backend,
		    EDataBookView *view)
{
	if ((e_data_book_view_get_flags (view) & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		EBookBackendM365 *bbm365 = E_BOOK_BACKEND_M365 (book_backend);
		GCancellable *cancellable;
		gpointer value = NULL;

		LOCK (bbm365);
		if (!g_hash_table_steal_extended (bbm365->priv->view_cancellables, view, NULL, &value))
			value = NULL;
		UNLOCK (bbm365);

		cancellable = value;

		if (cancellable) {
			g_cancellable_cancel (cancellable);
			g_clear_object (&cancellable);
		}
	}

	/* Chain up to parent's method. */
	E_BOOK_BACKEND_CLASS (e_book_backend_m365_parent_class)->impl_stop_view (book_backend, view);
}

static gboolean
ebb_m365_get_destination_address (EBackend *backend,
				  gchar **host,
				  guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_book_backend_get_registry (E_BOOK_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static void
ebb_m365_check_source_properties (EBookBackendM365 *bbm365)
{
	ESourceM365Folder *m365_folder_ext;
	guint max_people;
	gboolean cached_for_offline;

	if (!e_backend_get_source (E_BACKEND (bbm365)))
		return;

	cached_for_offline = ebb_m365_enum_cached_for_offline (bbm365);

	if ((cached_for_offline ? 1 : 0) != (bbm365->priv->cached_for_offline ? 1 : 0)) {
		EBookBackend *book_backend = E_BOOK_BACKEND (bbm365);
		gchar *value;

		bbm365->priv->cached_for_offline = cached_for_offline;

		value = ebb_m365_get_backend_property (book_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES);

		e_book_backend_notify_property_changed (book_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES, value);

		g_free (value);
	}

	m365_folder_ext = e_source_get_extension (e_backend_get_source (E_BACKEND (bbm365)), E_SOURCE_EXTENSION_M365_FOLDER);
	max_people = e_source_m365_folder_get_max_people (m365_folder_ext);

	if (max_people != bbm365->priv->max_people) {
		bbm365->priv->max_people = max_people;

		if (bbm365->priv->folder_kind == E_M365_FOLDER_KIND_PEOPLE &&
		    e_backend_get_online (E_BACKEND (bbm365)))
			e_book_meta_backend_schedule_refresh (E_BOOK_META_BACKEND (bbm365));
	}
}

static void
e_book_backend_m365_dispose (GObject *object)
{
	EBookBackendM365 *bbm365 = E_BOOK_BACKEND_M365 (object);

	ebb_m365_unset_connection_sync (bbm365, FALSE, NULL, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_m365_parent_class)->dispose (object);
}

static void
e_book_backend_m365_finalize (GObject *object)
{
	EBookBackendM365 *bbm365 = E_BOOK_BACKEND_M365 (object);

	g_rec_mutex_clear (&bbm365->priv->property_lock);
	g_hash_table_destroy (bbm365->priv->view_cancellables);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_m365_parent_class)->finalize (object);
}

static void
e_book_backend_m365_init (EBookBackendM365 *bbm365)
{
	bbm365->priv = e_book_backend_m365_get_instance_private (bbm365);

	g_rec_mutex_init (&bbm365->priv->property_lock);
	bbm365->priv->view_cancellables = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

	g_signal_connect (bbm365, "source-changed",
		G_CALLBACK (ebb_m365_check_source_properties), NULL);
}

static void
e_book_backend_m365_class_init (EBookBackendM365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;
	EBookMetaBackendClass *book_meta_backend_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendmicrosoft365.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendM365Factory";
	book_meta_backend_class->connect_sync = ebb_m365_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_m365_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_m365_get_changes_sync;
	book_meta_backend_class->load_contact_sync = ebb_m365_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_m365_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_m365_remove_contact_sync;
	book_meta_backend_class->search_sync = ebb_m365_search_sync;
	book_meta_backend_class->search_uids_sync = ebb_m365_search_uids_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = ebb_m365_get_backend_property;
	book_backend_class->impl_start_view = ebb_m365_start_view;
	book_backend_class->impl_stop_view = ebb_m365_stop_view;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ebb_m365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_book_backend_m365_dispose;
	object_class->finalize = e_book_backend_m365_finalize;
}
