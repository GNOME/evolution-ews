/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#define LIBICAL_GLIB_UNSTABLE_API
#include <libical-glib/libical-glib.h>
#undef LIBICAL_GLIB_UNSTABLE_API

#include <libedata-book/libedata-book.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"
#include "common/e-source-o365-folder.h"

#include "e-book-backend-o365.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define EC_ERROR_EX(_code,_msg) e_client_error_create (_code, _msg)
#define EBC_ERROR_EX(_code,_msg) e_book_client_error_create (_code, _msg)

#define EBB_O365_DATA_VERSION 1
#define EBB_O365_DATA_VERSION_KEY "o365-data-version"

#define LOCK(_bb) g_rec_mutex_lock (&_bb->priv->property_lock)
#define UNLOCK(_bb) g_rec_mutex_unlock (&_bb->priv->property_lock)

struct _EBookBackendO365Private {
	GRecMutex property_lock;
	EO365Connection *cnc;
	gchar *folder_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendO365, e_book_backend_o365, E_TYPE_BOOK_META_BACKEND)

static void
ebb_o365_contact_get_string_attribute (EO365Contact *o365_contact,
				       EContact *inout_contact,
				       EContactField field_id,
				       const gchar * (*o365_get_func) (EO365Contact *contact))
{
	e_contact_set (inout_contact, field_id, o365_get_func (o365_contact));
}

static void
ebb_o365_contact_add_string_attribute (EContact *new_contact,
				       EContact *old_contact,
				       EContactField field_id,
				       JsonBuilder *builder,
				       void (* o365_add_func) (JsonBuilder *builder,
							       const gchar *value))
{
	const gchar *new_value, *old_value;

	g_return_if_fail (o365_add_func != NULL);

	new_value = e_contact_get_const (new_contact, field_id);
	old_value = old_contact ? e_contact_get_const (old_contact, field_id) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0)
		o365_add_func (builder, new_value);
}

static gboolean
ebb_o365_contact_get_rev (EBookBackendO365 *bbo365,
			  EO365Contact *o365_contact,
			  EContact *inout_contact,
			  EContactField field_id,
			  EO365Connection *cnc,
			  GCancellable *cancellable,
			  GError **error)
{
	gchar time_string[100] = { 0 };
	struct tm stm;
	time_t value;

	value = e_o365_contact_get_last_modified_date_time (o365_contact);

	if (value <= (time_t) 0)
		value = time (NULL);

	gmtime_r (&value, &stm);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", &stm);

	e_contact_set (inout_contact, field_id, time_string);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_birthday (EBookBackendO365 *bbo365,
			       EO365Contact *o365_contact,
			       EContact *inout_contact,
			       EContactField field_id,
			       EO365Connection *cnc,
			       GCancellable *cancellable,
			       GError **error)
{
	time_t value;

	value = e_o365_contact_get_birthday (o365_contact);

	if (value > (time_t) 0) {
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
ebb_o365_contact_add_birthday (EBookBackendO365 *bbo365,
			       EContact *new_contact,
			       EContact *old_contact,
			       EContactField field_id,
			       const gchar *o365_id,
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

			e_o365_contact_add_birthday (builder, value);
		} else {
			e_o365_contact_add_birthday (builder, (time_t) 0);
		}
	}

	e_contact_date_free (new_dt);
	e_contact_date_free (old_dt);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_address (EBookBackendO365 *bbo365,
			      EO365Contact *o365_contact,
			      EContact *inout_contact,
			      EContactField field_id,
			      EO365Connection *cnc,
			      GCancellable *cancellable,
			      GError **error)
{
	EO365PhysicalAddress *phys_address = NULL;

	if (field_id == E_CONTACT_ADDRESS_WORK)
		phys_address = e_o365_contact_get_business_address (o365_contact);
	else if (field_id == E_CONTACT_ADDRESS_HOME)
		phys_address = e_o365_contact_get_home_address (o365_contact);
	else if (field_id == E_CONTACT_ADDRESS_OTHER)
		phys_address = e_o365_contact_get_other_address (o365_contact);
	else
		g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));

	if (phys_address) {
		EContactAddress addr;

		memset (&addr, 0, sizeof (EContactAddress));

		addr.locality = (gchar *) e_o365_physical_address_get_city (phys_address);
		addr.country = (gchar *) e_o365_physical_address_get_country_or_region (phys_address);
		addr.code = (gchar *) e_o365_physical_address_get_postal_code (phys_address);
		addr.region = (gchar *) e_o365_physical_address_get_state (phys_address);
		addr.street = (gchar *) e_o365_physical_address_get_street (phys_address);

		if (addr.locality || addr.country || addr.code || addr.region || addr.street)
			e_contact_set (inout_contact, field_id, &addr);
		else
			e_contact_set (inout_contact, field_id, NULL);
	}

	return TRUE;
}

static gboolean
ebb_o365_contact_address_equal (const EContactAddress *addr1,
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
ebb_o365_contact_add_address (EBookBackendO365 *bbo365,
			      EContact *new_contact,
			      EContact *old_contact,
			      EContactField field_id,
			      const gchar *o365_id,
			      JsonBuilder *builder,
			      GCancellable *cancellable,
			      GError **error)
{
	EContactAddress *new_addr, *old_addr;

	new_addr = e_contact_get (new_contact, field_id);
	old_addr = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_o365_contact_address_equal (new_addr, old_addr)) {
		void (* add_func) (JsonBuilder *builder,
				   const gchar *city,
				   const gchar *country_or_region,
				   const gchar *postal_code,
				   const gchar *state,
				   const gchar *street) = NULL;

		if (field_id == E_CONTACT_ADDRESS_WORK)
			add_func = e_o365_contact_add_business_address;
		else if (field_id == E_CONTACT_ADDRESS_HOME)
			add_func = e_o365_contact_add_home_address;
		else if (field_id == E_CONTACT_ADDRESS_OTHER)
			add_func = e_o365_contact_add_other_address;
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
ebb_o365_string_values_equal (GSList *new_values, /* const gchar * */
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
ebb_o365_string_list_values_equal (GList *new_values, /* const gchar * */
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
ebb_o365_contact_get_phone (EBookBackendO365 *bbo365,
			    EO365Contact *o365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EO365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	JsonArray *values = NULL;
	const gchar *type_val = NULL;

	if (field_id == E_CONTACT_PHONE_BUSINESS) {
		values = e_o365_contact_get_business_phones (o365_contact);
		type_val = "WORK";
	} else if (field_id == E_CONTACT_PHONE_HOME) {
		values = e_o365_contact_get_home_phones (o365_contact);
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

	return TRUE;
}

static GSList * /* gchar * */
ebb_o365_extract_phones (EContact *contact,
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
ebb_o365_contact_add_phone (EBookBackendO365 *bbo365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *o365_id,
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
		begin_func = e_o365_contact_begin_business_phones;
		end_func = e_o365_contact_end_business_phones;
		add_func = e_o365_contact_add_business_phone;
		type_val = "WORK";
	} else if (field_id == E_CONTACT_PHONE_HOME) {
		begin_func = e_o365_contact_begin_home_phones;
		end_func = e_o365_contact_end_home_phones;
		add_func = e_o365_contact_add_home_phone;
		type_val = NULL; /* everything else is treated as "HOME" phone */
	} else {
		g_warning ("%s: Uncaught field '%s'", G_STRFUNC, e_contact_vcard_attribute (field_id));
	}

	new_values = ebb_o365_extract_phones (new_contact, type_val);
	old_values = ebb_o365_extract_phones (old_contact, type_val);

	if (!ebb_o365_string_values_equal (new_values, old_values)) {
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
ebb_o365_contact_get_categories (EBookBackendO365 *bbo365,
				 EO365Contact *o365_contact,
				 EContact *inout_contact,
				 EContactField field_id,
				 EO365Connection *cnc,
				 GCancellable *cancellable,
				 GError **error)
{
	JsonArray *values;

	values = e_o365_contact_get_categories (o365_contact);

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
ebb_o365_extract_categories (EContact *contact,
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
ebb_o365_contact_add_categories (EBookBackendO365 *bbo365,
				 EContact *new_contact,
				 EContact *old_contact,
				 EContactField field_id,
				 const gchar *o365_id,
				 JsonBuilder *builder,
				 GCancellable *cancellable,
				 GError **error)
{
	GSList *new_values, *old_values;

	new_values = ebb_o365_extract_categories (new_contact, field_id);
	old_values = ebb_o365_extract_categories (old_contact, field_id);

	if (!ebb_o365_string_values_equal (new_values, old_values)) {
		GSList *link;

		e_o365_contact_begin_categories (builder);

		for (link = new_values; link; link = g_slist_next (link)) {
			const gchar *value = link->data;

			e_o365_contact_add_category (builder, value);
		}

		e_o365_contact_end_categories (builder);
	}

	g_slist_free_full (new_values, g_free);
	g_slist_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_emails (EBookBackendO365 *bbo365,
			     EO365Contact *o365_contact,
			     EContact *inout_contact,
			     EContactField field_id,
			     EO365Connection *cnc,
			     GCancellable *cancellable,
			     GError **error)
{
	JsonArray *values;

	values = e_o365_contact_get_email_addresses (o365_contact);

	if (values) {
		EVCard *vcard = E_VCARD (inout_contact);
		guint ii, len;

		len = json_array_get_length (values);

		for (ii = 0; ii < len; ii++) {
			EO365EmailAddress *address = json_array_get_object_element (values, len - ii - 1);

			if (address) {
				EVCardAttribute *attr;

				attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
				e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "OTHER");

				if (g_strcmp0 (e_o365_email_address_get_name (address), e_o365_email_address_get_address (address)) == 0) {
					e_vcard_add_attribute_with_value (vcard, attr, e_o365_email_address_get_address (address));
				} else {
					gchar *formatted;

					formatted = camel_internet_address_format_address (
						e_o365_email_address_get_name (address),
						e_o365_email_address_get_address (address));

					if (formatted && *formatted)
						e_vcard_add_attribute_with_value (vcard, attr, formatted);
					else
						e_vcard_attribute_free (attr);

					g_free (formatted);
				}
			}
		}
	}

	return TRUE;
}

static gboolean
ebb_o365_parse_qp_email (const gchar *string,
			 gchar **name,
			 gchar **email)
{
	struct _camel_header_address *address;
	gboolean res = FALSE;

	address = camel_header_address_decode (string, "UTF-8");

	if (address) {
		/* report success only when we have filled both name and email address */
		if (address->type == CAMEL_HEADER_ADDRESS_NAME && address->name && *address->name && address->v.addr && *address->v.addr) {
			*name = g_strdup (address->name);
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
			*name = g_strdup (const_name);
			*email = g_strdup (const_email);
			res = TRUE;
		}

		g_clear_object (&addr);
	}

	return res;
}

static gboolean
ebb_o365_contact_add_emails (EBookBackendO365 *bbo365,
			     EContact *new_contact,
			     EContact *old_contact,
			     EContactField field_id,
			     const gchar *o365_id,
			     JsonBuilder *builder,
			     GCancellable *cancellable,
			     GError **error)
{
	GList *new_values, *old_values;

	new_values = e_contact_get (new_contact, field_id);
	old_values = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_o365_string_list_values_equal (new_values, old_values)) {
		GList *link;

		e_o365_contact_begin_email_addresses (builder);

		for (link = new_values; link; link = g_list_next (link)) {
			const gchar *value = link->data;
			gchar *name = NULL, *address = NULL;

			if (ebb_o365_parse_qp_email (value, &name, &address))
				e_o365_add_email_address (builder, name, address);
			else
				e_o365_add_email_address (builder, NULL, value);

			g_free (name);
			g_free (address);
		}

		e_o365_contact_end_email_addresses (builder);
	}

	g_list_free_full (new_values, g_free);
	g_list_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_o365_contact_add_file_as (EBookBackendO365 *bbo365,
			      EContact *new_contact,
			      EContact *old_contact,
			      EContactField field_id,
			      const gchar *o365_id,
			      JsonBuilder *builder,
			      GCancellable *cancellable,
			      GError **error)
{
	const gchar *new_value;

	ebb_o365_contact_add_string_attribute (new_contact, old_contact, field_id, builder, e_o365_contact_add_file_as);

	new_value = e_contact_get_const (new_contact, E_CONTACT_FILE_AS);

	/* Set it always, to not be overwritten by server re-calculations on other property changes */
	e_o365_contact_add_display_name (builder, new_value);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_generation (EBookBackendO365 *bbo365,
				 EO365Contact *o365_contact,
				 EContact *inout_contact,
				 EContactField field_id,
				 EO365Connection *cnc,
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *value;

	value = e_o365_contact_get_generation (o365_contact);

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
ebb_o365_contact_add_generation (EBookBackendO365 *bbo365,
				 EContact *new_contact,
				 EContact *old_contact,
				 EContactField field_id,
				 const gchar *o365_id,
				 JsonBuilder *builder,
				 GCancellable *cancellable,
				 GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->suffixes, old_value->suffixes) == 0))
		e_o365_contact_add_generation (builder, new_value ? new_value->suffixes : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_im_addresses (EBookBackendO365 *bbo365,
				   EO365Contact *o365_contact,
				   EContact *inout_contact,
				   EContactField field_id,
				   EO365Connection *cnc,
				   GCancellable *cancellable,
				   GError **error)
{
	JsonArray *values;

	values = e_o365_contact_get_im_addresses (o365_contact);

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
ebb_o365_extract_im_addresses (EContact *contact)
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
ebb_o365_contact_add_im_addresses (EBookBackendO365 *bbo365,
				   EContact *new_contact,
				   EContact *old_contact,
				   EContactField field_id,
				   const gchar *o365_id,
				   JsonBuilder *builder,
				   GCancellable *cancellable,
				   GError **error)
{
	GSList *new_values, *old_values;

	new_values = ebb_o365_extract_im_addresses (new_contact);
	old_values = ebb_o365_extract_im_addresses (old_contact);

	if (!ebb_o365_string_values_equal (new_values, old_values)) {
		GSList *link;

		e_o365_contact_begin_im_addresses (builder);

		for (link = new_values; link; link = g_slist_next (link)) {
			const gchar *value = link->data;

			if (value && *value)
				e_o365_contact_add_im_address (builder, value);
		}

		e_o365_contact_end_im_addresses (builder);
	}

	g_slist_free_full (new_values, g_free);
	g_slist_free_full (old_values, g_free);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_middle_name (EBookBackendO365 *bbo365,
				  EO365Contact *o365_contact,
				  EContact *inout_contact,
				  EContactField field_id,
				  EO365Connection *cnc,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *value;

	value = e_o365_contact_get_middle_name (o365_contact);

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
ebb_o365_contact_add_middle_name (EBookBackendO365 *bbo365,
				  EContact *new_contact,
				  EContact *old_contact,
				  EContactField field_id,
				  const gchar *o365_id,
				  JsonBuilder *builder,
				  GCancellable *cancellable,
				  GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->additional, old_value->additional) == 0))
		e_o365_contact_add_middle_name (builder, new_value ? new_value->additional : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_title (EBookBackendO365 *bbo365,
			    EO365Contact *o365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EO365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	const gchar *value;

	value = e_o365_contact_get_title (o365_contact);

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
ebb_o365_contact_add_title (EBookBackendO365 *bbo365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *o365_id,
			    JsonBuilder *builder,
			    GCancellable *cancellable,
			    GError **error)
{
	EContactName *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!(new_value && old_value && g_strcmp0 (new_value->prefixes, old_value->prefixes) == 0))
		e_o365_contact_add_title (builder, new_value ? new_value->prefixes : NULL);

	e_contact_name_free (new_value);
	e_contact_name_free (old_value);

	return TRUE;
}

static gboolean
ebb_o365_contact_get_photo (EBookBackendO365 *bbo365,
			    EO365Contact *o365_contact,
			    EContact *inout_contact,
			    EContactField field_id,
			    EO365Connection *cnc,
			    GCancellable *cancellable,
			    GError **error)
{
	GByteArray *photo_data = NULL;
	GError *local_error = NULL;

	LOCK (bbo365);

	if (e_o365_connection_get_contact_photo_sync (cnc, NULL, bbo365->priv->folder_id,
		e_o365_contact_get_id (o365_contact), &photo_data, cancellable, &local_error) &&
	    photo_data && photo_data->len) {
		EContactPhoto *photo;

		photo = e_contact_photo_new ();
		e_contact_photo_set_inlined (photo, photo_data->data, photo_data->len);
		e_contact_photo_set_mime_type (photo, "image/jpeg");
		e_contact_set (inout_contact, field_id, photo);
		e_contact_photo_free (photo);
	}

	UNLOCK (bbo365);

	if (photo_data)
		g_byte_array_unref (photo_data);
	g_clear_error (&local_error);

	/* Even it could fail, ignore it and read as many contacts as possible, rather than stop on the first error */
	return TRUE;
}

static gboolean
ebb_o365_contact_photo_equal (EContactPhoto *photo1,
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

	return len1 == len2 &&
		memcmp (data1, data2, len1) == 0;
}

static gboolean
ebb_o365_contact_add_photo (EBookBackendO365 *bbo365,
			    EContact *new_contact,
			    EContact *old_contact,
			    EContactField field_id,
			    const gchar *o365_id,
			    JsonBuilder *builder,
			    GCancellable *cancellable,
			    GError **error)
{
	EContactPhoto *new_value, *old_value;

	new_value = e_contact_get (new_contact, field_id);
	old_value = old_contact ? e_contact_get (old_contact, field_id) : NULL;

	if (!ebb_o365_contact_photo_equal (new_value, old_value)) {
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

		LOCK (bbo365);

		if (!e_o365_connection_update_contact_photo_sync (bbo365->priv->cnc, NULL, bbo365->priv->folder_id,
			o365_id ? o365_id : e_contact_get_const (new_contact, E_CONTACT_UID), jpeg_photo, cancellable, &local_error)) {
			if (local_error) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}
		}

		UNLOCK (bbo365);

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
	const gchar *	(* o365_get_func)	(EO365Contact *o365_contact);
	gboolean	(* get_func)		(EBookBackendO365 *bbo365,
						 EO365Contact *o365_contact,
						 EContact *inout_contact,
						 EContactField field_id,
						 EO365Connection *cnc,
						 GCancellable *cancellable,
						 GError **error);
	void		(* o365_add_func)	(JsonBuilder *builder,
						 const gchar *value);
	gboolean	(* add_func)		(EBookBackendO365 *bbo365,
						 EContact *new_contact,
						 EContact *old_contact, /* nullable */
						 EContactField field_id,
						 const gchar *o365_id,
						 JsonBuilder *builder,
						 GCancellable *cancellable,
						 GError **error);
} mappings[] = {
	STRING_FIELD	(E_CONTACT_UID,			e_o365_contact_get_id,			NULL),
	COMPLEX_FIELD	(E_CONTACT_REV,			ebb_o365_contact_get_rev,		NULL),
	STRING_FIELD	(E_CONTACT_ASSISTANT,		e_o365_contact_get_assistant_name,	e_o365_contact_add_assistant_name),
	COMPLEX_FIELD	(E_CONTACT_BIRTH_DATE,		ebb_o365_contact_get_birthday,		ebb_o365_contact_add_birthday),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_WORK,	ebb_o365_contact_get_address,		ebb_o365_contact_add_address),
	STRING_FIELD	(E_CONTACT_HOMEPAGE_URL,	e_o365_contact_get_business_home_page,	e_o365_contact_add_business_home_page),
	COMPLEX_FIELD	(E_CONTACT_PHONE_BUSINESS,	ebb_o365_contact_get_phone,		ebb_o365_contact_add_phone),
	COMPLEX_FIELD	(E_CONTACT_CATEGORIES,		ebb_o365_contact_get_categories,	ebb_o365_contact_add_categories),
	STRING_FIELD	(E_CONTACT_ORG,			e_o365_contact_get_company_name,	e_o365_contact_add_company_name),
	STRING_FIELD	(E_CONTACT_ORG_UNIT,		e_o365_contact_get_department,		e_o365_contact_add_department),
	COMPLEX_FIELD	(E_CONTACT_EMAIL,		ebb_o365_contact_get_emails,		ebb_o365_contact_add_emails),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_o365_contact_get_generation,	ebb_o365_contact_add_generation),
	STRING_FIELD	(E_CONTACT_GIVEN_NAME,		e_o365_contact_get_given_name,		e_o365_contact_add_given_name),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_HOME,	ebb_o365_contact_get_address,		ebb_o365_contact_add_address),
	COMPLEX_FIELD	(E_CONTACT_PHONE_HOME,		ebb_o365_contact_get_phone,		ebb_o365_contact_add_phone),
	COMPLEX_FIELD	(E_CONTACT_IM_MSN,		ebb_o365_contact_get_im_addresses,	ebb_o365_contact_add_im_addresses),
	/* STRING_FIELD	(???,				e_o365_contact_get_initials,		e_o365_contact_add_initials), */
	STRING_FIELD	(E_CONTACT_TITLE,		e_o365_contact_get_job_title,		e_o365_contact_add_job_title),
	STRING_FIELD	(E_CONTACT_MANAGER,		e_o365_contact_get_manager,		e_o365_contact_add_manager),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_o365_contact_get_middle_name,	ebb_o365_contact_add_middle_name),
	STRING_FIELD	(E_CONTACT_PHONE_MOBILE,	e_o365_contact_get_mobile_phone,	e_o365_contact_add_mobile_phone),
	STRING_FIELD	(E_CONTACT_NICKNAME,		e_o365_contact_get_nick_name,		e_o365_contact_add_nick_name),
	STRING_FIELD	(E_CONTACT_OFFICE,		e_o365_contact_get_office_location,	e_o365_contact_add_office_location),
	COMPLEX_FIELD	(E_CONTACT_ADDRESS_OTHER,	ebb_o365_contact_get_address,		ebb_o365_contact_add_address),
	STRING_FIELD	(E_CONTACT_NOTE,		e_o365_contact_get_personal_notes,	e_o365_contact_add_personal_notes),
	STRING_FIELD 	(E_CONTACT_ROLE,		e_o365_contact_get_profession,		e_o365_contact_add_profession),
	STRING_FIELD 	(E_CONTACT_SPOUSE,		e_o365_contact_get_spouse_name,		e_o365_contact_add_spouse_name),
	STRING_FIELD	(E_CONTACT_FAMILY_NAME,		e_o365_contact_get_surname,		e_o365_contact_add_surname),
	COMPLEX_FIELD	(E_CONTACT_NAME,		ebb_o365_contact_get_title,		ebb_o365_contact_add_title),
	/* STRING_FIELD	(???,				e_o365_contact_get_yomi_company_name,	e_o365_contact_add_yomi_company_name), */
	/* STRING_FIELD	(???,				e_o365_contact_get_yomi_given_name,	e_o365_contact_add_yomi_given_name), */
	/* STRING_FIELD	(???,				e_o365_contact_get_yomi_surname,	e_o365_contact_add_yomi_surname), */
	COMPLEX_ADDFN	(E_CONTACT_FILE_AS,		e_o365_contact_get_file_as,		ebb_o365_contact_add_file_as),
	COMPLEX_FIELD_2	(E_CONTACT_PHOTO,		ebb_o365_contact_get_photo,		ebb_o365_contact_add_photo)
};

static EContact *
ebb_o365_json_contact_to_vcard (EBookBackendO365 *bbo365,
				EO365Contact *o365_contact,
				EO365Connection *cnc,
				gchar **out_object,
				GCancellable *cancellable,
				GError **error)
{
	EContact *contact;
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (o365_contact != NULL, NULL);
	g_return_val_if_fail (out_object != NULL, NULL);

	*out_object = NULL;

	contact = e_contact_new ();

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].o365_get_func) {
			ebb_o365_contact_get_string_attribute (o365_contact, contact, mappings[ii].field_id, mappings[ii].o365_get_func);
		} else if (mappings[ii].get_func) {
			success = mappings[ii].get_func (bbo365, o365_contact, contact, mappings[ii].field_id, cnc, cancellable, error);
		}
	}

	if (success)
		*out_object = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	else
		g_clear_object (&contact);

	return contact;
}

static JsonBuilder *
ebb_o365_contact_to_json_locked (EBookBackendO365 *bbo365,
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
	e_o365_json_begin_object_member (builder, NULL);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].o365_add_func) {
			ebb_o365_contact_add_string_attribute (new_contact, old_contact, mappings[ii].field_id, builder, mappings[ii].o365_add_func);
		} else if (!mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (bbo365, new_contact, old_contact, mappings[ii].field_id, NULL, builder, cancellable, error);
		}
	}

	e_o365_json_end_object_member (builder);

	if (!success)
		g_clear_object (&builder);

	return builder;
}

static gboolean
ebb_o365_contact_to_json_2nd_go_locked (EBookBackendO365 *bbo365,
					EContact *new_contact,
					EContact *old_contact, /* nullable */
					const gchar *o365_id,
					GCancellable *cancellable,
					GError **error)
{
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (new_contact != NULL, FALSE);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (bbo365, new_contact, old_contact, mappings[ii].field_id, o365_id, NULL, cancellable, error);
		}
	}

	return success;
}

static void
ebb_o365_convert_error_to_client_error (GError **perror)
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

static void
ebb_o365_maybe_disconnect_sync (EBookBackendO365 *bbo365,
			        GError **in_perror,
			        GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_O365 (bbo365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_book_meta_backend_disconnect_sync (E_BOOK_META_BACKEND (bbo365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (bbo365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gboolean
ebb_o365_unset_connection_sync (EBookBackendO365 *bbo365,
				gboolean is_disconnect,
				GCancellable *cancellable,
				GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (bbo365), FALSE);

	LOCK (bbo365);

	if (bbo365->priv->cnc) {
		if (is_disconnect)
			success = e_o365_connection_disconnect_sync (bbo365->priv->cnc, cancellable, error);
	}

	g_clear_object (&bbo365->priv->cnc);
	g_clear_pointer (&bbo365->priv->folder_id, g_free);

	UNLOCK (bbo365);

	return success;
}

static gboolean
ebb_o365_connect_sync (EBookMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	LOCK (bbo365);

	if (bbo365->priv->cnc) {
		UNLOCK (bbo365);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	} else {
		EBackend *backend;
		ESourceRegistry *registry;
		ESource *source;
		EO365Connection *cnc;
		ESourceO365Folder *o365_folder_extension;
		CamelO365Settings *o365_settings;
		gchar *folder_id;

		backend = E_BACKEND (bbo365);
		source = e_backend_get_source (backend);
		registry = e_book_backend_get_registry (E_BOOK_BACKEND (bbo365));
		o365_settings = camel_o365_settings_get_from_backend (backend, registry);
		g_warn_if_fail (o365_settings != NULL);

		o365_folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_O365_FOLDER);
		folder_id = e_source_o365_folder_dup_id (o365_folder_extension);

		if (folder_id) {
			cnc = e_o365_connection_new_for_backend (backend, registry, source, o365_settings);

			*out_auth_result = e_o365_connection_authenticate_sync (cnc, NULL, E_O365_FOLDER_KIND_CONTACTS, folder_id,
				out_certificate_pem, out_certificate_errors, cancellable, error);

			if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
				bbo365->priv->cnc = g_object_ref (cnc);

				g_warn_if_fail (bbo365->priv->folder_id == NULL);

				g_free (bbo365->priv->folder_id);
				bbo365->priv->folder_id = folder_id;

				folder_id = NULL;
				success = TRUE;

				e_book_backend_set_writable (E_BOOK_BACKEND (bbo365), TRUE);
			}
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Folder ID is not set")));
		}

		g_clear_object (&cnc);
		g_free (folder_id);
	}

	UNLOCK (bbo365);

	ebb_o365_convert_error_to_client_error (error);

	return success;
}

static gboolean
ebb_o365_disconnect_sync (EBookMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	return ebb_o365_unset_connection_sync (E_BOOK_BACKEND_O365 (meta_backend), TRUE, cancellable, error);
}

typedef struct _ObjectsDeltaData {
	EBookBackendO365 *bbo365;
	ECache *cache;
	GSList **out_created_objects;
	GSList **out_modified_objects;
	GSList **out_removed_objects;
} ObjectsDeltaData;

static gboolean
ebb_o365_get_objects_delta_cb (EO365Connection *cnc,
			       const GSList *results, /* JsonObject * - the returned objects from the server */
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	ObjectsDeltaData *odd = user_data;
	GSList *link;

	g_return_val_if_fail (odd != NULL, FALSE);

	for (link = (GSList *) results; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
		EO365Contact *contact = link->data;
		const gchar *id;

		if (!contact)
			continue;

		id = e_o365_contact_get_id (contact);

		if (!id)
			continue;

		if (e_o365_delta_is_removed_object (contact)) {
			*(odd->out_removed_objects) = g_slist_prepend (*(odd->out_removed_objects),
				e_book_meta_backend_info_new (id, NULL, NULL, NULL));
		} else {
			GSList **out_slist;
			EContact *vcard;
			gchar *object;

			if (e_cache_contains (odd->cache, id, E_CACHE_INCLUDE_DELETED))
				out_slist = odd->out_modified_objects;
			else
				out_slist = odd->out_created_objects;

			vcard = ebb_o365_json_contact_to_vcard (odd->bbo365, contact, cnc, &object, cancellable, error);

			g_clear_object (&vcard);

			if (!g_cancellable_is_cancelled (cancellable))
				g_warn_if_fail (object != NULL);

			if (object) {
				EBookMetaBackendInfo *nfo;

				nfo = e_book_meta_backend_info_new (id,
					e_o365_contact_get_change_key (contact),
					object, NULL);

				nfo->extra = object; /* assumes ownership, to avoid unnecessary re-allocation */

				*out_slist = g_slist_prepend (*out_slist, nfo);
			}
		}
	}

	return TRUE;
}

static gboolean
ebb_o365_get_changes_sync (EBookMetaBackend *meta_backend,
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
	EBookBackendO365 *bbo365;
	EBookCache *book_cache;
	ObjectsDeltaData odd;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), FALSE);

	odd.bbo365 = bbo365;
	odd.cache = E_CACHE (book_cache);
	odd.out_created_objects = out_created_objects;
	odd.out_modified_objects = out_modified_objects;
	odd.out_removed_objects = out_removed_objects;

	LOCK (bbo365);

	success = e_o365_connection_get_objects_delta_sync (bbo365->priv->cnc, NULL,
		E_O365_FOLDER_KIND_CONTACTS, bbo365->priv->folder_id, NULL, last_sync_tag, 0,
		ebb_o365_get_objects_delta_cb, &odd,
		out_new_sync_tag, cancellable, &local_error);

	if (e_o365_connection_util_delta_token_failed (local_error)) {
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

		success = e_o365_connection_get_objects_delta_sync (bbo365->priv->cnc, NULL,
			E_O365_FOLDER_KIND_CONTACTS, bbo365->priv->folder_id, NULL, NULL, 0,
			ebb_o365_get_objects_delta_cb, &odd,
			out_new_sync_tag, cancellable, &local_error);
	} else if (local_error) {
		g_propagate_error (error, local_error);
	}

	UNLOCK (bbo365);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	g_clear_object (&book_cache);

	return success;
}

static gboolean
ebb_o365_load_contact_sync (EBookMetaBackend *meta_backend,
			    const gchar *uid,
			    const gchar *extra,
			    EContact **out_contact,
			    gchar **out_extra,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendO365 *bbo365;
	EO365Contact *contact = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	LOCK (bbo365);

	success = e_o365_connection_get_contact_sync (bbo365->priv->cnc, NULL,
		bbo365->priv->folder_id, uid, &contact, cancellable, error);

	if (success) {
		*out_contact = ebb_o365_json_contact_to_vcard (bbo365, contact, bbo365->priv->cnc, out_extra, cancellable, error);

		if (contact)
			json_object_unref (contact);
	}

	UNLOCK (bbo365);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	return success;
}

static gboolean
ebb_o365_save_contact_sync (EBookMetaBackend *meta_backend,
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
	EBookBackendO365 *bbo365;
	EContact *tmp_contact = NULL, *old_contact = NULL;
	JsonBuilder *builder;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	if (GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_IS_LIST))) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED, _("Cannot save contact list into an Office 365 address book")));
		return FALSE;
	}

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	LOCK (bbo365);

	if (e_vcard_get_attribute (E_VCARD (contact), EVC_PHOTO)) {
		tmp_contact = e_contact_duplicate (contact);
		contact = tmp_contact;

		e_contact_inline_local_photos (contact, NULL);
	}

	if (extra && *extra)
		old_contact = e_contact_new_from_vcard (extra);

	builder = ebb_o365_contact_to_json_locked (bbo365, contact, old_contact, cancellable, error);

	if (builder) {
		if (overwrite_existing) {
			const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);

			success = e_o365_connection_update_contact_sync (bbo365->priv->cnc, NULL, bbo365->priv->folder_id,
				uid, builder, cancellable, error);

			if (success)
				success = ebb_o365_contact_to_json_2nd_go_locked (bbo365, contact, old_contact, uid, cancellable, error);

			if (success)
				*out_new_extra = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		} else {
			EO365Contact *created_contact = NULL;

			success = e_o365_connection_create_contact_sync (bbo365->priv->cnc, NULL, bbo365->priv->folder_id,
				builder,  &created_contact, cancellable, error);

			if (success && created_contact) {
				const gchar *o365_id = e_o365_contact_get_id (created_contact);

				success = ebb_o365_contact_to_json_2nd_go_locked (bbo365, contact, old_contact, o365_id, cancellable, error);
			}

			if (success && created_contact) {
				EContact *vcard;

				*out_new_uid = g_strdup (e_o365_contact_get_id (created_contact));

				vcard = ebb_o365_json_contact_to_vcard (bbo365, created_contact, bbo365->priv->cnc, out_new_extra, cancellable, error);
				g_clear_object (&vcard);
			}

			if (created_contact)
				json_object_unref (created_contact);
		}

		g_clear_object (&builder);
	}

	UNLOCK (bbo365);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	g_clear_object (&old_contact);
	g_clear_object (&tmp_contact);

	return success;
}

static gboolean
ebb_o365_remove_contact_sync (EBookMetaBackend *meta_backend,
			      EConflictResolution conflict_resolution,
			      const gchar *uid,
			      const gchar *extra,
			      const gchar *object,
			      guint32 opflags,
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	LOCK (bbo365);

	success = e_o365_connection_delete_contact_sync (bbo365->priv->cnc, NULL,
		bbo365->priv->folder_id, uid, cancellable, error);

	UNLOCK (bbo365);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	return success;
}

static gboolean
ebb_o365_search_sync (EBookMetaBackend *meta_backend,
		      const gchar *expr,
		      gboolean meta_contact,
		      GSList **out_contacts,
		      GCancellable *cancellable,
		      GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	/*ebb_o365_update_cache_for_expression (E_BOOK_BACKEND_O365 (meta_backend), expr, cancellable, NULL);*/

	/* Chain up to parent's method */
	if (!E_BOOK_META_BACKEND_CLASS (e_book_backend_o365_parent_class)->search_sync (meta_backend, expr, meta_contact,
		out_contacts, cancellable, error))
		return FALSE;

	return TRUE;
}

static gboolean
ebb_o365_search_uids_sync (EBookMetaBackend *meta_backend,
			   const gchar *expr,
			   GSList **out_uids,
			   GCancellable *cancellable,
			   GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	/*ebb_o365_update_cache_for_expression (E_BOOK_BACKEND_O365 (meta_backend), expr, cancellable, NULL);*/

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_o365_parent_class)->search_uids_sync (meta_backend, expr,
		out_uids, cancellable, error);
}

static gchar *
ebb_o365_get_backend_property (EBookBackend *book_backend,
			       const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			"net",
			"contact-lists",
			"do-initial-query",
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
	return E_BOOK_BACKEND_CLASS (e_book_backend_o365_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static gboolean
ebb_o365_get_destination_address (EBackend *backend,
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
e_book_backend_o365_dispose (GObject *object)
{
	EBookBackendO365 *bbo365 = E_BOOK_BACKEND_O365 (object);

	ebb_o365_unset_connection_sync (bbo365, FALSE, NULL, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_o365_parent_class)->dispose (object);
}

static void
e_book_backend_o365_finalize (GObject *object)
{
	EBookBackendO365 *bbo365 = E_BOOK_BACKEND_O365 (object);

	g_rec_mutex_clear (&bbo365->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_o365_parent_class)->finalize (object);
}

static void
e_book_backend_o365_init (EBookBackendO365 *bbo365)
{
	bbo365->priv = e_book_backend_o365_get_instance_private (bbo365);

	g_rec_mutex_init (&bbo365->priv->property_lock);
}

static void
e_book_backend_o365_class_init (EBookBackendO365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;
	EBookMetaBackendClass *book_meta_backend_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendoffice365.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendO365Factory";
	book_meta_backend_class->connect_sync = ebb_o365_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_o365_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_o365_get_changes_sync;
	book_meta_backend_class->load_contact_sync = ebb_o365_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_o365_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_o365_remove_contact_sync;
	book_meta_backend_class->search_sync = ebb_o365_search_sync;
	book_meta_backend_class->search_uids_sync = ebb_o365_search_uids_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = ebb_o365_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ebb_o365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_book_backend_o365_dispose;
	object_class->finalize = e_book_backend_o365_finalize;
}
