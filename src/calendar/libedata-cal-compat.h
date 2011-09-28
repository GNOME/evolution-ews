/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
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

#include <libedata-cal/e-data-cal.h>
#include <libedata-cal/e-data-cal-view.h>
#include <libedataserver/eds-version.h>

void	e_data_cal_respond_add_timezone_compat			(EDataCal *cal, guint32 opid, const gchar *tzid, GError *error);
void    e_data_cal_view_notify_objects_added_compat 		(EDataCalView *view, const GSList *objects);

#if ! EDS_CHECK_VERSION (3,1,0)

void		e_data_cal_respond_open				(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_respond_remove			(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_respond_refresh			(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_respond_get_object			(EDataCal *cal, guint32 opid, GError *error, const gchar *object);
void		e_data_cal_respond_get_object_list		(EDataCal *cal, guint32 opid, GError *error, const GSList *objects);
void		e_data_cal_respond_create_object		(EDataCal *cal, guint32 opid, GError *error, const gchar *uid, const gchar *object);
void		e_data_cal_respond_modify_object		(EDataCal *cal, guint32 opid, GError *error, const gchar *old_object, const gchar *object);
void		e_data_cal_respond_remove_object		(EDataCal *cal, guint32 opid, GError *error, const ECalComponentId *id, const gchar *old_object, const gchar *object);
void		e_data_cal_respond_receive_objects		(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_respond_send_objects			(EDataCal *cal, guint32 opid, GError *error, const GSList *users, const gchar *calobj);
void		e_data_cal_respond_discard_alarm		(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_respond_get_timezone			(EDataCal *cal, guint32 opid, GError *error, const gchar *tzobject);

void		e_data_cal_respond_get_free_busy		(EDataCal *cal, guint32 opid, GError *error);
void		e_data_cal_report_free_busy_data		(EDataCal *cal, const GSList *freebusy);
void            e_data_cal_view_notify_complete                 (EDataCalView *view, const GError *error);

#endif
