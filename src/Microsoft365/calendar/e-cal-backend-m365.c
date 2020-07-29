/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>
#include <libecal/libecal.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-m365-tz-utils.h"
#include "common/e-source-m365-folder.h"

#include "e-cal-backend-m365.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

#define LOCK(_cb) g_rec_mutex_lock (&_cb->priv->property_lock)
#define UNLOCK(_cb) g_rec_mutex_unlock (&_cb->priv->property_lock)

struct _ECalBackendM365Private {
	GRecMutex property_lock;
	EM365Connection *cnc;
	gchar *group_id;
	gchar *calendar_id;
	gchar *attachments_dir;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendM365, e_cal_backend_m365, E_TYPE_CAL_META_BACKEND)

static void
ecb_m365_convert_error_to_client_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror ||
	    (*perror)->domain == E_CLIENT_ERROR ||
	    (*perror)->domain == E_CAL_CLIENT_ERROR)
		return;

	/*if ((*perror)->domain == M365_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case M365_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_FOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_EVENTNOTFOUND:
		case M365_CONNECTION_ERROR_ITEMNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_UNAVAILABLE:
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

static const gchar *
ecb_m365_get_component_from_extra (const gchar *extra)
{
	gchar *enter;

	if (!extra)
		return NULL;

	enter = strchr (extra, '\n');

	if (!enter)
		return NULL;

	return enter + 1;
}

/* Modifies inout_extra, cannot be called multiple times with the same arguments */
static void
ecb_m365_split_extra (gchar *inout_extra,
		      const gchar **out_change_key,
		      const gchar **out_ical_comp)
{
	gchar *enter;

	if (!inout_extra)
		return;

	enter = (gchar *) ecb_m365_get_component_from_extra (inout_extra);
	g_return_if_fail (enter != NULL);

	enter[-1] = '\0';

	if (out_change_key)
		*out_change_key = inout_extra;

	if (out_ical_comp)
		*out_ical_comp = enter;
}

static void
ecb_m365_get_uid (ECalBackendM365 *cbm365,
		  EM365Event *m365_event,
		  ICalComponent *inout_comp,
		  ICalPropertyKind prop_kind)
{
	i_cal_component_set_uid (inout_comp, e_m365_event_get_id (m365_event));
}

static void
ecb_m365_get_date_time (ECalBackendM365 *cbm365,
			EM365Event *m365_event,
			ICalComponent *inout_comp,
			ICalPropertyKind prop_kind)
{
	time_t tt = (time_t) 0;

	if (prop_kind == I_CAL_CREATED_PROPERTY)
		tt = e_m365_event_get_created_date_time (m365_event);
	else if (prop_kind == I_CAL_LASTMODIFIED_PROPERTY)
		tt = e_m365_event_get_last_modified_date_time (m365_event);
	else
		g_warn_if_reached ();

	if (tt > (time_t) 0) {
		ICalProperty *prop;
		ICalTime *itt;

		itt = i_cal_time_new_from_timet_with_zone (tt, FALSE, i_cal_timezone_get_utc_timezone ());

		if (prop_kind == I_CAL_CREATED_PROPERTY)
			prop = i_cal_property_new_created (itt);
		else /* I_CAL_LASTMODIFIED_PROPERTY */
			prop = i_cal_property_new_lastmodified (itt);

		i_cal_component_take_property (inout_comp, prop);

		g_clear_object (&itt);
	}
}

static ICalTimezone *
ecb_m365_get_timezone_sync (ECalBackendM365 *cbm365,
			    const gchar *tzid)
{
	ICalTimezone *zone;
	ECalCache *cal_cache;

	if (!tzid)
		return NULL;

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbm365));

	if (!cal_cache)
		return NULL;

	zone = e_cal_cache_resolve_timezone_cb (tzid, cal_cache, NULL, NULL);

	g_object_unref (cal_cache);

	return zone;
}

static void
ecb_m365_get_date_time_zone (ECalBackendM365 *cbm365,
			     EM365Event *m365_event,
			     ICalComponent *inout_comp,
			     ICalPropertyKind prop_kind)
{
	EM365DateTimeWithZone *value = NULL;
	ICalTimezone *tz;
	ICalTime *itt;
	time_t tt;
	const gchar *tzid, *zone;

	if (prop_kind == I_CAL_DTSTART_PROPERTY) {
		value = e_m365_event_get_start (m365_event);
		tzid = e_m365_event_get_original_start_timezone (m365_event);
	} else if (prop_kind == I_CAL_DTEND_PROPERTY) {
		value = e_m365_event_get_end (m365_event);
		tzid = e_m365_event_get_original_end_timezone (m365_event);
	} else {
		g_warn_if_reached ();
		return;
	}

	if (!value)
		return;

	tt = e_m365_date_time_get_date_time (value);
	zone = e_m365_date_time_get_time_zone (value);

	if (zone && *zone)
		zone = e_m365_tz_utils_get_ical_equivalent (zone);

	tz = zone && *zone ? ecb_m365_get_timezone_sync (cbm365, zone) : NULL;

	if (!tz)
		tz = i_cal_timezone_get_utc_timezone ();

	itt = i_cal_time_new_from_timet_with_zone (tt, e_m365_event_get_is_all_day (m365_event), tz);

	tzid = e_m365_tz_utils_get_ical_equivalent (tzid);

	if (!tzid)
		tzid = "UTC";

	tz = ecb_m365_get_timezone_sync (cbm365, tzid);

	if (tz && !e_m365_event_get_is_all_day (m365_event))
		i_cal_time_convert_to_zone_inplace (itt, tz);

	if (prop_kind == I_CAL_DTSTART_PROPERTY)
		i_cal_component_set_dtstart (inout_comp, itt);
	else /* I_CAL_DTEND_PROPERTY */
		i_cal_component_set_dtend (inout_comp, itt);

	g_clear_object (&itt);
}

static void
ecb_m365_add_date_time_zone (ECalBackendM365 *cbm365,
			     ICalComponent *new_comp,
			     ICalComponent *old_comp,
			     ICalPropertyKind prop_kind,
			     JsonBuilder *builder)
{
	ICalProperty *new_prop;
	ICalParameter *new_param;
	ICalTime *old_value, *new_value;
	const gchar *new_tzid = NULL;
	void (* add_func) (JsonBuilder *builder, time_t date_time, const gchar *zone) = NULL;
	gboolean same = FALSE;

	if (prop_kind == I_CAL_DTSTART_PROPERTY) {
		new_value = i_cal_component_get_dtstart (new_comp);
		old_value = old_comp ? i_cal_component_get_dtstart (old_comp) : NULL;
		add_func = e_m365_event_add_start;
	} else if (prop_kind == I_CAL_DTEND_PROPERTY) {
		new_value = i_cal_component_get_dtend (new_comp);
		old_value = old_comp ? i_cal_component_get_dtend (old_comp) : NULL;
		add_func = e_m365_event_add_end;
	} else {
		g_warn_if_reached ();
		return;
	}

	if (!new_value && !old_value)
		return;

	new_prop = i_cal_component_get_first_property (new_comp, prop_kind);
	new_param = new_prop ? i_cal_property_get_first_parameter (new_prop, I_CAL_TZID_PARAMETER) : NULL;

	if (new_param)
		new_tzid = i_cal_parameter_get_tzid (new_param);

	if (new_value && old_value) {
		same = i_cal_time_compare (new_value, old_value) == 0;

		if (same) {
			ICalProperty *old_prop;
			ICalParameter *old_param;
			const gchar *old_tzid;

			old_prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;
			old_param = old_prop ? i_cal_property_get_first_parameter (old_prop, I_CAL_TZID_PARAMETER) : NULL;
			old_tzid = old_param ? i_cal_parameter_get_tzid (old_param) : NULL;

			same = g_strcmp0 (old_tzid, new_tzid) == 0;

			g_clear_object (&old_param);
			g_clear_object (&old_prop);
		}
	}

	if (!same) {
		ICalTimezone *izone = NULL;
		const gchar *wzone = NULL;
		time_t tt;

		if (new_tzid) {
			izone = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (cbm365), new_tzid);

			if (izone)
				wzone = e_m365_tz_utils_get_msdn_equivalent (i_cal_timezone_get_location (izone));
		}

		tt = i_cal_time_as_timet_with_zone (new_value, wzone ? NULL : izone);

		add_func (builder, tt, wzone);
	}

	g_clear_object (&new_prop);
	g_clear_object (&new_param);
	g_clear_object (&new_value);
	g_clear_object (&old_value);
}

static void
ecb_m365_get_categories (ECalBackendM365 *cbm365,
			 EM365Event *m365_event,
			 ICalComponent *inout_comp,
			 ICalPropertyKind prop_kind)
{
	JsonArray *categories;

	categories = e_m365_event_get_categories (m365_event);

	if (categories) {
		GString *categories_str = NULL;
		guint ii, len;

		len = json_array_get_length (categories);

		for (ii = 0; ii < len; ii++) {
			const gchar *category;

			category = json_array_get_string_element (categories, ii);

			if (category && *category) {
				gchar *ical_str = i_cal_value_encode_ical_string (category);

				if (ical_str && *ical_str) {
					if (!categories_str) {
						categories_str = g_string_new (ical_str);
					} else {
						g_string_append_c (categories_str, ',');
						g_string_append (categories_str, ical_str);
					}
				}

				g_free (ical_str);
			}
		}

		if (categories_str) {
			i_cal_component_take_property (inout_comp, i_cal_property_new_categories (categories_str->str));

			g_string_free (categories_str, TRUE);
		}
	}
}

static void
ecb_m365_extract_categories (ICalComponent *comp,
			     GHashTable **out_hash, /* gchar * ~> NULL */
			     GSList **out_slist) /* gchar * */
{
	ICalProperty *prop;

	if (!comp)
		return;

	for (prop = i_cal_component_get_first_property (comp, I_CAL_CATEGORIES_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_CATEGORIES_PROPERTY)) {
		const gchar *categories;

		categories = i_cal_property_get_categories (prop);

		if (categories && *categories) {
			if (out_hash && !*out_hash)
				*out_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

			if (strchr (categories, ',')) {
				gchar **strv;
				guint ii;

				strv = g_strsplit (categories, ",", -1);

				for (ii = 0; strv[ii]; ii++) {
					gchar *category = g_strchomp (strv[ii]);

					if (*category) {
						if (out_hash) {
							g_hash_table_insert (*out_hash, category, NULL);
						} else if (out_slist) {
							*out_slist = g_slist_prepend (*out_slist, category);
						} else {
							g_warn_if_reached ();
							g_free (category);
						}
					} else {
						g_free (category);
					}
				}

				g_free (strv);
			} else if (out_hash) {
				g_hash_table_insert (*out_hash, g_strchomp (g_strdup (categories)), NULL);
			} else if (out_slist) {
				*out_slist = g_slist_prepend (*out_slist, g_strchomp (g_strdup (categories)));
			} else {
				g_warn_if_reached ();
			}
		}
	}

	g_clear_object (&prop);

	if (out_slist && *out_slist)
		*out_slist = g_slist_reverse (*out_slist);
}

static void
ecb_m365_add_categories (ECalBackendM365 *cbm365,
			 ICalComponent *new_comp,
			 ICalComponent *old_comp,
			 ICalPropertyKind prop_kind,
			 JsonBuilder *builder)
{
	GHashTable *old_value = NULL;
	GSList *new_value = NULL;

	ecb_m365_extract_categories (new_comp, NULL, &new_value);
	ecb_m365_extract_categories (old_comp, &old_value, NULL);

	if (!new_value && !old_value)
		return;

	if (new_value) {
		GSList *link;
		gboolean same = FALSE;

		if (old_value && g_hash_table_size (old_value) == g_slist_length (new_value)) {
			same = TRUE;

			for (link = new_value; link && same; link = g_slist_next (link)) {
				const gchar *category = link->data;

				same = g_hash_table_contains (old_value, category);
			}
		}

		if (!same) {
			e_m365_event_begin_categories (builder);

			for (link = new_value; link; link = g_slist_next (link)) {
				const gchar *category = link->data;

				e_m365_event_add_category (builder, category);
			}

			e_m365_event_end_categories (builder);
		}
	} else {
		e_m365_event_begin_categories (builder);
		e_m365_event_end_categories (builder);
	}

	if (new_value)
		g_slist_free_full (new_value, g_free);
	if (old_value)
		g_hash_table_destroy (old_value);
}

static void
ecb_m365_get_subject (ECalBackendM365 *cbm365,
		      EM365Event *m365_event,
		      ICalComponent *inout_comp,
		      ICalPropertyKind prop_kind)
{
	const gchar *subject;

	subject = e_m365_event_get_subject (m365_event);

	if (subject)
		i_cal_component_set_summary (inout_comp, subject);
}

static void
ecb_m365_add_subject (ECalBackendM365 *cbm365,
		      ICalComponent *new_comp,
		      ICalComponent *old_comp,
		      ICalPropertyKind prop_kind,
		      JsonBuilder *builder)
{
	const gchar *new_value, *old_value;

	new_value = i_cal_component_get_summary (new_comp);
	old_value = old_comp ? i_cal_component_get_summary (old_comp) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0)
		e_m365_event_add_subject (builder, new_value ? new_value : "");
}

static void
ecb_m365_get_body (ECalBackendM365 *cbm365,
		   EM365Event *m365_event,
		   ICalComponent *inout_comp,
		   ICalPropertyKind prop_kind)
{
	EM365ItemBody *value;
	const gchar *content;

	value = e_m365_event_get_body (m365_event);
	content = value ? e_m365_item_body_get_content (value) : NULL;

	if (content && *content && strcmp (content, "\r\n") != 0)
		i_cal_component_set_description (inout_comp, content);
}

static void
ecb_m365_add_body (ECalBackendM365 *cbm365,
		   ICalComponent *new_comp,
		   ICalComponent *old_comp,
		   ICalPropertyKind prop_kind,
		   JsonBuilder *builder)
{
	const gchar *new_value, *old_value;

	new_value = i_cal_component_get_description (new_comp);
	old_value = old_comp ? i_cal_component_get_description (old_comp) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0)
		e_m365_event_add_body (builder, E_M365_ITEM_BODY_CONTENT_TYPE_TEXT, new_value);
}

static void
ecb_m365_get_sensitivity (ECalBackendM365 *cbm365,
			  EM365Event *m365_event,
			  ICalComponent *inout_comp,
			  ICalPropertyKind prop_kind)
{
	EM365SensitivityType value;
	ICalProperty_Class cls = I_CAL_CLASS_NONE;

	value = e_m365_event_get_sensitivity (m365_event);

	if (value == E_M365_SENSITIVITY_NORMAL)
		cls = I_CAL_CLASS_PUBLIC;
	else if (value == E_M365_SENSITIVITY_PERSONAL || value == E_M365_SENSITIVITY_PRIVATE)
		cls = I_CAL_CLASS_PRIVATE;
	else if (value == E_M365_SENSITIVITY_CONFIDENTIAL)
		cls = I_CAL_CLASS_CONFIDENTIAL;

	if (cls != I_CAL_CLASS_NONE)
		i_cal_component_take_property (inout_comp, i_cal_property_new_class (cls));
}

static void
ecb_m365_add_sensitivity (ECalBackendM365 *cbm365,
			  ICalComponent *new_comp,
			  ICalComponent *old_comp,
			  ICalPropertyKind prop_kind,
			  JsonBuilder *builder)
{
	ICalProperty_Class new_value = I_CAL_CLASS_NONE, old_value = I_CAL_CLASS_NONE;
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (new_comp, prop_kind);

	if (prop) {
		new_value = i_cal_property_get_class (prop);
		g_clear_object (&prop);
	}

	prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (prop) {
		old_value = i_cal_property_get_class (prop);
		g_clear_object (&prop);
	}

	if (new_value != old_value) {
		EM365SensitivityType value = E_M365_SENSITIVITY_NOT_SET;

		if (new_value == I_CAL_CLASS_PUBLIC)
			value = E_M365_SENSITIVITY_NORMAL;
		else if (new_value == I_CAL_CLASS_PRIVATE)
			value = E_M365_SENSITIVITY_PRIVATE;
		else if (new_value == I_CAL_CLASS_CONFIDENTIAL)
			value = E_M365_SENSITIVITY_CONFIDENTIAL;

		e_m365_event_add_sensitivity (builder, value);
	}
}

static void
ecb_m365_get_show_as (ECalBackendM365 *cbm365,
		      EM365Event *m365_event,
		      ICalComponent *inout_comp,
		      ICalPropertyKind prop_kind)
{
	EM365FreeBusyStatusType value;
	ICalPropertyTransp transp = I_CAL_TRANSP_NONE;

	value = e_m365_event_get_show_as (m365_event);

	if (value == E_M365_FREE_BUSY_STATUS_FREE)
		transp = I_CAL_TRANSP_TRANSPARENT;
	else if (value == E_M365_FREE_BUSY_STATUS_BUSY)
		transp = I_CAL_TRANSP_OPAQUE;

	if (transp != I_CAL_TRANSP_NONE)
		i_cal_component_take_property (inout_comp, i_cal_property_new_transp (transp));
}

static void
ecb_m365_add_show_as (ECalBackendM365 *cbm365,
		      ICalComponent *new_comp,
		      ICalComponent *old_comp,
		      ICalPropertyKind prop_kind,
		      JsonBuilder *builder)
{
	ICalPropertyTransp new_value = I_CAL_TRANSP_NONE, old_value = I_CAL_TRANSP_NONE;
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (new_comp, prop_kind);

	if (prop) {
		new_value = i_cal_property_get_transp (prop);
		g_clear_object (&prop);
	}

	prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (prop) {
		old_value = i_cal_property_get_transp (prop);
		g_clear_object (&prop);
	}

	if (new_value != old_value) {
		EM365FreeBusyStatusType value = E_M365_FREE_BUSY_STATUS_NOT_SET;

		if (new_value == I_CAL_TRANSP_TRANSPARENT)
			value = E_M365_FREE_BUSY_STATUS_FREE;
		else if (new_value == I_CAL_TRANSP_OPAQUE)
			value = E_M365_FREE_BUSY_STATUS_BUSY;

		e_m365_event_add_show_as (builder, value);
	}
}

static void
ecb_m365_get_location (ECalBackendM365 *cbm365,
		       EM365Event *m365_event,
		       ICalComponent *inout_comp,
		       ICalPropertyKind prop_kind)
{
	EM365Location *value;
	const gchar *tmp;

	value = e_m365_event_get_location (m365_event);

	if (!value)
		return;

	tmp = e_m365_location_get_display_name (value);

	if (tmp && *tmp)
		i_cal_component_set_location (inout_comp, tmp);
}

static void
ecb_m365_add_location (ECalBackendM365 *cbm365,
		       ICalComponent *new_comp,
		       ICalComponent *old_comp,
		       ICalPropertyKind prop_kind,
		       JsonBuilder *builder)
{
	const gchar *new_value, *old_value;

	new_value = i_cal_component_get_location (new_comp);
	old_value = old_comp ? i_cal_component_get_location (old_comp) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0) {
		if (new_value && *new_value) {
			e_m365_event_begin_location (builder);
			e_m365_location_add_display_name (builder, new_value);
			e_m365_event_end_location (builder);
		} else {
			e_m365_event_add_null_location (builder);
		}
	}
}

static void
ecb_m365_get_organizer (ECalBackendM365 *cbm365,
			EM365Event *m365_event,
			ICalComponent *inout_comp,
			ICalPropertyKind prop_kind)
{
	EM365Recipient *value;
	JsonArray *attendees;
	const gchar *name;
	const gchar *address;

	value = e_m365_event_get_organizer (m365_event);

	if (!value)
		return;

	/* Include the organizer only if there is at least one attendee */
	attendees = e_m365_event_get_attendees (m365_event);

	if (!attendees || !json_array_get_length (attendees))
		return;

	name = e_m365_recipient_get_name (value);
	address = e_m365_recipient_get_address (value);

	if (address && *address) {
		ECalComponentOrganizer *organizer;
		gchar *mailto_addr;

		mailto_addr = g_strconcat ("mailto:", address, NULL);
		organizer = e_cal_component_organizer_new ();
		e_cal_component_organizer_set_value (organizer, mailto_addr);
		g_free (mailto_addr);

		if (name && *name)
			e_cal_component_organizer_set_cn (organizer, name);

		i_cal_component_take_property (inout_comp, e_cal_component_organizer_get_as_property (organizer));
		e_cal_component_organizer_free (organizer);
	}
}

static const gchar *
ecb_m365_strip_mailto (const gchar *value)
{
	if (value && g_ascii_strncasecmp (value, "mailto:", 7) == 0)
		return value + 7;

	return value;
}

static void
ecb_m365_add_organizer (ECalBackendM365 *cbm365,
			ICalComponent *new_comp,
			ICalComponent *old_comp,
			ICalPropertyKind prop_kind,
			JsonBuilder *builder)
{
	ECalComponentOrganizer *new_value = NULL, *old_value = NULL;
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (new_comp, prop_kind);

	if (prop) {
		new_value = e_cal_component_organizer_new_from_property (prop);
		g_clear_object (&prop);
	}

	prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (prop) {
		old_value = e_cal_component_organizer_new_from_property (prop);
		g_clear_object (&prop);
	}

	if (new_value != old_value && (
	    g_strcmp0 (new_value ? e_cal_component_organizer_get_cn (new_value) : NULL,
		       old_value ? e_cal_component_organizer_get_cn (old_value) : NULL) != 0 ||
	    g_strcmp0 (new_value ? ecb_m365_strip_mailto (e_cal_component_organizer_get_value (new_value)) : NULL,
		       old_value ? ecb_m365_strip_mailto (e_cal_component_organizer_get_value (old_value)) : NULL) != 0)) {
		if (new_value) {
			e_m365_event_add_organizer (builder,
						    e_cal_component_organizer_get_cn (new_value),
						    ecb_m365_strip_mailto (e_cal_component_organizer_get_value (new_value)));
		} else {
			e_m365_event_add_null_organizer (builder);
		}
	}

	e_cal_component_organizer_free (new_value);
	e_cal_component_organizer_free (old_value);
}

static void
ecb_m365_get_attendees (ECalBackendM365 *cbm365,
			EM365Event *m365_event,
			ICalComponent *inout_comp,
			ICalPropertyKind prop_kind)
{
	JsonArray *array;
	guint ii, sz;

	array = e_m365_event_get_attendees (m365_event);

	if (!array)
		return;

	sz = json_array_get_length (array);

	for (ii = 0; ii < sz; ii++) {
		EM365Attendee *m365_attendee;
		EM365ResponseStatus *m365_status;
		EM365AttendeeType m365_att_type;
		EM365EmailAddress *m365_address;
		ECalComponentAttendee *e_attendee;
		ICalParameterRole role = I_CAL_ROLE_NONE;
		gchar *mailto_addr;

		m365_attendee = json_array_get_object_element (array, ii);

		if (!m365_attendee)
			continue;

		m365_address = e_m365_attendee_get_email_address (m365_attendee);

		if (!m365_address || !e_m365_email_address_get_address (m365_address))
			continue;

		e_attendee = e_cal_component_attendee_new ();

		mailto_addr = g_strconcat ("mailto:", e_m365_email_address_get_address (m365_address), NULL);
		e_cal_component_attendee_set_value (e_attendee, mailto_addr);
		g_free (mailto_addr);

		if (e_m365_email_address_get_name (m365_address))
			e_cal_component_attendee_set_cn (e_attendee, e_m365_email_address_get_name (m365_address));

		m365_status = e_m365_attendee_get_status (m365_attendee);

		if (m365_status) {
			EM365ResponseType m365_response;
			ICalParameterPartstat partstat = I_CAL_PARTSTAT_NONE;

			m365_response = e_m365_response_status_get_response (m365_status);

			if (m365_response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED)
				partstat = I_CAL_PARTSTAT_TENTATIVE;
			else if (m365_response == E_M365_RESPONSE_ACCEPTED)
				partstat = I_CAL_PARTSTAT_ACCEPTED;
			else if (m365_response == E_M365_RESPONSE_DECLINED)
				partstat = I_CAL_PARTSTAT_DECLINED;
			else if (m365_response == E_M365_RESPONSE_NOT_RESPONDED)
				partstat = I_CAL_PARTSTAT_NEEDSACTION;

			if (partstat != I_CAL_PARTSTAT_NONE) {
				time_t tt;

				e_cal_component_attendee_set_partstat (e_attendee, partstat);

				tt = e_m365_response_status_get_time (m365_status);

				if (tt > (time_t) 0) {
					ECalComponentParameterBag *params;
					ICalParameter *param;
					gchar *tmp;

					tmp = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) tt);
					params = e_cal_component_attendee_get_parameter_bag (e_attendee);

					param = i_cal_parameter_new_x (tmp);
					i_cal_parameter_set_xname (param, "X-M365-STATUS-TIME");

					e_cal_component_parameter_bag_take (params, param);

					g_free (tmp);
				}
			}
		}

		m365_att_type = e_m365_attendee_get_type (m365_attendee);

		if (m365_att_type == E_M365_ATTENDEE_REQUIRED) {
			role = I_CAL_ROLE_REQPARTICIPANT;
			e_cal_component_attendee_set_cutype (e_attendee, I_CAL_CUTYPE_INDIVIDUAL);
		} else if (m365_att_type == E_M365_ATTENDEE_OPTIONAL) {
			role = I_CAL_ROLE_OPTPARTICIPANT;
			e_cal_component_attendee_set_cutype (e_attendee, I_CAL_CUTYPE_INDIVIDUAL);
		} else if (m365_att_type == E_M365_ATTENDEE_RESOURCE) {
			e_cal_component_attendee_set_cutype (e_attendee, I_CAL_CUTYPE_RESOURCE);
		}

		if (role != I_CAL_ROLE_NONE)
			e_cal_component_attendee_set_role (e_attendee, role);

		i_cal_component_take_property (inout_comp, e_cal_component_attendee_get_as_property (e_attendee));

		e_cal_component_attendee_free (e_attendee);
	}
}

static void
ecb_m365_extract_attendees (ICalComponent *comp,
			    GHashTable **out_hash, /* const gchar *ECalComponentAttendee::value ~> ECalComponentAttendee * */
			    GSList **out_slist) /* ECalComponentAttendee * */
{
	ICalProperty *prop;

	if (!comp)
		return;

	for (prop = i_cal_component_get_first_property (comp, I_CAL_ATTENDEE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_ATTENDEE_PROPERTY)) {
		ECalComponentAttendee *attendee;

		attendee = e_cal_component_attendee_new_from_property (prop);

		if (attendee && e_cal_component_attendee_get_value (attendee)) {
			if (out_hash) {
				if (!*out_hash)
					*out_hash = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, NULL, e_cal_component_attendee_free);

				g_hash_table_insert (*out_hash, (gpointer) e_cal_component_attendee_get_value (attendee), attendee);
			} else if (out_slist) {
				*out_slist = g_slist_prepend (*out_slist, attendee);
			} else {
				g_warn_if_reached ();
				e_cal_component_attendee_free (attendee);
			}
		} else {
			e_cal_component_attendee_free (attendee);
		}
	}

	g_clear_object (&prop);

	if (out_slist && *out_slist)
		*out_slist = g_slist_reverse (*out_slist);
}

static void
ecb_m365_add_attendees (ECalBackendM365 *cbm365,
			ICalComponent *new_comp,
			ICalComponent *old_comp,
			ICalPropertyKind prop_kind,
			JsonBuilder *builder)
{
	GHashTable *old_value = NULL;
	GSList *new_value = NULL;

	ecb_m365_extract_attendees (new_comp, NULL, &new_value);
	ecb_m365_extract_attendees (old_comp, &old_value, NULL);

	if (!new_value && !old_value)
		return;

	if (new_value) {
		GSList *link;
		gboolean same = FALSE;

		if (old_value && g_hash_table_size (old_value) == g_slist_length (new_value)) {
			same = TRUE;

			for (link = new_value; link && same; link = g_slist_next (link)) {
				ECalComponentAttendee *new_att = link->data, *old_att;

				old_att = g_hash_table_lookup (old_value, e_cal_component_attendee_get_value (new_att));

				same = old_att && e_cal_component_attendee_get_value (old_att) && e_cal_component_attendee_get_value (new_att) &&
					g_ascii_strcasecmp (e_cal_component_attendee_get_value (old_att), e_cal_component_attendee_get_value (new_att)) == 0 &&
					g_strcmp0 (e_cal_component_attendee_get_cn (old_att), e_cal_component_attendee_get_cn (new_att)) == 0 &&
					e_cal_component_attendee_get_partstat (old_att) == e_cal_component_attendee_get_partstat (new_att) &&
					e_cal_component_attendee_get_cutype (old_att) == e_cal_component_attendee_get_cutype (new_att) &&
					e_cal_component_attendee_get_role (old_att) == e_cal_component_attendee_get_role (new_att);
			}
		}

		if (!same) {
			e_m365_event_begin_attendees (builder);

			for (link = new_value; link; link = g_slist_next (link)) {
				ECalComponentAttendee *attendee = link->data;
				EM365AttendeeType att_type;
				EM365ResponseType response = E_M365_RESPONSE_NONE;
				time_t response_time = (time_t) 0;
				ICalParameterPartstat partstat;
				const gchar *address;

				address = ecb_m365_strip_mailto (e_cal_component_attendee_get_value (attendee));

				if (e_cal_component_attendee_get_cutype (attendee) == I_CAL_CUTYPE_RESOURCE)
					att_type = E_M365_ATTENDEE_RESOURCE;
				else if (e_cal_component_attendee_get_role (attendee) == I_CAL_ROLE_REQPARTICIPANT ||
					 e_cal_component_attendee_get_role (attendee) == I_CAL_ROLE_CHAIR)
					att_type = E_M365_ATTENDEE_REQUIRED;
				else if (e_cal_component_attendee_get_role (attendee) == I_CAL_ROLE_OPTPARTICIPANT)
					att_type = E_M365_ATTENDEE_OPTIONAL;
				else /* Fallback */
					att_type = E_M365_ATTENDEE_REQUIRED;

				partstat = e_cal_component_attendee_get_partstat (attendee);

				if (partstat == I_CAL_PARTSTAT_TENTATIVE)
					response = E_M365_RESPONSE_TENTATIVELY_ACCEPTED;
				else if (partstat == I_CAL_PARTSTAT_ACCEPTED)
					response = E_M365_RESPONSE_ACCEPTED;
				else if (partstat == I_CAL_PARTSTAT_DECLINED)
					response = E_M365_RESPONSE_DECLINED;
				else if (partstat == I_CAL_PARTSTAT_NEEDSACTION)
					response = E_M365_RESPONSE_NOT_RESPONDED;

				if (response != E_M365_RESPONSE_NONE) {
					ECalComponentParameterBag *params;
					guint ii, sz;

					params = e_cal_component_attendee_get_parameter_bag (attendee);
					sz = e_cal_component_parameter_bag_get_count (params);

					for (ii = 0; ii < sz; ii++) {
						ICalParameter *param;

						param = e_cal_component_parameter_bag_get (params, ii);

						if (param && i_cal_parameter_isa (param) == I_CAL_X_PARAMETER &&
						    i_cal_parameter_get_xname (param) &&
						    g_ascii_strcasecmp (i_cal_parameter_get_xname (param), "X-M365-STATUS-TIME") == 0) {
							const gchar *xvalue;

							xvalue = i_cal_parameter_get_xvalue (param);

							if (xvalue && *xvalue) {
								gint64 value;

								value = g_ascii_strtoll (xvalue, NULL, 10);

								if (value)
									response_time = (time_t) value;
							}
						}
					}
				}

				e_m365_event_add_attendee (builder, att_type, response, response_time, e_cal_component_attendee_get_cn (attendee), address);
			}

			e_m365_event_end_attendees (builder);
		}
	} else {
		e_m365_event_add_null_attendees (builder);
	}

	if (new_value)
		g_slist_free_full (new_value, e_cal_component_attendee_free);
	if (old_value)
		g_hash_table_destroy (old_value);
}

static void
ecb_m365_get_importance (ECalBackendM365 *cbm365,
			 EM365Event *m365_event,
			 ICalComponent *inout_comp,
			 ICalPropertyKind prop_kind)
{
	EM365ImportanceType value;
	ICalProperty *prop = NULL;

	value = e_m365_event_get_importance (m365_event);

	if (value == E_M365_IMPORTANCE_LOW)
		prop = i_cal_property_new_priority (9);
	else if (value == E_M365_IMPORTANCE_NORMAL)
		prop = i_cal_property_new_priority (5);
	else if (value == E_M365_IMPORTANCE_HIGH)
		prop = i_cal_property_new_priority (1);

	if (prop)
		i_cal_component_take_property (inout_comp, prop);
}

static void
ecb_m365_add_importance (ECalBackendM365 *cbm365,
			 ICalComponent *new_comp,
			 ICalComponent *old_comp,
			 ICalPropertyKind prop_kind,
			 JsonBuilder *builder)
{
	gint old_value = -1, new_value = -1;
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (new_comp, prop_kind);

	if (prop) {
		new_value = i_cal_property_get_priority (prop);
		g_clear_object (&prop);
	}

	prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (prop) {
		old_value = i_cal_property_get_priority (prop);
		g_clear_object (&prop);
	}

	if (new_value != old_value) {
		EM365ImportanceType value = E_M365_IMPORTANCE_NOT_SET;

		if (new_value >= 1 && new_value <= 4) {
			value = E_M365_IMPORTANCE_HIGH;
		} else if (new_value == 5) {
			value = E_M365_IMPORTANCE_NORMAL;
		} else if (new_value >= 6 && new_value <= 9) {
			value = E_M365_IMPORTANCE_LOW;
		}

		e_m365_event_add_importance (builder, value);
	}
}

static void
ecb_m365_get_status (ECalBackendM365 *cbm365,
		     EM365Event *m365_event,
		     ICalComponent *inout_comp,
		     ICalPropertyKind prop_kind)
{
	ICalPropertyStatus status = I_CAL_STATUS_NONE;

	if (e_m365_event_get_is_cancelled (m365_event)) {
		status = I_CAL_STATUS_CANCELLED;
	} else {
		EM365ResponseStatus *response_status;

		response_status = e_m365_event_get_response_status (m365_event);

		if (response_status) {
			EM365ResponseType response;

			response = e_m365_response_status_get_response (response_status);

			if (response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED)
				status = I_CAL_STATUS_TENTATIVE;
			else if (response == E_M365_RESPONSE_ACCEPTED)
				status = I_CAL_STATUS_CONFIRMED;
			else if (response == E_M365_RESPONSE_DECLINED)
				status = I_CAL_STATUS_CANCELLED;
			else if (response == E_M365_RESPONSE_NOT_RESPONDED)
				status = I_CAL_STATUS_NEEDSACTION;
		}
	}

	if (status != I_CAL_STATUS_NONE)
		i_cal_component_take_property (inout_comp, i_cal_property_new_status (status));
}

static ICalRecurrenceWeekday
ecb_m365_day_of_week_to_ical (EM365DayOfWeekType dow)
{
	switch (dow) {
	case E_M365_DAY_OF_WEEK_SUNDAY:
		return I_CAL_SUNDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_MONDAY:
		return I_CAL_MONDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_TUESDAY:
		return I_CAL_TUESDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_WEDNESDAY:
		return I_CAL_WEDNESDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_THURSDAY:
		return I_CAL_THURSDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_FRIDAY:
		return I_CAL_FRIDAY_WEEKDAY;
	case E_M365_DAY_OF_WEEK_SATURDAY:
		return I_CAL_SATURDAY_WEEKDAY;
	default:
		break;
	}

	return I_CAL_NO_WEEKDAY;
}

static EM365DayOfWeekType
ecb_m365_day_of_week_from_ical (ICalRecurrenceWeekday dow)
{
	switch (dow) {
	case I_CAL_SUNDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_SUNDAY;
		break;
	case I_CAL_MONDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_MONDAY;
		break;
	case I_CAL_TUESDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_TUESDAY;
		break;
	case I_CAL_WEDNESDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_WEDNESDAY;
		break;
	case I_CAL_THURSDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_THURSDAY;
		break;
	case I_CAL_FRIDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_FRIDAY;
		break;
	case I_CAL_SATURDAY_WEEKDAY:
		return E_M365_DAY_OF_WEEK_SATURDAY;
		break;
	default:
		break;
	}

	return E_M365_DAY_OF_WEEK_UNKNOWN;
}

static void
ecb_m365_set_index_to_ical (ICalRecurrence *recr,
			    EM365WeekIndexType index)
{
	gint by_pos = -2;

	switch (index) {
	case E_M365_WEEK_INDEX_FIRST:
		by_pos = 1;
		break;
	case E_M365_WEEK_INDEX_SECOND:
		by_pos = 2;
		break;
	case E_M365_WEEK_INDEX_THIRD:
		by_pos = 3;
		break;
	case E_M365_WEEK_INDEX_FOURTH:
		by_pos = 4;
		break;
	case E_M365_WEEK_INDEX_LAST:
		by_pos = -1;
		break;
	default:
		break;
	}

	if (by_pos != -2)
		i_cal_recurrence_set_by_set_pos (recr, 0, by_pos);
}

static void
ecb_m365_add_index_from_ical (JsonBuilder *builder,
			      gint by_pos)
{
	EM365WeekIndexType index = E_M365_WEEK_INDEX_UNKNOWN;

	if (by_pos == 1)
		index = E_M365_WEEK_INDEX_FIRST;
	else if (by_pos == 2)
		index = E_M365_WEEK_INDEX_SECOND;
	else if (by_pos == 3)
		index = E_M365_WEEK_INDEX_THIRD;
	else if (by_pos == 4)
		index = E_M365_WEEK_INDEX_FOURTH;
	else if (by_pos == -1)
		index = E_M365_WEEK_INDEX_LAST;

	if (index != E_M365_WEEK_INDEX_UNKNOWN)
		e_m365_recurrence_pattern_add_index (builder, index);
}

static void
ecb_m365_set_days_of_week_to_ical (ICalRecurrence *recr,
				   JsonArray *days_of_week)
{
	gint ii, jj, sz;

	if (!days_of_week)
		return;

	ii = 0;
	sz = json_array_get_length (days_of_week);

	for (jj = 0; jj < sz; jj++) {
		ICalRecurrenceWeekday week_day;

		week_day = ecb_m365_day_of_week_to_ical (e_m365_array_get_day_of_week_element (days_of_week, jj));

		if (week_day != I_CAL_SUNDAY_WEEKDAY) {
			i_cal_recurrence_set_by_day (recr, ii, week_day);
			ii++;
		}
	}

	i_cal_recurrence_set_by_day (recr, ii, I_CAL_RECURRENCE_ARRAY_MAX);
}

static void
ecb_m365_add_days_of_week_from_ical (JsonBuilder *builder,
				     ICalRecurrence *recr)
{
	gint ii;

	e_m365_recurrence_pattern_begin_days_of_week (builder);

	for (ii = 0; ii < I_CAL_BY_DAY_SIZE; ii++) {
		ICalRecurrenceWeekday week_day;
		EM365DayOfWeekType m365_week_day;

		week_day = i_cal_recurrence_get_by_day (recr, ii);

		if (((gint) week_day) == I_CAL_RECURRENCE_ARRAY_MAX)
			break;

		m365_week_day = ecb_m365_day_of_week_from_ical (week_day);

		if (m365_week_day != E_M365_DAY_OF_WEEK_UNKNOWN)
			e_m365_recurrence_pattern_add_day_of_week (builder, m365_week_day);
	}

	e_m365_recurrence_pattern_end_days_of_week (builder);
}

static gboolean
ecb_m365_get_recurrence (ECalBackendM365 *cbm365,
			 EM365Event *m365_event,
			 ICalComponent *inout_comp,
			 ICalPropertyKind prop_kind,
			 GCancellable *cancellable,
			 GError **error)
{
	EM365PatternedRecurrence *m365_recr;
	EM365RecurrencePattern *m365_pattern;
	EM365RecurrenceRange *m365_range;
	ICalRecurrence *ical_recr;
	ICalRecurrenceWeekday week_day;
	gint month;

	m365_recr = e_m365_event_get_recurrence (m365_event);
	m365_pattern = m365_recr ? e_m365_patterned_recurrence_get_pattern (m365_recr) : NULL;
	m365_range = m365_recr ? e_m365_patterned_recurrence_get_range (m365_recr) : NULL;

	if (!m365_recr || !m365_pattern || !m365_range)
		return TRUE;

	ical_recr = i_cal_recurrence_new ();

	switch (e_m365_recurrence_pattern_get_type (m365_pattern)) {
	case E_M365_RECURRENCE_PATTERN_DAILY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_DAILY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));
		ecb_m365_set_days_of_week_to_ical (ical_recr, e_m365_recurrence_pattern_get_days_of_week (m365_pattern));
		break;
	case E_M365_RECURRENCE_PATTERN_WEEKLY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_WEEKLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));

		week_day = ecb_m365_day_of_week_to_ical (e_m365_recurrence_pattern_get_first_day_of_week (m365_pattern));

		if (week_day != I_CAL_NO_WEEKDAY)
			i_cal_recurrence_set_week_start (ical_recr, week_day);

		ecb_m365_set_days_of_week_to_ical (ical_recr, e_m365_recurrence_pattern_get_days_of_week (m365_pattern));
		break;
	case E_M365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_MONTHLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));
		i_cal_recurrence_set_by_month_day (ical_recr, 0, e_m365_recurrence_pattern_get_day_of_month (m365_pattern));
		break;
	case E_M365_RECURRENCE_PATTERN_RELATIVE_MONTHLY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_MONTHLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));
		ecb_m365_set_days_of_week_to_ical (ical_recr, e_m365_recurrence_pattern_get_days_of_week (m365_pattern));
		week_day = ecb_m365_day_of_week_to_ical (e_m365_recurrence_pattern_get_first_day_of_week (m365_pattern));

		if (week_day != I_CAL_NO_WEEKDAY)
			i_cal_recurrence_set_week_start (ical_recr, week_day);

		ecb_m365_set_index_to_ical (ical_recr, e_m365_recurrence_pattern_get_index (m365_pattern));
		break;
	case E_M365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_YEARLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));
		i_cal_recurrence_set_by_month_day (ical_recr, 0, e_m365_recurrence_pattern_get_day_of_month (m365_pattern));

		month = e_m365_recurrence_pattern_get_month (m365_pattern);

		if (month >= 1 && month <= 12)
			i_cal_recurrence_set_by_month (ical_recr, 0, month);
		break;
	case E_M365_RECURRENCE_PATTERN_RELATIVE_YEARLY:
		i_cal_recurrence_set_freq (ical_recr, I_CAL_YEARLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));
		ecb_m365_set_days_of_week_to_ical (ical_recr, e_m365_recurrence_pattern_get_days_of_week (m365_pattern));
		week_day = ecb_m365_day_of_week_to_ical (e_m365_recurrence_pattern_get_first_day_of_week (m365_pattern));

		if (week_day != I_CAL_NO_WEEKDAY)
			i_cal_recurrence_set_week_start (ical_recr, week_day);

		ecb_m365_set_index_to_ical (ical_recr, e_m365_recurrence_pattern_get_index (m365_pattern));

		month = e_m365_recurrence_pattern_get_month (m365_pattern);

		if (month >= 1 && month <= 12)
			i_cal_recurrence_set_by_month (ical_recr, 0, month);
		break;
	default:
		g_object_unref (ical_recr);
		g_warning ("%s: Unknown pattern type: %d", G_STRFUNC, e_m365_recurrence_pattern_get_type (m365_pattern));
		/* Ignore the error (in the code) and continue. */
		return TRUE;
	}

	switch (e_m365_recurrence_range_get_type (m365_range)) {
	case E_M365_RECURRENCE_RANGE_ENDDATE:
		if (e_m365_recurrence_range_get_end_date (m365_range) > 0) {
			guint yy = 0, mm = 0, dd = 0;

			if (e_m365_date_decode (e_m365_recurrence_range_get_end_date (m365_range), &yy, &mm, &dd)) {
				ICalTime *itt;

				itt = i_cal_time_new ();
				i_cal_time_set_date (itt, yy, mm, dd);
				i_cal_time_set_is_date (itt, TRUE);

				i_cal_recurrence_set_until (ical_recr, itt);

				g_clear_object (&itt);
			}
		}
		break;
	case E_M365_RECURRENCE_RANGE_NOEND:
		break;
	case E_M365_RECURRENCE_RANGE_NUMBERED:
		i_cal_recurrence_set_count (ical_recr, e_m365_recurrence_range_get_number_of_occurrences (m365_range));
		break;
	default:
		g_warning ("%s: Unknown range type: %d", G_STRFUNC, e_m365_recurrence_range_get_type (m365_range));
		g_object_unref (ical_recr);
		/* Ignore the error (in the code) and continue. */
		return TRUE;
	}

	i_cal_component_take_property (inout_comp, i_cal_property_new_rrule (ical_recr));

	g_object_unref (ical_recr);

	return TRUE;
}

static gboolean
ecb_m365_add_recurrence (ECalBackendM365 *cbm365,
			 ICalComponent *new_comp,
			 ICalComponent *old_comp,
			 ICalPropertyKind prop_kind,
			 const gchar *m365_id,
			 JsonBuilder *builder,
			 GCancellable *cancellable,
			 GError **error)
{
	ICalProperty *new_value, *old_value;
	gboolean success = TRUE;

	if (i_cal_component_count_properties (new_comp, prop_kind) > 1) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Microsoft 365 calendar cannot store more than one recurrence")));

		return FALSE;
	}

	if (i_cal_component_count_properties (new_comp, I_CAL_RDATE_PROPERTY) > 0 ||
	    i_cal_component_count_properties (new_comp, I_CAL_EXDATE_PROPERTY) > 0 ||
	    i_cal_component_count_properties (new_comp, I_CAL_EXRULE_PROPERTY) > 0) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Microsoft 365 calendar cannot store component with RDATE, EXDATE or RRULE properties")));

		return FALSE;
	}

	new_value = i_cal_component_get_first_property (new_comp, prop_kind);
	old_value = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (!new_value && !old_value)
		return TRUE;

	if (new_value) {
		ICalRecurrence *new_rrule;
		gboolean same = FALSE;

		new_rrule = i_cal_property_get_rrule (new_value);

		if (old_value && new_rrule) {
			ICalRecurrence *old_rrule;

			old_rrule = i_cal_property_get_rrule (old_value);

			if (old_rrule) {
				gchar *new_str, *old_str;

				new_str = i_cal_recurrence_to_string (new_rrule);
				old_str = i_cal_recurrence_to_string (old_rrule);

				same = g_strcmp0 (new_str, old_str) == 0;

				g_free (new_str);
				g_free (old_str);
			}

			g_clear_object (&old_rrule);
		}

		if (!same && new_rrule) {
			EM365DayOfWeekType week_day;
			ICalTime *dtstart;
			gint by_pos, month, yy = 0, mm = 0, dd = 0;

			e_m365_event_begin_recurrence (builder);
			e_m365_patterned_recurrence_begin_pattern (builder);

			switch (i_cal_recurrence_get_freq (new_rrule)) {
			case I_CAL_DAILY_RECURRENCE:
				e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_DAILY);
				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));
				ecb_m365_add_days_of_week_from_ical (builder, new_rrule);
				break;
			case I_CAL_WEEKLY_RECURRENCE:
				e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_WEEKLY);
				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));

				week_day = ecb_m365_day_of_week_from_ical (i_cal_recurrence_get_week_start (new_rrule));

				if (week_day != E_M365_DAY_OF_WEEK_UNKNOWN)
					e_m365_recurrence_pattern_add_first_day_of_week (builder, week_day);

				ecb_m365_add_days_of_week_from_ical (builder, new_rrule);
				break;
			case I_CAL_MONTHLY_RECURRENCE:
				by_pos = i_cal_recurrence_get_by_set_pos (new_rrule, 0);

				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));

				if (by_pos == I_CAL_RECURRENCE_ARRAY_MAX) {
					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY);
					e_m365_recurrence_pattern_add_day_of_month (builder, i_cal_recurrence_get_by_month_day (new_rrule, 0));
				} else {
					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_RELATIVE_MONTHLY);

					week_day = ecb_m365_day_of_week_from_ical (i_cal_recurrence_get_week_start (new_rrule));

					if (week_day != E_M365_DAY_OF_WEEK_UNKNOWN)
						e_m365_recurrence_pattern_add_first_day_of_week (builder, week_day);

					ecb_m365_add_days_of_week_from_ical (builder, new_rrule);
					ecb_m365_add_index_from_ical (builder, by_pos);
				}
				break;
			case I_CAL_YEARLY_RECURRENCE:
				by_pos = i_cal_recurrence_get_by_set_pos (new_rrule, 0);

				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));

				month = i_cal_recurrence_get_by_month (new_rrule, 0);

				if (month >= 1 && month <= 12)
					e_m365_recurrence_pattern_add_month (builder, month);

				if (by_pos == I_CAL_RECURRENCE_ARRAY_MAX) {
					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY);
					e_m365_recurrence_pattern_add_day_of_month (builder, i_cal_recurrence_get_by_month_day (new_rrule, 0));
				} else {
					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_RELATIVE_YEARLY);

					week_day = ecb_m365_day_of_week_from_ical (i_cal_recurrence_get_week_start (new_rrule));

					if (week_day != E_M365_DAY_OF_WEEK_UNKNOWN)
						e_m365_recurrence_pattern_add_first_day_of_week (builder, week_day);

					ecb_m365_add_days_of_week_from_ical (builder, new_rrule);
					ecb_m365_add_index_from_ical (builder, by_pos);
				}

				break;
			default:
				g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED,
					_("Unknown recurrence frequency (%d)"), i_cal_recurrence_get_freq (new_rrule));

				success = FALSE;
				break;
			}

			e_m365_patterned_recurrence_end_pattern (builder);
			e_m365_patterned_recurrence_begin_range (builder);

			dtstart = i_cal_component_get_dtstart (new_comp);
			i_cal_time_get_date (dtstart, &yy, &mm, &dd);
			g_clear_object (&dtstart);

			e_m365_recurrence_range_add_start_date (builder, e_m365_date_encode (yy, mm, dd));

			if (!i_cal_recurrence_get_count (new_rrule)) {
				ICalTime *until;
				gint yy = 0, mm = 0, dd = 0;

				until = i_cal_recurrence_get_until (new_rrule);

				if (until)
					i_cal_time_get_date (until, &yy, &mm, &dd);

				if (!until || yy == 0) {
					e_m365_recurrence_range_add_type (builder, E_M365_RECURRENCE_RANGE_NOEND);
				} else {
					e_m365_recurrence_range_add_type (builder, E_M365_RECURRENCE_RANGE_ENDDATE);
					e_m365_recurrence_range_add_end_date (builder, e_m365_date_encode (yy, mm, dd));
				}

				g_clear_object (&until);
			} else {
				e_m365_recurrence_range_add_type (builder, E_M365_RECURRENCE_RANGE_NUMBERED);
				e_m365_recurrence_range_add_number_of_occurrences (builder, i_cal_recurrence_get_count (new_rrule));
			}

			e_m365_patterned_recurrence_end_range (builder);
			e_m365_event_end_recurrence (builder);
		}

		g_clear_object (&new_rrule);
	} else {
		e_m365_event_add_null_recurrence (builder);
	}

	g_clear_object (&new_value);
	g_clear_object (&old_value);

	return success;
}

static gboolean
ecb_m365_get_reminder (ECalBackendM365 *cbm365,
		       EM365Event *m365_event,
		       ICalComponent *inout_comp,
		       ICalPropertyKind prop_kind,
		       GCancellable *cancellable,
		       GError **error)
{
	if (e_m365_event_get_is_reminder_on (m365_event)) {
		ECalComponentAlarm *alarm;
		ECalComponentAlarmTrigger *trigger;
		ICalDuration *duration;

		duration = i_cal_duration_new_from_int (-60 * e_m365_event_get_reminder_minutes_before_start (m365_event));
		trigger = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, duration);
		g_object_unref (duration);

		alarm = e_cal_component_alarm_new ();
		e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
		e_cal_component_alarm_take_summary (alarm, e_cal_component_text_new (e_m365_event_get_subject (m365_event), NULL));
		e_cal_component_alarm_take_description (alarm, e_cal_component_text_new (e_m365_event_get_subject (m365_event), NULL));
		e_cal_component_alarm_take_trigger (alarm, trigger);

		i_cal_component_take_component (inout_comp, e_cal_component_alarm_get_as_component (alarm));

		e_cal_component_alarm_free (alarm);
	}

	return TRUE;
}

static gboolean
ecb_m365_add_reminder (ECalBackendM365 *cbm365,
		       ICalComponent *new_comp,
		       ICalComponent *old_comp,
		       ICalPropertyKind prop_kind,
		       const gchar *m365_id,
		       JsonBuilder *builder,
		       GCancellable *cancellable,
		       GError **error)
{
	ICalComponent *new_value, *old_value;
	gboolean success = TRUE;

	if (i_cal_component_count_components (new_comp, I_CAL_VALARM_COMPONENT) > 1) {
		g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Microsoft 365 calendar cannot store more that one event reminder")));
		return FALSE;
	}

	new_value = i_cal_component_get_first_component (new_comp, I_CAL_VALARM_COMPONENT);
	old_value = old_comp ? i_cal_component_get_first_component (old_comp, I_CAL_VALARM_COMPONENT) : NULL;

	if (!new_value && !old_value)
		return TRUE;

	if (new_value) {
		ECalComponentAlarm *new_alarm;
		ECalComponentAlarmTrigger *new_trigger;
		ICalDuration *new_duration = NULL;
		gboolean changed = TRUE;

		new_alarm = e_cal_component_alarm_new_from_component (new_value);
		new_trigger = new_alarm ? e_cal_component_alarm_get_trigger (new_alarm) : NULL;

		success = new_trigger && e_cal_component_alarm_trigger_get_kind (new_trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;

		if (success) {
			new_duration = e_cal_component_alarm_trigger_get_duration (new_trigger);

			success = new_duration && i_cal_duration_as_int (new_duration) <= 0;
		}

		if (!success) {
			g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Microsoft 365 calendar can store only a reminder before event start")));
		}

		if (success && old_value && new_trigger) {
			ECalComponentAlarm *old_alarm;
			ECalComponentAlarmTrigger *old_trigger;

			old_alarm = e_cal_component_alarm_new_from_component (old_value);
			old_trigger = old_alarm ? e_cal_component_alarm_get_trigger (old_alarm) : NULL;

			if (old_trigger) {
				changed = e_cal_component_alarm_trigger_get_kind (new_trigger) != e_cal_component_alarm_trigger_get_kind (old_trigger);

				if (!changed) {
					ICalDuration *old_duration;

					old_duration = e_cal_component_alarm_trigger_get_duration (old_trigger);

					changed = !old_duration || i_cal_duration_as_int (new_duration) != i_cal_duration_as_int (old_duration);
				}
			}

			e_cal_component_alarm_free (old_alarm);
		}

		if (success && changed) {
			e_m365_event_add_is_reminder_on (builder, TRUE);
			e_m365_event_add_reminder_minutes_before_start (builder, i_cal_duration_as_int (new_duration) / -60);
		}

		e_cal_component_alarm_free (new_alarm);
	} else {
		e_m365_event_add_is_reminder_on (builder, FALSE);
	}

	g_clear_object (&new_value);
	g_clear_object (&old_value);

	return success;
}



static gboolean
ecb_m365_get_attachments (ECalBackendM365 *cbm365,
			  EM365Event *m365_event,
			  ICalComponent *inout_comp,
			  ICalPropertyKind prop_kind,
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *attachments = NULL, *link;
	gboolean success = TRUE;

	if (!e_m365_event_get_has_attachments (m365_event))
		return TRUE;

	if (!e_m365_connection_list_event_attachments_sync (cbm365->priv->cnc, NULL,
		cbm365->priv->group_id, cbm365->priv->calendar_id, e_m365_event_get_id (m365_event), "id,name,contentType,contentBytes",
		&attachments, cancellable, error)) {
		return FALSE;
	}

	for (link = attachments; link && success; link = g_slist_next (link)) {
		CamelStream *content_stream;
		EM365Attachment *m365_attach = link->data;
		gchar *filename;

		if (!m365_attach || e_m365_attachment_get_data_type (m365_attach) != E_M365_ATTACHMENT_DATA_TYPE_FILE ||
		    !e_m365_attachment_get_name (m365_attach))
			continue;

		filename = g_build_filename (cbm365->priv->attachments_dir, e_m365_event_get_id (m365_event), e_m365_attachment_get_id (m365_attach), NULL);

		content_stream = camel_stream_fs_new_with_name (filename, O_CREAT | O_TRUNC | O_WRONLY, 0666, error);

		if (content_stream) {
			CamelMimeFilter *filter;
			CamelStream *filter_stream;
			const gchar *base64_data;

			filter_stream = camel_stream_filter_new (content_stream);

			filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
			camel_stream_filter_add (CAMEL_STREAM_FILTER (filter_stream), filter);
			g_object_unref (filter);

			base64_data = e_m365_file_attachment_get_content_bytes (m365_attach);

			if (base64_data && *base64_data)
				success = camel_stream_write (filter_stream, base64_data, strlen (base64_data), cancellable, error) != -1;

			camel_stream_flush (filter_stream, cancellable, NULL);
			g_object_unref (filter_stream);

			camel_stream_flush (content_stream, cancellable, NULL);
			g_object_unref (content_stream);

			if (success) {
				gchar *uri;

				uri = g_filename_to_uri (filename, NULL, error);

				if (uri) {
					ICalAttach *ical_attach;
					ICalParameter *param;
					ICalProperty *prop;
					gchar *enc_uri;
					const gchar *tmp;

					enc_uri = i_cal_value_encode_ical_string (uri);
					ical_attach = i_cal_attach_new_from_url (enc_uri);
					prop = i_cal_property_new_attach (ical_attach);

					tmp = e_m365_attachment_get_name (m365_attach);

					if (!tmp || !*tmp)
						tmp = "attachment.dat";

					param = i_cal_parameter_new_filename (tmp);
					i_cal_property_take_parameter (prop, param);

					tmp = e_m365_attachment_get_content_type (m365_attach);

					if (tmp && *tmp) {
						param = i_cal_parameter_new_fmttype (tmp);
						i_cal_property_take_parameter (prop, param);
					}

					param = i_cal_parameter_new_x (e_m365_attachment_get_id (m365_attach));
					i_cal_parameter_set_xname (param, "X-M365-ATTACHMENTID");
					i_cal_property_take_parameter (prop, param);

					i_cal_component_take_property (inout_comp, prop);

					g_object_unref (ical_attach);
					g_free (enc_uri);
					g_free (uri);
				} else {
					success = FALSE;
				}
			}
		} else {
			success = FALSE;
		}

		g_free (filename);
	}

	g_slist_free_full (attachments, (GDestroyNotify) json_object_unref);

	return success;
}

static void
ecb_m365_extract_attachments (ICalComponent *comp,
			      GHashTable **out_hash, /* gchar *attachment_id ~> ICalProperty * */
			      GSList **out_slist) /* ICalProperty * */
{
	ICalProperty *prop;

	if (!comp)
		return;

	for (prop = i_cal_component_get_first_property (comp, I_CAL_ATTACH_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_ATTACH_PROPERTY)) {
		if (out_slist) {
			*out_slist = g_slist_prepend (*out_slist, g_object_ref (prop));
		} else if (out_hash) {
			gchar *attach_id;

			attach_id = i_cal_property_get_parameter_as_string (prop, "X-M365-ATTACHMENTID");
			g_warn_if_fail (attach_id != NULL);

			if (attach_id) {
				if (!*out_hash)
					*out_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

				g_hash_table_insert (*out_hash, attach_id, g_object_ref (prop));
			}
		} else {
			g_warn_if_reached ();
		}
	}

	g_clear_object (&prop);

	if (out_slist && *out_slist)
		*out_slist = g_slist_reverse (*out_slist);
}

static gboolean
ecb_m365_add_attachments (ECalBackendM365 *cbm365,
			  ICalComponent *new_comp,
			  ICalComponent *old_comp,
			  ICalPropertyKind prop_kind,
			  const gchar *m365_id,
			  JsonBuilder *builder,
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *new_attachs = NULL;
	GHashTable *old_attachs = NULL;
	gboolean success = TRUE;

	if (!i_cal_component_count_properties (new_comp, I_CAL_ATTACH_PROPERTY) &&
	    !(old_comp ? i_cal_component_count_properties (old_comp, I_CAL_ATTACH_PROPERTY) : 0)) {
		return TRUE;
	}

	ecb_m365_extract_attachments (new_comp, NULL, &new_attachs);
	ecb_m365_extract_attachments (old_comp, &old_attachs, NULL);

	if (new_attachs) {
		GSList *link, *save_attachs = new_attachs;

		if (old_attachs) {
			save_attachs = NULL;

			for (link = new_attachs; link; link = g_slist_next (link)) {
				ICalProperty *prop = link->data;
				gchar *attach_id;

				attach_id = i_cal_property_get_parameter_as_string (prop, "X-M365-ATTACHMENTID");

				if (!attach_id || !g_hash_table_remove (old_attachs, attach_id)) {
					save_attachs = g_slist_prepend (save_attachs, g_object_ref (prop));
				}
			}

			if (save_attachs)
				save_attachs = g_slist_reverse (save_attachs);
		}

		for (link = save_attachs; link && success; link = g_slist_next (link)) {
			ICalProperty *prop = link->data;
			ICalAttach *attach;
			JsonBuilder *builder = NULL;

			attach = i_cal_property_get_attach (prop);

			if (!attach)
				continue;

			if (i_cal_attach_get_is_url (attach)) {
				const gchar *data;
				gchar *uri;

				data = i_cal_attach_get_url (attach);
				uri = i_cal_value_decode_ical_string (data);

				if (uri && g_ascii_strncasecmp (uri, "file://", 7) == 0) {
					CamelStream *content_stream;
					gchar *filename;

					filename = g_filename_from_uri (uri, NULL, error);
					content_stream = filename ? camel_stream_fs_new_with_name (filename, O_RDONLY, 0, error) : NULL;

					if (content_stream) {
						CamelMimeFilter *filter;
						CamelStream *filter_stream;
						CamelStream *base64_stream;

						base64_stream = camel_stream_mem_new ();
						filter_stream = camel_stream_filter_new (base64_stream);

						filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
						camel_stream_filter_add (CAMEL_STREAM_FILTER (filter_stream), filter);
						g_object_unref (filter);

						success = camel_stream_write_to_stream (content_stream, filter_stream, cancellable, error) != -1;

						camel_stream_flush (filter_stream, cancellable, NULL);
						g_object_unref (filter_stream);

						/* Ensure the stream is NUL-terminated, thus it can be used as a string */
						camel_stream_write (base64_stream, "\0", 1, cancellable, NULL);

						camel_stream_flush (base64_stream, cancellable, NULL);
						g_object_unref (content_stream);

						if (success) {
							GByteArray *bytes;

							bytes = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (base64_stream));

							builder = json_builder_new_immutable ();
							e_m365_attachment_begin_attachment (builder, E_M365_ATTACHMENT_DATA_TYPE_FILE);
							e_m365_file_attachment_add_content_bytes (builder, (const gchar *) bytes->data);
						}

						g_object_unref (base64_stream);
					} else {
						success = FALSE;
					}

					g_free (filename);
				} else {
					success = FALSE;

					if (uri)
						g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR, _("Cannot store attachment with URI “%s”"), uri);
					else
						g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Failed to read attachment URI")));
				}

				g_free (uri);
			} else {
				const gchar *base64_data;

				base64_data = i_cal_attach_get_data (attach);

				if (base64_data) {
					builder = json_builder_new_immutable ();
					e_m365_attachment_begin_attachment (builder, E_M365_ATTACHMENT_DATA_TYPE_FILE);
					e_m365_file_attachment_add_content_bytes (builder, base64_data);
				} else {
					g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Failed to get inline attachment data")));
				}
			}

			if (builder) {
				ICalParameter *param;
				const gchar *tmp;

				param = i_cal_property_get_first_parameter (prop, I_CAL_FILENAME_PARAMETER);

				if (param) {
					tmp = i_cal_parameter_get_filename (param);

					if (tmp && *tmp)
						e_m365_attachment_add_name (builder, tmp);

					g_clear_object (&param);
				}

				param = i_cal_property_get_first_parameter (prop, I_CAL_FMTTYPE_PARAMETER);

				if (param) {
					tmp = i_cal_parameter_get_fmttype (param);

					if (tmp && *tmp)
						e_m365_attachment_add_content_type (builder, tmp);
					else
						e_m365_attachment_add_content_type (builder, "application/octet-stream");

					g_clear_object (&param);
				} else {
					e_m365_attachment_add_content_type (builder, "application/octet-stream");
				}

				e_m365_attachment_end_attachment (builder);

				success = e_m365_connection_add_event_attachment_sync (cbm365->priv->cnc, NULL,
					cbm365->priv->group_id, cbm365->priv->calendar_id, m365_id,
					builder, NULL, cancellable, error);

				g_object_unref (builder);
			}

			g_object_unref (attach);
		}

		if (save_attachs != new_attachs)
			g_slist_free_full (save_attachs, g_object_unref);
	}

	if (old_attachs && success) {
		GHashTableIter iter;
		gpointer key;

		g_hash_table_iter_init (&iter, old_attachs);

		while (g_hash_table_iter_next (&iter, &key, NULL) && success) {
			const gchar *attachment_id = key;

			success = e_m365_connection_delete_event_attachment_sync (cbm365->priv->cnc, NULL,
				cbm365->priv->group_id, cbm365->priv->calendar_id, i_cal_component_get_uid (new_comp),
				attachment_id, cancellable, error);
		}
	}

	if (old_attachs)
		g_hash_table_destroy (old_attachs);
	g_slist_free_full (new_attachs, g_object_unref);

	return success;
}

#define SIMPLE_FIELD(propknd, getfn, addfn) { propknd, FALSE, getfn, NULL, addfn, NULL }
#define COMPLEX_FIELD(propknd, getfn, addfn) { propknd, FALSE, NULL, getfn, NULL, addfn }
#define COMPLEX_FIELD_2(propknd, getfn, addfn) { propknd, TRUE, NULL, getfn, NULL, addfn }

struct _mappings {
	ICalPropertyKind prop_kind;
	gboolean add_in_second_go;
	void		(* get_simple_func)	(ECalBackendM365 *cbm365,
						 EM365Event *m365_event,
						 ICalComponent *inout_comp,
						 ICalPropertyKind prop_kind);
	gboolean	(* get_func)		(ECalBackendM365 *cbm365,
						 EM365Event *m365_event,
						 ICalComponent *inout_comp,
						 ICalPropertyKind prop_kind,
						 GCancellable *cancellable,
						 GError **error);
	void		(* add_simple_func)	(ECalBackendM365 *cbm365,
						 ICalComponent *new_comp,
						 ICalComponent *old_comp, /* nullable */
						 ICalPropertyKind prop_kind,
						 JsonBuilder *builder);
	gboolean	(* add_func)		(ECalBackendM365 *cbm365,
						 ICalComponent *new_comp,
						 ICalComponent *old_comp, /* nullable */
						 ICalPropertyKind prop_kind,
						 const gchar *m365_id,
						 JsonBuilder *builder,
						 GCancellable *cancellable,
						 GError **error);
} mappings[] = {
	SIMPLE_FIELD	(I_CAL_UID_PROPERTY,		ecb_m365_get_uid,		NULL),
	SIMPLE_FIELD	(I_CAL_CREATED_PROPERTY,	ecb_m365_get_date_time,		NULL),
	SIMPLE_FIELD	(I_CAL_LASTMODIFIED_PROPERTY,	ecb_m365_get_date_time,		NULL),
	SIMPLE_FIELD	(I_CAL_DTSTART_PROPERTY,	ecb_m365_get_date_time_zone,	ecb_m365_add_date_time_zone),
	SIMPLE_FIELD	(I_CAL_DTEND_PROPERTY,		ecb_m365_get_date_time_zone,	ecb_m365_add_date_time_zone),
	SIMPLE_FIELD	(I_CAL_CATEGORIES_PROPERTY,	ecb_m365_get_categories,	ecb_m365_add_categories),
	SIMPLE_FIELD	(I_CAL_SUMMARY_PROPERTY,	ecb_m365_get_subject,		ecb_m365_add_subject),
	SIMPLE_FIELD	(I_CAL_DESCRIPTION_PROPERTY,	ecb_m365_get_body,		ecb_m365_add_body),
	SIMPLE_FIELD	(I_CAL_CLASS_PROPERTY,		ecb_m365_get_sensitivity,	ecb_m365_add_sensitivity),
	SIMPLE_FIELD	(I_CAL_TRANSP_PROPERTY,		ecb_m365_get_show_as,		ecb_m365_add_show_as),
	SIMPLE_FIELD	(I_CAL_LOCATION_PROPERTY,	ecb_m365_get_location,		ecb_m365_add_location),
	SIMPLE_FIELD	(I_CAL_ORGANIZER_PROPERTY,	ecb_m365_get_organizer,		ecb_m365_add_organizer),
	SIMPLE_FIELD	(I_CAL_ATTENDEE_PROPERTY,	ecb_m365_get_attendees,		ecb_m365_add_attendees),
	SIMPLE_FIELD	(I_CAL_PRIORITY_PROPERTY,	ecb_m365_get_importance,	ecb_m365_add_importance),
	SIMPLE_FIELD	(I_CAL_STATUS_PROPERTY,		ecb_m365_get_status,		NULL),
	COMPLEX_FIELD	(I_CAL_RRULE_PROPERTY,		ecb_m365_get_recurrence,	ecb_m365_add_recurrence),
	COMPLEX_FIELD	(I_CAL_X_PROPERTY,		ecb_m365_get_reminder,		ecb_m365_add_reminder),
	COMPLEX_FIELD_2	(I_CAL_ATTACH_PROPERTY,		ecb_m365_get_attachments,	ecb_m365_add_attachments)
};

static gchar *
ecb_m365_join_to_extra (const gchar *change_key,
			const gchar *ical_comp)
{
	if (!change_key && !ical_comp)
		return NULL;

	return g_strconcat (change_key ? change_key : "", "\n", ical_comp, NULL);
}

static ICalComponent *
ecb_m365_json_to_ical (ECalBackendM365 *cbm365,
		       EM365Event *m365_event,
		       GCancellable *cancellable,
		       GError **error)
{
	ICalComponent *icomp;
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (m365_event != NULL, NULL);

	icomp = i_cal_component_new_vevent ();

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].get_simple_func) {
			mappings[ii].get_simple_func (cbm365, m365_event, icomp, mappings[ii].prop_kind);
		} else if (mappings[ii].get_func) {
			success = mappings[ii].get_func (cbm365, m365_event, icomp, mappings[ii].prop_kind, cancellable, error);
		}
	}

	if (!success)
		g_clear_object (&icomp);

	return icomp;
}

static ECalMetaBackendInfo *
ecb_m365_json_to_ical_nfo (ECalBackendM365 *cbm365,
			   EM365Event *m365_event,
			   GCancellable *cancellable,
			   GError **error)
{
	ECalMetaBackendInfo *nfo;
	ICalComponent *icomp;

	icomp = ecb_m365_json_to_ical (cbm365, m365_event, cancellable, error);

	if (!icomp)
		return NULL;

	nfo = e_cal_meta_backend_info_new (i_cal_component_get_uid (icomp),
		e_m365_event_get_change_key (m365_event),
		NULL, NULL);

	if (nfo) {
		nfo->object = i_cal_component_as_ical_string (icomp);
		nfo->extra = ecb_m365_join_to_extra (e_m365_event_get_change_key (m365_event), nfo->object);
	}

	g_clear_object (&icomp);

	return nfo;
}

static JsonBuilder *
ecb_m365_ical_to_json_locked (ECalBackendM365 *cbm365,
			      ICalComponent *new_comp,
			      ICalComponent *old_comp, /* nullable */
			      GCancellable *cancellable,
			      GError **error)
{
	JsonBuilder *builder;
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (new_comp != NULL, NULL);

	builder = json_builder_new_immutable ();
	e_m365_json_begin_object_member (builder, NULL);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].add_simple_func) {
			mappings[ii].add_simple_func (cbm365, new_comp, old_comp, mappings[ii].prop_kind, builder);
		} else if (!mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (cbm365, new_comp, old_comp, mappings[ii].prop_kind, NULL, builder, cancellable, error);
		}
	}

	e_m365_json_end_object_member (builder);

	if (!success)
		g_clear_object (&builder);

	return builder;
}

static gboolean
ecb_m365_ical_to_json_2nd_go_locked (ECalBackendM365 *cbm365,
				     ICalComponent *new_comp,
				     ICalComponent *old_comp, /* nullable */
				     const gchar *m365_id,
				     GCancellable *cancellable,
				     GError **error)
{
	gint ii;
	gboolean success = TRUE;

	g_return_val_if_fail (new_comp != NULL, FALSE);

	for (ii = 0; success && ii < G_N_ELEMENTS (mappings); ii++) {
		if (mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (cbm365, new_comp, old_comp, mappings[ii].prop_kind, m365_id, NULL, cancellable, error);
		}
	}

	return success;
}

static gboolean
ecb_m365_download_event_changes_locked (ECalBackendM365 *cbm365,
					const GSList *ids,
					GSList **out_info_objects,
					GCancellable *cancellable,
					GError **error)
{
	GSList *events = NULL, *link;

	if (!ids)
		return TRUE;

	if (!e_m365_connection_get_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->calendar_id, ids, NULL, NULL, &events, cancellable, error))
		return FALSE;

	for (link = events; link; link = g_slist_next (link)) {
		EM365Event *event = link->data;
		ECalMetaBackendInfo *nfo;

		if (!event)
			continue;

		nfo = ecb_m365_json_to_ical_nfo (cbm365, event, cancellable, error);

		if (nfo)
			*out_info_objects = g_slist_prepend (*out_info_objects, nfo);
	}

	g_slist_free_full (events, (GDestroyNotify) json_object_unref);

	return TRUE;
}

static void
ecb_m365_maybe_disconnect_sync (ECalBackendM365 *cbm365,
				GError **in_perror,
				GCancellable *cancellable)
{
	g_return_if_fail (E_IS_CAL_BACKEND_M365 (cbm365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_cal_meta_backend_disconnect_sync (E_CAL_META_BACKEND (cbm365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (cbm365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gboolean
ecb_m365_unset_connection_sync (ECalBackendM365 *cbm365,
				gboolean is_disconnect,
				GCancellable *cancellable,
				GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (cbm365), FALSE);

	LOCK (cbm365);

	if (cbm365->priv->cnc) {
		if (is_disconnect)
			success = e_m365_connection_disconnect_sync (cbm365->priv->cnc, cancellable, error);
	}

	g_clear_object (&cbm365->priv->cnc);
	g_clear_pointer (&cbm365->priv->group_id, g_free);
	g_clear_pointer (&cbm365->priv->calendar_id, g_free);

	UNLOCK (cbm365);

	return success;
}

static gboolean
ecb_m365_connect_sync (ECalMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	ECalBackendM365 *cbm365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	if (cbm365->priv->cnc) {
		UNLOCK (cbm365);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	} else {
		EBackend *backend;
		ESourceRegistry *registry;
		ESource *source;
		EM365Connection *cnc;
		ESourceM365Folder *m365_folder_extension;
		CamelM365Settings *m365_settings;
		gchar *group_id;
		gchar *calendar_id;

		backend = E_BACKEND (cbm365);
		source = e_backend_get_source (backend);
		registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbm365));
		m365_settings = camel_m365_settings_get_from_backend (backend, registry);
		g_warn_if_fail (m365_settings != NULL);

		m365_folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);
		group_id = e_source_m365_folder_dup_group_id (m365_folder_extension);
		calendar_id = e_source_m365_folder_dup_id (m365_folder_extension);

		if (calendar_id) {
			cnc = e_m365_connection_new_for_backend (backend, registry, source, m365_settings);

			*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_CALENDAR, group_id, calendar_id,
				out_certificate_pem, out_certificate_errors, cancellable, error);

			if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
				cbm365->priv->cnc = g_object_ref (cnc);

				g_warn_if_fail (cbm365->priv->group_id == NULL);
				g_warn_if_fail (cbm365->priv->calendar_id == NULL);

				g_free (cbm365->priv->group_id);
				cbm365->priv->group_id = group_id;

				g_free (cbm365->priv->calendar_id);
				cbm365->priv->calendar_id = calendar_id;

				group_id = NULL;
				calendar_id = NULL;
				success = TRUE;

				e_cal_backend_set_writable (E_CAL_BACKEND (cbm365), TRUE);
			}
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Folder ID is not set")));
		}

		g_clear_object (&cnc);
		g_free (group_id);
		g_free (calendar_id);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);

	return success;
}

static gboolean
ecb_m365_disconnect_sync (ECalMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);

	return ecb_m365_unset_connection_sync (E_CAL_BACKEND_M365 (meta_backend), TRUE, cancellable, error);
}

static gboolean
ecb_m365_get_changes_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendM365 *cbm365;
	ECalCache *cal_cache;
	GSList *events = NULL, *link;
	gboolean full_read;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	LOCK (cbm365);

	full_read = !e_cache_get_count (E_CACHE (cal_cache), E_CACHE_INCLUDE_DELETED, cancellable, NULL);

	success = e_m365_connection_list_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->calendar_id, NULL,
		full_read ? NULL : "id,changeKey", &events, cancellable, error);

	if (success) {
		GSList *new_ids = NULL; /* const gchar *, borrowed from 'events' objects */
		GSList *changed_ids = NULL; /* const gchar *, borrowed from 'events' objects */

		for (link = events; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
			EM365Event *event = link->data;
			const gchar *id, *change_key;
			gchar *extra = NULL;

			if (!event)
				continue;

			id = e_m365_event_get_id (event);
			change_key = e_m365_event_get_change_key (event);

			if (e_cal_cache_get_component_extra (cal_cache, id, NULL, &extra, cancellable, NULL)) {
				const gchar *saved_change_key = NULL;

				ecb_m365_split_extra (extra, &saved_change_key, NULL);

				if (g_strcmp0 (saved_change_key, change_key) == 0) {
					g_free (extra);
					continue;
				} else if (full_read) {
					ECalMetaBackendInfo *nfo;

					nfo = ecb_m365_json_to_ical_nfo (cbm365, event, cancellable, NULL);

					if (nfo)
						*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
				} else {
					changed_ids = g_slist_prepend (changed_ids, (gpointer) id);
				}

				g_free (extra);
			} else if (full_read) {
				ECalMetaBackendInfo *nfo;

				nfo = ecb_m365_json_to_ical_nfo (cbm365, event, cancellable, NULL);

				if (nfo)
					*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
			} else {
				new_ids = g_slist_prepend (new_ids, (gpointer) id);
			}
		}

		if (new_ids) {
			new_ids = g_slist_reverse (new_ids);
			success = ecb_m365_download_event_changes_locked (cbm365, new_ids, out_created_objects, cancellable, error);
		}

		if (success && changed_ids) {
			changed_ids = g_slist_reverse (changed_ids);
			success = ecb_m365_download_event_changes_locked (cbm365, changed_ids, out_modified_objects, cancellable, error);
		}

		g_slist_free (new_ids);
		g_slist_free (changed_ids);
	}

	g_slist_free_full (events, (GDestroyNotify) json_object_unref);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_clear_object (&cal_cache);

	return success;
}

static gboolean
ecb_m365_load_component_sync (ECalMetaBackend *meta_backend,
			      const gchar *uid,
			      const gchar *extra,
			      ICalComponent **out_component,
			      gchar **out_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendM365 *cbm365;
	EM365Event *event = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	success = e_m365_connection_get_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
		cbm365->priv->calendar_id, uid, NULL, NULL, &event, cancellable, error);

	if (success) {
		*out_component = ecb_m365_json_to_ical (cbm365, event, cancellable, error);

		if (*out_component) {
			gchar *ical_str;

			ical_str = i_cal_component_as_ical_string (*out_component);

			*out_extra = ecb_m365_join_to_extra (e_m365_event_get_change_key (event), ical_str);

			g_free (ical_str);
		} else {
			success = FALSE;
		}
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	return success;
}

static gboolean
ecb_m365_save_component_sync (ECalMetaBackend *meta_backend,
			      gboolean overwrite_existing,
			      EConflictResolution conflict_resolution,
			      const GSList *instances,
			      const gchar *extra,
			      guint32 opflags,
			      gchar **out_new_uid,
			      gchar **out_new_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendM365 *cbm365;
	ICalComponent *new_comp, *old_comp = NULL;
	JsonBuilder *builder;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);

	if (instances->next) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Can store only simple events into Microsoft 365 calendar")));

		return FALSE;
	}

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	new_comp = e_cal_component_get_icalcomponent (instances->data);

	if (extra && *extra) {
		const gchar *comp_str;

		comp_str = ecb_m365_get_component_from_extra (extra);

		if (comp_str)
			old_comp = i_cal_component_new_from_string (comp_str);
	}

	builder = ecb_m365_ical_to_json_locked (cbm365, new_comp, old_comp, cancellable, error);

	if (builder) {
		if (overwrite_existing) {
			const gchar *uid = i_cal_component_get_uid (new_comp);

			success = e_m365_connection_update_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->calendar_id, uid, builder, cancellable, error);

			if (success)
				success = ecb_m365_ical_to_json_2nd_go_locked (cbm365, new_comp, old_comp, uid, cancellable, error);

			if (success) {
				/* To re-read it from the server */
				*out_new_uid = g_strdup (uid);
			}
		} else {
			EM365Event *created_event = NULL;

			success = e_m365_connection_create_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->calendar_id, builder, &created_event, cancellable, error);

			if (success && created_event) {
				const gchar *m365_id = e_m365_event_get_id (created_event);

				success = ecb_m365_ical_to_json_2nd_go_locked (cbm365, new_comp, old_comp, m365_id, cancellable, error);
			}

			if (success && created_event) {
				ICalComponent *icomp;

				*out_new_uid = g_strdup (e_m365_event_get_id (created_event));

				icomp = ecb_m365_json_to_ical (cbm365, created_event, cancellable, error);

				if (icomp) {
					gchar *ical_str;

					ical_str = i_cal_component_as_ical_string (icomp);

					*out_new_extra = ecb_m365_join_to_extra (e_m365_event_get_change_key (created_event), ical_str);

					g_clear_object (&icomp);
					g_free (ical_str);
				} else {
					success = FALSE;
				}
			}

			if (created_event)
				json_object_unref (created_event);
		}

		g_clear_object (&builder);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_clear_object (&old_comp);

	return success;
}

static gboolean
ecb_m365_remove_component_sync (ECalMetaBackend *meta_backend,
				EConflictResolution conflict_resolution,
				const gchar *uid,
				const gchar *extra,
				const gchar *object,
				guint32 opflags,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendM365 *cbm365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	success = e_m365_connection_delete_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
		cbm365->priv->calendar_id, uid, cancellable, error);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	return success;
}

static gchar *
ecb_m365_get_backend_property (ECalBackend *cal_backend,
			      const gchar *prop_name)
{
	ECalBackendM365 *cbm365;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	cbm365 = E_CAL_BACKEND_M365 (cal_backend);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			",",
			E_CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS,
			E_CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY,
			E_CAL_STATIC_CAPABILITY_REMOVE_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			E_CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			E_CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK,
			E_CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT,
			E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES,
			E_CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START,
			E_CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE,
			E_CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME,
			E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM,
			E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR,
			E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbm365)),
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		CamelM365Settings *m365_settings;

		m365_settings = camel_m365_settings_get_from_backend (E_BACKEND (cal_backend), e_cal_backend_get_registry (cal_backend));

		return camel_m365_settings_dup_email (m365_settings);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* Microsoft 365 does not support email based alarms */
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_m365_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static gboolean
ecb_m365_get_destination_address (EBackend *backend,
				  gchar **host,
				  guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_cal_backend_get_registry (E_CAL_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static gchar *
ecb_m365_dup_component_revision (ECalCache *cal_cache,
				 ICalComponent *icomp,
				 gpointer user_data)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static void
ecb_m365_constructed (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);
	ECalCache *cal_cache;
	gchar *cache_dirname;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbm365));
	g_return_if_fail (cal_cache != NULL);

	cache_dirname = g_path_get_dirname (e_cache_get_filename (E_CACHE (cal_cache)));
	g_signal_connect (cal_cache, "dup-component-revision", G_CALLBACK (ecb_m365_dup_component_revision), NULL);

	g_clear_object (&cal_cache);

	cbm365->priv->attachments_dir = g_build_filename (cache_dirname, "attachments", NULL);
	g_mkdir_with_parents (cbm365->priv->attachments_dir, 0777);

	g_free (cache_dirname);

	e_m365_tz_utils_ref_windows_zones ();
}

static void
ecb_m365_dispose (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);

	ecb_m365_unset_connection_sync (cbm365, FALSE, NULL, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->dispose (object);
}

static void
ecb_m365_finalize (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);

	g_free (cbm365->priv->attachments_dir);

	g_rec_mutex_clear (&cbm365->priv->property_lock);

	e_m365_tz_utils_unref_windows_zones ();

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->finalize (object);
}

static void
e_cal_backend_m365_init (ECalBackendM365 *cbm365)
{
	cbm365->priv = e_cal_backend_m365_get_instance_private (cbm365);

	g_rec_mutex_init (&cbm365->priv->property_lock);
}

static void
e_cal_backend_m365_class_init (ECalBackendM365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_m365_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_m365_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_m365_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_m365_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_m365_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_m365_remove_component_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_m365_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ecb_m365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_m365_constructed;
	object_class->dispose = ecb_m365_dispose;
	object_class->finalize = ecb_m365_finalize;
}
