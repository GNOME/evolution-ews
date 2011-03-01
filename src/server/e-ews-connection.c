/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <ctype.h>
#include <glib/gi18n-lib.h>
#include "e-ews-connection.h"
#include <libedataserver/e-flag.h>
#include "e-ews-message.h"
#include "e-ews-item-change.h"

#define d(x) x

/* For the number of connections */
#define EWS_CONNECTION_MAX_REQUESTS 10

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->queue_lock))

G_DEFINE_TYPE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections_permissions = NULL;
static gboolean ews_next_request (gpointer _cnc);
static gint comp_func (gconstpointer a, gconstpointer b);
static GQuark ews_connection_error_quark (void);
typedef void (*response_cb) (ESoapResponse *response, gpointer data);
static void ews_response_cb (SoupSession *session, SoupMessage *msg, gpointer data);
static void 
ews_connection_authenticate	(SoupSession *sess, SoupMessage *msg,
				 SoupAuth *auth, gboolean retrying, 
				 gpointer data);

/* Connection APIS */

#define  EWS_CONNECTION_ERROR \
         (ews_connection_error_quark ())

struct _EEwsConnectionPrivate {
	SoupSession *soup_session;

	gchar *uri;
	gchar *username;
	gchar *password;
	gchar *email;

	GSList *jobs;
	GSList *active_job_queue;
	GStaticRecMutex queue_lock;
};

typedef struct _EwsNode EwsNode;
typedef struct _EwsAsyncData EwsAsyncData;

struct _EwsAsyncData {
	GSList *folders_created;
	GSList *folders_updated;	
	GSList *folders_deleted;
	
	GSList *items_created;
	GSList *items_updated;
	GSList *items_deleted;
	
	GSList *items;
	gchar *sync_state;
	gboolean includes_last_item;
};

struct _EwsNode {
	ESoapMessage *msg;
	EEwsConnection *cnc;
	GSimpleAsyncResult *simple;

	gint pri;		/* the command priority */
	response_cb cb;
	
	GCancellable *cancellable;
	gulong cancel_handler_id;
};

typedef struct {
  GAsyncResult *res;
  EFlag *eflag;
} EwsSyncData;

/* Static Functions */

static GQuark
ews_connection_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "ews-connection-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

static void
async_data_free (EwsAsyncData *async_data)
{
	g_free (async_data);
}

static void
ews_sync_reply_cb	(GObject *object,
			 GAsyncResult *res,
			 gpointer user_data)
{

  EwsSyncData *sync_data = user_data;

  sync_data->res = g_object_ref (res);
  e_flag_set (sync_data->eflag);
}

static EwsNode *
ews_node_new ()
{
	EwsNode *node;

	node = g_new0 (EwsNode, 1);
	return node;
}

static gchar*
autodiscover_parse_protocol(xmlNode *node)
{
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "ASUrl")) {
			char *asurl = (char *)xmlNodeGetContent(node);
			if (asurl) {
				printf("Got ASUrl %s\n", asurl);
				return asurl;
			}
		}
	}
	return NULL;
}

static gint
comp_func (gconstpointer a, gconstpointer b)
{
	EwsNode *node1 = (EwsNode *) a;
	EwsNode *node2 = (EwsNode *) b;

	if (node1->pri > node2->pri)
		return -1;
	else if (node1->pri < node2->pri)
		return 1;
	else
		return 0;
}

static void
ews_parse_soap_fault (ESoapResponse *response, GError **error)
{
	ESoapParameter *param;
	gchar *faultstring = NULL;

	param = e_soap_response_get_first_parameter_by_name(response, "faultstring");
	if (param)
		faultstring = e_soap_parameter_get_string_value(param);


	g_set_error (error, EWS_CONNECTION_ERROR, ERROR_UNKNOWN,
		     "%s", faultstring?:"No <ResponseMessages> or SOAP <faultstring> in response");

	g_free(faultstring);
}

static gboolean
ews_get_response_status (ESoapParameter *param, GError **error)
{
	ESoapParameter *subparam;
	gchar *value;
	gboolean ret = TRUE;

	value = e_soap_parameter_get_property (param, "ResponseClass");

	if (!g_ascii_strcasecmp (value, "Error")) {
		gchar *desc, *res;
		gint error_code = ERROR_UNKNOWN;

		subparam = e_soap_parameter_get_first_child_by_name (param, "MessageText");
		desc = e_soap_parameter_get_string_value (subparam);

		subparam = e_soap_parameter_get_first_child_by_name (param, "ResponseCode");
		res = e_soap_parameter_get_string_value (subparam);
		
		error_code = ews_get_error_code ((const gchar *) res);

		g_set_error	(error, 
				 EWS_CONNECTION_ERROR,
				 error_code,
				 "%s", desc);

		g_free (desc);
		g_free (res);
		ret = FALSE;
	}

	g_free (value);

	return ret;
}

static gboolean
ews_next_request (gpointer _cnc)
{
	EEwsConnection *cnc = _cnc;
	GSList *l;
	EwsNode *node;

	QUEUE_LOCK (cnc);

	l = cnc->priv->jobs;

	if (!l || g_slist_length (cnc->priv->active_job_queue) >= EWS_CONNECTION_MAX_REQUESTS) {
		QUEUE_UNLOCK (cnc);
		return FALSE;
	}
	
	node = (EwsNode *) l->data;

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (node->msg)->request_body));
		/* print request's body */
		printf ("\n The request headers");
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (node->msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}

	/* Remove the node from the priority queue */
	cnc->priv->jobs = g_slist_remove (cnc->priv->jobs, (gconstpointer *) node);

	/* Add to active job queue */
	cnc->priv->active_job_queue = g_slist_append (cnc->priv->active_job_queue, node);
	
	soup_session_queue_message (cnc->priv->soup_session, SOUP_MESSAGE (node->msg), ews_response_cb, node);

	QUEUE_UNLOCK (cnc);
	return FALSE;
}

/**
 * ews_active_job_done 
 * @cnc: 
 * @msg: 
 * Removes the node from active Queue and free's the node	 
 * 
 * Returns: 
 **/
static void
ews_active_job_done (EEwsConnection *cnc, EwsNode *ews_node)
{
	QUEUE_LOCK (cnc);

	cnc->priv->active_job_queue = g_slist_remove (cnc->priv->active_job_queue, ews_node);
	if (ews_node->cancellable)
		g_signal_handler_disconnect (ews_node->cancellable, ews_node->cancel_handler_id);
	
	QUEUE_UNLOCK (cnc);

	g_free (ews_node);
	g_idle_add_full(G_PRIORITY_DEFAULT, ews_next_request, cnc, NULL);
}

static void 
ews_cancel_request (GCancellable *cancellable,
		   gpointer user_data)
{
	EwsNode *node = user_data;
	EEwsConnection *cnc = node->cnc;
	GSimpleAsyncResult *simple = node->simple;
	ESoapMessage *msg = node->msg;
	GSList *found;

	QUEUE_LOCK (cnc);
	found = g_slist_find (cnc->priv->active_job_queue, node);
	QUEUE_UNLOCK (cnc);

	g_simple_async_result_set_error	(simple,
			EWS_CONNECTION_ERROR,
			ERROR_CANCELLED,
			_("Operation Cancelled"));
	if (found)
		soup_session_cancel_message (cnc->priv->soup_session, SOUP_MESSAGE (msg), SOUP_STATUS_CANCELLED);
	else {
		QUEUE_LOCK (cnc);
		cnc->priv->jobs = g_slist_remove (cnc->priv->jobs, (gconstpointer) node);
		QUEUE_UNLOCK (cnc);

		ews_response_cb (cnc->priv->soup_session, SOUP_MESSAGE (msg), node);
	}
}

static void
ews_connection_queue_request (EEwsConnection *cnc, ESoapMessage *msg, response_cb cb, gint pri, GCancellable *cancellable, GSimpleAsyncResult *simple)
{
	EwsNode *node;

	node = ews_node_new ();
	node->msg = msg;
	node->pri = pri;
	node->cb = cb;
	node->cnc = cnc;
	node->simple = simple;

	QUEUE_LOCK (cnc);
	cnc->priv->jobs = g_slist_insert_sorted (cnc->priv->jobs, (gpointer *) node, (GCompareFunc) comp_func);
 	QUEUE_UNLOCK (cnc);

	if (cancellable) {
		node->cancellable = cancellable;	
		node->cancel_handler_id = g_cancellable_connect	(cancellable,
								 G_CALLBACK (ews_cancel_request),
								 (gpointer) node, NULL);
	}

	g_idle_add_full(G_PRIORITY_DEFAULT, ews_next_request, cnc, NULL);
}

/* Response callbacks */

static void
ews_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapResponse *response;
	
	if (enode->cancellable && g_cancellable_is_cancelled (enode->cancellable))
		goto exit;

	response = e_soap_message_parse_response ((ESoapMessage *) msg);
	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
	} else {
		/* TODO: The stdout can be replaced with Evolution's
		   Logging framework also */
		if (response && g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 1))
			e_soap_response_dump_response (response, stdout);

		enode->cb (response, enode);
		g_object_unref (response);
	}

exit:
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (enode->cnc, enode);
}

static void
sync_hierarchy_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	gchar *new_sync_state = NULL, *value;
	GSList *folders_created = NULL, *folders_updated = NULL, *folders_deleted = NULL;
	gboolean success = TRUE, includes_last_item = FALSE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "SyncFolderHierarchyResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastFolderInRange");
	
	if (!strcmp (e_soap_parameter_get_string_value (node), "true"))
		includes_last_item = TRUE;

	node = e_soap_parameter_get_first_child_by_name (subparam, "Changes");
	
	if (node) {
		ESoapParameter *subparam1;
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Create");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Create")) {
			EEwsFolder *folder;

			folder = e_ews_folder_new_from_soap_parameter (subparam1);
			folders_created = g_slist_append (folders_created, folder);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Update");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Update")) {
			EEwsFolder *folder;

			folder = e_ews_folder_new_from_soap_parameter (subparam1);
			folders_updated = g_slist_append (folders_updated, folder);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Delete");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Delete")) {
			ESoapParameter *folder_param;

			folder_param = e_soap_parameter_get_first_child_by_name (subparam1, "FolderId");
			value = e_soap_parameter_get_property (folder_param, "Id");
			folders_deleted = g_slist_append (folders_deleted, value);
		}
	}

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->folders_created = folders_created;
	async_data->folders_updated = folders_updated;
	async_data->folders_deleted = folders_deleted;
	async_data->sync_state = new_sync_state;
	async_data->includes_last_item = includes_last_item;
}

static void
sync_folder_items_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	gchar *new_sync_state = NULL, *value;
	GSList *items_created = NULL, *items_updated = NULL, *items_deleted = NULL;
	gboolean success = TRUE, includes_last_item = FALSE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}
		
	subparam = e_soap_parameter_get_first_child_by_name (param, "SyncFolderItemsResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastItemInRange");
	if (!strcmp (e_soap_parameter_get_string_value (node), "true"))
		includes_last_item = TRUE;

	node = e_soap_parameter_get_first_child_by_name (subparam, "Changes");
	
	if (node) {
		ESoapParameter *subparam1;
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Create");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Create")) {
			EEwsItem *item;

			item = e_ews_item_new_from_soap_parameter (subparam1);
			items_created = g_slist_append (items_created, item);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Update");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Update")) {
			EEwsItem *item;

			item = e_ews_item_new_from_soap_parameter (subparam1);
			items_updated = g_slist_append (items_updated, item);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Delete");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Delete")) {
			ESoapParameter *item_param;

			item_param = e_soap_parameter_get_first_child_by_name (subparam1, "ItemId");
			value = e_soap_parameter_get_property (item_param, "Id");
			items_deleted = g_slist_append (items_deleted, value);
		}
	}

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->items_created = items_created;
	async_data->items_updated = items_updated;
	async_data->items_deleted = items_deleted;
	async_data->sync_state = new_sync_state;
	async_data->includes_last_item = includes_last_item;
}

static void
get_items_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	GSList *items = NULL;
	EEwsItem *item;
	gboolean success = TRUE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}
	
	for (subparam = e_soap_parameter_get_first_child_by_name (param, "GetItemResponseMessage");
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child_by_name (subparam, "GetItemResponseMessage")) {

		node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

		success = ews_get_response_status (subparam, &error);
		if (!success) {
			g_simple_async_result_set_from_error (enode->simple, error);
			return;
		}

		for (node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
		     node; node = e_soap_parameter_get_next_child_by_name (subparam, "Items")) {
			item = e_ews_item_new_from_soap_parameter (node);
			items = g_slist_append (items, item);
		}
	}

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->items = items;
}

static void
delete_items_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	gboolean success = TRUE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}
	
	for (subparam = e_soap_parameter_get_first_child_by_name (param, "DeleteItemResponseMessage");
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child_by_name (subparam, "DeleteItemResponseMessage")) {

		node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

		success = ews_get_response_status (subparam, &error);
		if (!success) {
			g_simple_async_result_set_from_error (enode->simple, error);
			return;
		}
	}
}

static void
update_items_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	gboolean success = TRUE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}
	
	for (subparam = e_soap_parameter_get_first_child_by_name (param, "UpdateItemResponseMessage");
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child_by_name (subparam, "UpdateItemResponseMessage")) {

		node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

		success = ews_get_response_status (subparam, &error);
		if (!success) {
			g_simple_async_result_set_from_error (enode->simple, error);
			return;
		}
	}
}

static void
create_items_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	GSList *ids = NULL;
	EEwsItem *item;
	gboolean success = TRUE;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}
	
	for (subparam = e_soap_parameter_get_first_child_by_name (param, "CreateItemResponseMessage");
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child_by_name (subparam, "CreateItemResponseMessage")) {

		node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

		success = ews_get_response_status (subparam, &error);
		if (!success) {
			g_simple_async_result_set_from_error (enode->simple, error);
			return;
		}

		for (node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
		     node != NULL;
		     node = e_soap_parameter_get_next_child_by_name (node, "Items")) {

			item = e_ews_item_new_from_soap_parameter (node);
			ids = g_slist_append (ids, (gpointer)item);
		}
	}
	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->items = ids;
}

static void
resolve_names_response_cb (ESoapResponse *response, gpointer data)
{
	EwsNode *enode = (EwsNode *) data;
	ESoapParameter *param, *subparam, *node;
	gboolean includes_last_item;
	gboolean success = TRUE;
	GSList *mailboxes = NULL, *contact_items = NULL;
	EwsAsyncData *async_data;
	GError *error = NULL;

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	if (!param) {
		ews_parse_soap_fault(response, &error);
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "ResolveNamesResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_set_from_error (enode->simple, error);
		return;
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "ResolutionSet");
	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastItemInRange");
	
	if (!strcmp (e_soap_parameter_get_string_value (node), "true"))
		includes_last_item = TRUE;

	for (subparam = e_soap_parameter_get_first_child_by_name (subparam, "Resolution");
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child_by_name (subparam, "Resolution")) {
		EwsMailbox *mb;

		node = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
		mb = e_ews_item_mailbox_from_soap_param (node);
		mailboxes = g_slist_append (mailboxes, mb);

		/* TODO parse contacts */
	}
	
	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	
	/* Reuse existing variables */
	async_data->items = mailboxes;
	async_data->includes_last_item = includes_last_item;
	async_data->items_created = contact_items;
}

static void
e_ews_connection_dispose (GObject *object)
{
	EEwsConnection *cnc = (EEwsConnection *) object;
	EEwsConnectionPrivate *priv;
	gchar *hash_key;

	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	priv = cnc->priv;

	/* remove the connection from the hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
					    priv->username ? priv->username : "",
					    priv->password ? priv->password : "",
					    priv->uri ? priv->uri : "");
		g_hash_table_remove (loaded_connections_permissions, hash_key);
		if (g_hash_table_size (loaded_connections_permissions) == 0) {
			g_hash_table_destroy (loaded_connections_permissions);
			loaded_connections_permissions = NULL;
		}
		g_free (hash_key);
	}

	g_signal_handlers_disconnect_by_func	(priv->soup_session, ews_connection_authenticate, cnc);

	if (priv->soup_session) {
		g_object_unref (priv->soup_session);
		priv->soup_session = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->email) {
		g_free (priv->email);
		priv->email = NULL;
	}

	if (priv->jobs) {
		g_slist_free (priv->jobs);
		priv->jobs = NULL;
	}

	if (priv->active_job_queue) {
		g_slist_free (priv->active_job_queue);
		priv->active_job_queue = NULL;
	}
	
	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_connection_finalize (GObject *object)
{
	EEwsConnection *cnc = (EEwsConnection *) object;
	EEwsConnectionPrivate *priv;

	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	priv = cnc->priv;
	g_static_rec_mutex_free (&priv->queue_lock);
	
	g_free (priv);
	cnc->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_ews_connection_class_init (EEwsConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_connection_dispose;
	object_class->finalize = e_ews_connection_finalize;
}

static void
e_ews_connection_init (EEwsConnection *cnc)
{
	EEwsConnectionPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsConnectionPrivate, 1);
	cnc->priv = priv;

	/* create the SoupSession for this connection */
	priv->soup_session = soup_session_async_new_with_options (SOUP_SESSION_USE_NTLM, TRUE, NULL);

        if (getenv("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 2)) {
                SoupLogger *logger;
                logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
                soup_session_add_feature(priv->soup_session, SOUP_SESSION_FEATURE(logger));
        }


	g_static_rec_mutex_init (&priv->queue_lock);

	g_signal_connect (priv->soup_session, "authenticate", G_CALLBACK(ews_connection_authenticate), cnc);
}

static void 
ews_connection_authenticate	(SoupSession *sess, SoupMessage *msg,
				 SoupAuth *auth, gboolean retrying, 
				 gpointer data)
{
	EEwsConnection *cnc = data;
	
	if (retrying) {
		g_print ("Authentication failed.");
		return;
	}
	soup_auth_authenticate (auth, cnc->priv->username, cnc->priv->password);
}


/* Connection APIS */

/**
 * e_ews_connection_new 
 * @uri: Exchange server uri
 * @username: 
 * @password: 
 * @error: Currently unused, but may require in future. Can take NULL value.
 * 
 * This does not authenticate to the server. It merely stores the username and password.
 * Authentication happens when a request is made to the server.
 * 
 * Returns: EEwsConnection
 **/
EEwsConnection *
e_ews_connection_new (const gchar *uri, const gchar *username, const gchar *password, GError **error)
{
	EEwsConnection *cnc;
	gchar *hash_key;

	static GStaticMutex connecting = G_STATIC_MUTEX_INIT;

	g_static_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ("%s:%s@%s",
				username ? username : "",
				password ? password : "",
				uri);
		cnc = g_hash_table_lookup (loaded_connections_permissions, hash_key);
		g_free (hash_key);

		if (E_IS_EWS_CONNECTION (cnc)) {
			g_object_ref (cnc);
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
	}
	
	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_EWS_CONNECTION, NULL);
	
	cnc->priv->username = g_strdup (username);
	cnc->priv->password = g_strdup (password);
	cnc->priv->uri = g_strdup (uri);


	/* add the connection to the loaded_connections_permissions hash table */
	hash_key = g_strdup_printf ("%s:%s@%s",
			cnc->priv->username ? cnc->priv->username : "",
			cnc->priv->password ? cnc->priv->password : "",
			cnc->priv->uri);
	if (loaded_connections_permissions == NULL)
		loaded_connections_permissions = g_hash_table_new_full (g_str_hash, g_str_equal,
				g_free, NULL);
	g_hash_table_insert (loaded_connections_permissions, hash_key, cnc);

	/* free memory */
	g_static_mutex_unlock (&connecting);
	return cnc;

}

static xmlDoc *
e_ews_autodiscover_ws_xml(const gchar *email)
{
	xmlDoc *doc;
	xmlNode *node, *child;
	xmlNs *ns;

	doc = xmlNewDoc((xmlChar *) "1.0");
	node = xmlNewDocNode(doc, NULL, (xmlChar *)"Autodiscover", NULL);
	xmlDocSetRootElement(doc, node);
	ns = xmlNewNs (node,
		       (xmlChar *)"http://schemas.microsoft.com/exchange/autodiscover/outlook/requestschema/2006", NULL);

	node = xmlNewChild(node, ns, (xmlChar *)"Request", NULL);
	child = xmlNewChild(node, ns, (xmlChar *)"EMailAddress",
			    (xmlChar *)email);
	child = xmlNewChild(node, ns, (xmlChar *)"AcceptableResponseSchema", 
			    (xmlChar *)"http://schemas.microsoft.com/exchange/autodiscover/outlook/responseschema/2006a");
	
	return doc;
}

static guint
e_ews_autodiscover_ws_send(gchar *url, const gchar *email,
			   const gchar *password, SoupMessage **msg_parm,
			   EEwsConnection **cnc_parm, xmlDoc *doc)
{
	SoupMessage *msg;
	EEwsConnection *cnc;
	xmlOutputBuffer *buf;
	guint status;
       
	*cnc_parm = cnc = e_ews_connection_new (url, email, password, NULL);

	*msg_parm = msg = soup_message_new("GET", url);
	soup_message_headers_append (msg->request_headers,
				     "User-Agent", "libews/0.1");

	doc = e_ews_autodiscover_ws_xml(email);
	buf = xmlAllocOutputBuffer(NULL);
	xmlNodeDumpOutput(buf, doc, xmlDocGetRootElement(doc), 0, 1, NULL);
	xmlOutputBufferFlush(buf);

	soup_message_set_request(msg, "application/xml", SOUP_MEMORY_COPY,
				 (gchar *)buf->buffer->content,
				 buf->buffer->use);
				 
	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->request_body));
		/* print request's body */
		printf ("\n The request headers");
		printf ("\n ===================");
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}

	status = soup_session_send_message (cnc->priv->soup_session, msg);

	xmlOutputBufferClose (buf);

	return status;
}

gchar*
e_ews_autodiscover_ws_url (const gchar *email, const gchar *password, GError **error)
{
	gchar *url;
	gchar *domain;
	gchar *asurl = NULL;
	SoupMessage *msg;
	xmlDoc *doc;
	xmlNode *node;
	guint status;
	EEwsConnection *cnc;

	g_return_val_if_fail (password != NULL, NULL);
	g_return_val_if_fail (email != NULL, NULL);

	domain = strchr(email, '@');
	if (!(domain && *domain)) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			-1,
			_("Wrong email id"));
		
		return NULL;
	}
	domain++;

	url = g_strdup_printf("https://%s/autodiscover/autodiscover.xml", domain);
	doc = e_ews_autodiscover_ws_xml(email);

	status = e_ews_autodiscover_ws_send(url, email, password, &msg, &cnc,
					    doc);
	/*
	 * According to MS documentation (http://msdn.microsoft.com/en-us/library/ee332364.aspx)
	 * there are a couple of autodiscover URLs to try in the following preferred order
	 */

	url = g_strdup_printf("https://%s/autodiscover/autodiscover.xml", domain);
	status = e_ews_autodiscover_ws_send(url, email, password, &msg, &cnc, doc);

	if (status != 200) {
		url = g_strdup_printf("https://autodiscover.%s/autodiscover/autodiscover.xml", domain);
		status = e_ews_autodiscover_ws_send(url, email, password, &msg, &cnc, doc);
	}

	xmlFreeDoc (doc);

	if (status != 200) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			status,
			_("Code: %d - Unexpected response from server"),
			status);
		goto failed;
	}

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->response_body));
		/* print response body */
		printf ("\n The response headers");
		printf ("\n =====================");
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (msg)->response_body->data, stdout);
		fputc ('\n', stdout);
	}
	
	doc = xmlReadMemory (msg->response_body->data, msg->response_body->length,
			     "autodiscover.xml", NULL, 0);
	if (!doc) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			-1,
			_("Failed to parse autodiscover response XML"));
		goto failed;
	}
	node = xmlDocGetRootElement(doc);
	if (strcmp((char *)node->name, "Autodiscover")) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			-1,
			_("Failed to find <Autodiscover> element\n"));
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Response"))
			break;
	}
	if (!node) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			-1,
			_("Failed to find <Response> element\n"));
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Account"))
			break;
	}
	if (!node) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			-1,
			_("Failed to find <Account> element\n"));
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Protocol") &&
		    (asurl = autodiscover_parse_protocol(node)))
			break;
	}
failed:
	g_object_unref (cnc);
	return asurl;
}

void
e_ews_connection_set_mailbox	(EEwsConnection *cnc, 
				 const gchar *email)
{
	
	g_return_if_fail (email != NULL);

	g_free (cnc->priv->email);
	cnc->priv->email = g_strdup (email);
}

/**
 * e_ews_connection_sync_folder_items_start
 * @cnc: The EWS Connection
 * @pri: The priority associated with the request
 * @sync_state: To sync with the previous requests
 * @folder_id: The folder to which the items belong
 * @default_props: Can take one of the values: IdOnly,Default or AllProperties
 * @additional_props: Specify any additional properties to be fetched
 * @max_entries: Maximum number of items to be returned
 * @cb: Responses are parsed and returned to this callback
 * @cancellable: a GCancellable to monitor cancelled operations
 * @user_data: user data passed to callback
 **/
void
e_ews_connection_sync_folder_items_start	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const gchar *additional_props,
						 guint max_entries,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderItems", NULL, NULL);
	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", default_props);
	
	if (additional_props && *additional_props) {
		gchar **prop = g_strsplit (additional_props, " ", 0);
		gint i = 0;

		e_soap_message_start_element (msg, "AdditionalProperties", "types", NULL);
		while (prop[i]) {
			e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", "types", NULL, "FieldURI", prop [i]);
			i++;
		}
		g_strfreev (prop);
		e_soap_message_end_element (msg);
	}

	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "SyncFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", "types", NULL, "Id", fid);
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", NULL, sync_state);

	/* Max changes requested */
	e_ews_message_write_int_parameter (msg, "MaxChangesReturned", NULL, max_entries);

	/* Complete the footer and print the request */
	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_sync_folder_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, sync_folder_items_response_cb, pri, cancellable, simple);
}

gboolean
e_ews_connection_sync_folder_items_finish	(EEwsConnection *cnc,
						 GAsyncResult *result,
					 	 gchar **sync_state,
						 gboolean *includes_last_item,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_sync_folder_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	
	*sync_state = async_data->sync_state;
	*includes_last_item = async_data->includes_last_item;
	*items_created = async_data->items_created;
	*items_updated = async_data->items_updated;
	*items_deleted = async_data->items_deleted;

	return TRUE;
}

gboolean
e_ews_connection_sync_folder_items	(EEwsConnection *cnc,
					 gint pri,
					 gchar **sync_state,
					 const gchar *fid,
					 const gchar *default_props,
					 const gchar *additional_props,
					 guint max_entries,
					 gboolean *includes_last_item,
					 GSList **items_created,
					 GSList **items_updated,
					 GSList **items_deleted,
					 GCancellable *cancellable,
					 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_sync_folder_items_start	(cnc, pri, *sync_state, fid,
							 default_props, additional_props, 
							 max_entries,
							 ews_sync_reply_cb, cancellable,
							 (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_sync_folder_items_finish (cnc, sync_data->res,
							    sync_state,
							    includes_last_item,
							    items_created,
							    items_updated,
							    items_deleted,
							    error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void 
e_ews_connection_sync_folder_hierarchy_start	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *sync_state,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderHierarchy", NULL, NULL);
	e_soap_message_start_element (msg, "FolderShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", "AllProperties");
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", NULL, sync_state);

	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_sync_folder_hierarchy_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, sync_hierarchy_response_cb, pri, cancellable, simple);
}


gboolean
e_ews_connection_sync_folder_hierarchy_finish	(EEwsConnection *cnc,
						 GAsyncResult *result,
					 	 gchar **sync_state,
						 gboolean *includes_last_folder,
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_sync_folder_hierarchy_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	
	*sync_state = async_data->sync_state;
	*includes_last_folder = async_data->includes_last_item;
	*folders_created = async_data->folders_created;
	*folders_updated = async_data->folders_updated;
	*folders_deleted = async_data->folders_deleted;

	return TRUE;
}

gboolean
e_ews_connection_sync_folder_hierarchy	(EEwsConnection *cnc,
					 gint pri,
					 gchar **sync_state,
					 gboolean *includes_last_folder,
					 GSList **folders_created,
					 GSList **folders_updated,
					 GSList **folders_deleted,
					 GCancellable *cancellable,
					 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_sync_folder_hierarchy_start	(cnc, pri, *sync_state,
							 ews_sync_reply_cb, cancellable,
							 (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_sync_folder_hierarchy_finish (cnc, sync_data->res,
								sync_state,
								includes_last_folder,
								folders_created,
								folders_updated,
								folders_deleted,
								error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_get_items_start	(EEwsConnection *cnc,
					 gint pri,
					 GSList *ids,
					 const gchar *default_props,
					 const gchar *additional_props,
					 gboolean include_mime,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	GSList *l;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "GetItem", NULL, NULL);

	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", default_props);
	
	if (include_mime)
		e_ews_message_write_string_parameter (msg, "IncludeMimeContent", "types", "true");
	else
		e_ews_message_write_string_parameter (msg, "IncludeMimeContent", "types", "false");
	
	if (additional_props && *additional_props) {
		gchar **prop = g_strsplit (additional_props, " ", 0);
		gint i = 0;

		e_soap_message_start_element (msg, "AdditionalProperties", "types", NULL);
		while (prop[i]) {
			e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", "types", NULL, "FieldURI", prop [i]);
			i++;
		}
		g_strfreev (prop);
		e_soap_message_end_element (msg);
	}
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "ItemIds", NULL, NULL);
	
	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", "types", NULL, "Id", l->data);
	
	e_soap_message_end_element (msg);
	
	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_get_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, get_items_response_cb, pri, cancellable, simple);
}

gboolean
e_ews_connection_get_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **items,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_get_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*items = async_data->items;
	
	return TRUE;
}

gboolean		
e_ews_connection_get_items	(EEwsConnection *cnc,
				 gint pri, 
				 GSList *ids,
				 const gchar *default_props,
				 const gchar *additional_props,
				 gboolean include_mime,
				 GSList **items, 
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_get_items_start	(cnc, pri,ids, default_props,
						 additional_props, include_mime,
						 ews_sync_reply_cb, cancellable,
						 (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_get_items_finish (cnc, 
						    sync_data->res, 
						    items, 
						    error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_delete_items_start	(EEwsConnection *cnc,
					 gint pri,
					 GSList *ids,
					 const gchar *delete_type,
					 const gchar *send_cancels,
					 const gchar *affected_tasks,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	GSList *l;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "DeleteItem",
					     "DeleteType", delete_type);

	if (send_cancels)
		e_soap_message_add_attribute (msg, "SendMeetingCancellations",
					      send_cancels, NULL, NULL);
	if (affected_tasks)
		e_soap_message_add_attribute (msg, "AffectedTaskOccurrences",
					      affected_tasks, NULL, NULL);

	e_soap_message_start_element (msg, "ItemIds", NULL, NULL);
	
	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", "types", NULL, "Id", l->data);
	
	e_soap_message_end_element (msg);
	
	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_delete_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, delete_items_response_cb, pri, cancellable, simple);
}

gboolean
e_ews_connection_delete_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_delete_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean		
e_ews_connection_delete_items	(EEwsConnection *cnc,
				 gint pri,
				 GSList *ids,
				 const gchar *delete_type,
				 const gchar *send_cancels,
				 const gchar *affected_tasks,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_delete_items_start (cnc, pri, ids, delete_type,
					     send_cancels, affected_tasks,
					     ews_sync_reply_cb, cancellable,
					     (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_delete_items_finish (cnc, sync_data->res,
						       error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_update_items_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *conflict_res,
					 const gchar *msg_disposition,
					 const gchar *send_invites,
					 const gchar *folder_id,
					 EEwsRequestCreationCallback create_cb,
					 gpointer create_user_data,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "UpdateItem",
					     NULL, NULL);

	if (conflict_res)
		e_soap_message_add_attribute (msg, "ConflictResolution",
					      conflict_res, NULL, NULL);
	if (msg_disposition)
		e_soap_message_add_attribute (msg, "MessageDisposition",
					      msg_disposition, NULL, NULL);
	if (send_invites)
		e_soap_message_add_attribute (msg, "SendMeetingInvitationsOrCancellations",
					      send_invites, NULL, NULL);

	if (folder_id) {
		e_soap_message_start_element (msg, "SavedItemFolderId", NULL, NULL);
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId",
						     "types", NULL, "Id", folder_id);
		e_soap_message_end_element (msg);
	}

	e_soap_message_start_element (msg, "ItemChanges", NULL, NULL);
	
	create_cb (msg, create_user_data);

	e_soap_message_end_element (msg); /* ItemChanges */

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_update_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, update_items_response_cb, pri, cancellable, simple);
}

gboolean
e_ews_connection_update_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_update_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean		
e_ews_connection_update_items	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *conflict_res,
				 const gchar *msg_disposition,
				 const gchar *send_invites,
				 const gchar *folder_id,
				 EEwsRequestCreationCallback create_cb,
				 gpointer create_user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_update_items_start (cnc, pri, conflict_res,
					     msg_disposition, send_invites,
					     folder_id,
					     create_cb, create_user_data,
					     ews_sync_reply_cb, cancellable,
					     (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_update_items_finish (cnc, sync_data->res,
						       error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_create_items_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *msg_disposition,
					 const gchar *send_invites,
					 const gchar *folder_id,
					 EEwsRequestCreationCallback create_cb,
					 gpointer create_user_data,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	
	msg = e_ews_message_new_with_header (cnc->priv->uri, "CreateItem",
					     NULL, NULL);

	if (msg_disposition)
		e_soap_message_add_attribute (msg, "MessageDisposition",
					      msg_disposition, NULL, NULL);
	if (send_invites)
		e_soap_message_add_attribute (msg, "SendMeetingInvitations",
					      send_invites, NULL, NULL);

	if (folder_id) {
		e_soap_message_start_element (msg, "SavedItemFolderId", NULL, NULL);
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId",
						     "types", NULL, "Id", folder_id);
		e_soap_message_end_element (msg);
	}

	e_soap_message_start_element (msg, "Items", NULL, NULL);
	
	create_cb (msg, create_user_data);

	e_soap_message_end_element (msg); /* Items */
	
	e_ews_message_write_footer (msg); /* CreateItem */

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_create_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, create_items_response_cb, pri, cancellable, simple);
}

static const gchar *
get_search_scope_str (EwsContactsSearchScope scope)
{
	switch (scope) {
		case EWS_SEARCH_AD:
			return "ActiveDirectory";
		case EWS_SEARCH_AD_CONTACTS:
			return "ActiveDirectoryContacts";
		case EWS_SEARCH_CONTACTS:
			return "Contacts";
		case EWS_SEARCH_CONTACTS_AD:
			return "ContactsActiveDirectory";
		default:
			g_assert_not_reached ();
			return NULL;

	}
}

gboolean
e_ews_connection_create_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **ids,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_create_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	*ids = async_data->items;

	return TRUE;
}

gboolean		
e_ews_connection_create_items	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *msg_disposition,
				 const gchar *send_invites,
				 const gchar *folder_id,
				 EEwsRequestCreationCallback create_cb,
				 gpointer create_user_data,
				 GSList **ids,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_create_items_start (cnc, pri, msg_disposition,
					     send_invites, folder_id,
					     create_cb, create_user_data,
					     ews_sync_reply_cb, cancellable,
					     (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_create_items_finish (cnc, sync_data->res,
						       ids, error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_resolve_names_start 	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *resolve_name,
					 EwsContactsSearchScope scope,
					 GSList *parent_folder_ids,
					 gboolean fetch_contact_data,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	GSList *l;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "ResolveNames", NULL, NULL);

	if (fetch_contact_data)
		e_soap_message_add_attribute (msg, "ReturnFullContactData", "true", NULL, NULL);
	else
		e_soap_message_add_attribute (msg, "ReturnFullContactData", "false", NULL, NULL);

	e_soap_message_add_attribute (msg, "SearchScope", get_search_scope_str (scope), NULL, NULL);

	if (parent_folder_ids) {
		e_soap_message_start_element (msg, "ParentFolderIds", NULL, NULL);

		for (l = parent_folder_ids; l != NULL; l = g_slist_next (l)) {
			EwsFolderId *fid = (EwsFolderId *) l->data;
			
			if (fid->is_distinguished_id)
				e_soap_message_start_element (msg, "DistinguishedFolderId", NULL, NULL);
			else
				e_soap_message_start_element (msg, "FolderId", NULL, NULL);
		
			e_soap_message_add_attribute (msg, "Id", fid->id, NULL, NULL);
			e_soap_message_add_attribute (msg, "ChangeKey", fid->change_key, NULL, NULL);

			if (fid->is_distinguished_id)
				e_ews_message_write_string_parameter (msg, "Mailbox", NULL, cnc->priv->email);

			e_soap_message_end_element (msg);
		}

		e_soap_message_end_element (msg);
	}

	e_ews_message_write_string_parameter (msg, "UnresolvedEntry", NULL, resolve_name);

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_resolve_names_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, resolve_names_response_cb, pri, cancellable, simple);
}

gboolean	
e_ews_connection_resolve_names_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **mailboxes,
					 GSList **contact_items,
					 gboolean *includes_last_item,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_resolve_names_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	
	*includes_last_item = async_data->includes_last_item;
	*contact_items = async_data->items_created;
	*mailboxes = async_data->items;
	
	return TRUE;	
}
