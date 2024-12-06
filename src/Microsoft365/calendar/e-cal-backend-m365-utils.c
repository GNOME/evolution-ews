/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libecal/libecal.h>

#include "common/e-m365-connection.h"
#include "common/e-m365-tz-utils.h"
#include "e-cal-backend-m365-recur-blob.h"
#include "e-ews-common-utils.h"

#include "e-cal-backend-m365-utils.h"

#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

static void
ecb_m365_json_to_ical_recur_blob (JsonObject *m365_object,
				  ETimezoneCache *timezone_cache,
				  ICalComponent **inout_icomp)
{
	const gchar *base64_blob;
	ICalTimezone *recur_zone = NULL;
	ICalProperty *prop;
	GSList *extra_detached = NULL;

	g_return_if_fail (m365_object != NULL);
	g_return_if_fail (inout_icomp != NULL);
	g_return_if_fail (*inout_icomp != NULL);

	if (!e_cal_util_component_has_recurrences (*inout_icomp))
		return;

	base64_blob = e_m365_json_get_string_single_value_extended_property (m365_object, E_M365_RECURRENCE_BLOB_NAME);
	if (!base64_blob || !*base64_blob)
		return;

	prop = i_cal_component_get_first_property (*inout_icomp, I_CAL_DTSTART_PROPERTY);
	if (prop) {
		ICalParameter *param;

		param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
		if (param) {
			const gchar *tzid;

			tzid = i_cal_parameter_get_tzid (param);

			if (tzid && *tzid)
				recur_zone = e_timezone_cache_get_timezone (timezone_cache, tzid);

			g_clear_object (&param);
		}

		g_clear_object (&prop);
	}

	if (e_cal_backend_m365_decode_recur_blob (base64_blob, *inout_icomp, recur_zone, &extra_detached)) {
		if (extra_detached) {
			ICalComponent *vcalendar;
			GSList *link;

			vcalendar = i_cal_component_new_vcalendar ();
			i_cal_component_take_component (vcalendar, *inout_icomp);

			for (link = extra_detached; link; link = g_slist_next (link)) {
				ICalComponent *icomp = link->data;

				i_cal_component_take_component (vcalendar, icomp);
			}

			*inout_icomp = vcalendar;

			g_slist_free (extra_detached);
		}
	}
}

static void
ecb_m365_get_uid (EM365Connection *cnc,
		  const gchar *group_id,
		  const gchar *folder_id,
		  const gchar *attachments_dir,
		  ETimezoneCache *timezone_cache,
		  JsonObject *m365_object,
		  ICalComponent *inout_comp,
		  ICalPropertyKind prop_kind)
{
	const gchar *id;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		id = e_m365_event_get_id (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		id = e_m365_task_get_id (m365_object);
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	i_cal_component_set_uid (inout_comp, id);
}

static void
ecb_m365_get_date_time (EM365Connection *cnc,
			const gchar *group_id,
			const gchar *folder_id,
			const gchar *attachments_dir,
			ETimezoneCache *timezone_cache,
			JsonObject *m365_object,
			ICalComponent *inout_comp,
			ICalPropertyKind prop_kind)
{
	time_t tt = (time_t) 0;

	if (prop_kind == I_CAL_CREATED_PROPERTY) {
		switch (i_cal_component_isa (inout_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			tt = e_m365_event_get_created_date_time (m365_object);
			break;
		case I_CAL_VTODO_COMPONENT:
			tt = e_m365_task_get_created_date_time (m365_object);
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	} else if (prop_kind == I_CAL_LASTMODIFIED_PROPERTY) {
		switch (i_cal_component_isa (inout_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			tt = e_m365_event_get_last_modified_date_time (m365_object);
			break;
		case I_CAL_VTODO_COMPONENT:
			tt = e_m365_task_get_last_modified_date_time (m365_object);
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	} else {
		g_warn_if_reached ();
	}

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

static void
ecb_m365_get_date_time_zone (EM365Connection *cnc,
			     const gchar *group_id,
			     const gchar *folder_id,
			     const gchar *attachments_dir,
			     ETimezoneCache *timezone_cache,
			     JsonObject *m365_object,
			     ICalComponent *inout_comp,
			     ICalPropertyKind prop_kind)
{
	EM365DateTimeWithZone *value;
	ICalTimezone *tz;
	ICalTime *itt;
	time_t tt;
	const gchar *tzid, *zone;
	gboolean use_user_timezone = FALSE;
	gboolean is_date;

	if (prop_kind == I_CAL_DTSTART_PROPERTY) {
		switch (i_cal_component_isa (inout_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			value = e_m365_event_get_start (m365_object);
			tzid = e_m365_event_get_original_start_timezone (m365_object);
			is_date = e_m365_event_get_is_all_day (m365_object);
			break;
		case I_CAL_VTODO_COMPONENT:
			value = e_m365_task_get_start_date_time (m365_object);
			tzid = "UTC";
			is_date = TRUE;
			use_user_timezone = TRUE;
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	} else if (prop_kind == I_CAL_DTEND_PROPERTY) {
		value = e_m365_event_get_end (m365_object);
		tzid = e_m365_event_get_original_end_timezone (m365_object);
		is_date = e_m365_event_get_is_all_day (m365_object);
	} else if (prop_kind == I_CAL_COMPLETED_PROPERTY) {
		value = e_m365_task_get_completed_date_time (m365_object);
		tzid = "UTC";
		is_date = TRUE;
		use_user_timezone = TRUE;
	} else if (prop_kind == I_CAL_DUE_PROPERTY) {
		value = e_m365_task_get_due_date_time (m365_object);
		tzid = "UTC";
		is_date = TRUE;
		use_user_timezone = TRUE;
	} else {
		g_warn_if_reached ();
		return;
	}

	if (!value)
		return;

	tt = e_m365_date_time_get_date_time (value);
	if (use_user_timezone) {
		tz = e_m365_tz_utils_get_user_timezone ();
	} else {
		zone = e_m365_date_time_get_time_zone (value);

		if (zone && *zone)
			zone = e_m365_tz_utils_get_ical_equivalent (zone);

		tz = zone && *zone ? e_timezone_cache_get_timezone (timezone_cache, zone) : NULL;
	}

	if (!tz)
		tz = i_cal_timezone_get_utc_timezone ();

	itt = i_cal_time_new_from_timet_with_zone (tt, is_date && !use_user_timezone, tz);

	if (is_date && use_user_timezone)
		i_cal_time_set_is_date (itt, TRUE);

	i_cal_time_set_timezone (itt, tz);

	if (!is_date) {
		tzid = e_m365_tz_utils_get_ical_equivalent (tzid);

		if (!tzid)
			tzid = "UTC";

		tz = e_timezone_cache_get_timezone (timezone_cache, tzid);

		if (tz && !is_date)
			i_cal_time_convert_to_zone_inplace (itt, tz);
	}

	if (prop_kind == I_CAL_DTSTART_PROPERTY)
		i_cal_component_set_dtstart (inout_comp, itt);
	else if (prop_kind == I_CAL_DTEND_PROPERTY)
		i_cal_component_set_dtend (inout_comp, itt);
	else if (prop_kind == I_CAL_COMPLETED_PROPERTY)
		i_cal_component_take_property (inout_comp, i_cal_property_new_completed (itt));
	else /* if (prop_kind == I_CAL_DUE_PROPERTY) */
		i_cal_component_set_due (inout_comp, itt);

	g_clear_object (&itt);
}

static void
ecb_m365_add_date_time_zone (EM365Connection *cnc,
			     const gchar *group_id,
			     const gchar *folder_id,
			     ETimezoneCache *timezone_cache,
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

		switch (i_cal_component_isa (new_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			add_func = e_m365_event_add_start;
			break;
		case I_CAL_VTODO_COMPONENT:
			add_func = e_m365_task_add_start_date_time;
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	} else if (prop_kind == I_CAL_DTEND_PROPERTY) {
		new_value = i_cal_component_get_dtend (new_comp);
		old_value = old_comp ? i_cal_component_get_dtend (old_comp) : NULL;
		add_func = e_m365_event_add_end;
	} else if (prop_kind == I_CAL_COMPLETED_PROPERTY) {
		ICalProperty *old_prop;

		new_prop = i_cal_component_get_first_property (new_comp, prop_kind);
		old_prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;
		new_value = new_prop ? i_cal_property_get_completed (new_prop) : NULL;
		old_value = old_prop ? i_cal_property_get_completed (old_prop) : NULL;
		add_func = e_m365_task_add_completed_date_time;

		g_clear_object (&new_prop);
		g_clear_object (&old_prop);
	} else if (prop_kind == I_CAL_DUE_PROPERTY) {
		new_value = i_cal_component_get_due (new_comp);
		old_value = old_comp ? i_cal_component_get_due (old_comp) : NULL;
		add_func = e_m365_task_add_due_date_time;
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

		if (new_tzid)
			izone = e_timezone_cache_get_timezone (timezone_cache, new_tzid);

		if (!new_tzid || !*new_tzid || !izone)
			izone = e_ews_common_utils_get_configured_icaltimezone ();

		if (izone)
			wzone = e_m365_tz_utils_get_msdn_equivalent (i_cal_timezone_get_location (izone));

		tt = i_cal_time_as_timet_with_zone (new_value, wzone ? NULL : izone);

		add_func (builder, tt, wzone);
	}

	g_clear_object (&new_prop);
	g_clear_object (&new_param);
	g_clear_object (&new_value);
	g_clear_object (&old_value);
}

static void
ecb_m365_get_categories (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 const gchar *attachments_dir,
			 ETimezoneCache *timezone_cache,
			 JsonObject *m365_object,
			 ICalComponent *inout_comp,
			 ICalPropertyKind prop_kind)
{
	JsonArray *categories;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		categories = e_m365_event_get_categories (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		categories = e_m365_task_get_categories (m365_object);
		break;
	default:
		g_warn_if_reached ();
		return;
	}

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
ecb_m365_add_categories (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 ETimezoneCache *timezone_cache,
			 ICalComponent *new_comp,
			 ICalComponent *old_comp,
			 ICalPropertyKind prop_kind,
			 JsonBuilder *builder)
{
	GHashTable *old_value = NULL;
	GSList *new_value = NULL;
	void (* begin_categories_func) (JsonBuilder *builder);
	void (* end_categories_func) (JsonBuilder *builder);
	void (* add_category_func) (JsonBuilder *builder, const gchar *category);

	switch (i_cal_component_isa (new_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		begin_categories_func = e_m365_event_begin_categories;
		end_categories_func = e_m365_event_end_categories;
		add_category_func = e_m365_event_add_category;
		break;
	case I_CAL_VTODO_COMPONENT:
		begin_categories_func = e_m365_task_begin_categories;
		end_categories_func = e_m365_task_end_categories;
		add_category_func = e_m365_task_add_category;
		break;
	default:
		g_warn_if_reached ();
		return;
	}

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
			begin_categories_func (builder);

			for (link = new_value; link; link = g_slist_next (link)) {
				const gchar *category = link->data;

				add_category_func (builder, category);
			}

			end_categories_func (builder);
		}
	} else {
		begin_categories_func (builder);
		end_categories_func (builder);
	}

	if (new_value)
		g_slist_free_full (new_value, g_free);
	if (old_value)
		g_hash_table_destroy (old_value);
}

static void
ecb_m365_get_subject (EM365Connection *cnc,
		      const gchar *group_id,
		      const gchar *folder_id,
		      const gchar *attachments_dir,
		      ETimezoneCache *timezone_cache,
		      EM365Event *m365_object,
		      ICalComponent *inout_comp,
		      ICalPropertyKind prop_kind)
{
	const gchar *subject;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		subject = e_m365_event_get_subject (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		subject = e_m365_task_get_title (m365_object);
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	if (subject)
		i_cal_component_set_summary (inout_comp, subject);
}

static void
ecb_m365_add_subject (EM365Connection *cnc,
		      const gchar *group_id,
		      const gchar *folder_id,
		      ETimezoneCache *timezone_cache,
		      ICalComponent *new_comp,
		      ICalComponent *old_comp,
		      ICalPropertyKind prop_kind,
		      JsonBuilder *builder)
{
	const gchar *new_value, *old_value;

	new_value = i_cal_component_get_summary (new_comp);
	old_value = old_comp ? i_cal_component_get_summary (old_comp) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0) {
		switch (i_cal_component_isa (new_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			e_m365_event_add_subject (builder, new_value ? new_value : "");
			break;
		case I_CAL_VTODO_COMPONENT:
			e_m365_task_add_title (builder, new_value ? new_value : "");
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	}
}

static void
ecb_m365_get_body (EM365Connection *cnc,
		   const gchar *group_id,
		   const gchar *folder_id,
		   const gchar *attachments_dir,
		   ETimezoneCache *timezone_cache,
		   JsonObject *m365_object,
		   ICalComponent *inout_comp,
		   ICalPropertyKind prop_kind)
{
	EM365ItemBody *value;
	const gchar *content;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		value = e_m365_event_get_body (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		value = e_m365_task_get_body (m365_object);
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	content = value ? e_m365_item_body_get_content (value) : NULL;

	if (content && *content && strcmp (content, "\r\n") != 0)
		i_cal_component_set_description (inout_comp, content);
}

static void
ecb_m365_add_body (EM365Connection *cnc,
		   const gchar *group_id,
		   const gchar *folder_id,
		   ETimezoneCache *timezone_cache,
		   ICalComponent *new_comp,
		   ICalComponent *old_comp,
		   ICalPropertyKind prop_kind,
		   JsonBuilder *builder)
{
	const gchar *new_value, *old_value;

	new_value = i_cal_component_get_description (new_comp);
	old_value = old_comp ? i_cal_component_get_description (old_comp) : NULL;

	if (g_strcmp0 (new_value, old_value) != 0) {
		switch (i_cal_component_isa (new_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			e_m365_event_add_body (builder, E_M365_ITEM_BODY_CONTENT_TYPE_TEXT, new_value);
			break;
		case I_CAL_VTODO_COMPONENT:
			e_m365_task_add_body (builder, E_M365_ITEM_BODY_CONTENT_TYPE_TEXT, new_value);
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	}
}

static void
ecb_m365_get_sensitivity (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  const gchar *attachments_dir,
			  ETimezoneCache *timezone_cache,
			  JsonObject *m365_object,
			  ICalComponent *inout_comp,
			  ICalPropertyKind prop_kind)
{
	EM365SensitivityType value;
	ICalProperty_Class cls = I_CAL_CLASS_NONE;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		value = e_m365_event_get_sensitivity (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		return;
	default:
		g_warn_if_reached ();
		return;
	}

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
ecb_m365_add_sensitivity (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  ETimezoneCache *timezone_cache,
			  ICalComponent *new_comp,
			  ICalComponent *old_comp,
			  ICalPropertyKind prop_kind,
			  JsonBuilder *builder)
{
	ICalProperty_Class new_value = I_CAL_CLASS_NONE, old_value = I_CAL_CLASS_NONE;
	ICalProperty *prop;

	if (i_cal_component_isa (new_comp) == I_CAL_VTODO_COMPONENT)
		return;

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

		switch (i_cal_component_isa (new_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			e_m365_event_add_sensitivity (builder, value);
			break;
		case I_CAL_VTODO_COMPONENT:
			break;
		default:
			g_warn_if_reached ();
			return;
		}
	}
}

static void
ecb_m365_get_show_as (EM365Connection *cnc,
		      const gchar *group_id,
		      const gchar *folder_id,
		      const gchar *attachments_dir,
		      ETimezoneCache *timezone_cache,
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
ecb_m365_add_show_as (EM365Connection *cnc,
		      const gchar *group_id,
		      const gchar *folder_id,
		      ETimezoneCache *timezone_cache,
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
ecb_m365_get_location (EM365Connection *cnc,
		       const gchar *group_id,
		       const gchar *folder_id,
		       const gchar *attachments_dir,
		       ETimezoneCache *timezone_cache,
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
ecb_m365_add_location (EM365Connection *cnc,
		       const gchar *group_id,
		       const gchar *folder_id,
		       ETimezoneCache *timezone_cache,
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
ecb_m365_get_organizer (EM365Connection *cnc,
			const gchar *group_id,
			const gchar *folder_id,
			const gchar *attachments_dir,
			ETimezoneCache *timezone_cache,
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
ecb_m365_add_organizer (EM365Connection *cnc,
			const gchar *group_id,
			const gchar *folder_id,
			ETimezoneCache *timezone_cache,
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
ecb_m365_get_attendees (EM365Connection *cnc,
			const gchar *group_id,
			const gchar *folder_id,
			const gchar *attachments_dir,
			ETimezoneCache *timezone_cache,
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
ecb_m365_add_attendees (EM365Connection *cnc,
			const gchar *group_id,
			const gchar *folder_id,
			ETimezoneCache *timezone_cache,
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
ecb_m365_get_importance (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 const gchar *attachments_dir,
			 ETimezoneCache *timezone_cache,
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
ecb_m365_add_importance (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 ETimezoneCache *timezone_cache,
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
ecb_m365_get_event_status (EM365Connection *cnc,
			   const gchar *group_id,
			   const gchar *folder_id,
			   const gchar *attachments_dir,
			   ETimezoneCache *timezone_cache,
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

	if (by_pos != -2) {
		#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
		i_cal_recurrence_resize_by_array (recr, I_CAL_BY_SET_POS, 1);
		i_cal_recurrence_set_by (recr, I_CAL_BY_SET_POS, 0, by_pos);
		#else
		i_cal_recurrence_set_by_set_pos (recr, 0, by_pos);
		i_cal_recurrence_set_by_set_pos (recr, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		#endif
	}
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
			#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
			i_cal_recurrence_resize_by_array (recr, I_CAL_BY_DAY, ii + 1);
			i_cal_recurrence_set_by (recr, I_CAL_BY_DAY, ii, week_day);
			#else
			i_cal_recurrence_set_by_day (recr, ii, week_day);
			#endif

			ii++;
		}
	}

	#ifndef HAVE_I_CAL_RECURRENCE_GET_BY
	i_cal_recurrence_set_by_day (recr, ii, I_CAL_RECURRENCE_ARRAY_MAX);
	#endif
}

static void
ecb_m365_add_days_of_week_from_ical (JsonBuilder *builder,
				     ICalRecurrence *recr)
{
	gint ii, sz;

	#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
	sz = i_cal_recurrence_get_by_array_size (recr, I_CAL_BY_DAY);
	#else
	for (sz = 0; i_cal_recurrence_get_by_day (recr, sz) != I_CAL_RECURRENCE_ARRAY_MAX; sz++) {
		/* only count them */
	}
	#endif

	e_m365_recurrence_pattern_begin_days_of_week (builder);

	for (ii = 0; ii < sz; ii++) {
		ICalRecurrenceWeekday week_day;
		EM365DayOfWeekType m365_week_day;

		#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
		week_day = i_cal_recurrence_get_by (recr, I_CAL_BY_DAY, ii);
		#else
		week_day = i_cal_recurrence_get_by_day (recr, ii);
		#endif

		m365_week_day = ecb_m365_day_of_week_from_ical (week_day);

		if (m365_week_day != E_M365_DAY_OF_WEEK_UNKNOWN)
			e_m365_recurrence_pattern_add_day_of_week (builder, m365_week_day);
	}

	e_m365_recurrence_pattern_end_days_of_week (builder);
}

static gboolean
ecb_m365_get_recurrence (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 const gchar *attachments_dir,
			 ETimezoneCache *timezone_cache,
			 JsonObject *m365_object,
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
	gshort value;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		m365_recr = e_m365_event_get_recurrence (m365_object);
		break;
	case I_CAL_VTODO_COMPONENT:
		m365_recr = e_m365_task_get_recurrence (m365_object);
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

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
		value = e_m365_recurrence_pattern_get_day_of_month (m365_pattern);

		i_cal_recurrence_set_freq (ical_recr, I_CAL_MONTHLY_RECURRENCE);
		i_cal_recurrence_set_interval (ical_recr, e_m365_recurrence_pattern_get_interval (m365_pattern));

		#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
		i_cal_recurrence_resize_by_array (ical_recr, I_CAL_BY_MONTH_DAY, 1);
		i_cal_recurrence_set_by (ical_recr, I_CAL_BY_MONTH_DAY, 0, value);
		#else
		i_cal_recurrence_set_by_month_day (ical_recr, 0, value);
		#endif
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

		value = e_m365_recurrence_pattern_get_day_of_month (m365_pattern);

		#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
		i_cal_recurrence_resize_by_array (ical_recr, I_CAL_BY_MONTH_DAY, 1);
		i_cal_recurrence_set_by (ical_recr, I_CAL_BY_MONTH_DAY, 0, value);
		#else
		i_cal_recurrence_set_by_month_day (ical_recr, 0, value);
		#endif

		month = e_m365_recurrence_pattern_get_month (m365_pattern);

		if (month >= 1 && month <= 12) {
			#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
			i_cal_recurrence_resize_by_array (ical_recr, I_CAL_BY_MONTH, 1);
			i_cal_recurrence_set_by (ical_recr, I_CAL_BY_MONTH, 0, month);
			#else
			i_cal_recurrence_set_by_month (ical_recr, 0, month);
			#endif
		}
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

		if (month >= 1 && month <= 12) {
			#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
			i_cal_recurrence_resize_by_array (ical_recr, I_CAL_BY_MONTH, 1);
			i_cal_recurrence_set_by (ical_recr, I_CAL_BY_MONTH, 0, month);
			#else
			i_cal_recurrence_set_by_month (ical_recr, 0, month);
			#endif
		}
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
			gint yy = 0, mm = 0, dd = 0;

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
ecb_m365_add_recurrence (EM365Connection *cnc,
			 const gchar *group_id,
			 const gchar *folder_id,
			 ETimezoneCache *timezone_cache,
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
	void (* begin_recurrence_func) (JsonBuilder *builder);
	void (* end_recurrence_func) (JsonBuilder *builder);
	void (* add_null_recurrence_func) (JsonBuilder *builder);

	switch (i_cal_component_isa (new_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		begin_recurrence_func = e_m365_event_begin_recurrence;
		end_recurrence_func = e_m365_event_end_recurrence;
		add_null_recurrence_func = e_m365_event_add_null_recurrence;
		break;
	case I_CAL_VTODO_COMPONENT:
		begin_recurrence_func = e_m365_task_begin_recurrence;
		end_recurrence_func = e_m365_task_end_recurrence;
		add_null_recurrence_func = e_m365_task_add_null_recurrence;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	if (i_cal_component_count_properties (new_comp, prop_kind) > 1) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Microsoft 365 calendar cannot store more than one recurrence")));

		return FALSE;
	}

	if (i_cal_component_count_properties (new_comp, I_CAL_RDATE_PROPERTY) > 0 ||
	    i_cal_component_count_properties (new_comp, I_CAL_EXRULE_PROPERTY) > 0) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Microsoft 365 calendar cannot store component with RDATE or EXRULE properties")));

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
			gboolean has_by_pos;

			begin_recurrence_func (builder);
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
				#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
				has_by_pos = i_cal_recurrence_get_by_array_size (new_rrule, I_CAL_BY_SET_POS) > 0;
				by_pos = has_by_pos ? i_cal_recurrence_get_by (new_rrule, I_CAL_BY_SET_POS, 0) : 0;
				#else
				by_pos = i_cal_recurrence_get_by_set_pos (new_rrule, 0);
				has_by_pos = by_pos != I_CAL_RECURRENCE_ARRAY_MAX;
				#endif

				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));

				if (!has_by_pos) {
					gshort bymonthday;

					#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
					bymonthday = i_cal_recurrence_get_by (new_rrule, I_CAL_BY_MONTH_DAY, 0);
					#else
					bymonthday = i_cal_recurrence_get_by_month_day (new_rrule, 0);
					#endif

					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY);
					e_m365_recurrence_pattern_add_day_of_month (builder, bymonthday);
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
				#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
				has_by_pos = i_cal_recurrence_get_by_array_size (new_rrule, I_CAL_BY_SET_POS) > 0;
				by_pos = has_by_pos ? i_cal_recurrence_get_by (new_rrule, I_CAL_BY_SET_POS, 0) : 0;
				month = i_cal_recurrence_get_by_array_size (new_rrule, I_CAL_BY_MONTH) > 0 ?
					i_cal_recurrence_get_by (new_rrule, I_CAL_BY_MONTH, 0) : -1;
				#else
				by_pos = i_cal_recurrence_get_by_set_pos (new_rrule, 0);
				has_by_pos = by_pos != I_CAL_RECURRENCE_ARRAY_MAX;
				month = i_cal_recurrence_get_by_month (new_rrule, 0);
				#endif

				e_m365_recurrence_pattern_add_interval (builder, i_cal_recurrence_get_interval (new_rrule));

				if (month >= 1 && month <= 12)
					e_m365_recurrence_pattern_add_month (builder, month);

				if (!has_by_pos) {
					gshort bymonthday;

					#ifdef HAVE_I_CAL_RECURRENCE_GET_BY
					bymonthday = i_cal_recurrence_get_by (new_rrule, I_CAL_BY_MONTH_DAY, 0);
					#else
					bymonthday = i_cal_recurrence_get_by_month_day (new_rrule, 0);
					#endif

					e_m365_recurrence_pattern_add_type (builder, E_M365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY);
					e_m365_recurrence_pattern_add_day_of_month (builder, bymonthday);
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

				yy = 0;
				mm = 0;
				dd = 0;
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
			end_recurrence_func (builder);
		}

		g_clear_object (&new_rrule);
	} else {
		add_null_recurrence_func (builder);
	}

	g_clear_object (&new_value);
	g_clear_object (&old_value);

	return success;
}

static gboolean
ecb_m365_get_reminder (EM365Connection *cnc,
		       const gchar *group_id,
		       const gchar *folder_id,
		       const gchar *attachments_dir,
		       ETimezoneCache *timezone_cache,
		       EM365Event *m365_object,
		       ICalComponent *inout_comp,
		       ICalPropertyKind prop_kind,
		       GCancellable *cancellable,
		       GError **error)
{
	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		if (e_m365_event_get_is_reminder_on (m365_object)) {
			ECalComponentAlarm *alarm;
			ECalComponentAlarmTrigger *trigger;
			ICalDuration *duration;

			duration = i_cal_duration_new_from_int (-60 * e_m365_event_get_reminder_minutes_before_start (m365_object));
			trigger = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, duration);
			g_object_unref (duration);

			alarm = e_cal_component_alarm_new ();
			e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
			e_cal_component_alarm_take_summary (alarm, e_cal_component_text_new (e_m365_event_get_subject (m365_object), NULL));
			e_cal_component_alarm_take_description (alarm, e_cal_component_text_new (e_m365_event_get_subject (m365_object), NULL));
			e_cal_component_alarm_take_trigger (alarm, trigger);

			i_cal_component_take_component (inout_comp, e_cal_component_alarm_get_as_component (alarm));

			e_cal_component_alarm_free (alarm);
		}
		break;
	case I_CAL_VTODO_COMPONENT:
		if (e_m365_task_get_is_reminder_on (m365_object)) {
			EM365DateTimeWithZone *reminder_dt;

			reminder_dt = e_m365_task_get_reminder_date_time (m365_object);

			if (reminder_dt) {
				ECalComponentAlarm *alarm;
				ECalComponentAlarmTrigger *trigger;
				ICalTimezone *tz;
				ICalTime *itt;
				time_t tt;
				const gchar *zone;

				tt = e_m365_date_time_get_date_time (reminder_dt);
				zone = e_m365_date_time_get_time_zone (reminder_dt);

				if (zone && *zone)
					zone = e_m365_tz_utils_get_ical_equivalent (zone);

				tz = zone && *zone ? e_timezone_cache_get_timezone (timezone_cache, zone) : NULL;

				if (!tz)
					tz = i_cal_timezone_get_utc_timezone ();

				itt = i_cal_time_new_from_timet_with_zone (tt, FALSE, tz);
				trigger = e_cal_component_alarm_trigger_new_absolute (itt);
				g_object_unref (itt);

				alarm = e_cal_component_alarm_new ();
				e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
				e_cal_component_alarm_take_summary (alarm, e_cal_component_text_new (e_m365_task_get_title (m365_object), NULL));
				e_cal_component_alarm_take_description (alarm, e_cal_component_text_new (e_m365_task_get_title (m365_object), NULL));
				e_cal_component_alarm_take_trigger (alarm, trigger);

				i_cal_component_take_component (inout_comp, e_cal_component_alarm_get_as_component (alarm));

				e_cal_component_alarm_free (alarm);
			}
		}
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	return TRUE;
}

static gboolean
ecb_m365_add_reminder (EM365Connection *cnc,
		       const gchar *group_id,
		       const gchar *folder_id,
		       ETimezoneCache *timezone_cache,
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
		g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Microsoft 365 calendar cannot store more than one event reminder")));
		return FALSE;
	}

	new_value = i_cal_component_get_first_component (new_comp, I_CAL_VALARM_COMPONENT);
	old_value = old_comp ? i_cal_component_get_first_component (old_comp, I_CAL_VALARM_COMPONENT) : NULL;

	if (!new_value && !old_value)
		return TRUE;

	if (new_value) {
		ECalComponentAlarm *new_alarm;
		ECalComponentAlarmTrigger *new_trigger;
		ICalComponentKind kind;
		ICalDuration *new_duration = NULL;
		ICalTime *new_absolute_time = NULL;
		gboolean changed = TRUE;

		kind = i_cal_component_isa (new_comp);

		new_alarm = e_cal_component_alarm_new_from_component (new_value);
		new_trigger = new_alarm ? e_cal_component_alarm_get_trigger (new_alarm) : NULL;

		switch (kind) {
		case I_CAL_VEVENT_COMPONENT:
			success = new_trigger && e_cal_component_alarm_trigger_get_kind (new_trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
			if (success) {
				new_duration = e_cal_component_alarm_trigger_get_duration (new_trigger);

				success = new_duration && i_cal_duration_as_int (new_duration) <= 0;
			}

			if (!success) {
				g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Microsoft 365 event can have only a reminder before event start")));
			}
			break;
		case I_CAL_VTODO_COMPONENT:
			success = new_trigger && e_cal_component_alarm_trigger_get_kind (new_trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE;

			if (success) {
				new_absolute_time = e_cal_component_alarm_trigger_get_absolute_time (new_trigger);

				success = new_absolute_time != NULL;
			}

			if (!success) {
				g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Microsoft 365 task can have only a reminder with absolute time")));
			}
			break;
		default:
			g_warn_if_reached ();
			success = FALSE;
			break;
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
					ICalTime *old_absolute_time;

					switch (kind) {
					case I_CAL_VEVENT_COMPONENT:
						old_duration = e_cal_component_alarm_trigger_get_duration (old_trigger);

						changed = !old_duration || i_cal_duration_as_int (new_duration) != i_cal_duration_as_int (old_duration);
						break;
					case I_CAL_VTODO_COMPONENT:
						old_absolute_time = e_cal_component_alarm_trigger_get_absolute_time (old_trigger);

						changed = !old_absolute_time || i_cal_time_compare (new_absolute_time, old_absolute_time) != 0;
						break;
					default:
						g_warn_if_reached ();
						changed = FALSE;
						break;
					}
				}
			}

			e_cal_component_alarm_free (old_alarm);
		}

		if (success && changed) {
			ICalTimezone *izone = NULL;
			const gchar *wzone = NULL;
			time_t tt;

			switch (kind) {
			case I_CAL_VEVENT_COMPONENT:
				e_m365_event_add_is_reminder_on (builder, TRUE);
				e_m365_event_add_reminder_minutes_before_start (builder, i_cal_duration_as_int (new_duration) / -60);
				break;
			case I_CAL_VTODO_COMPONENT:
				izone = i_cal_time_get_timezone (new_absolute_time);

				if (izone)
					wzone = e_m365_tz_utils_get_msdn_equivalent (i_cal_timezone_get_location (izone));

				tt = i_cal_time_as_timet_with_zone (new_absolute_time, wzone ? NULL : izone);

				e_m365_task_add_is_reminder_on (builder, TRUE);
				e_m365_task_add_reminder_date_time (builder, tt, wzone);
				break;
			default:
				g_warn_if_reached ();
				break;
			}
		}

		e_cal_component_alarm_free (new_alarm);
	} else {
		switch (i_cal_component_isa (new_comp)) {
		case I_CAL_VEVENT_COMPONENT:
			e_m365_event_add_is_reminder_on (builder, FALSE);
			break;
		case I_CAL_VTODO_COMPONENT:
			e_m365_task_add_is_reminder_on (builder, FALSE);
			break;
		default:
			g_warn_if_reached ();
			break;
		}
	}

	g_clear_object (&new_value);
	g_clear_object (&old_value);

	return success;
}

static gboolean
ecb_m365_add_online_meeting (EM365Connection *cnc,
			     const gchar *group_id,
			     const gchar *folder_id,
			     ETimezoneCache *timezone_cache,
			     ICalComponent *new_comp,
			     ICalComponent *old_comp,
			     ICalPropertyKind prop_kind,
			     const gchar *m365_id,
			     JsonBuilder *builder,
			     GCancellable *cancellable,
			     GError **error)
{
	/* This can be set only for new events */
	if (old_comp)
		return TRUE;

	if (e_cal_util_component_has_x_property (new_comp, "X-M365-ONLINE-MEETING")) {
		e_m365_event_add_is_online_meeting (builder, TRUE);
		e_m365_event_add_online_meeting_provider (builder, E_M365_ONLINE_MEETING_PROVIDER_TEAMS_FOR_BUSINESS);
	}

	return TRUE;
}

static gchar *
ecb_m365_calc_hash (const gchar *value)
{
	gchar *hash;

	if (!value)
		return g_strdup ("empty");

	/* the MD5 is fine here, the id-s are not that long to cause the collision;
	   it's also short, which is for good */
	hash = g_compute_checksum_for_string (G_CHECKSUM_MD5, value, -1);
	if (!hash)
		hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, value, -1);
	if (!hash)
		hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, value, -1);

	/* fallback when cannot compute the hash */
	if (!hash) {
		gchar *ptr;

		hash = g_strdup (value);

		/* remove forward- and back-slashes from the string */
		for (ptr = hash; *ptr; ptr++) {
			if (*ptr == '/' || *ptr == '\\')
				*ptr = '_';
		}
	}

	return hash;
}

static gboolean
ecb_m365_get_attachments (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  const gchar *attachments_dir,
			  ETimezoneCache *timezone_cache,
			  JsonObject *m365_object,
			  ICalComponent *inout_comp,
			  ICalPropertyKind prop_kind,
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *attachments = NULL, *link;
	const gchar *id;
	gchar *event_id_hash;
	gboolean success = TRUE;

	switch (i_cal_component_isa (inout_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		if (!e_m365_event_get_has_attachments (m365_object))
			return TRUE;

		id = e_m365_event_get_id (m365_object);

		if (!e_m365_connection_list_event_attachments_sync (cnc, NULL,
			group_id, folder_id, id, "id,name,contentType",
			&attachments, cancellable, error)) {
			return FALSE;
		}
		break;
	case I_CAL_VTODO_COMPONENT:
		return TRUE;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	event_id_hash = ecb_m365_calc_hash (id);

	for (link = attachments; link && success; link = g_slist_next (link)) {
		CamelStream *content_stream;
		EM365Attachment *m365_attach = link->data;
		gchar *filename, *attach_id_hash;

		if (!m365_attach || e_m365_attachment_get_data_type (m365_attach) != E_M365_ATTACHMENT_DATA_TYPE_FILE ||
		    !e_m365_attachment_get_name (m365_attach))
			continue;

		attach_id_hash = ecb_m365_calc_hash (e_m365_attachment_get_id (m365_attach));

		filename = g_build_filename (attachments_dir, event_id_hash, NULL);
		g_mkdir_with_parents (filename, 0777);
		g_free (filename);

		filename = g_build_filename (attachments_dir, event_id_hash, attach_id_hash, NULL);

		content_stream = camel_stream_fs_new_with_name (filename, O_CREAT | O_TRUNC | O_WRONLY, 0666, error);

		if (content_stream) {
			success = e_m365_connection_get_event_attachment_sync (cnc, NULL, group_id, folder_id, id,
				e_m365_attachment_get_id (m365_attach), e_m365_connection_util_read_raw_data_cb, content_stream,
				cancellable, error);

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
		g_free (attach_id_hash);
	}

	g_slist_free_full (attachments, (GDestroyNotify) json_object_unref);
	g_free (event_id_hash);

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
ecb_m365_add_attachments (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  ETimezoneCache *timezone_cache,
			  ICalComponent *new_comp,
			  ICalComponent *old_comp,
			  ICalPropertyKind prop_kind,
			  const gchar *m365_id,
			  JsonBuilder *in_builder,
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *new_attachs = NULL;
	GHashTable *old_attachs = NULL;
	gboolean (* add_attachment_func) (EM365Connection *cnc,
					  const gchar *user_override,
					  const gchar *group_id,
					  const gchar *folder_id,
					  const gchar *item_id,
					  JsonBuilder *in_attachment,
					  EM365Attachment **out_attachment,
					  GCancellable *cancellable,
					  GError **error);
	gboolean (* delete_attachment_func) (EM365Connection *cnc,
					     const gchar *user_override,
					     const gchar *group_id,
					     const gchar *folder_id,
					     const gchar *item_id,
					     const gchar *attachment_id,
					     GCancellable *cancellable,
					     GError **error);
	gboolean success = TRUE;

	switch (i_cal_component_isa (new_comp)) {
	case I_CAL_VEVENT_COMPONENT:
		add_attachment_func = e_m365_connection_add_event_attachment_sync;
		delete_attachment_func = e_m365_connection_delete_event_attachment_sync;
		break;
	case I_CAL_VTODO_COMPONENT:
		if (!e_cal_util_component_has_property (new_comp, I_CAL_ATTACH_PROPERTY))
			return TRUE;
		g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED,
			_("Microsoft 365 task cannot have attachments."));
		return FALSE;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

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
			JsonBuilder *attbuilder = NULL;

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

							attbuilder = json_builder_new_immutable ();
							e_m365_attachment_begin_attachment (attbuilder, E_M365_ATTACHMENT_DATA_TYPE_FILE);
							e_m365_file_attachment_add_content_bytes (attbuilder, (const gchar *) bytes->data);
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
					attbuilder = json_builder_new_immutable ();
					e_m365_attachment_begin_attachment (attbuilder, E_M365_ATTACHMENT_DATA_TYPE_FILE);
					e_m365_file_attachment_add_content_bytes (attbuilder, base64_data);
				} else {
					g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Failed to get inline attachment data")));
				}
			}

			if (attbuilder) {
				ICalParameter *param;
				const gchar *tmp;

				param = i_cal_property_get_first_parameter (prop, I_CAL_FILENAME_PARAMETER);

				if (param) {
					tmp = i_cal_parameter_get_filename (param);

					if (tmp && *tmp)
						e_m365_attachment_add_name (attbuilder, tmp);

					g_clear_object (&param);
				}

				param = i_cal_property_get_first_parameter (prop, I_CAL_FMTTYPE_PARAMETER);

				if (param) {
					tmp = i_cal_parameter_get_fmttype (param);

					if (tmp && *tmp)
						e_m365_attachment_add_content_type (attbuilder, tmp);
					else
						e_m365_attachment_add_content_type (attbuilder, "application/octet-stream");

					g_clear_object (&param);
				} else {
					e_m365_attachment_add_content_type (attbuilder, "application/octet-stream");
				}

				e_m365_attachment_end_attachment (attbuilder);

				success = add_attachment_func (cnc, NULL,
					group_id, folder_id, m365_id,
					attbuilder, NULL, cancellable, error);

				g_object_unref (attbuilder);
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

			success = delete_attachment_func (cnc, NULL,
				group_id, folder_id, i_cal_component_get_uid (new_comp),
				attachment_id, cancellable, error);
		}
	}

	if (old_attachs)
		g_hash_table_destroy (old_attachs);
	g_slist_free_full (new_attachs, g_object_unref);

	return success;
}

static void
ecb_m365_get_task_status (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  const gchar *attachments_dir,
			  ETimezoneCache *timezone_cache,
			  EM365Task *m365_task,
			  ICalComponent *inout_comp,
			  ICalPropertyKind prop_kind)
{
	ICalPropertyStatus status = I_CAL_STATUS_NONE;

	switch (e_m365_task_get_status (m365_task)) {
	case E_M365_STATUS_NOT_STARTED:
		break;
	case E_M365_STATUS_IN_PROGRESS:
	case E_M365_STATUS_WAITING_ON_OTHERS:
		status = I_CAL_STATUS_INPROCESS;
		break;
	case E_M365_STATUS_COMPLETED:
		status = I_CAL_STATUS_COMPLETED;
		break;
	case E_M365_STATUS_DEFERRED:
		status = I_CAL_STATUS_CANCELLED;
		break;
	default:
		break;
	}

	if (status != I_CAL_STATUS_NONE)
		i_cal_component_take_property (inout_comp, i_cal_property_new_status (status));
}

static void
ecb_m365_add_task_status (EM365Connection *cnc,
			  const gchar *group_id,
			  const gchar *folder_id,
			  ETimezoneCache *timezone_cache,
			  ICalComponent *new_comp,
			  ICalComponent *old_comp,
			  ICalPropertyKind prop_kind,
			  JsonBuilder *builder)
{
	ICalProperty *new_prop, *old_prop;
	ICalPropertyStatus new_value, old_value;

	new_prop = i_cal_component_get_first_property (new_comp, prop_kind);
	old_prop = old_comp ? i_cal_component_get_first_property (old_comp, prop_kind) : NULL;

	if (!new_prop && !old_prop)
		return;

	new_value = new_prop ? i_cal_property_get_status (new_prop) : I_CAL_STATUS_NONE;
	old_value = old_prop ? i_cal_property_get_status (old_prop) : I_CAL_STATUS_NONE;

	if (new_value != old_value) {
		EM365StatusType value = E_M365_STATUS_UNKNOWN;

		switch (new_value) {
		case I_CAL_STATUS_NONE:
			value = E_M365_STATUS_NOT_STARTED;
			break;
		case I_CAL_STATUS_INPROCESS:
			value = E_M365_STATUS_IN_PROGRESS;
			break;
		case I_CAL_STATUS_COMPLETED:
			value = E_M365_STATUS_COMPLETED;
			break;
		case I_CAL_STATUS_CANCELLED:
			value = E_M365_STATUS_DEFERRED;
			break;
		default:
			break;
		}

		if (value != E_M365_STATUS_UNKNOWN)
			e_m365_task_add_status (builder, value);
	}

	g_clear_object (&new_prop);
	g_clear_object (&old_prop);
}

#define SIMPLE_FIELD(propknd, getfn, addfn) { propknd, FALSE, getfn, NULL, addfn, NULL }
#define COMPLEX_FIELD(propknd, getfn, addfn) { propknd, FALSE, NULL, getfn, NULL, addfn }
#define COMPLEX_FIELD_2(propknd, getfn, addfn) { propknd, TRUE, NULL, getfn, NULL, addfn }

struct _mappings {
	ICalPropertyKind prop_kind;
	gboolean add_in_second_go;
	void		(* get_simple_func)	(EM365Connection *cnc,
						 const gchar *group_id,
						 const gchar *folder_id,
						 const gchar *attachments_dir,
						 ETimezoneCache *timezone_cache,
						 EM365Event *m365_event,
						 ICalComponent *inout_comp,
						 ICalPropertyKind prop_kind);
	gboolean	(* get_func)		(EM365Connection *cnc,
						 const gchar *group_id,
						 const gchar *folder_id,
						 const gchar *attachments_dir,
						 ETimezoneCache *timezone_cache,
						 EM365Event *m365_event,
						 ICalComponent *inout_comp,
						 ICalPropertyKind prop_kind,
						 GCancellable *cancellable,
						 GError **error);
	void		(* add_simple_func)	(EM365Connection *cnc,
						 const gchar *group_id,
						 const gchar *folder_id,
						 ETimezoneCache *timezone_cache,
						 ICalComponent *new_comp,
						 ICalComponent *old_comp, /* nullable */
						 ICalPropertyKind prop_kind,
						 JsonBuilder *builder);
	gboolean	(* add_func)		(EM365Connection *cnc,
						 const gchar *group_id,
						 const gchar *folder_id,
						 ETimezoneCache *timezone_cache,
						 ICalComponent *new_comp,
						 ICalComponent *old_comp, /* nullable */
						 ICalPropertyKind prop_kind,
						 const gchar *m365_id,
						 JsonBuilder *builder,
						 GCancellable *cancellable,
						 GError **error);
} event_mappings[] = {
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
	SIMPLE_FIELD	(I_CAL_STATUS_PROPERTY,		ecb_m365_get_event_status,	NULL),
	COMPLEX_FIELD	(I_CAL_RRULE_PROPERTY,		ecb_m365_get_recurrence,	ecb_m365_add_recurrence),
	COMPLEX_FIELD	(I_CAL_X_PROPERTY,		ecb_m365_get_reminder,		ecb_m365_add_reminder),
	COMPLEX_FIELD	(I_CAL_X_PROPERTY,		NULL,				ecb_m365_add_online_meeting),
	COMPLEX_FIELD_2	(I_CAL_ATTACH_PROPERTY,		ecb_m365_get_attachments,	ecb_m365_add_attachments)
}, task_mappings[] = {
	SIMPLE_FIELD	(I_CAL_UID_PROPERTY,		ecb_m365_get_uid,		NULL),
	SIMPLE_FIELD	(I_CAL_CREATED_PROPERTY,	ecb_m365_get_date_time,		NULL),
	SIMPLE_FIELD	(I_CAL_LASTMODIFIED_PROPERTY,	ecb_m365_get_date_time,		NULL),
	SIMPLE_FIELD	(I_CAL_DTSTART_PROPERTY,	ecb_m365_get_date_time_zone,	ecb_m365_add_date_time_zone),
	SIMPLE_FIELD	(I_CAL_DUE_PROPERTY,		ecb_m365_get_date_time_zone,	ecb_m365_add_date_time_zone),
	SIMPLE_FIELD	(I_CAL_COMPLETED_PROPERTY,	ecb_m365_get_date_time_zone,	ecb_m365_add_date_time_zone),
	SIMPLE_FIELD	(I_CAL_CATEGORIES_PROPERTY,	ecb_m365_get_categories,	ecb_m365_add_categories),
	SIMPLE_FIELD	(I_CAL_SUMMARY_PROPERTY,	ecb_m365_get_subject,		ecb_m365_add_subject),
	SIMPLE_FIELD	(I_CAL_DESCRIPTION_PROPERTY,	ecb_m365_get_body,		ecb_m365_add_body),
	SIMPLE_FIELD	(I_CAL_CLASS_PROPERTY,		ecb_m365_get_sensitivity,	ecb_m365_add_sensitivity),
	SIMPLE_FIELD	(I_CAL_STATUS_PROPERTY,		ecb_m365_get_task_status,	ecb_m365_add_task_status),
	COMPLEX_FIELD	(I_CAL_RRULE_PROPERTY,		ecb_m365_get_recurrence,	ecb_m365_add_recurrence),
	COMPLEX_FIELD	(I_CAL_X_PROPERTY,		ecb_m365_get_reminder,		ecb_m365_add_reminder),
	COMPLEX_FIELD_2	(I_CAL_ATTACH_PROPERTY,		ecb_m365_get_attachments,	ecb_m365_add_attachments)
};

static const struct _mappings *
ecb_m365_get_mappings_for_kind (ICalComponentKind kind,
				guint *out_n_elements)
{
	if (kind == I_CAL_VEVENT_COMPONENT) {
		*out_n_elements = G_N_ELEMENTS (event_mappings);
		return event_mappings;
	}

	if (kind == I_CAL_VTODO_COMPONENT) {
		*out_n_elements = G_N_ELEMENTS (task_mappings);
		return task_mappings;
	}

	g_warn_if_reached ();

	return NULL;
}

ICalComponent *
e_cal_backend_m365_utils_json_to_ical (EM365Connection *cnc,
				       const gchar *group_id,
				       const gchar *folder_id,
				       const gchar *attachments_dir,
				       ETimezoneCache *timezone_cache,
				       ICalComponentKind kind,
				       JsonObject *m365_object,
				       GCancellable *cancellable,
				       GError **error)
{
	const struct _mappings *mappings;
	ICalComponent *icomp = NULL;
	guint ii, n_elements = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (m365_object != NULL, NULL);

	mappings = ecb_m365_get_mappings_for_kind (kind, &n_elements);
	g_return_val_if_fail (mappings != NULL, NULL);

	if (kind == I_CAL_VEVENT_COMPONENT)
		icomp = i_cal_component_new_vevent ();
	else if (kind == I_CAL_VTODO_COMPONENT)
		icomp = i_cal_component_new_vtodo ();
	else
		g_warn_if_reached ();

	if (!icomp)
		return NULL;

	for (ii = 0; success && ii < n_elements; ii++) {
		if (mappings[ii].get_simple_func) {
			mappings[ii].get_simple_func (cnc,
				group_id,
				folder_id,
				attachments_dir,
				timezone_cache,
				m365_object,
				icomp,
				mappings[ii].prop_kind);
		} else if (mappings[ii].get_func) {
			success = mappings[ii].get_func (cnc,
				group_id,
				folder_id,
				attachments_dir,
				timezone_cache,
				m365_object,
				icomp,
				mappings[ii].prop_kind,
				cancellable,
				error);
		}
	}

	if (success && kind == I_CAL_VEVENT_COMPONENT)
		ecb_m365_json_to_ical_recur_blob (m365_object, timezone_cache, &icomp);

	if (!success)
		g_clear_object (&icomp);

	return icomp;
}

JsonBuilder *
e_cal_backend_m365_utils_ical_to_json (EM365Connection *cnc,
				       const gchar *group_id,
				       const gchar *folder_id,
				       ETimezoneCache *timezone_cache,
				       ICalComponentKind kind,
				       ICalComponent *new_comp,
				       ICalComponent *old_comp, /* nullable */
				       GCancellable *cancellable,
				       GError **error)
{
	const struct _mappings *mappings;
	JsonBuilder *builder;
	guint ii, n_elements = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (new_comp != NULL, NULL);

	mappings = ecb_m365_get_mappings_for_kind (kind, &n_elements);
	g_return_val_if_fail (mappings != NULL, NULL);

	builder = json_builder_new_immutable ();
	e_m365_json_begin_object_member (builder, NULL);

	for (ii = 0; success && ii < n_elements; ii++) {
		if (mappings[ii].add_simple_func) {
			mappings[ii].add_simple_func (cnc,
				group_id,
				folder_id,
				timezone_cache,
				new_comp,
				old_comp,
				mappings[ii].prop_kind,
				builder);
		} else if (!mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (cnc,
				group_id,
				folder_id,
				timezone_cache,
				new_comp,
				old_comp,
				mappings[ii].prop_kind,
				NULL,
				builder,
				cancellable,
				error);
		}
	}

	e_m365_json_end_object_member (builder);

	if (!success)
		g_clear_object (&builder);

	return builder;
}

gboolean
e_cal_backend_m365_utils_ical_to_json_2nd_go (EM365Connection *cnc,
					      const gchar *group_id,
					      const gchar *folder_id,
					      ETimezoneCache *timezone_cache,
					      ICalComponentKind kind,
					      ICalComponent *new_comp,
					      ICalComponent *old_comp, /* nullable */
					      const gchar *m365_id,
					      GCancellable *cancellable,
					      GError **error)
{
	const struct _mappings *mappings;
	guint ii, n_elements = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (new_comp != NULL, FALSE);
	g_return_val_if_fail (m365_id != NULL, FALSE);

	mappings = ecb_m365_get_mappings_for_kind (kind, &n_elements);
	g_return_val_if_fail (mappings != NULL, FALSE);

	for (ii = 0; success && ii < n_elements; ii++) {
		if (mappings[ii].add_in_second_go && mappings[ii].add_func) {
			success = mappings[ii].add_func (cnc,
				group_id,
				folder_id,
				timezone_cache,
				new_comp,
				old_comp,
				mappings[ii].prop_kind,
				m365_id,
				NULL,
				cancellable,
				error);
		}
	}

	return success;
}
