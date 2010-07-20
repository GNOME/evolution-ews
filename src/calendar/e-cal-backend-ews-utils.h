/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_CAL_BACKEND_EWS_UTILS_H
#define E_CAL_BACKEND_EWS_UTILS_H

#include <e-ews-connection.h>
#include <libecal/e-cal-component.h>
#include <e-cal-backend-ews.h>

G_BEGIN_DECLS

#define GW_EVENT_TYPE_ID "@4:"
#define GW_TODO_TYPE_ID "@3:"

/* Default reminder */
#define CALENDAR_CONFIG_PREFIX "/apps/evolution/calendar"
#define CALENDAR_CONFIG_DEFAULT_REMINDER CALENDAR_CONFIG_PREFIX "/other/use_default_reminder"
#define CALENDAR_CONFIG_DEFAULT_REMINDER_INTERVAL CALENDAR_CONFIG_PREFIX "/other/default_reminder_interval"
#define CALENDAR_CONFIG_DEFAULT_REMINDER_UNITS CALENDAR_CONFIG_PREFIX "/other/default_reminder_units"

/*
 * Items management
 */
EEwsItem       *e_ews_item_new_from_cal_component (const gchar *container, ECalBackendEws *cbews, ECalComponent *comp);
EEwsItem  *e_ews_item_new_for_delegate_from_cal (ECalBackendEws *cbews, ECalComponent *comp);
ECalComponent *e_ews_item_to_cal_component (EEwsItem *item, ECalBackendEws *cbews);
void          e_ews_item_set_changes (EEwsItem *item, EEwsItem *cached_item);

/*
 * Connection-related utility functions
 */
EEwsConnectionStatus e_ews_connection_create_appointment (EEwsConnection *cnc, const gchar *container, ECalBackendEws *cbews, ECalComponent *comp, GSList **id_list);
EEwsConnectionStatus e_ews_connection_send_appointment (ECalBackendEws *cbews, const gchar *container, ECalComponent *comp, icalproperty_method method, gboolean all_instances, ECalComponent **created_comp, icalparameter_partstat *pstatus);
EEwsConnectionStatus e_ews_connection_get_freebusy_info (ECalBackendEws *cbews, GList *users, time_t start, time_t end, GList **freebusy);
gboolean e_cal_backend_ews_store_settings (EwsSettings *hold);
gboolean e_cal_backend_ews_utils_check_delegate (ECalComponent *comp, const gchar *email);

/*
 * Component related utility functions
 */

const gchar *e_cal_component_get_ews_id (ECalComponent *comp);
G_END_DECLS

#endif
