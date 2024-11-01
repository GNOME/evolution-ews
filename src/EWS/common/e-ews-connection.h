/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: JP Rosevear <jpr@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_CONNECTION_H
#define E_EWS_CONNECTION_H

#include <glib-object.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include <libedataserver/libedataserver.h>
#include <libebackend/libebackend.h>

#include "e-soap-request.h"
#include "e-soap-response.h"
#include "ews-errors.h"
#include "e-ews-folder.h"
#include "e-ews-item.h"
#include "e-ews-oof-settings.h"
#include "camel-ews-settings.h"

/* For network stream reading */
#define EWS_BUFFER_SIZE 16384

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

	void	(* password_will_expire)	(EEwsConnection *connection,
						 gint in_days,
						 const gchar *service_url);
};

enum {
	EWS_PRIORITY_LOW,
	EWS_PRIORITY_MEDIUM,
	EWS_PRIORITY_HIGH
};

typedef gboolean(*EEwsRequestCreationCallback)	(ESoapRequest *request,
						 gpointer user_data,
						 GError **error);
typedef void	(*EEwsResponseCallback)		(ESoapResponse *response,
						 GSimpleAsyncResult *simple);
typedef gboolean(*EEwsStreamingEventsReadCallback)
						(gconstpointer buffer,
						 gssize nread,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);
typedef void	(*EEwsStreamingEventsFinishedCallback)
						(gpointer user_data,
						 const GError *error);

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
	EWS_NONE_OCCURRENCES = 0,
	EWS_ALL_OCCURRENCES,
	EWS_SPECIFIED_OCCURRENCE_ONLY
} EwsAffectedTaskOccurrencesType;

typedef enum {
	E_EWS_SIZE_REQUESTED_UNKNOWN = 0,
	E_EWS_SIZE_REQUESTED_48X48 = 48,
	E_EWS_SIZE_REQUESTED_64X64 = 64,
	E_EWS_SIZE_REQUESTED_96X96 = 96,
	E_EWS_SIZE_REQUESTED_120X120 = 120,
	E_EWS_SIZE_REQUESTED_240X240 = 240,
	E_EWS_SIZE_REQUESTED_360X360 = 360,
	E_EWS_SIZE_REQUESTED_432X432 = 432,
	E_EWS_SIZE_REQUESTED_504X504 = 504,
	E_EWS_SIZE_REQUESTED_648X648 = 648
} EEwsSizeRequested;

typedef enum {
	E_EWS_USER_CONFIGURATION_PROPERTIES_UNKNOWN = -1,
	E_EWS_USER_CONFIGURATION_PROPERTIES_ID,
	E_EWS_USER_CONFIGURATION_PROPERTIES_DICTIONARY,
	E_EWS_USER_CONFIGURATION_PROPERTIES_XMLDATA,
	E_EWS_USER_CONFIGURATION_PROPERTIES_BINARYDATA /*,
	E_EWS_USER_CONFIGURATION_PROPERTIES_ALL - skip it, be specific */
} EEwsUserConfigurationProperties;

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
		e_ews_indexed_field_uri_new	(const gchar *uri,
						 const gchar *index);
void		e_ews_indexed_field_uri_free	(EEwsIndexedFieldURI *id_field_uri);

EEwsAdditionalProps *
		e_ews_additional_props_new	(void);
void		e_ews_additional_props_free	(EEwsAdditionalProps *add_props);

EEwsNotificationEvent *
		e_ews_notification_event_new	(void);
void		e_ews_notification_event_free	(EEwsNotificationEvent *event);

void		ews_oal_free			(EwsOAL *oal);
void		ews_oal_details_free		(EwsOALDetails *details);

GType		e_ews_connection_get_type	(void);
EEwsConnection *e_ews_connection_new		(ESource *source,
						 const gchar *uri,
						 CamelEwsSettings *settings);
EEwsConnection *e_ews_connection_new_full	(ESource *source,
						 const gchar *uri,
						 CamelEwsSettings *settings,
						 gboolean allow_connection_reuse);
EEwsConnection *e_ews_connection_new_for_backend(EBackend *backend,
						 ESourceRegistry *registry,
						 const gchar *uri,
						 CamelEwsSettings *settings);
void		e_ews_connection_set_testing_sources
						(EEwsConnection *cnc,
						 gboolean testing_sources);
gboolean	e_ews_connection_get_testing_sources
						(EEwsConnection *cnc);
void		e_ews_connection_update_credentials
						(EEwsConnection *cnc,
						 const ENamedParameters *credentials);
ESourceAuthenticationResult
		e_ews_connection_try_credentials_sync
						(EEwsConnection *cnc,
						 const ENamedParameters *credentials,
						 ESource *use_source,
						 gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors,
						 GCancellable *cancellable,
						 GError **error);
ESource *	e_ews_connection_get_source	(EEwsConnection *cnc);
gboolean	e_ews_connection_get_ssl_error_details
						(EEwsConnection *cnc,
						 gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors);
const gchar *	e_ews_connection_get_uri	(EEwsConnection *cnc);
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
gboolean	e_ews_connection_get_backoff_enabled
						(EEwsConnection *cnc);
void		e_ews_connection_set_backoff_enabled
						(EEwsConnection *cnc,
						 gboolean enabled);
gboolean	e_ews_connection_get_disconnected_flag
						(EEwsConnection *cnc);
void		e_ews_connection_set_disconnected_flag
						(EEwsConnection *cnc,
						 gboolean disconnected_flag);
gchar *		e_ews_connection_dup_last_subscription_id
						(EEwsConnection *cnc);
void		e_ews_connection_set_last_subscription_id
						(EEwsConnection *cnc,
						 const gchar *subscription_id);
EEwsConnection *e_ews_connection_find		(const gchar *uri,
						 CamelEwsSettings *ews_settings);
GSList *	e_ews_connection_list_existing	(void); /* EEwsConnection * */

gboolean	e_ews_autodiscover_ws_url_sync	(ESource *source,
						 CamelEwsSettings *settings,
						 const gchar *email_address,
						 const gchar *password,
						 gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors,
						 GCancellable *cancellable,
						 GError **error);
const gchar *	e_ews_connection_get_mailbox	(EEwsConnection *cnc);
void		e_ews_connection_set_mailbox	(EEwsConnection *cnc,
						 const gchar *email);

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

void		ews_user_id_free		(EwsUserId *id);
void		ews_delegate_info_free		(EwsDelegateInfo *info);

gboolean	e_ews_connection_sync_folder_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *old_sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 guint max_entries,
						 gchar **out_new_sync_state,
						 gboolean *out_includes_last_item,
						 GSList **out_items_created, /* EEwsItem * */
						 GSList **items_updated, /* EEwsItem * */
						 GSList **items_deleted, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);

typedef void	(*EwsConvertQueryCallback)	(ESoapRequest *request,
						 const gchar *query,
						 EEwsFolderType type);

gboolean	e_ews_connection_find_folder_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *fid,
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 EwsSortOrder *sort_order,
						 const gchar *query,
						 GPtrArray *only_ids, /* element-type utf8 */
						 EEwsFolderType type,
						 gboolean *out_includes_last_item,
						 GSList **out_items, /* EEwsItem * */
						 EwsConvertQueryCallback convert_query_cb,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_items_sync	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids, /* gchar * */
						 const gchar *default_props,
						 const EEwsAdditionalProps *add_props,
						 gboolean include_mime,
						 const gchar *mime_directory,
						 EEwsBodyType body_type,
						 GSList **out_items, /* EEwsItem * */
						 ESoapResponseProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_delete_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids, /* gchar * */
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_delete_items_in_chunks_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids, /* gchar * */
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GError **error);
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
gboolean	e_ews_connection_update_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *conflict_res,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **out_items, /* EEwsItem * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_create_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const EwsFolderId *fid,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **out_items, /* EEwsItem * */
						 GCancellable *cancellable,
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
gboolean	e_ews_connection_resolve_names_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 EwsContactsSearchScope scope,
						 GSList *parent_folder_ids,
						 gboolean fetch_contact_data,
						 gboolean *out_includes_last_item,
						 GSList **out_mailboxes, /* EwsMailbox * */
						 GSList **out_contact_items, /* EEwsItem * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_expand_dl_sync	(EEwsConnection *cnc,
						 gint pri,
						 const EwsMailbox *mb,
						 gboolean *out_includes_last_item,
						 GSList **out_mailboxes, /* EwsMailbox * */
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
gboolean	e_ews_connection_create_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *parent_folder_id,
						 gboolean is_distinguished_id,
						 const gchar *folder_name,
						 EEwsFolderType folder_type,
						 EwsFolderId **out_folder_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_delete_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 GCancellable *cancellable,
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
gboolean	e_ews_connection_update_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_move_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *to_folder,
						 const gchar *folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_shape,
						 const EEwsAdditionalProps *add_props,
						 GSList *folder_ids, /* EwsFolderId * */
						 GSList **out_folders, /* EEwsFolder * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_move_items_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 const GSList *ids, /* gchar * */
						 GSList **out_items, /* EEwsItem * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_move_items_in_chunks_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 const GSList *ids, /* gchar * */
						 GSList **out_items, /* EEwsItems * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_create_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsId *parent,
						 const GSList *files, /* EEwsAttachmentInfo * */
						 gboolean is_contact_photo,
						 gchar **out_change_key,
						 GSList **out_attachments_ids, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_delete_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *attachments_ids, /* gchar * */
						 gchar **out_new_change_key,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_attachments_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *uid,
						 const GSList *ids, /* const gchar * */
						 const gchar *cache_directory,
						 gboolean include_mime,
						 GSList **out_attachments, /* EEwsAttachmentInfo * */
						 ESoapResponseProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_oal_list_sync
						(EEwsConnection *cnc,
						 GSList **out_oals, /* EwsOAL * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_oal_detail_sync
						(EEwsConnection *cnc,
						 const gchar *oal_uri,
						 const gchar *oal_id,
						 const gchar *oal_element,
						 const gchar *old_etag,
						 GSList **out_elements, /* EwsOALDetails * */
						 gchar **out_etag,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_download_oal_file_sync
						(EEwsConnection *cnc,
						 const gchar *oal_uri,
						 const gchar *cache_filename,
						 ESoapResponseProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_free_busy_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback free_busy_cb,
						 gpointer create_user_data,
						 GSList **out_free_busy, /* ICalComponent * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 gboolean include_permissions,
						 EwsDelegateDeliver *out_deliver_to,
						 GSList **out_delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_add_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_remove_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const GSList *delegate_ids, /* EwsUserId * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_update_delegate_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 EwsDelegateDeliver deliver_to,
						 const GSList *delegates, /* EwsDelegateInfo * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_folder_permissions_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 GSList **out_permissions, /* EEwsPermission * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_set_folder_permissions_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *folder_id,
						 EEwsFolderType folder_type,
						 const GSList *permissions, /* EEwsPermission * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_password_expiration_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 gchar **out_exp_date,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_folder_info_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const EwsFolderId *folder_id,
						 EEwsFolder **out_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_find_folder_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsFolderId *fid,
						 gboolean *out_includes_last_item,
						 GSList **out_folders, /* EEwsFolder * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_query_auth_methods_sync
						(EEwsConnection *cnc,
						 gint pri,
						 GSList **out_auth_methods, /* gchar * */
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_enable_notifications_sync
						(EEwsConnection *cnc,
						 GSList *folders,
						 guint *subscription_key);
void		e_ews_connection_disable_notifications_sync
						(EEwsConnection *cnc,
						 guint subscription_key);
gboolean	e_ews_connection_get_server_time_zones_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *msdn_locations, /* gchar * */
						 GSList **out_tzds, /* EEwsCalendarTimeZoneDefinition */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_user_photo_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *email,
						 EEwsSizeRequested size_requested,
						 gchar **out_picture_data, /* base64-encoded */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_user_configuration_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsFolderId *fid,
						 const gchar *config_name,
						 EEwsUserConfigurationProperties props,
						 gchar **out_properties,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_convert_id_sync(EEwsConnection *cnc,
						 gint pri,
						 const gchar *email,
						 const gchar *folder_id,
						 const gchar *from_format,
						 const gchar *to_format,
						 gchar **out_converted_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_get_user_oof_settings_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsOofSettings *inout_oof_settings, /* caller-allocates */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_set_user_oof_settings_sync
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsOofState state,
						 EEwsExternalAudience external_audience,
						 const GDateTime *date_start,
						 const GDateTime *date_end,
						 const gchar *internal_reply,
						 const gchar *external_reply,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_subscribe_sync	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *folder_ids, /* gchar * */
						 gchar **out_subscription_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_ews_connection_unsubscribe_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *subscription_id,
						 GCancellable *cancellable,
						 GError **error);
GInputStream *	e_ews_connection_prepare_streaming_events_sync
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *subscription_id,
						 ESoupSession **out_session,
						 SoupMessage **out_message,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif
