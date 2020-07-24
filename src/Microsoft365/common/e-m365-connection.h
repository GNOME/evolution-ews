/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_CONNECTION_H
#define E_M365_CONNECTION_H

#include <glib-object.h>

#include <libebackend/libebackend.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "camel-m365-settings.h"
#include "e-m365-enums.h"
#include "e-m365-json-utils.h"

/* Currently, as of 2020-06-17, there is a limitation to 20 requests:
   https://docs.microsoft.com/en-us/graph/known-issues#json-batching */
#define E_M365_BATCH_MAX_REQUESTS 20

/* Standard GObject macros */
#define E_TYPE_M365_CONNECTION \
	(e_m365_connection_get_type ())
#define E_M365_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_M365_CONNECTION, EM365Connection))
#define E_M365_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_M365_CONNECTION, EM365ConnectionClass))
#define E_IS_M365_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_M365_CONNECTION))
#define E_IS_M365_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_M365_CONNECTION))
#define E_M365_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_M365_CONNECTION))

G_BEGIN_DECLS

typedef enum _EM365ApiVersion {
	E_M365_API_V1_0,
	E_M365_API_BETA
} EM365ApiVersion;

typedef struct _EM365Connection EM365Connection;
typedef struct _EM365ConnectionClass EM365ConnectionClass;
typedef struct _EM365ConnectionPrivate EM365ConnectionPrivate;

/* Returns whether can continue */
typedef gboolean (* EM365ConnectionJsonFunc)	(EM365Connection *cnc,
						 const GSList *results, /* JsonObject * - the returned objects from the server */
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

typedef gboolean (* EM365ConnectionRawDataFunc)	(EM365Connection *cnc,
						 SoupMessage *message,
						 GInputStream *raw_data_stream,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

struct _EM365Connection {
	GObject parent;
	EM365ConnectionPrivate *priv;
};

struct _EM365ConnectionClass {
	GObjectClass parent_class;
};

gboolean	e_m365_connection_util_delta_token_failed
						(const GError *error);

GType		e_m365_connection_get_type	(void) G_GNUC_CONST;

EM365Connection *
		e_m365_connection_new		(ESource *source,
						 CamelM365Settings *settings);
EM365Connection *
		e_m365_connection_new_for_backend
						(EBackend *backend,
						 ESourceRegistry *registry,
						 ESource *source,
						 CamelM365Settings *settings);
EM365Connection *
		e_m365_connection_new_full	(ESource *source,
						 CamelM365Settings *settings,
						 gboolean allow_reuse);
ESource *	e_m365_connection_get_source	(EM365Connection *cnc);
CamelM365Settings *
		e_m365_connection_get_settings	(EM365Connection *cnc);
guint		e_m365_connection_get_concurrent_connections
						(EM365Connection *cnc);
void		e_m365_connection_set_concurrent_connections
						(EM365Connection *cnc,
						 guint concurrent_connections);
GProxyResolver *e_m365_connection_ref_proxy_resolver
						(EM365Connection *cnc);
void		e_m365_connection_set_proxy_resolver
						(EM365Connection *cnc,
						 GProxyResolver *proxy_resolver);
ESoupAuthBearer *
		e_m365_connection_ref_bearer_auth
						(EM365Connection *cnc);
void		e_m365_connection_set_bearer_auth
						(EM365Connection *cnc,
						 ESoupAuthBearer *bearer_auth);
gboolean	e_m365_connection_get_ssl_error_details
						(EM365Connection *cnc,
						 gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors);
ESourceAuthenticationResult
		e_m365_connection_authenticate_sync
						(EM365Connection *cnc,
						 const gchar *user_override,
						 EM365FolderKind kind,
						 const gchar *group_id,
						 const gchar *folder_id,
						 gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_disconnect_sync
						(EM365Connection *cnc,
						 GCancellable *cancellable,
						 GError **error);
gchar *		e_m365_connection_construct_uri	(EM365Connection *cnc,
						 gboolean include_user,
						 const gchar *user_override,
						 EM365ApiVersion api_version,
						 const gchar *api_part, /* NULL for 'users', empty string to skip */
						 const gchar *resource,
						 const gchar *id, /* NULL to skip */
						 const gchar *path,
						 ...) G_GNUC_NULL_TERMINATED;
gboolean	e_m365_connection_json_node_from_message
						(SoupMessage *message,
						 GInputStream *input_stream,
						 JsonNode **out_node,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_batch_request_sync
						(EM365Connection *cnc,
						 EM365ApiVersion api_version,
						 GPtrArray *requests, /* SoupMessage * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_call_gather_into_slist
						(EM365Connection *cnc,
						 const GSList *results, /* JsonObject * - the returned objects from the server */
						 gpointer user_data, /* expects GSList **, aka pointer to a GSList *, where it copies the 'results' */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_categories_sync
						(EM365Connection *cnc,
						 const gchar *user_override,
						 GSList **out_categories, /* EM365Category * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_list_mail_folders_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *from_path, /* path for the folder to read, NULL for top user folder */
						 const gchar *select, /* nullable - properties to select */
						 GSList **out_folders, /* EM365MailFolder * - the returned mailFolder objects */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_folders_delta_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 EM365FolderKind kind,
						 const gchar *select, /* nullable - properties to select */
						 const gchar *delta_link, /* previous delta link */
						 guint max_page_size, /* 0 for default by the server */
						 EM365ConnectionJsonFunc func, /* function to call with each result set */
						 gpointer func_user_data, /* user data passed into the 'func' */
						 gchar **out_delta_link,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_mail_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id, /* nullable - then the 'inbox' is used */
						 const gchar *select, /* nullable - properties to select */
						 EM365MailFolder **out_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_mail_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *parent_folder_id, /* NULL for the folder root */
						 const gchar *display_name,
						 EM365MailFolder **out_mail_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_mail_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_copy_move_mail_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *src_folder_id,
						 const gchar *des_folder_id,
						 gboolean do_copy,
						 EM365MailFolder **out_mail_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_rename_mail_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *display_name,
						 EM365MailFolder **out_mail_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_objects_delta_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 EM365FolderKind kind,
						 const gchar *folder_id, /* folder ID to get delta messages in */
						 const gchar *select, /* nullable - properties to select */
						 const gchar *delta_link, /* previous delta link */
						 guint max_page_size, /* 0 for default by the server */
						 EM365ConnectionJsonFunc func, /* function to call with each result set */
						 gpointer func_user_data, /* user data passed into the 'func' */
						 gchar **out_delta_link,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_mail_message_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *message_id,
						 EM365ConnectionRawDataFunc func,
						 gpointer func_user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_mail_message_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
						 JsonBuilder *mail_message, /* filled mailMessage object */
						 EM365MailMessage **out_created_message, /* free with json_object_unref() */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_add_mail_message_attachment_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *message_id, /* the message to add it to */
						 JsonBuilder *attachment, /* filled attachment object */
						 gchar **out_attachment_id,
						 GCancellable *cancellable,
						 GError **error);
SoupMessage *	e_m365_connection_prepare_update_mail_message
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *message_id,
						 JsonBuilder *mail_message, /* values to update, as a mailMessage object */
						 GError **error);
gboolean	e_m365_connection_update_mail_message_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *message_id,
						 JsonBuilder *mail_message, /* values to update, as a mailMessage object */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_copy_move_mail_messages_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const GSList *message_ids, /* const gchar * */
						 const gchar *des_folder_id,
						 gboolean do_copy,
						 GSList **out_des_message_ids, /* Camel-pooled gchar *, can be partial */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_mail_messages_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const GSList *message_ids, /* const gchar * */
						 GSList **out_deleted_ids, /* (transfer container): const gchar *, borrowed from message_ids */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_send_mail_message_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *message_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_send_mail_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 JsonBuilder *request, /* filled sendMail object */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_contacts_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id, /* nullable - then the default 'contacts' folder is returned */
						 const gchar *select, /* nullable - properties to select */
						 EM365Folder **out_folder,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_contact_photo_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *contact_id,
						 GByteArray **out_photo,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_update_contact_photo_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *contact_id,
						 const GByteArray *jpeg_photo, /* nullable - to remove the photo */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_contact_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *contact_id,
						 EM365Contact **out_contact,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_contact_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
						 JsonBuilder *contact, /* filled contact object */
						 EM365Contact **out_created_contact, /* free with json_object_unref() */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_update_contact_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *contact_id,
						 JsonBuilder *contact, /* values to update, as a contact object */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_contact_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *folder_id,
						 const gchar *contact_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_list_calendar_groups_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 GSList **out_groups, /* EM365CalendarGroup * - the returned calendarGroup objects */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_calendar_group_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *name,
						 EM365CalendarGroup **out_created_group,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_calendar_group_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id,
						 EM365CalendarGroup **out_group,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_update_calendar_group_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id,
						 const gchar *name,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_calendar_group_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_list_calendars_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - calendar group for group calendars */
						 const gchar *select, /* nullable - properties to select */
						 GSList **out_calendars, /* EM365Calendar * - the returned calendar objects */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_calendar_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 JsonBuilder *calendar,
						 EM365Calendar **out_created_calendar,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_calendar_folder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id, /* nullable - then the default calendar is used */
						 const gchar *select, /* nullable - properties to select */
						 EM365Calendar **out_calendar,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_update_calendar_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 const gchar *name, /* nullable - to keep the existing name */
						 EM365CalendarColorType color,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_calendar_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_list_events_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - calendar group for group calendars */
						 const gchar *calendar_id,
						 const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
						 const gchar *select, /* nullable - properties to select */
						 GSList **out_events, /* EM365Event * - the returned event objects */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_create_event_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 JsonBuilder *event,
						 EM365Calendar **out_created_event,
						 GCancellable *cancellable,
						 GError **error);
SoupMessage *	e_m365_connection_prepare_get_event
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
						 const gchar *select, /* nullable - properties to select */
						 GError **error);
gboolean	e_m365_connection_get_event_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
						 const gchar *select, /* nullable - properties to select */
						 EM365Event **out_event,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_events_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const GSList *event_ids, /* const gchar * */
						 const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
						 const gchar *select, /* nullable - properties to select */
						 GSList **out_events, /* EM365Event *, in the same order as event_ids; can return partial list */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_update_event_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 JsonBuilder *event,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_event_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_response_event_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 EM365ResponseType response, /* uses only accepted/tentatively accepted/declined values */
						 const gchar *comment, /* nullable */
						 gboolean send_response,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_dismiss_reminder_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable - then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_list_event_attachments_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 const gchar *select, /* nullable - properties to select */
						 GSList **out_attachments, /* EM365Attachment * */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_get_event_attachment_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 const gchar *attachment_id,
						 EM365ConnectionRawDataFunc func,
						 gpointer func_user_data,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_add_event_attachment_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 JsonBuilder *in_attachment,
						 EM365Attachment **out_attachment, /* nullable */
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_m365_connection_delete_event_attachment_sync
						(EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 const gchar *group_id, /* nullable, then the default group is used */
						 const gchar *calendar_id,
						 const gchar *event_id,
						 const gchar *attachment_id,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_M365_CONNECTION_H */
