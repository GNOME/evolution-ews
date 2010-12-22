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
#include "e-ews-message.h"

#define d(x) x

/* For the number of connections */
#define EWS_CONNECTION_MAX_REQUESTS 2

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->queue_lock))

G_DEFINE_TYPE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections_permissions = NULL;
static void ews_next_request (EEwsConnection *cnc);
static gint comp_func (gconstpointer a, gconstpointer b);
static GQuark ews_connection_error_quark (void);
typedef void (*response_cb) (SoupSession *session, SoupMessage *msg, gpointer user_data);
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

	GSList *jobs;
	GSList *active_job_queue;
	GStaticRecMutex queue_lock;
};

enum {
	NEXT_REQUEST,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct _EWSNode EWSNode;
typedef struct _EwsAsyncData EwsAsyncData;

struct _EwsAsyncData {
	GSList *folders_created;
	GSList *folders_updated;	
	GSList *folders_deleted;
	
	GSList *items_created;
	GSList *items_updated;
	GSList *items_deleted;

	EEwsItem *item;
	gchar *sync_state;
};

struct _EWSNode {
	ESoapMessage *msg;
	EEwsConnection *cnc;
	GSimpleAsyncResult *simple;

	gint pri;		/* the command priority */
	response_cb cb;
};

typedef struct {
  GAsyncResult *res;
  GMainContext *context;
  GMainLoop *loop;
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
  g_main_loop_quit (sync_data->loop);
}

static EWSNode *
ews_node_new ()
{
	EWSNode *node;

	node = g_malloc0 (sizeof (EWSNode));
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
	EWSNode *node1 = (EWSNode *) a;
	EWSNode *node2 = (EWSNode *) b;

	g_print ("\tSorting based on priority...\n");
	if (node1->pri > node2->pri)
		return 1;
	else
		return -1;
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


static void
soup_request_unqueued (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
	EEwsConnection *cnc;

	cnc = E_EWS_CONNECTION (user_data);

	ews_next_request (cnc);
}

static void
ews_next_request (EEwsConnection *cnc)
{
	GSList *l;
	EWSNode *node;

	QUEUE_LOCK (cnc);

	l = cnc->priv->jobs;

	if (!l || g_slist_length (cnc->priv->active_job_queue) >= EWS_CONNECTION_MAX_REQUESTS) {
		QUEUE_UNLOCK (cnc);
		return;
	}
	
	node = (EWSNode *) l->data;

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)) {
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
	
	soup_session_queue_message (cnc->priv->soup_session, SOUP_MESSAGE (node->msg), node->cb, node);

	QUEUE_UNLOCK (cnc);
}

/**
 * ews_active_job_done 
 * @cnc: 
 * @msg: 
 * Removes the node from active Queue and free's the node	 
 * 
 * Returns: 
 **/
static gboolean
ews_active_job_done (EEwsConnection *cnc, SoupMessage *msg)
{
	EWSNode *ews_node = NULL;
	GSList *l = NULL;
	gboolean found = FALSE;

	QUEUE_LOCK (cnc);

	for (l = cnc->priv->active_job_queue; l!= NULL ;l = g_slist_next (l)) {
		ews_node = (EWSNode *) l->data;
		if (SOUP_MESSAGE (ews_node->msg) == msg) {
			found = TRUE;
			cnc->priv->active_job_queue = g_slist_remove (cnc->priv->active_job_queue, ews_node);
			break;
		}
	}
	
	QUEUE_UNLOCK (cnc);

	g_free (ews_node);
	return found;
}

static void 
ews_cancel_request (GCancellable *cancellable,
		   gpointer user_data)
{
	EWSNode *node = user_data;
	EEwsConnection *cnc = node->cnc;
	GSimpleAsyncResult *simple = node->simple;
	ESoapMessage *msg = node->msg;
	gboolean found = FALSE;

	g_print ("\nCanceled this request\n");

	found = ews_active_job_done (cnc, SOUP_MESSAGE (node->msg));
	if (found)
		soup_session_cancel_message (cnc->priv->soup_session, SOUP_MESSAGE (msg), SOUP_STATUS_CANCELLED);
	else {
		QUEUE_LOCK (cnc);
		cnc->priv->jobs = g_slist_remove (cnc->priv->jobs, (gconstpointer) node);
		QUEUE_UNLOCK (cnc);

		g_free (node);
	}

	g_simple_async_result_set_error	(simple,
					 EWS_CONNECTION_ERROR,
					 ERROR_CANCELLED,
					 _("Operation Cancelled"));
	g_simple_async_result_complete_in_idle (simple);
}

static void
ews_connection_queue_request (EEwsConnection *cnc, ESoapMessage *msg, response_cb cb, gint pri, GCancellable *cancellable, GSimpleAsyncResult *simple)
{
	EWSNode *node;

	node = ews_node_new ();
	node->msg = msg;
	node->pri = pri;
	node->cb = cb;
	node->cnc = cnc;
	node->simple = simple;

	QUEUE_LOCK (cnc);
	cnc->priv->jobs = g_slist_insert_sorted (cnc->priv->jobs, (gpointer *) node, (GCompareFunc) comp_func);
 	QUEUE_UNLOCK (cnc);

	if (cancellable)
		g_cancellable_connect	(cancellable,
					 G_CALLBACK (ews_cancel_request),
					 (gpointer) node, NULL);

	g_signal_emit	(cnc, signals[NEXT_REQUEST], 0);
}

/* Response callbacks */

/* Just dump the response for debugging */
static void
dump_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EWSNode *enode = (EWSNode *) data;
	EEwsConnection *cnc = enode->cnc;
	EwsAsyncData *async_data;

	soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->request_body));
	/* print request's body */
	g_print ("\n------\n");
	fputs (SOUP_MESSAGE (msg)->request_body->data, stdout);
	fputc ('\n', stdout);

	response = e_soap_message_parse_response ((ESoapMessage *) msg);

	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
		goto exit;	
	}
		
	/* README: The stdout can be replaced with Evolution's
	Logging framework also */

	e_soap_response_dump_response (response, stdout);
	g_print ("\n------\n");

	/* free memory */
	g_object_unref (response);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
exit:	
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (cnc, msg);
}

static void
create_folder_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EWSNode *enode = (EWSNode *) data;
	EEwsConnection *cnc = enode->cnc;
	EwsAsyncData *async_data;
	ESoapParameter *param, *subparam, *node;
	gboolean test;
	gchar *value;

	response = e_soap_message_parse_response ((ESoapMessage *) msg);
	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
		goto exit;	
	}

	if (response && g_getenv ("EWS_DEBUG")) {
		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		e_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	subparam = e_soap_parameter_get_first_child_by_name (param, "CreateFolderResponseMessage");
	test = ews_get_response_status (subparam, NULL);
	if (!test)
		goto error;

	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	node = e_soap_parameter_get_first_child_by_name (subparam, "Folders");
	node = e_soap_parameter_get_first_child_by_name (node, "Folder");
	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderId");

	value = e_soap_parameter_get_property (subparam, "Id");
	g_print ("\nThe folder id is...%s\n", value);
	g_free (value);
	
error:
	/* free memory */
	g_object_unref (response);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
exit:	
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (cnc, msg);
}

static void
sync_hierarchy_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EWSNode *enode = (EWSNode *) data;	
	EEwsConnection *cnc = enode->cnc;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	gchar *new_sync_state = NULL, *value;
	GSList *folders_created = NULL, *folders_updated = NULL, *folders_deleted = NULL;
	gboolean success = TRUE;
	GError *error = NULL;

	response = e_soap_message_parse_response ((ESoapMessage *) msg);

	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
		goto exit;	
	}

	if (response && g_getenv ("EWS_DEBUG"))
		e_soap_response_dump_response (response, stdout);

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	subparam = e_soap_parameter_get_first_child_by_name (param, "SyncFolderHierarchyResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_propagate_error (enode->simple, &error);
		g_object_unref (response);
		goto exit;	
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastFolderInRange");
	
	/* FIXME Understand and handle this better. No change in data */
	if (!strcmp (e_soap_parameter_get_string_value (node), "true")) {
	}

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

	/* free memory */
	g_object_unref (response);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->folders_created = folders_created;
	async_data->folders_updated = folders_updated;
	async_data->folders_deleted = folders_deleted;
	async_data->sync_state = new_sync_state;

exit:	
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (cnc, msg);
}

static void
sync_folder_items_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EWSNode *enode = (EWSNode *) data;	
	EEwsConnection *cnc = enode->cnc;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	gchar *new_sync_state = NULL, *value;
	GSList *items_created = NULL, *items_updated = NULL, *items_deleted = NULL;
	gboolean success = TRUE;
	GError *error = NULL;

	response = e_soap_message_parse_response ((ESoapMessage *) msg);

	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
		goto exit;	
	}

	if (response && g_getenv ("EWS_DEBUG"))
		e_soap_response_dump_response (response, stdout);

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	subparam = e_soap_parameter_get_first_child_by_name (param, "SyncFolderItemsResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_propagate_error (enode->simple, &error);
		g_object_unref (response);
		goto exit;	
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastItemInRange");
	/* FIXME Understand and handle this better. No change in data */
	if (!strcmp (e_soap_parameter_get_string_value (node), "true")) {
	}

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

			item_param = e_soap_parameter_get_first_child_by_name (subparam1, "FolderId");
			value = e_soap_parameter_get_property (item_param, "Id");
			items_deleted = g_slist_append (items_deleted, value);
		}
	}

	/* free memory */
	g_object_unref (response);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->items_created = items_created;
	async_data->items_updated = items_updated;
	async_data->items_deleted = items_deleted;
	async_data->sync_state = new_sync_state;

exit:	
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (cnc, msg);
}

static void
get_item_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EWSNode *enode = (EWSNode *) data;	
	EEwsConnection *cnc = enode->cnc;
	ESoapParameter *param, *subparam, *node;
	EwsAsyncData *async_data;
	gchar *value;
	EEwsItem *item;
	gboolean success = TRUE;
	GError *error = NULL;

	response = e_soap_message_parse_response ((ESoapMessage *) msg);

	if (!response) {
		g_simple_async_result_set_error	(enode->simple,
						 EWS_CONNECTION_ERROR,
						 ERROR_NORESPONSE,
						 _("No response"));
		goto exit;	
	}

	if (response && g_getenv ("EWS_DEBUG"))
		e_soap_response_dump_response (response, stdout);

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	subparam = e_soap_parameter_get_first_child_by_name (param, "GetItemResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	success = ews_get_response_status (subparam, &error);
	if (!success) {
		g_simple_async_result_propagate_error (enode->simple, &error);
		g_object_unref (response);
		goto exit;	
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
	
	if (node)
		item = e_ews_item_new_from_soap_parameter (node);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->item = item;

exit:
	g_simple_async_result_complete_in_idle (enode->simple);
	ews_active_job_done (cnc, msg);
}

static void
e_ews_connection_dispose (GObject *object)
{
	EEwsConnection *cnc = (EEwsConnection *) object;
	EEwsConnectionPrivate *priv;
	gchar *hash_key;

	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));

	priv = cnc->priv;
	printf ("ews connection dispose \n");

	/* removed the connection from the hash table */
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
	g_signal_handlers_disconnect_by_func 	(priv->soup_session, soup_request_unqueued, cnc);

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

	klass->next_request = NULL;

	/**
	 * EEwsConnection::next_request
	 **/
	signals[NEXT_REQUEST] = g_signal_new (
		"next_request",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EEwsConnectionClass, next_request),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
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
	g_static_rec_mutex_init (&priv->queue_lock);

	g_signal_connect (cnc, "next_request", G_CALLBACK (ews_next_request), NULL);
	g_signal_connect (priv->soup_session, "authenticate", G_CALLBACK(ews_connection_authenticate), cnc);
	g_signal_connect (priv->soup_session, "request-unqueued", G_CALLBACK (soup_request_unqueued), cnc);
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

gchar*
e_ews_autodiscover_ws_url (const gchar *email, const gchar *password, GError **error)
{
	gchar *url;
	gchar *domain;
	gchar *asurl = NULL;
	SoupMessage *msg;
	xmlDoc *doc;
	xmlNode *node, *child;
	xmlNs *ns;
	guint status;
	xmlOutputBuffer *buf;
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
	cnc = e_ews_connection_new (url, email, password, NULL);

	msg = soup_message_new("GET", url);
	soup_message_headers_append (msg->request_headers,
				     "User-Agent", "libews/0.1");

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
	
	buf = xmlAllocOutputBuffer(NULL);
	xmlNodeDumpOutput(buf, doc, xmlDocGetRootElement(doc), 0, 1, NULL);
	xmlOutputBufferFlush(buf);

	soup_message_set_request(msg, "application/xml", SOUP_MEMORY_COPY,
				 (gchar *)buf->buffer->content,
				 buf->buffer->use);
				 
	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)) {
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
	xmlFreeDoc (doc);

	if (status != 200) {
		g_set_error (
			error, EWS_CONNECTION_ERROR,
			status,
			_("Code: %d - Unexpected response from server"),
			status);
		goto failed;
	}

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)){
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
						 EwsFolderId *fid,
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

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderItems");
	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", default_props);
	
	if (additional_props) {
		gchar **prop = g_strsplit (additional_props, " ", 0);
		gint i = 0;

		e_soap_message_start_element (msg, "AdditionalProperties", "types", NULL);
		while (prop[i]) {
			gchar *item_prop = g_strdup_printf ("item:%s", prop[i]);
			e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", "types", NULL, "FieldURI", item_prop);
			g_free (item_prop);
			i++;
		}
		g_strfreev (prop);
	}

	e_soap_message_end_element (msg);
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "SyncFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", "types", NULL, "Id", fid->id);
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

void
e_ews_connection_sync_folder_items_finish	(EEwsConnection *cnc, 
						 GAsyncResult *result,
					 	 gchar **sync_state, 
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_sync_folder_items_start));

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return;
	
	*sync_state = async_data->sync_state;
	*items_created = async_data->items_created;
	*items_updated = async_data->items_updated;
	*items_deleted = async_data->items_deleted;

	return;
}

/* FIXME implement this as async apis
void
e_ews_connection_sync_folder_items (EEwsConnection *cnc, const gchar *sync_state, const gchar *folder_name, GCancellable *cancellable)
{
	ESoapMessage *msg;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderItems");
	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", "Default");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "SyncFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", folder_name);
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", NULL, sync_state);

	 Max changes requested 
	e_ews_message_write_int_parameter (msg, "MaxChangesReturned", NULL, 100);

	 Complete the footer and print the request 
	e_ews_message_write_footer (msg);

	ews_connection_queue_request (cnc, msg, dump_response_cb, cancellable, EWS_PRIORITY_MEDIUM);
}

void
e_ews_connection_create_folder (EEwsConnection *cnc, GCancellable *cancellable)
{
	ESoapMessage *msg;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "CreateFolder");

	e_soap_message_start_element (msg, "ParentFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", "msgfolderroot");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Folders", NULL, NULL);
	e_soap_message_start_element (msg, "Folder", "types", NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", "types", "TestBharath");
	e_soap_message_end_element (msg);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	ews_connection_queue_request (cnc, msg, create_folder_response_cb, cancellable, EWS_PRIORITY_HIGH);
} */

void 
e_ews_connection_create_folder_start	(EEwsConnection *cnc, 
					 gint pri, 
					 GAsyncReadyCallback cb, 
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "CreateFolder");

	e_soap_message_start_element (msg, "ParentFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", "msgfolderroot");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Folders", NULL, NULL);
	e_soap_message_start_element (msg, "Folder", "types", NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", "types", "TestBharath");
	e_soap_message_end_element (msg);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_create_folder_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, create_folder_response_cb, pri, cancellable, simple);
}


void
e_ews_connection_create_folder_finish	(EEwsConnection *cnc, 
					 GAsyncResult *result,
					 guint folder_id,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_create_folder_start));

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return;
	
	return;
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

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderHierarchy");
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


void
e_ews_connection_sync_folder_hierarchy_finish	(EEwsConnection *cnc, 
						 GAsyncResult *result,
					 	 gchar **sync_state, 
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_sync_folder_hierarchy_start));

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return;
	
	*sync_state = async_data->sync_state;
	*folders_created = async_data->folders_created;
	*folders_updated = async_data->folders_updated;
	*folders_deleted = async_data->folders_deleted;

	return;
}

void
e_ews_connection_sync_folder_hierarchy	(EEwsConnection *cnc, 
					 gint pri, 
					 gchar **sync_state, 
					 GSList **folders_created,
					 GSList **folders_updated,
					 GSList **folders_deleted,
					 GCancellable *cancellable, 
					 GError **error)
{
	EwsSyncData *sync_data;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->context = g_main_context_new ();
	sync_data->loop = g_main_loop_new (sync_data->context, FALSE);
	
	g_main_context_push_thread_default (sync_data->context);
	e_ews_connection_sync_folder_hierarchy_start	(cnc, pri, *sync_state, 
							 ews_sync_reply_cb, cancellable, 
							 (gpointer) sync_data);

	g_main_loop_run (sync_data->loop);
	e_ews_connection_sync_folder_hierarchy_finish	(cnc, sync_data->res, 
							 sync_state,
							 folders_created,
							 folders_updated,
							 folders_deleted,
							 error);

	g_main_context_unref (sync_data->context);
	g_main_loop_unref (sync_data->loop);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return;
}

void
e_ews_connection_get_item_start		(EEwsConnection *cnc,
					 gint pri,
					 EwsId *fid,
					 const gchar *default_props,
					 const gchar *additional_props,
					 const gchar *include_mime,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "GetItem");

	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", default_props);
	e_ews_message_write_string_parameter (msg, "IncludeMimeContent", "types", include_mime);
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "ItemIds", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", "types", NULL, "Id", fid->id);
	e_soap_message_end_element (msg);
	
	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_get_item_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, get_item_response_cb, pri, cancellable, simple);
}

void
e_ews_connection_get_item_finish	(EEwsConnection *cnc, 
					 GAsyncResult *result,
					 EEwsItem **item,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_get_item_start));

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return;

	*item = async_data->item;
	
	return;
}
