/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
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

#ifndef E_EWS_CONNECTION_H
#define E_EWS_CONNECTION_H

#include <glib-object.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include "e-soap-message.h"
#include "ews-errors.h"
#include "e-ews-folder.h"
#include "e-ews-item.h"
#include "camel-ews-settings.h"

/* Standard GObject macros */
#define E_TYPE_EWS_CONNECTION \
	(e_ews_connection_get_type ())
#define E_EWS_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_CONNECTION, EEwsConnection))
#define E_EWS_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_CONNECTION, EEwsConnectionClass))
#define E_IS_EWS_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_CONNECTION))
#define E_IS_EWS_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_CONNECTION))
#define E_EWS_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_CONNECTION, EEwsConnectionClass))

G_BEGIN_DECLS

typedef struct _EEwsConnection EEwsConnection;
typedef struct _EEwsConnectionClass EEwsConnectionClass;
typedef struct _EEwsConnectionPrivate EEwsConnectionPrivate;

struct _EEwsConnection {
	GObject parent;
	EEwsConnectionPrivate *priv;
};

struct _EEwsConnectionClass {
	GObjectClass parent_class;
};

enum {
	EWS_PRIORITY_LOW,
	EWS_PRIORITY_MEDIUM,
	EWS_PRIORITY_HIGH
};

typedef void	(*EEwsRequestCreationCallback)	(ESoapMessage *msg,
						 gpointer user_data);
typedef void	(*EwsProgressFn)		(gpointer object,
						 gint percent);
typedef void	(*EEwsResponseCallback)		(ESoapResponse *response,
						 GSimpleAsyncResult *simple);

typedef enum {
	EWS_SEARCH_AD,
	EWS_SEARCH_AD_CONTACTS,
	EWS_SEARCH_CONTACTS,
	EWS_SEARCH_CONTACTS_AD
} EwsContactsSearchScope;

typedef enum {
	EWS_HARD_DELETE = 1,
	EWS_SOFT_DELETE,
	EWS_MOVE_TO_DELETED_ITEMS
} EwsDeleteType;

typedef enum {
	EWS_SEND_TO_NONE = 1,
	EWS_SEND_ONLY_TO_ALL,
	EWS_SEND_TO_ALL_AND_SAVE_COPY
} EwsSendMeetingCancellationsType;

typedef enum {
	EWS_ALL_OCCURRENCES = 1,
	EWS_SPECIFIED_OCCURRENCE_ONLY
} EwsAffectedTaskOccurrencesType;

typedef enum {
	E_EWS_BODY_TYPE_ANY,
	E_EWS_BODY_TYPE_BEST,
	E_EWS_BODY_TYPE_HTML,
	E_EWS_BODY_TYPE_TEXT
} EEwsBodyType;

typedef struct {
	gchar *id;
	gchar *dn;
	gchar *name;
} EwsOAL;

typedef struct {
	gchar *type;
	guint32 seq;
	guint32 ver;
	guint32 size;
	guint32 uncompressed_size;
	gchar *sha;
	gchar *filename;
} EwsOALDetails;

typedef struct {
	gchar *sid;
	gchar *primary_smtp;
	gchar *display_name;
	gchar *distinguished_user;
	gchar *external_user;
} EwsUserId;

typedef enum {
	EwsPermissionLevel_Unknown = 0,
	EwsPermissionLevel_None,
	EwsPermissionLevel_Reviewer,
	EwsPermissionLevel_Author,
	EwsPermissionLevel_Editor,
	EwsPermissionLevel_Custom
} EwsPermissionLevel;

typedef struct {
	EwsUserId *user_id;
	EwsPermissionLevel calendar, tasks, inbox, contacts, notes, journal;
	gboolean meetingcopies;
	gboolean view_priv_items;
} EwsDelegateInfo;

typedef enum {
	EwsDelegateDeliver_DelegatesOnly,
	EwsDelegateDeliver_DelegatesAndMe,
	EwsDelegateDeliver_DelegatesAndSendInformationToMe
} EwsDelegateDeliver;

typedef enum {
	NORMAL_FIELD_URI,
	INDEXED_FIELD_URI,
	EXTENDED_FIELD_URI
} EwsFieldURIType;

typedef struct {
	gchar *distinguished_prop_set_id;
	gchar *prop_set_id;
	gchar *prop_tag;
	gchar *prop_name;
	gchar *prop_id;
	gchar *prop_type;
} EEwsExtendedFieldURI;

typedef struct {
	gchar *field_uri;
	gchar *field_index;
} EEwsIndexedFieldURI;

typedef struct {
	gchar *field_uri;
	GSList *extended_furis;
	GSList *indexed_furis;
} EEwsAdditionalProps;

typedef struct {
	gchar *order;
	gint uri_type;
	gpointer field_uri;
} EwsSortOrder;

typedef struct {
	gchar *id;
	gsize len;
} EwsPhotoAttachmentInfo;

typedef enum {
	E_EWS_NOTIFICATION_EVENT_COPIED = 0,
	E_EWS_NOTIFICATION_EVENT_CREATED,
	E_EWS_NOTIFICATION_EVENT_DELETED,
	E_EWS_NOTIFICATION_EVENT_MODIFIED,
	E_EWS_NOTIFICATION_EVENT_MOVED,
	E_EWS_NOTIFICATION_EVENT_STATUS
} EEwsNotificationEventType;

typedef struct {
	EEwsNotificationEventType type;
	gboolean is_item;
	gchar *folder_id;
	gchar *old_folder_id;
} EEwsNotificationEvent;

/*
 * <To Kind=""/>
 */
typedef struct {
	gchar *kind;
	gchar *value;
} EEwsCalendarTo;

/*
 * <AbsoluteDateTransition>
 *     <To/>
 *     <DateTime/>
 * </AbsoluteDateTransition>
 */
typedef struct {
	EEwsCalendarTo *to;
	gchar *date_time;
} EEwsCalendarAbsoluteDateTransition;

/*
 * <RecurringDayTransition>
 *     <To/>
 *     <TimeOffset/>
 *     <Month/>
 *     <DayOfWeek/>
 *     <Ocurrence/>
 * </RecurringDayTransition>
 */
typedef struct {
	EEwsCalendarTo *to;
	gchar *time_offset;
	gchar *month;
	gchar *day_of_week;
	gchar *occurrence;
} EEwsCalendarRecurringDayTransition;

/*
 * <RecurringDateTransition>
 *     <To/>
 *     <TimeOffset/>
 *     <Month/>
 *     <Day/>
 * </RecurringDateTransition>
 */
typedef struct {
	EEwsCalendarTo *to;
	gchar *time_offset;
	gchar *month;
	gchar *day;
} EEwsCalendarRecurringDateTransition;

/*
 * <Period Bias="" Name="" Id=""/>
 */
typedef struct {
	gchar *bias;
	gchar *name;
	gchar *id;
} EEwsCalendarPeriod;

/*
 * <TransitionsGroup Id="">
 *     <Transition>
 *         <To/>
 *     <Transition>
 *     <AbsoluteDateTransition/>
 *     <RecurringDayTransition/>
 *     <RecurringDateTransition/>
 * </TransitionsGroup>
 */
typedef struct {
	gchar *id;
	EEwsCalendarTo *transition;
	GSList *absolute_date_transitions; /* EEwsCalendarAbsoluteDateTransition */
	GSList *recurring_day_transitions; /* EEwsCalendarRecurringDayTransition */
	GSList *recurring_date_transitions; /* EEwsCalendarRecurringDateTransition */
} EEwsCalendarTransitionsGroup;

/*
 * <Transitions Id="">
 *     <Transition>
 *         <To/>
 *     <Transition>
 *     <AbsoluteDateTransition/>
 *     <RecurringDayTransition/>
 *     <RecurringDateTransition/>
 * </Transitions>
 */
typedef struct {
	EEwsCalendarTo *transition;
	GSList *absolute_date_transitions; /* EEwsCalendarAbsoluteDateTransition */
	GSList *recurring_day_transitions; /* EEwsCalendarRecurringDayTransition */
	GSList *recurring_date_transitions; /* EEwsCalendarRecurringDateTransition */
} EEwsCalendarTransitions;

/*
 * <TimeZoneDefinition Id="" Name="">
 *     <Periods>
 *         <Period/>
 *     </Periods>
 *     <TransitionsGroups>
 *         <TransitionsGroup/>
 *     </TransitionsGroups>
 *     <Transitions/>
 * </TimeZoneDefinition>
 */
typedef struct {
	gchar *name;
	gchar *id;
	GSList *periods; /* EEwsCalendarPeriod */
	GSList *transitions_groups; /* EEwsCalendarTrasitionsGroup */
	EEwsCalendarTransitions *transitions;
} EEwsCalendarTimeZoneDefinition;

EEwsCalendarTo *
		e_ews_calendar_to_new		(void);
void		e_ews_calendar_to_free		(EEwsCalendarTo *to);

EEwsCalendarAbsoluteDateTransition *
		e_ews_calendar_absolute_date_transition_new
						(void);
void		e_ews_calendar_absolute_date_transition_free
						(EEwsCalendarAbsoluteDateTransition *adt);

EEwsCalendarRecurringDayTransition *
		e_ews_calendar_recurring_day_transition_new
						(void);
void		e_ews_calendar_recurring_day_transition_free
						(EEwsCalendarRecurringDayTransition *rdayt);

EEwsCalendarRecurringDateTransition *
		e_ews_calendar_recurring_date_transition_new
						(void);
void		e_ews_calendar_recurring_date_transition_free
						(EEwsCalendarRecurringDateTransition *rdatet);

EEwsCalendarPeriod *
		e_ews_calendar_period_new	(void);
void		e_ews_calendar_period_free	(EEwsCalendarPeriod *period);

EEwsCalendarTransitionsGroup *
		e_ews_calendar_transitions_group_new
						(void);
void		e_ews_calendar_transitions_group_free
						(EEwsCalendarTransitionsGroup *tg);

EEwsCalendarTransitions *
		e_ews_calendar_transitions_new	(void);
void		e_ews_calendar_transitions_free	(EEwsCalendarTransitions *transitions);

EEwsCalendarTimeZoneDefinition *
		e_ews_calendar_time_zone_definition_new
						(void);
void		e_ews_calendar_time_zone_definition_free
						(EEwsCalendarTimeZoneDefinition *tzd);

EEwsExtendedFieldURI *
		e_ews_extended_field_uri_new	(void);
void		e_ews_extended_field_uri_free	(EEwsExtendedFieldURI *ex_field_uri);

EEwsIndexedFieldURI *
		e_ews_indexed_field_uri_new	(void);
void		e_ews_indexed_field_uri_free	(EEwsIndexedFieldURI *id_field_uri);

EEwsAdditionalProps *
		e_ews_additional_props_new	(void);
void		e_ews_additional_props_free	(EEwsAdditionalProps *add_props);

EEwsNotificationEvent *
		e_ews_notification_event_new	(void);
void		e_ews_notification_event_free	(EEwsNotificationEvent *event);

void		ews_oal_free			(EwsOAL *oal);
void		ews_oal_details_free		(EwsOALDetails *details);

void		e_ews_connection_utils_unref_in_thread
						(gpointer object);

GType		e_ews_connection_get_type	(void);
EEwsConnection *e_ews_connection_new		(const gchar *uri,
						 CamelEwsSettings *settings);
EEwsConnection *e_ews_connection_new_full	(const gchar *uri,
						 CamelEwsSettings *settings,
						 gboolean allow_connection_reuse);
void		e_ews_connection_update_credentials
						(EEwsConnection *cnc,
						 const ENamedParameters *credentials);
ESourceAuthenticationResult
		e_ews_connection_try_credentials_sync
						(EEwsConnection *cnc,
						 const ENamedParameters *credentials,
						 GCancellable *cancellable,
						 GError **error);
const gchar *	e_ews_connection_get_uri	(EEwsConnection *cnc);
const gchar *	e_ews_connection_get_password	(EEwsConnection *cnc);
gchar *		e_ews_connection_dup_password	(EEwsConnection *cnc);
void		e_ews_connection_set_password	(EEwsConnection *cnc,
						 const gchar *password);
const gchar *	e_ews_connection_get_impersonate_user
						(EEwsConnection *cnc);
GProxyResolver *
		e_ews_connection_ref_proxy_resolver
						(EEwsConnection *cnc);
void		e_ews_connection_set_proxy_resolver
						(EEwsConnection *cnc,
						 GProxyResolver *proxy_resolver);
CamelEwsSettings *
		e_ews_connection_ref_settings	(EEwsConnection *cnc);
SoupSession *	e_ews_connection_ref_soup_session
						(EEwsConnection *cnc);
EEwsConnection *e_ews_connection_find		(const gchar *uri,
						 const gchar *username);
void		e_ews_connection_queue_request	(EEwsConnection *cnc,
						 ESoapMessage *msg,
						 EEwsResponseCallback cb,
						 gint pri,
						 GCancellable *cancellable,
						 GSimpleAsyncResult *simple);

gboolean	e_ews_autodiscover_ws_url_sync	(CamelEwsSettings *settings,
						 const gchar *email_address,
						 const gchar *password,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_autodiscover_ws_url	(CamelEwsSettings *settings,
						 const gchar *email_address,
						 const gchar *password,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_autodiscover_ws_url_finish
						(CamelEwsSettings *settings,
						 GAsyncResult *result,
						 GError **error);
const gchar *	e_ews_connection_get_mailbox	(EEwsConnection *cnc);
void		e_ews_connection_set_mailbox	(EEwsConnection *cnc,
						 const gchar *email);

void		ews_user_id_free		(EwsUserId *id);
void		ews_delegate_info_free		(EwsDelegateInfo *info);

void		e_ews_connection_sync_folder_items
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *old_sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 guint max_entries,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_sync_folder_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gchar **new_sync_state,
						 gboolean *includes_last_item,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GError **error);
gboolean	e_ews_connection_sync_folder_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *old_sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 guint max_entries,
						 gchar **new_sync_state,
						 gboolean *includes_last_item,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GCancellable *cancellable,
						 GError **error);

typedef void	(*EwsConvertQueryCallback)	(ESoapMessage *msg,
						 const gchar *query,
						 EEwsFolderType type);

void		e_ews_connection_find_folder_items
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *fid,
						 const gchar *props,
						 const EEwsAdditionalProps *add_props,
						 EwsSortOrder *sort_order,
						 const gchar *query,
						 EEwsFolderType type,
						 EwsConvertQueryCallback convert_query_cb,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_find_folder_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gboolean *includes_last_item,
						 GSList **items,
						 GError **error);
gboolean	e_ews_connection_find_folder_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *fid,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 EwsSortOrder *sort_order,
						 const gchar *query,
						 EEwsFolderType type,
						 gboolean *includes_last_item,
						 GSList **items,
						 EwsConvertQueryCallback convert_query_cb,
						 GCancellable *cancellable,
						 GError **error);

EEwsServerVersion
		e_ews_connection_get_server_version
						(EEwsConnection *cnc);
void		e_ews_connection_set_server_version
						(EEwsConnection *cnc,
						 EEwsServerVersion version);
void		e_ews_connection_set_server_version_from_string
						(EEwsConnection *cnc,
						 const gchar *version);
gboolean	e_ews_connection_satisfies_server_version
						(EEwsConnection *cnc,
						 EEwsServerVersion versio);

void		e_ews_connection_get_items	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 gboolean include_mime,
						 const gchar *mime_directory,
						 EEwsBodyType body_type,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);
gboolean	e_ews_connection_get_items_sync	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 gboolean include_mime,
						 const gchar *mime_directory,
						 EEwsBodyType body_type,
						 GSList **items,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_delete_items	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_delete_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_delete_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_delete_item	(EEwsConnection *cnc,
						 gint pri,
						 EwsId *id,
						 guint index,
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_delete_item_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsId *id,
						 guint index,
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_update_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *conflict_res,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_update_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **ids,
						 GError **error);
gboolean	e_ews_connection_update_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *conflict_res,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **ids,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_create_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const EwsFolderId *fid,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_create_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **ids,
						 GError **error);
gboolean	e_ews_connection_create_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const EwsFolderId *fid,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **ids,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_sync_folder_hierarchy
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *sync_state,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_sync_folder_hierarchy_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gchar **sync_state,
						 gboolean *includes_last_folder,
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GError **error);
gboolean	e_ews_connection_sync_folder_hierarchy_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *old_sync_state,
						 gchar **new_sync_state,
						 gboolean *includes_last_folder,
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_resolve_names	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 EwsContactsSearchScope scope,
						 GSList *parent_folder_ids,
						 gboolean fetch_contact_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_resolve_names_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **mailboxes,
						 GSList **contact_items,
						 gboolean *includes_last_item,
						 GError **error);
gboolean	e_ews_connection_resolve_names_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 EwsContactsSearchScope scope,
						 GSList *parent_folder_ids,
						 gboolean fetch_contact_data,
						 GSList **mailboxes,
						 GSList **contact_items,
						 gboolean *includes_last_item,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_expand_dl	(EEwsConnection *cnc,
						 gint pri,
						 const EwsMailbox *mb,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_expand_dl_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **mailboxes,
						 gboolean *includes_last_item,
						 GError **error);
gboolean	e_ews_connection_expand_dl_sync	(EEwsConnection *cnc,
						 gint pri,
						 const EwsMailbox *mb,
						 GSList **mailboxes,
						 gboolean *includes_last_item,
						 GCancellable *cancellable,
						 GError **error);

gboolean	e_ews_connection_ex_to_smtp_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *name,
						 const gchar *ex_address,
						 gchar **smtp_address,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_create_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *parent_folder_id,
						 gboolean is_distinguished_id,
						 const gchar *folder_name,
						 EEwsFolderType folder_type,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_create_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 EwsFolderId **folder_id,
						 GError **error);
gboolean	e_ews_connection_create_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *parent_folder_id,
						 gboolean is_distinguished_id,
						 const gchar *folder_name,
						 EEwsFolderType folder_type,
						 EwsFolderId **folder_id,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_delete_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_delete_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_delete_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_empty_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 gboolean delete_subfolders,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_empty_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_empty_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 gboolean delete_subfolders,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_update_folder	(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_update_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_update_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_move_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *to_folder,
						 const gchar *folder,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_move_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_move_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *to_folder,
						 const gchar *folder,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_shape,
						 const EEwsAdditionalProps *add_props,
						 GSList *folder_ids,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **folders,
						 GError **error);
gboolean	e_ews_connection_get_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_shape,
						 const EEwsAdditionalProps *add_props,
						 GSList *folder_ids,
						 GSList **folders,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_move_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 const GSList *ids,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_move_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);
gboolean	e_ews_connection_move_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 const GSList *ids,
						 GSList **items_ret,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_move_items_in_chunks_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 const GSList *ids,
						 GSList **items,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_create_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsId *parent,
						 const GSList *files,
						 gboolean is_contact_photo,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_create_attachments_finish
						(EEwsConnection *cnc,
						 gchar **change_key,
						 GSList **attachments_ids,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_create_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsId *parent,
						 const GSList *files,
						 gboolean is_contact_photo,
						 gchar **change_key,
						 GSList **attachments_ids,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_delete_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *attachments_ids,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_delete_attachments_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **parents_ids,
						 GError **error);
gboolean	e_ews_connection_delete_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *attachments_ids,
						 GSList **parents_ids,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *comp_uid,
						 const GSList *ids,
						 const gchar *cache,
						 gboolean include_mime,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_attachments_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);
gboolean	e_ews_connection_get_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *comp_uid,
						 const GSList *ids,
						 const gchar *cache,
						 gboolean include_mime,
						 GSList **items,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);

gboolean	e_ews_connection_get_oal_list_sync
						(EEwsConnection *cnc,
						 GSList **oals,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_get_oal_list	(EEwsConnection *cnc,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_oal_list_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **oals,
						 GError **error);
gboolean	e_ews_connection_get_oal_detail_sync
						(EEwsConnection *cnc,
						 const gchar *oal_id,
						 const gchar *oal_element,
						 const gchar *old_etag,
						 GSList **elements,
						 gchar **etag,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_get_oal_detail	(EEwsConnection *cnc,
						 const gchar *oal_id,
						 const gchar *oal_element,
						 const gchar *etag,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_oal_detail_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **elements,
						 gchar **etag,
						 GError **error);

void		e_ews_connection_get_free_busy	(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback free_busy_cb,
						 gpointer free_busy_user_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_free_busy_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **free_busy,
						 GError **error);
gboolean	e_ews_connection_get_free_busy_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback free_busy_cb,
						 gpointer create_user_data,
						 GSList **free_busy,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_download_oal_file_sync
						(EEwsConnection *cnc,
						 const gchar *cache_filename,
						 EwsProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_download_oal_file
						(EEwsConnection *cnc,
						 const gchar *cache_filename,
						 EwsProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GAsyncReadyCallback cb,
						 gpointer user_data);
gboolean	e_ews_connection_download_oal_file_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

void		e_ews_connection_get_delegate	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 gboolean include_permissions,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_delegate_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 EwsDelegateDeliver *deliver_to,
						 GSList **delegates, /* EwsDelegateInfo * */
						 GError **error);
gboolean	e_ews_connection_get_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 gboolean include_permissions,
						 EwsDelegateDeliver *deliver_to,
						 GSList **delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_add_delegate	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_add_delegate_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_add_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_remove_delegate
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegate_ids, /* EwsUserId * */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_remove_delegate_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_remove_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegate_ids, /* EwsUserId * */
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_update_delegate
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 EwsDelegateDeliver deliver_to,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_update_delegate_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_update_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 EwsDelegateDeliver deliver_to,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_get_folder_permissions
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_folder_permissions_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **permissions,
						 GError **error);
gboolean	e_ews_connection_get_folder_permissions_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 GSList **permissions,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_set_folder_permissions
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 EEwsFolderType folder_type,
						 const GSList *permissions,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_set_folder_permissions_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_set_folder_permissions_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 EEwsFolderType folder_type,
						 const GSList *permissions,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_password_expiration
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);

gboolean	e_ews_connection_get_password_expiration_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gchar **exp_date,
						 GError **error);

gboolean	e_ews_connection_get_password_expiration_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 gchar **exp_date,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_folder_info
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const EwsFolderId *folder_id,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_folder_info_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 EEwsFolder **folder,
						 GError **error);
gboolean	e_ews_connection_get_folder_info_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const EwsFolderId *folder_id,
						 EEwsFolder **folder,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_find_folder	(EEwsConnection *cnc,
						 gint pri,
						 const EwsFolderId *fid,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_find_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gboolean *includes_last_item,
						 GSList **folders,
						 GError **error);
gboolean	e_ews_connection_find_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsFolderId *fid,
						 gboolean *includes_last_item,
						 GSList **folders,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_query_auth_methods
						(EEwsConnection *cnc,
						 gint pri,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_query_auth_methods_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **auth_methods,
						 GError **error);
gboolean	e_ews_connection_query_auth_methods_sync
						(EEwsConnection *cnc,
						 gint pri,
						 GSList **auth_methods,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_enable_notifications_sync
						(EEwsConnection *cnc,
						 GSList *folders,
						 guint *subscription_key);
void		e_ews_connection_disable_notifications_sync
						(EEwsConnection *cnc,
						 guint subscription_key);
void		e_ews_connection_get_server_time_zones
						(EEwsConnection *cnc,
						 gint pri,
						 GSList *msdn_locations,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ews_connection_get_server_time_zones_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **tzds, /* EEwsCalendarTimeZoneDefinition */
						 GError **error);
gboolean	e_ews_connection_get_server_time_zones_sync
						(EEwsConnection *cnc,
						 gint pri,
						 GSList *msdn_locations,
						 GSList **tzds, /* EEwsCalendarTimeZoneDefinition */
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif
