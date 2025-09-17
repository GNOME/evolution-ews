/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>
#include <libedata-cal/libedata-cal.h>

#include "e-ews-common-utils.h"

/* This is copy of e_mail_folder_uri_parse(), to not depend on the evolution code
   in the library code (and to not bring gtk+ into random processes). */
gboolean
e_ews_common_utils_mail_folder_uri_parse (CamelSession *session,
					  const gchar *folder_uri,
					  CamelStore **out_store,
					  gchar **out_folder_name,
					  GError **error)
{
	CamelURL *url;
	CamelService *service = NULL;
	gchar *folder_name = NULL;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (folder_uri != NULL, FALSE);

	url = camel_url_new (folder_uri, error);
	if (url == NULL)
		return FALSE;

	/* Current URI Format: 'folder://' STORE_UID '/' FOLDER_PATH */
	if (g_strcmp0 (url->protocol, "folder") == 0) {

		if (url->host != NULL) {
			gchar *uid;

			if (url->user == NULL || *url->user == '\0')
				uid = g_strdup (url->host);
			else
				uid = g_strconcat (
					url->user, "@", url->host, NULL);

			service = camel_session_ref_service (session, uid);
			g_free (uid);
		}

		if (url->path != NULL && *url->path == '/')
			folder_name = camel_url_decode_path (url->path + 1);

	/* This style was used to reference accounts by UID before
	 * CamelServices themselves had UIDs.  Some examples are:
	 *
	 * Special cases:
	 *
	 *   'email://local@local/' FOLDER_PATH
	 *   'email://vfolder@local/' FOLDER_PATH
	 *
	 * General case:
	 *
	 *   'email://' ACCOUNT_UID '/' FOLDER_PATH
	 *
	 * Note: ACCOUNT_UID is now equivalent to STORE_UID, and
	 *       the STORE_UIDs for the special cases are 'local'
	 *       and 'vfolder'.
	 */
	} else if (g_strcmp0 (url->protocol, "email") == 0) {
		gchar *uid = NULL;

		/* Handle the special cases. */
		if (g_strcmp0 (url->host, "local") == 0) {
			if (g_strcmp0 (url->user, "local") == 0)
				uid = g_strdup ("local");
			if (g_strcmp0 (url->user, "vfolder") == 0)
				uid = g_strdup ("vfolder");
		}

		/* Handle the general case. */
		if (uid == NULL && url->host != NULL) {
			if (url->user == NULL)
				uid = g_strdup (url->host);
			else
				uid = g_strdup_printf (
					"%s@%s", url->user, url->host);
		}

		if (uid != NULL) {
			service = camel_session_ref_service (session, uid);
			g_free (uid);
		}

		if (url->path != NULL && *url->path == '/')
			folder_name = camel_url_decode_path (url->path + 1);

	}

	if (CAMEL_IS_STORE (service) && folder_name != NULL) {
		if (out_store != NULL)
			*out_store = CAMEL_STORE (g_object_ref (service));

		if (out_folder_name != NULL) {
			*out_folder_name = folder_name;
			folder_name = NULL;
		}

		success = TRUE;
	} else {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID,
			_("Invalid folder URI “%s”"),
			folder_uri);
	}

	if (service != NULL)
		g_object_unref (service);

	g_free (folder_name);

	camel_url_free (url);

	return success;
}

/* This is copy of e_str_replace_string(), to not depend on the evolution code
   in the library code (and to not bring gtk+ into random processes). */
GString *
e_ews_common_utils_str_replace_string (const gchar *text,
				       const gchar *before,
				       const gchar *after)
{
	const gchar *p, *next;
	GString *str;
	gint find_len;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (before != NULL, NULL);
	g_return_val_if_fail (*before, NULL);

	find_len = strlen (before);
	str = g_string_new ("");

	p = text;
	while (next = strstr (p, before), next) {
		if (p < next)
			g_string_append_len (str, p, next - p);

		if (after && *after)
			g_string_append (str, after);

		p = next + find_len;
	}

	return g_string_append (str, p);
}

/* This corresponds to calendar_config_get_icaltimezone(), to not depend on the evolution code
   in the library code (and to not bring gtk+ into random processes). */
ICalTimezone *
e_ews_common_utils_get_configured_icaltimezone (void)
{
	ICalTimezone *zone = NULL;
	gchar *location = NULL;

	if (e_ews_common_utils_gsettings_schema_exists ("org.gnome.evolution.calendar")) {
		GSettings *settings;

		settings = g_settings_new ("org.gnome.evolution.calendar");

		if (g_settings_get_boolean (settings, "use-system-timezone"))
			location = e_cal_util_get_system_timezone_location ();
		else
			location = g_settings_get_string (settings, "timezone");

		g_clear_object (&settings);
	}

	if (!location)
		location = e_cal_util_get_system_timezone_location ();

	if (location) {
		zone = i_cal_timezone_get_builtin_timezone (location);

		g_free (location);
	}

	return zone;
}

static ECalComponentAttendee *
get_attendee (GSList *attendees,
	      const gchar *address,
	      GHashTable *aliases)
{
	GSList *l;

	if (!address)
		return NULL;

	for (l = attendees; l; l = l->next) {
		ECalComponentAttendee *attendee = l->data;
		const gchar *nomailto;

		nomailto = e_cal_util_get_attendee_email (attendee);

		if (!nomailto || !*nomailto)
			continue;

		if ((address && e_cal_util_email_addresses_equal (nomailto, address)) ||
		    (aliases && g_hash_table_contains (aliases, nomailto))) {
			return attendee;
		}
	}

	return NULL;
}

static ECalComponentAttendee *
get_attendee_if_attendee_sentby_is_user (GSList *attendees,
					 const gchar *address,
					 GHashTable *aliases)
{
	GSList *l;

	for (l = attendees; l; l = l->next) {
		ECalComponentAttendee *attendee = l->data;
		const gchar *nomailto;

		nomailto = e_cal_util_strip_mailto (e_cal_component_attendee_get_sentby (attendee));
		if (!nomailto || !*nomailto)
			continue;

		if ((address && e_cal_util_email_addresses_equal (nomailto, address)) ||
		    (aliases && g_hash_table_contains (aliases, nomailto))) {
			return attendee;
		}
	}

	return NULL;
}

/* This is adapted copy of itip_get_comp_attendee(), to avoid dependency
   on the evolution code in the backend. */
static gchar *
eecu_get_comp_attendee (ESourceRegistry *registry,
			ECalComponent *comp,
			ECalBackend *cal_backend)
{
	ESource *source;
	GSList *attendees;
	ECalComponentAttendee *attendee = NULL;
	GList *list, *link;
	const gchar *extension_name;
	gchar *address = NULL;

	attendees = e_cal_component_get_attendees (comp);

	if (cal_backend)
		address = e_cal_backend_get_backend_property (cal_backend, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS);

	if (address != NULL && *address != '\0') {
		attendee = get_attendee (attendees, address, NULL);

		if (attendee) {
			gchar *user_email;

			user_email = g_strdup (e_cal_util_get_attendee_email (attendee));
			g_slist_free_full (attendees, e_cal_component_attendee_free);
			g_free (address);

			return user_email;
		}

		attendee = get_attendee_if_attendee_sentby_is_user (attendees, address, NULL);

		if (attendee != NULL) {
			gchar *user_email;

			user_email = g_strdup (e_cal_util_strip_mailto (e_cal_component_attendee_get_sentby (attendee)));
			g_slist_free_full (attendees, e_cal_component_attendee_free);
			g_free (address);

			return user_email;
		}
	}

	g_free (address);
	address = NULL;

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	list = e_source_registry_list_enabled (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESourceMailIdentity *extension;
		GHashTable *aliases;

		source = E_SOURCE (link->data);

		extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
		extension = e_source_get_extension (source, extension_name);
		address = e_source_mail_identity_dup_address (extension);
		aliases = e_source_mail_identity_get_aliases_as_hash_table (extension);

		attendee = get_attendee (attendees, address, aliases);
		if (attendee != NULL) {
			gchar *user_email;

			user_email = g_strdup (e_cal_util_get_attendee_email (attendee));
			g_slist_free_full (attendees, e_cal_component_attendee_free);

			if (aliases)
				g_hash_table_destroy (aliases);
			g_free (address);

			g_list_free_full (list, g_object_unref);

			return user_email;
		}

		/* If the account was not found in the attendees list, then
		 * let's check the 'sentby' fields of the attendees if we can
		 * find the account. */
		attendee = get_attendee_if_attendee_sentby_is_user (attendees, address, aliases);
		if (attendee) {
			gchar *user_email;

			user_email = g_strdup (e_cal_util_strip_mailto (e_cal_component_attendee_get_sentby (attendee)));
			g_slist_free_full (attendees, e_cal_component_attendee_free);

			if (aliases)
				g_hash_table_destroy (aliases);
			g_free (address);

			g_list_free_full (list, g_object_unref);

			return user_email;
		}

		if (aliases)
			g_hash_table_destroy (aliases);
		g_free (address);
	}

	g_list_free_full (list, g_object_unref);

	/* We could not find the attendee in the component, so just give
	 * the default account address if the email address is not set in
	 * the backend. */
	/* FIXME do we have a better way ? */
	e_cal_util_get_default_name_and_address (registry, NULL, &address);

	g_slist_free_full (attendees, e_cal_component_attendee_free);

	return address;
}

static ICalProperty *
find_attendee (ICalComponent *icomp,
	       const gchar *address,
	       GHashTable *aliases)
{
	ICalProperty *prop;

	if (address == NULL)
		return NULL;

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
		gchar *attendee;
		gchar *text;

		attendee = i_cal_property_get_value_as_string (prop);

		 if (!attendee)
			continue;

		text = g_strdup (e_cal_util_strip_mailto (attendee));
		text = g_strstrip (text);
		if (text && ((!g_ascii_strcasecmp (address, text)) ||
		    (aliases && g_hash_table_contains (aliases, text)))) {
			g_free (text);
			g_free (attendee);
			break;
		}
		g_free (text);
		g_free (attendee);
	}

	return prop;
}

static ICalProperty *
find_attendee_if_sentby (ICalComponent *icomp,
			 const gchar *address,
			 GHashTable *aliases)
{
	ICalProperty *prop;

	if (address == NULL)
		return NULL;

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
		ICalParameter *param;
		const gchar *attendee_sentby;
		gchar *text;

		param = i_cal_property_get_first_parameter (prop, I_CAL_SENTBY_PARAMETER);
		if (!param)
			continue;

		attendee_sentby = i_cal_parameter_get_sentby (param);

		if (!attendee_sentby) {
			g_object_unref (param);
			continue;
		}

		text = g_strdup (e_cal_util_strip_mailto (attendee_sentby));
		text = g_strstrip (text);

		g_object_unref (param);

		if (text && ((!g_ascii_strcasecmp (address, text)) ||
		    (aliases && g_hash_table_contains (aliases, text)))) {
			g_free (text);
			break;
		}
		g_free (text);
	}

	return prop;
}

ICalProperty * /* (transfer full) */
e_ews_common_utils_find_attendee (ECalBackend *cal_backend,
				  ICalComponent *icomp,
				  GHashTable *aliases)
{
	ECalComponent *comp;
	ICalProperty *attendee = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND (cal_backend), NULL);
	g_return_val_if_fail (I_CAL_IS_COMPONENT (icomp), NULL);

	comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));
	if (comp) {
		ESourceRegistry *registry;
		gchar *my_address;

		registry = e_cal_backend_get_registry (cal_backend);
		my_address = eecu_get_comp_attendee (registry, comp, cal_backend);

		attendee = find_attendee (icomp, my_address ? my_address : "", aliases);
		if (!attendee)
			attendee = find_attendee_if_sentby (icomp, my_address ? my_address : "", aliases);

		g_free (my_address);
		g_object_unref (comp);
	}

	return attendee;
}

gboolean
e_ews_common_utils_gsettings_schema_exists (const gchar *schema_id)
{
	GSettingsSchema *schema;

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (), schema_id, TRUE);

	if (!schema)
		return FALSE;

	g_settings_schema_unref (schema);

	return TRUE;
}

GHashTable *
e_ews_common_utils_dup_mail_addresses (ESourceRegistry *registry,
				       ESource *child_source,
				       gchar **inout_user_email)
{
	GHashTable *aliases = NULL;
	GList *identities, *link;
	const gchar *parent_uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (child_source), NULL);

	parent_uid = e_source_get_parent (child_source);

	if (!parent_uid || !*parent_uid)
		return NULL;

	identities = e_source_registry_list_enabled (registry, E_SOURCE_EXTENSION_MAIL_IDENTITY);

	for (link = identities; link; link = g_list_next (link)) {
		ESource *mail_identity = link->data;

		if (g_strcmp0 (parent_uid, e_source_get_parent (mail_identity)) == 0) {
			ESourceMailIdentity *extension;
			gchar *address;

			extension = e_source_get_extension (mail_identity, E_SOURCE_EXTENSION_MAIL_IDENTITY);
			aliases = e_source_mail_identity_get_aliases_as_hash_table (extension);

			address = e_source_mail_identity_dup_address (extension);
			if (address && *address) {
				if (inout_user_email && (!*inout_user_email || !**inout_user_email)) {
					g_free (*inout_user_email);
					*inout_user_email = g_strdup (address);
				}

				if (!aliases)
					aliases = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, g_free);
				g_hash_table_insert (aliases, address, e_source_mail_identity_dup_name (extension));
			} else {
				g_free (address);
			}
			break;
		}
	}

	g_list_free_full (identities, g_object_unref);

	return aliases;
}
