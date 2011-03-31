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
#include "ews-marshal.h"

#define d(x) x

/* For the number of connections */
#define EWS_CONNECTION_MAX_REQUESTS 10

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->queue_lock))

G_DEFINE_TYPE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

struct _EwsNode;
static GObjectClass *parent_class = NULL;
static GStaticMutex connecting = G_STATIC_MUTEX_INIT;
static GHashTable *loaded_connections_permissions = NULL;
static gboolean ews_next_request (gpointer _cnc);
static gint comp_func (gconstpointer a, gconstpointer b);

typedef void (*response_cb) (ESoapParameter *param, struct _EwsNode *enode);
static void ews_response_cb (SoupSession *session, SoupMessage *msg, gpointer data);

static void 
ews_connection_authenticate	(SoupSession *sess, SoupMessage *msg,
				 SoupAuth *auth, gboolean retrying, 
				 gpointer data);

/* Connection APIS */

struct _EEwsConnectionPrivate {
	SoupSession *soup_session;
	GThread *soup_thread;
	GMainLoop *soup_loop;
	GMainContext *soup_context;

	gchar *uri;
	gchar *username;
	gchar *password;
	gchar *email;

	GSList *jobs;
	GSList *active_job_queue;
	GStaticRecMutex queue_lock;
};

enum {
	AUTHENTICATE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct _EwsNode EwsNode;
typedef struct _EwsAsyncData EwsAsyncData;

struct _EwsAsyncData {
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
	gboolean complete_sync;

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

GQuark
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
			if (asurl)
				return asurl;
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

static void ews_trigger_next_request(EEwsConnection *cnc)
{
	GSource *source;

	source = g_idle_source_new ();
	g_source_set_priority (source, G_PRIORITY_DEFAULT);
	g_source_set_callback (source, ews_next_request, cnc, NULL);
	g_source_attach (source, cnc->priv->soup_context);
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
	ews_trigger_next_request(cnc);
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
ews_connection_queue_request (EEwsConnection *cnc, ESoapMessage *msg, response_cb cb, gint pri, GCancellable *cancellable, GSimpleAsyncResult *simple, gboolean complete_sync)
{
	EwsNode *node;

	node = ews_node_new ();
	node->msg = msg;
	node->pri = pri;
	node->cb = cb;
	node->cnc = cnc;
	node->complete_sync = complete_sync;
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

	ews_trigger_next_request(cnc);
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
		ESoapParameter *param, *subparam;
		GError *error = NULL;

		/* TODO: The stdout can be replaced with Evolution's
		   Logging framework also */
		if (response && g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) >= 1))
			e_soap_response_dump_response (response, stdout);

		param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
		if (param) {
			/* Iterate over all "*ResponseMessage" elements. */
			for (subparam = e_soap_parameter_get_first_child (param);
			     subparam;
			     subparam = e_soap_parameter_get_next_child (subparam)) {
				int l = strlen ((char *)subparam->name);
				if (l < 15 || strcmp((char *)subparam->name + l - 15, "ResponseMessage")) {
					g_warning ("Unexpected element '%s' in place of ResponseMessage",
						   subparam->name);
					continue;
				}

				if (!ews_get_response_status (subparam, &error)) {
					g_simple_async_result_set_from_error (enode->simple, error);
					break;
				}
				if (enode->cb)
					enode->cb (subparam, enode);
			}
		} else
			ews_parse_soap_fault (response, &error);

		if (error)
			g_simple_async_result_set_from_error (enode->simple, error);

		g_object_unref (response);
	}

exit:
	if (enode->complete_sync) {
		GAsyncResult *async = G_ASYNC_RESULT (enode->simple);

		/* If we just call g_simple_async_result_complete() then it
		   will bitch about being called in the wrong context, even
		   though we *know* it's OK. So instead, just call the
		   callback directly. We *know* it's ews_sync_reply_cb(),
		   because that's the only way the complete_sync flag gets
		   set */
		ews_sync_reply_cb (NULL, async, g_async_result_get_user_data (async));
	} else {
		g_simple_async_result_complete_in_idle (enode->simple);
	}
	ews_active_job_done (enode->cnc, enode);
}

typedef gpointer (*ItemParser) (ESoapParameter *param);

static void
sync_xxx_response_cb (ESoapParameter *subparam, EwsNode *enode, ItemParser parser,
		      const gchar *last_tag, const gchar *delete_id_tag)
{
	ESoapParameter *node;
	EwsAsyncData *async_data;
	gchar *new_sync_state = NULL, *value;
	GSList *items_created = NULL, *items_updated = NULL, *items_deleted = NULL;
	gboolean includes_last_item = FALSE;

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);

	node = e_soap_parameter_get_first_child_by_name (subparam, last_tag);
	if (!strcmp (e_soap_parameter_get_string_value (node), "true"))
		includes_last_item = TRUE;

	node = e_soap_parameter_get_first_child_by_name (subparam, "Changes");
	
	if (node) {
		ESoapParameter *subparam1;
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Create");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Create")) {
			EEwsFolder *folder;

			folder = parser (subparam1);
			items_created = g_slist_append (items_created, folder);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Update");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Update")) {
			EEwsFolder *folder;

			folder = parser (subparam1);
			items_updated = g_slist_append (items_updated, folder);
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Delete");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Delete")) {
			ESoapParameter *folder_param;

			folder_param = e_soap_parameter_get_first_child_by_name (subparam1, delete_id_tag);
			value = e_soap_parameter_get_property (folder_param, "Id");
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
sync_hierarchy_response_cb (ESoapParameter *subparam, EwsNode *enode)
{
	sync_xxx_response_cb (subparam, enode, (ItemParser)e_ews_folder_new_from_soap_parameter,
			      "IncludesLastFolderInRange", "FolderId");
}

static void
sync_folder_items_response_cb (ESoapParameter *subparam, EwsNode *enode)
{
	sync_xxx_response_cb (subparam, enode, (ItemParser) e_ews_item_new_from_soap_parameter,
			      "IncludesLastItemInRange", "ItemId");
}

/* Used for CreateItems and GetItems */
static void
get_items_response_cb (ESoapParameter *subparam, EwsNode *enode)
{
	ESoapParameter *node;
	EwsAsyncData *async_data;
	EEwsItem *item;

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);

	for (node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
	     node; node = e_soap_parameter_get_next_child_by_name (subparam, "Items")) {
		item = e_ews_item_new_from_soap_parameter (node);
		async_data->items = g_slist_append (async_data->items, item);
	}
}

static void
resolve_names_response_cb (ESoapParameter *subparam, EwsNode *enode)
{
	ESoapParameter *node;
	gboolean includes_last_item;
	GSList *mailboxes = NULL, *contact_items = NULL;
	EwsAsyncData *async_data;
	gchar *prop;

	subparam = e_soap_parameter_get_first_child_by_name (subparam, "ResolutionSet");
	prop = e_soap_parameter_get_property (subparam, "IncludesLastItemInRange");
	
	if (prop && !strcmp (prop, "true"))
		includes_last_item = TRUE;
	g_free (prop);

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

/* TODO scan all folders if we support creating multiple folders in the request */
static void
ews_create_folder_cb (ESoapParameter *soapparam, EwsNode *enode)
{
	ESoapParameter *param, *node;
	EwsAsyncData *async_data;
	EwsFolderId *fid = NULL;
	GSList *fids = NULL;

	node = e_soap_parameter_get_first_child_by_name (soapparam, "Folders");
	node = e_soap_parameter_get_first_child_by_name (node, "Folder");
	param = e_soap_parameter_get_first_child_by_name (node, "FolderId");

	fid = g_new0 (EwsFolderId, 1);
	fid->id = e_soap_parameter_get_property (param, "Id");
	fid->change_key = e_soap_parameter_get_property (param, "ChangeKey");
	fids = g_slist_append (fids, fid);

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);
	async_data->items_created = fids;
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
		hash_key = g_strdup_printf ("%s@%s",
					    priv->username ? priv->username : "",
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

		g_main_loop_quit(priv->soup_loop);
		g_thread_join(priv->soup_thread);
		priv->soup_thread = NULL;

		g_main_loop_unref(priv->soup_loop);
		priv->soup_loop = NULL;
		g_main_context_unref(priv->soup_context);
		priv->soup_context = NULL;
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

	klass->authenticate = NULL;

       /**
        * EEwsConnection::authenticate
        **/
	signals[AUTHENTICATE] = g_signal_new (
	      "authenticate",
	      G_OBJECT_CLASS_TYPE (klass),
	      G_SIGNAL_RUN_FIRST,
	      G_STRUCT_OFFSET (EEwsConnectionClass, authenticate),
	      NULL, NULL,
	      ews_marshal_VOID__OBJECT_OBJECT_BOOLEAN,
	      G_TYPE_NONE, 3,
	      SOUP_TYPE_MESSAGE, SOUP_TYPE_AUTH, G_TYPE_BOOLEAN);
}


static gpointer e_ews_soup_thread (gpointer user_data)
{
	EEwsConnectionPrivate *priv = user_data;

	g_main_context_push_thread_default (priv->soup_context);
	g_main_loop_run (priv->soup_loop);
	g_main_context_pop_thread_default (priv->soup_context);
	return NULL;
}

static void
e_ews_connection_init (EEwsConnection *cnc)
{
	EEwsConnectionPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsConnectionPrivate, 1);
	cnc->priv = priv;

	priv->soup_context = g_main_context_new ();
	priv->soup_loop = g_main_loop_new (priv->soup_context, FALSE);

	priv->soup_thread = g_thread_create(e_ews_soup_thread, priv, TRUE, NULL);
	
	/* create the SoupSession for this connection */
	priv->soup_session = soup_session_async_new_with_options (SOUP_SESSION_USE_NTLM, TRUE, 
								  SOUP_SESSION_ASYNC_CONTEXT, priv->soup_context, NULL);

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
		g_free (cnc->priv->password);
		cnc->priv->password = NULL;
	}

	if (cnc->priv->password) {
		soup_auth_authenticate (auth, cnc->priv->username,
					cnc->priv->password);
		return;
	}

	g_signal_emit (cnc, signals[AUTHENTICATE], 0, msg, auth, retrying);
}

void
e_ews_connection_authenticate (EEwsConnection *cnc,
			       SoupAuth *auth, const gchar *user,
			       const gchar *passwd, GError *error)
{
	if (error) {
		g_warning ("Auth error: %s", error->message);
		g_clear_error (&error);
		return;
	}

	if (user) {
		g_free (cnc->priv->username);
		cnc->priv->username = g_strdup (user);
	}

	g_free (cnc->priv->password);
	cnc->priv->password = g_strdup(passwd);

	soup_auth_authenticate (auth, cnc->priv->username,
				cnc->priv->password);
}
/* Connection APIS */

/**
 * e_ews_connection_find
 * @uri: Exchange server uri
 * @username: 
 * 
 * Find an existing connection for this user/uri, if it exists.
 * 
 * Returns: EEwsConnection
 **/
EEwsConnection *
e_ews_connection_find (const gchar *uri, const gchar *username)
{
	EEwsConnection *cnc;
	gchar *hash_key;

	g_static_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions) {
		hash_key = g_strdup_printf ("%s@%s",
				username ? username : "",
				uri);
		cnc = g_hash_table_lookup (loaded_connections_permissions, hash_key);
		g_free (hash_key);

		if (E_IS_EWS_CONNECTION (cnc)) {
			g_object_ref (cnc);
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
	}
		     
	g_static_mutex_unlock (&connecting);

	return NULL;
}

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
e_ews_connection_new (const gchar *uri, const gchar *username, const gchar *password, 
		      GCallback authenticate_cb, gpointer authenticate_ctx,
		      GError **error)
{
	EEwsConnection *cnc;
	gchar *hash_key;

	g_static_mutex_lock (&connecting);

	/* search the connection in our hash table */
	if (loaded_connections_permissions != NULL) {
		hash_key = g_strdup_printf ("%s@%s",
				username ? username : "",
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

	/* register a handler to the authenticate signal */
	if (authenticate_cb)
		g_signal_connect (cnc, "authenticate",
				  authenticate_cb, authenticate_ctx);

	/* add the connection to the loaded_connections_permissions hash table */
	hash_key = g_strdup_printf ("%s@%s",
			cnc->priv->username ? cnc->priv->username : "",
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

struct _autodiscover_data {
	EEwsConnection *cnc;
	GSimpleAsyncResult *simple;
	SoupMessage *msgs[2];
	EEwsAutoDiscoverCallback cb;
	gpointer cbdata;
};


/* Called in the context e_ews_autodiscover_ws_url() was called from,
   with the final result. */
static void autodiscover_done_cb (GObject *cnc, GAsyncResult *res,
				  gpointer user_data)
{
	struct _autodiscover_data *ad = user_data;
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
	GError *error = NULL;
	gchar *url = NULL;

	if (!g_simple_async_result_propagate_error (simple, &error))
		url = g_simple_async_result_get_op_res_gpointer (simple);

	ad->cb (url, ad->cbdata, error);
	g_object_unref (G_OBJECT (ad->cnc));
	g_free (ad);
}

/* Called when each soup message completes */
static void
autodiscover_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)

{
	GError *error = NULL;
	struct _autodiscover_data *ad = data;
	guint status = msg->status_code;
	xmlDoc *doc;
	xmlNode *node;
	char *asurl;
	int idx;

	for (idx = 0; idx < 2; idx++) {
		if (ad->msgs[idx] == msg)
			break;
	}
	if (idx == 2) {
		/* We already got removed (cancelled). Do nothing */
		return;
	}

	ad->msgs[idx] = NULL;

	if (status != 200) {
		g_set_error (
			     &error, EWS_CONNECTION_ERROR,
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
			     &error, EWS_CONNECTION_ERROR,
			     -1,
			     _("Failed to parse autodiscover response XML"));
		goto failed;
	}
	node = xmlDocGetRootElement(doc);
	if (strcmp((char *)node->name, "Autodiscover")) {
		g_set_error (
			     &error, EWS_CONNECTION_ERROR,
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
			     &error, EWS_CONNECTION_ERROR,
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
			     &error, EWS_CONNECTION_ERROR,
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

	/* We have a good response; cancel all the others */
	for (idx = 0; idx < 2; idx++) {
		if (ad->msgs[idx]) {
			SoupMessage *m = ad->msgs[idx];
			ad->msgs[idx] = NULL;
			soup_session_cancel_message (ad->cnc->priv->soup_session,
						     m, SOUP_STATUS_CANCELLED);
		}
	}
	g_simple_async_result_set_op_res_gpointer (ad->simple, asurl, NULL);
	g_simple_async_result_complete_in_idle (ad->simple);
	return;

failed:
	for (idx = 0; idx < 2; idx++) {
		if (ad->msgs[idx]) {
			/* There's another request outstanding.
			   Hope that it has better luck. */
			g_clear_error (&error);
			return;
		}
	}

	/* FIXME: We're actually returning the *last* error here,
	   and in some cases (stupid firewalls causing timeouts)
	   that's going to be the least interesting one. We probably
	   want the *first* error */
	g_simple_async_result_set_from_error (ad->simple, error);
	g_simple_async_result_complete_in_idle (ad->simple);
}

static SoupMessage *
e_ews_autodiscover_ws_msg(EEwsConnection *cnc, gchar *url,
			  xmlOutputBuffer *buf, struct _autodiscover_data *ad)
{
	SoupMessage *msg;
       
	msg = soup_message_new("GET", url);
	soup_message_headers_append (msg->request_headers,
				     "User-Agent", "libews/0.1");


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

	return msg;
}

void
e_ews_autodiscover_ws_url (EEwsAutoDiscoverCallback cb, gpointer cbdata,
			   gchar *email, gchar *password)
{
	struct _autodiscover_data *ad;
	xmlOutputBuffer *buf;
	GError *error = NULL;
	gchar *url;
	gchar *domain;
	xmlDoc *doc;
	EEwsConnection *cnc;

	if (!password || !email) {
		g_set_error (&error, EWS_CONNECTION_ERROR,
			     -1, _("Both email and password must be provided"));
		goto err;
	}

	domain = strchr(email, '@');
	if (!(domain && *domain)) {
		g_set_error (&error, EWS_CONNECTION_ERROR,
			     -1, _("Wrong email id"));
		goto err;
	}
	domain++;

	doc = e_ews_autodiscover_ws_xml(email);
	buf = xmlAllocOutputBuffer(NULL);
	xmlNodeDumpOutput(buf, doc, xmlDocGetRootElement(doc), 0, 1, NULL);
	xmlOutputBufferFlush(buf);

	url = g_strdup_printf("https://%s/autodiscover/autodiscover.xml", domain);

	/* FIXME: Get username from config; don't assume same as email */
	cnc = e_ews_connection_new (url, email, password, NULL, NULL, &error);
	if (!cnc) {
	err:
		g_free (email);
		g_free (password);
		cb (NULL, cbdata, error);
		return;
	}

	/*
	 * http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
	 * supposed to try $domain and then autodiscover.$domain. But some
	 * people have broken firewalls on the former which drop packets
	 * instead of rejecting connections, and make the request take ages
	 * to time out. So run both queries in parallel and let the fastest
	 * (successful) one win.
	 */
	ad = g_new0 (struct _autodiscover_data, 1);
	ad->cb = cb;
	ad->cbdata = cbdata;
	ad->cnc = cnc;
	ad->simple = g_simple_async_result_new (G_OBJECT (cnc), autodiscover_done_cb,
					    ad, e_ews_autodiscover_ws_url);
	ad->msgs[0] = e_ews_autodiscover_ws_msg(cnc, url, buf, ad);
	g_free (url);

	url = g_strdup_printf("https://autodiscover.%s/autodiscover/autodiscover.xml", domain);
	ad->msgs[1] = e_ews_autodiscover_ws_msg(cnc, url, buf, ad);
	g_free (url);

	/* These have to be submitted only after they're both set in ad->msgs[]
	   or there will be races with fast completion */
	soup_session_queue_message (cnc->priv->soup_session, ad->msgs[0],
				    autodiscover_response_cb, ad);
	soup_session_queue_message (cnc->priv->soup_session, ad->msgs[1],
				    autodiscover_response_cb, ad);

	g_object_unref (cnc); /* the GSimpleAsyncResult holds it now */

	g_free (email);
	g_free (password);

	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);
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

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderItems", NULL, NULL, EWS_EXCHANGE_2007);
	e_soap_message_start_element (msg, "ItemShape", "messages", NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", NULL, default_props);
	
	if (additional_props && *additional_props) {
		gchar **prop = g_strsplit (additional_props, " ", 0);
		gint i = 0;

		e_soap_message_start_element (msg, "AdditionalProperties", NULL, NULL);
		while (prop[i]) {
			e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", prop [i]);
			i++;
		}
		g_strfreev (prop);
		e_soap_message_end_element (msg);
	}

	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "SyncFolderId", "messages", NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL, NULL, "Id", fid);
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", "messages", sync_state);

	/* Max changes requested */
	e_ews_message_write_int_parameter (msg, "MaxChangesReturned", "messages", max_entries);

	/* Complete the footer and print the request */
	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_sync_folder_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, sync_folder_items_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
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

	msg = e_ews_message_new_with_header (cnc->priv->uri, "SyncFolderHierarchy", NULL, NULL, EWS_EXCHANGE_2007);
	e_soap_message_start_element (msg, "FolderShape", "messages", NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", NULL, "AllProperties");
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", "messages", sync_state);

	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_sync_folder_hierarchy_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, sync_hierarchy_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
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
	*folders_created = async_data->items_created;
	*folders_updated = async_data->items_updated;
	*folders_deleted = async_data->items_deleted;

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
					 const GSList *ids,
					 const gchar *default_props,
					 const gchar *additional_props,
					 gboolean include_mime,
					 GAsyncReadyCallback cb,
					 ESoapProgressFn progress_fn,
					 gpointer progress_data,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	const GSList *l;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "GetItem", NULL, NULL, EWS_EXCHANGE_2007);

	if (progress_fn && progress_data)
		e_soap_message_set_progress_fn (msg, progress_fn, progress_data);

	e_soap_message_start_element (msg, "ItemShape", "messages", NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", NULL, default_props);
	
	if (include_mime)
		e_ews_message_write_string_parameter (msg, "IncludeMimeContent", NULL, "true");
	else
		e_ews_message_write_string_parameter (msg, "IncludeMimeContent", NULL, "false");
	
	if (additional_props && *additional_props) {
		gchar **prop = g_strsplit (additional_props, " ", 0);
		gint i = 0;

		e_soap_message_start_element (msg, "AdditionalProperties", NULL, NULL);
		while (prop[i]) {
			/* XX FIXME: Come up with a better way of doing this */
			if (!g_ascii_strncasecmp (prop[i], "mapi:int:0x", 11)) {
				e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
				e_soap_message_add_attribute (msg, "PropertyTag", prop[i] + 9, NULL, NULL);
				e_soap_message_add_attribute (msg, "PropertyType", "Integer", NULL, NULL);
				e_soap_message_end_element (msg);
			} else {
				e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", prop [i]);
			}
			i++;
		}
		g_strfreev (prop);
		e_soap_message_end_element (msg);
	}
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "ItemIds", "messages", NULL);
	
	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", NULL, NULL, "Id", l->data);
	
	e_soap_message_end_element (msg);
	
	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_get_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, get_items_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
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
				 const GSList *ids,
				 const gchar *default_props,
				 const gchar *additional_props,
				 gboolean include_mime,
				 GSList **items, 
				 ESoapProgressFn progress_fn,
				 gpointer progress_data,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_get_items_start	(cnc, pri,ids, default_props,
						 additional_props, include_mime,
						 ews_sync_reply_cb, 
						 progress_fn, progress_data,
						 cancellable,
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
					     "DeleteType", delete_type, EWS_EXCHANGE_2007);

	if (send_cancels)
		e_soap_message_add_attribute (msg, "SendMeetingCancellations",
					      send_cancels, NULL, NULL);
	if (affected_tasks)
		e_soap_message_add_attribute (msg, "AffectedTaskOccurrences",
					      affected_tasks, NULL, NULL);

	e_soap_message_start_element (msg, "ItemIds", "messages", NULL);
	
	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", NULL, NULL, "Id", l->data);
	
	e_soap_message_end_element (msg);
	
	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_delete_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, NULL, pri, cancellable, simple,
				      cb == ews_sync_reply_cb);
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
					     NULL, NULL, EWS_EXCHANGE_2007);

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
		e_soap_message_start_element (msg, "SavedItemFolderId", "messages", NULL);
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId",
						     NULL, NULL, "Id", folder_id);
		e_soap_message_end_element (msg);
	}

	e_soap_message_start_element (msg, "ItemChanges", "messages", NULL);
	
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

	ews_connection_queue_request (cnc, msg, get_items_response_cb, pri, cancellable, simple,
				      cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_update_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **ids,
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
	if (ids)
		*ids = async_data->items;

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
				 GSList **ids,
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
						       ids, error);
	
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
					     NULL, NULL, EWS_EXCHANGE_2007);

	if (msg_disposition)
		e_soap_message_add_attribute (msg, "MessageDisposition",
					      msg_disposition, NULL, NULL);
	if (send_invites)
		e_soap_message_add_attribute (msg, "SendMeetingInvitations",
					      send_invites, NULL, NULL);

	if (folder_id) {
		e_soap_message_start_element (msg, "SavedItemFolderId", "messages", NULL);
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId",
						     NULL, NULL, "Id", folder_id);
		e_soap_message_end_element (msg);
	}

	e_soap_message_start_element (msg, "Items", "messages", NULL);
	
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

	ews_connection_queue_request (cnc, msg, get_items_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
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

	msg = e_ews_message_new_with_header (cnc->priv->uri, "ResolveNames", NULL, NULL, EWS_EXCHANGE_2007_SP1);

	e_soap_message_add_attribute (msg, "SearchScope", get_search_scope_str (scope), NULL, NULL);
	
	if (fetch_contact_data)
		e_soap_message_add_attribute (msg, "ReturnFullContactData", "true", NULL, NULL);
	else
		e_soap_message_add_attribute (msg, "ReturnFullContactData", "false", NULL, NULL);

	if (parent_folder_ids) {
		e_soap_message_start_element (msg, "ParentFolderIds", "messages", NULL);

		for (l = parent_folder_ids; l != NULL; l = g_slist_next (l)) {
			EwsFolderId *fid = (EwsFolderId *) l->data;
			
			if (fid->is_distinguished_id)
				e_soap_message_start_element (msg, "DistinguishedFolderId", NULL, NULL);
			else
				e_soap_message_start_element (msg, "FolderId", NULL, NULL);
		
			e_soap_message_add_attribute (msg, "Id", fid->id, NULL, NULL);
			if (fid->change_key)
				e_soap_message_add_attribute (msg, "ChangeKey", fid->change_key, NULL, NULL);

			if (fid->is_distinguished_id && cnc->priv->email)
				e_ews_message_write_string_parameter (msg, "Mailbox", NULL, cnc->priv->email);

			e_soap_message_end_element (msg);
		}

		e_soap_message_end_element (msg);
	}

	e_ews_message_write_string_parameter (msg, "UnresolvedEntry", "messages", resolve_name);

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_resolve_names_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, resolve_names_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
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
	
	if (contact_items)
		*contact_items = async_data->items_created;
	*mailboxes = async_data->items;
	
	return TRUE;	
}

gboolean
e_ews_connection_resolve_names	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *resolve_name,
				 EwsContactsSearchScope scope,
				 GSList *parent_folder_ids,
				 gboolean fetch_contact_data,
				 GSList **mailboxes,
				 GSList **contact_items,
				 gboolean *includes_last_item,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_resolve_names_start (cnc, pri, resolve_name,
					     scope, parent_folder_ids,
					     fetch_contact_data,
					     ews_sync_reply_cb, cancellable,
					     (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_resolve_names_finish (cnc, sync_data->res,
						       mailboxes, contact_items,
						       includes_last_item, error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_update_folder_start	(EEwsConnection *cnc,
					 gint pri,
					 EEwsRequestCreationCallback create_cb,
					 gpointer create_user_data,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "UpdateFolder",
					     NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "FolderChanges", "messages", NULL);
	
	create_cb (msg, create_user_data);

	e_soap_message_end_element (msg); /* FolderChanges */

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_update_folder_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, NULL, pri, cancellable, simple,
				      cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_update_folder_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_update_folder_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean		
e_ews_connection_update_folder	(EEwsConnection *cnc,
				 gint pri,
				 EEwsRequestCreationCallback create_cb,
				 gpointer create_user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_update_folder_start (cnc, pri,
					      create_cb, create_user_data,
					      ews_sync_reply_cb, cancellable,
					      (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_update_folder_finish (cnc, sync_data->res,
							error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_move_folder_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *to_folder,
					 const gchar *folder,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "MoveFolder",
					     NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "ToFolderId", "messages", NULL);
	if (to_folder)
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL,
								     NULL, "Id", to_folder);
	else
		e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", NULL,
								     NULL, "Id", "msgfolderroot");

	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "FolderIds", "messages", NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL,
							     NULL, "Id", folder);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
                                      user_data,
                                      e_ews_connection_move_folder_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, NULL, pri, cancellable, simple,
				      cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_move_folder_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_move_folder_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean		
e_ews_connection_move_folder	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *to_folder,
				 const gchar *folder,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_move_folder_start (cnc, pri, to_folder, folder,
					    ews_sync_reply_cb, cancellable,
					    (gpointer) sync_data); 
		       				 	
	e_flag_wait (sync_data->eflag);
	
	result = e_ews_connection_move_folder_finish (cnc, sync_data->res,
						      error);
	
	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
e_ews_connection_create_folder_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *parent_folder_id,
					 gboolean is_distinguished_id,
					 const gchar *folder_name,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "CreateFolder", NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "ParentFolderId", "messages", NULL);
	
	if (is_distinguished_id)
		e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", NULL, NULL, "Id", parent_folder_id);
	else
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL, NULL, "Id", parent_folder_id);
		
	if (is_distinguished_id && cnc->priv->email)
		e_ews_message_write_string_parameter (msg, "Mailbox", NULL, cnc->priv->email);

	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Folders", "messages", NULL);
	e_soap_message_start_element(msg, "Folder", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", NULL, folder_name);

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

	ews_connection_queue_request (cnc, msg, ews_create_folder_cb, pri, cancellable, simple, cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_create_folder_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 EwsFolderId **fid,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_create_folder_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*fid = (EwsFolderId *) async_data->items_created->data;
	g_slist_free (async_data->items_created);

	return TRUE;
}


gboolean		
e_ews_connection_create_folder	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *parent_folder_id,
				 gboolean is_distinguished_id,
				 const gchar *folder_name,
				 EwsFolderId **folder_id,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_create_folder_start	(cnc, pri, parent_folder_id, 
						 is_distinguished_id,
						 folder_name,
						 ews_sync_reply_cb, 
						 cancellable,
						 (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_create_folder_finish (cnc, sync_data->res,
							folder_id,
							error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;

}

void
e_ews_connection_move_items_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *folder_id,
					 gboolean docopy,
					 GSList *ids,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	GSList *l;

	if (docopy)
		msg = e_ews_message_new_with_header (cnc->priv->uri, "CopyItem",
					     NULL, NULL, EWS_EXCHANGE_2007);
	else
		msg = e_ews_message_new_with_header (cnc->priv->uri, "MoveItem",
					     NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "ToFolderId", "messages", NULL);
	e_soap_message_start_element (msg, "FolderId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", folder_id, NULL, NULL);
	e_soap_message_end_element (msg); /* FolderId */
	e_soap_message_end_element (msg); /* ToFolderId */

	e_soap_message_start_element (msg, "ItemIds", "messages", NULL);
	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", NULL, NULL, "Id", l->data);
	e_soap_message_end_element (msg); /* ItemIds */

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
					    cb,
					    user_data,
					    e_ews_connection_move_items_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, get_items_response_cb, pri, cancellable, simple,
				      cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_move_items_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **items,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_move_items_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*items = async_data->items;

	return TRUE;
}

gboolean
e_ews_connection_move_items	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *folder_id,
				 gboolean docopy,
				 GSList *ids,
				 GSList **items,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();

	e_ews_connection_move_items_start (cnc, pri, folder_id, docopy, ids,
					   ews_sync_reply_cb, cancellable,
					   (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_move_items_finish (cnc, sync_data->res,
						     items, error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

void
/**
 * e_ews_connection_delete_folder_start 
 * @cnc: 
 * @pri: 
 * @folder_id: folder to be deleted 
 * @is_distinguished_id: 
 * @delete_type: "HardDelete", "SoftDelete", "MoveToDeletedItems"
 * @cb: 
 * @cancellable: 
 * @user_data: 
 **/
e_ews_connection_delete_folder_start	(EEwsConnection *cnc,
					 gint pri,
					 const gchar *folder_id,
					 gboolean is_distinguished_id,
					 const gchar *delete_type,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "DeleteFolder", "DeleteType", delete_type, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "FolderIds", "messages", NULL);
	
	if (is_distinguished_id)
		e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", NULL, NULL, "Id", folder_id);
	else
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL, NULL, "Id", folder_id);
	
	/*This element is required for delegate access*/	
	if (is_distinguished_id && cnc->priv->email)
		e_ews_message_write_string_parameter (msg, "Mailbox", NULL, cnc->priv->email);

	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

      	simple = g_simple_async_result_new (G_OBJECT (cnc),
                                      cb,
				      user_data,
                                      e_ews_connection_delete_folder_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, NULL, pri, cancellable, simple, cb == ews_sync_reply_cb);
}


gboolean
e_ews_connection_delete_folder_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_delete_folder_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}

gboolean		
/**
 * e_ews_connection_delete_folder 
 * @cnc: 
 * @pri: 
 * @folder_id: folder to be deleted 
 * @is_distinguished_id: 
 * @delete_type: "HardDelete", "SoftDelete", "MoveToDeletedItems"
 * @cancellable: 
 * @error: 
 **/
e_ews_connection_delete_folder	(EEwsConnection *cnc,
				 gint pri,
				 const gchar *folder_id,
				 gboolean is_distinguished_id,
				 const gchar *delete_type,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();
	
	e_ews_connection_delete_folder_start	(cnc, pri, folder_id, 
						 is_distinguished_id,
						 delete_type,
						 ews_sync_reply_cb, 
						 cancellable,
						 (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_delete_folder_finish (cnc, sync_data->res,
							error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;

}

static void get_attachments_response_cb (ESoapParameter *subparam, EwsNode *enode);

void
e_ews_connection_get_attachments_start	(EEwsConnection *cnc,
					 gint pri,
					 GSList *ids,
					 gboolean include_mime,
					 GAsyncReadyCallback cb,
					 ESoapProgressFn progress_fn,
					 gpointer progress_data,
					 GCancellable *cancellable,
					 gpointer user_data)
{
	ESoapMessage *msg;
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;
	GSList *l;

	msg = e_ews_message_new_with_header (cnc->priv->uri, "GetAttachment", NULL, NULL, EWS_EXCHANGE_2007);

	/* not sure why I need it, need to check */
	if (progress_fn && progress_data)
		e_soap_message_set_progress_fn (msg, progress_fn, progress_data);

	/* wrtie empty attachments shape, need to discover maybe usefull in some cases*/
	e_soap_message_start_element (msg, "AttachmentShape", "messages", NULL);
	e_soap_message_end_element(msg);

	/* start interation over all items to get the attachemnts */
	e_soap_message_start_element (msg, "AttachmentIds", "messages", NULL);

	for (l = ids; l != NULL; l = g_slist_next (l))
		e_ews_message_write_string_parameter_with_attribute (msg, "AttachmentId", NULL, NULL, "Id", l->data);

	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	simple = g_simple_async_result_new (G_OBJECT (cnc),
				      cb,
				      user_data,
				      e_ews_connection_get_attachments_start);

	async_data = g_new0 (EwsAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (
		simple, async_data, (GDestroyNotify) async_data_free);

	ews_connection_queue_request (cnc, msg, get_attachments_response_cb, pri,
				      cancellable, simple, cb == ews_sync_reply_cb);
}

gboolean
e_ews_connection_get_attachments_finish	(EEwsConnection *cnc,
					 GAsyncResult *result,
					 GSList **items,
					 GError **error)
{
	GSimpleAsyncResult *simple;
	EwsAsyncData *async_data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (cnc), e_ews_connection_get_attachments_start),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*items = async_data->items;

	return TRUE;
}

gboolean
e_ews_connection_get_attachments(EEwsConnection *cnc,
				 gint pri,
				 GSList *ids,
				 gboolean include_mime,
				 GSList **items,
				 ESoapProgressFn progress_fn,
				 gpointer progress_data,
				 GCancellable *cancellable,
				 GError **error)
{
	EwsSyncData *sync_data;
	gboolean result;

	sync_data = g_new0 (EwsSyncData, 1);
	sync_data->eflag = e_flag_new ();

	e_ews_connection_get_attachments_start	(cnc, pri,ids, include_mime,
						 ews_sync_reply_cb,
						 progress_fn, progress_data,
						 cancellable,
						 (gpointer) sync_data);

	e_flag_wait (sync_data->eflag);

	result = e_ews_connection_get_attachments_finish(cnc,
						    sync_data->res,
						    items,
						    error);

	e_flag_free (sync_data->eflag);
	g_object_unref (sync_data->res);
	g_free (sync_data);

	return result;
}

static void
get_attachments_response_cb (ESoapParameter *param, EwsNode *enode)
{
	ESoapParameter *subparam,*subparam1, *attspara;
	EwsAsyncData *async_data;
	CalendarAttachment *calendar_attachment;

	async_data = g_simple_async_result_get_op_res_gpointer (enode->simple);

	attspara = e_soap_parameter_get_first_child_by_name (param, "Attachments");

	for (subparam = e_soap_parameter_get_first_child (attspara); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {

		calendar_attachment = g_new0 (CalendarAttachment, 1);

		calendar_attachment->type = e_soap_parameter_get_name(subparam);

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "AttachmentId");

		calendar_attachment->id = e_soap_parameter_get_property (subparam1, "Id");

		if (!g_ascii_strcasecmp (calendar_attachment->type, "ItemAttachment")) {
			calendar_attachment->data = e_ews_item_new_from_soap_parameter (subparam);
		}
		else if (!g_ascii_strcasecmp (calendar_attachment->type, "FileAttachment")) {
			calendar_attachment->data = e_ews_item_new_file_attachment_from_soap_parameter(e_soap_parameter_get_next_child(subparam1));
		}
		async_data->items = g_slist_append (async_data->items, calendar_attachment);
	}

}
