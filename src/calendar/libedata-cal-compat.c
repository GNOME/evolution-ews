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

#include "libedata-cal-compat.h"

void
e_data_cal_respond_add_timezone_compat	(EDataCal *cal, guint32 opid, const gchar *tzid, GError *error)
{
#if ! EDS_CHECK_VERSION (3,1,0)
	e_data_cal_notify_timezone_added (cal, opid, error, tzid);
#else
	e_data_cal_respond_add_timezone (cal, opid, error);
#endif	
}


void    
e_data_cal_view_notify_objects_added_compat	(EDataCalView *view, const GSList *objects)
{
#if ! EDS_CHECK_VERSION (3,1,0)
	GList *l = NULL;
	GSList *sl;

	for (sl = objects; sl != NULL; sl = g_slist_next (sl))
		l = g_list_prepend (l, sl->data);
	l = g_list_reverse (l);

	e_data_cal_view_notify_objects_added (view, (const GList *) l);
	g_list_free (l);
#else
	e_data_cal_view_notify_objects_added (view, objects);
#endif	
}

#if ! EDS_CHECK_VERSION (3,1,0)


void		
e_data_cal_respond_get_timezone			(EDataCal *cal, guint32 opid, GError *error, const gchar *tzobject)
{
	e_data_cal_notify_timezone_requested (cal, opid, error, tzobject);
}

void		
e_data_cal_respond_discard_alarm	(EDataCal *cal, guint32 opid, GError *error)
{
	e_data_cal_notify_alarm_discarded (cal, opid, error);
}

void		
e_data_cal_respond_remove	(EDataCal *cal, guint32 opid, GError *error)
{
	e_data_cal_notify_remove (cal, opid, error);
}

void		
e_data_cal_respond_get_object	(EDataCal *cal, guint32 opid, GError *error, const gchar *object)
{
	e_data_cal_notify_object (cal, opid, error, object);
}


void		
e_data_cal_respond_get_object_list	(EDataCal *cal, guint32 opid, GError *error, const GSList *objects)
{
	GList *l = NULL;
	GSList *sl = NULL;

	for (sl = objects; sl != NULL; sl = g_slist_next (sl))
		l = g_list_prepend (l, sl->data);
	l = g_list_reverse (l);

	e_data_cal_notify_object_list (cal, opid, error, l);
	g_list_free (l);
}

void		
e_data_cal_respond_create_object	(EDataCal *cal, guint32 opid, GError *error, const gchar *uid, const gchar *object)
{
	e_cal_backend_notify_object_created (cal, opid, error, uid, object);
}

void		
e_data_cal_respond_modify_object	(EDataCal *cal, guint32 opid, GError *error, const gchar *old_object, const gchar *object)
{
	e_data_cal_notify_object_modified (cal, opid, error, old_object, object);
}


void		
e_data_cal_respond_remove_object	(EDataCal *cal, guint32 opid, GError *error, const ECalComponentId *id, const gchar *old_object, const gchar *object);
{
	e_data_cal_notify_object_removed (cal, opid, error, id, old_object, object);
}

void		
e_data_cal_respond_receive_objects	(EDataCal *cal, guint32 opid, GError *error)
{
	e_data_cal_notify_objects_received (cal, opid, error);
}

void
e_data_cal_respond_send_objects		(EDataCal *cal, guint32 opid, GError *error, const GSList *users, const gchar *calobj)
{
	GList *l;
	GSList *sl;
	
	for (sl = users; sl != NULL; sl = g_slist_next (sl))
		l = g_list_prepend (l, sl->data);
	l = g_list_reverse (l);


	e_data_cal_notify_objects_sent	(cal, opid, error, l, calobj);
	g_list_free (l);
}

void		
e_data_cal_respond_open	(EDataCal *cal, guint32 opid, GError *error)
{
	e_data_cal_notify_open (cal, opid, error);
}

void		
e_data_cal_respond_refresh	(EDataCal *cal, guint32 opid, GError *error)
{
	e_data_cal_notify_refresh (cal, opid, error);
}

void            
e_data_cal_view_notify_complete                 (EDataCalView *view, const GError *error);
{
	e_data_cal_view_notify_done (view, error);
}

#endif
