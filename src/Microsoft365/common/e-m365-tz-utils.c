/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <gio/gio.h>

#include "e-ews-common-utils.h"

#include "e-m365-tz-utils.h"

/*
 * A bunch of global variables used to map the ICalTimezone to MSDN[0] format.
 * Also, some auxiliar functions to translate from one tz type to another.
 *
 * [0]: http://msdn.microsoft.com/en-us/library/ms912391(v=winembedded.11).aspx
 */
static GRecMutex tz_mutex;
static GHashTable *ical_to_msdn = NULL;
static GHashTable *msdn_to_ical = NULL;
static guint tables_counter = 0;

void
e_m365_tz_utils_ref_windows_zones (void)
{
	const gchar *xpath_eval_exp;
	gchar *filename = NULL;
	xmlDocPtr doc;
	xmlXPathContextPtr xpath_ctxt;
	xmlXPathObjectPtr xpath_obj;
	xmlNodeSetPtr nodes;
	gint i, len;

	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL && msdn_to_ical != NULL) {
		g_hash_table_ref (ical_to_msdn);
		g_hash_table_ref (msdn_to_ical);
		tables_counter++;

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	filename = g_build_filename (M365_DATADIR, "windowsZones.xml", NULL);

	doc = xmlReadFile (filename, NULL, 0);

	if (doc == NULL) {
		g_warning (G_STRLOC "Could not map %s file.", filename);
		g_free (filename);

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	xpath_eval_exp = "/supplementalData/windowsZones/mapTimezones/mapZone";

	xpath_ctxt = xmlXPathNewContext (doc);
	xpath_obj = xmlXPathEvalExpression (BAD_CAST xpath_eval_exp, xpath_ctxt);

	if (xpath_obj == NULL) {
		g_warning (G_STRLOC "Unable to evaluate xpath expression \"%s\".", xpath_eval_exp);
		xmlXPathFreeContext (xpath_ctxt);
		xmlFreeDoc (doc);
		g_free (filename);

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	nodes = xpath_obj->nodesetval;
	len = nodes->nodeNr;

	msdn_to_ical = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	ical_to_msdn = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	tables_counter++;

	for (i = 0; i < len; i++) {
		xmlChar *msdn = xmlGetProp (nodes->nodeTab[i], BAD_CAST "other");
		xmlChar *ical = xmlGetProp (nodes->nodeTab[i], BAD_CAST "type");
		gchar **tokens;
		gint tokens_len;

		tokens = g_strsplit ((gchar *) ical, " ", 0);
		tokens_len = g_strv_length (tokens);
		if (tokens_len == 1) {
			if (!g_hash_table_lookup (msdn_to_ical, msdn))
				g_hash_table_insert (msdn_to_ical, g_strdup ((gchar *) msdn), g_strdup ((gchar *) ical));

			if (!g_hash_table_lookup (ical_to_msdn, ical))
				g_hash_table_insert (ical_to_msdn, g_strdup ((gchar *) ical), g_strdup ((gchar *) msdn));
		} else {
			gint j;
			for (j = 0; j < tokens_len; j++) {
				if (!g_hash_table_lookup (msdn_to_ical, msdn))
					g_hash_table_insert (msdn_to_ical, g_strdup ((gchar *) msdn), g_strdup (tokens[j]));

				if (!g_hash_table_lookup (ical_to_msdn, tokens[j]))
					g_hash_table_insert (ical_to_msdn, g_strdup (tokens[j]), g_strdup ((gchar *) msdn));
			}
		}

		g_strfreev (tokens);
		xmlFree (ical);
		xmlFree (msdn);
	}

	xmlXPathFreeObject (xpath_obj);
	xmlXPathFreeContext (xpath_ctxt);
	xmlFreeDoc (doc);
	g_free (filename);

	g_rec_mutex_unlock (&tz_mutex);
}

void
e_m365_tz_utils_unref_windows_zones (void)
{
	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL)
		g_hash_table_unref (ical_to_msdn);

	if (msdn_to_ical != NULL)
		g_hash_table_unref (msdn_to_ical);

	if (tables_counter > 0) {
		tables_counter--;

		if (tables_counter == 0) {
			ical_to_msdn = NULL;
			msdn_to_ical = NULL;
		}
	}

	g_rec_mutex_unlock (&tz_mutex);
}

const gchar *
e_m365_tz_utils_get_msdn_equivalent (const gchar *ical_tz_location)
{
	const gchar *msdn_tz_location = NULL;

	if (!ical_tz_location || !*ical_tz_location)
		return NULL;

	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn == NULL) {
		g_rec_mutex_unlock (&tz_mutex);

		g_warn_if_reached ();
		return NULL;
	}

	msdn_tz_location = g_hash_table_lookup (ical_to_msdn, ical_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return msdn_tz_location;
}

const gchar *
e_m365_tz_utils_get_ical_equivalent (const gchar *msdn_tz_location)
{
	const gchar *ical_tz_location = NULL;

	if (!msdn_tz_location || !*msdn_tz_location)
		return NULL;

	g_rec_mutex_lock (&tz_mutex);
	if (msdn_to_ical == NULL) {
		g_rec_mutex_unlock (&tz_mutex);

		g_warn_if_reached ();
		return NULL;
	}

	ical_tz_location = g_hash_table_lookup (msdn_to_ical, msdn_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return ical_tz_location;
}

ICalTimezone *
e_m365_tz_utils_get_user_timezone (void)
{
	gchar *location = NULL;
	ICalTimezone *zone = NULL;

	if (e_ews_common_utils_gsettings_schema_exists ("org.gnome.evolution.calendar")) {
		GSettings *settings;

		settings = g_settings_new ("org.gnome.evolution.calendar");

		if (g_settings_get_boolean (settings, "use-system-timezone"))
			location = e_cal_util_get_system_timezone_location ();
		else
			location = g_settings_get_string (settings, "timezone");

		g_object_unref (settings);
	}

	if (!location)
		location = e_cal_util_get_system_timezone_location ();

	if (location)
		zone = i_cal_timezone_get_builtin_timezone (location);

	g_free (location);

	return zone;
}
