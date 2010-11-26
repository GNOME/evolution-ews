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
#include "e-ews-folder.h"

#define d(x) x

/* For the number of connections */
#define EWS_CONNECTIONS_NUMBER 2

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->queue_lock))

G_DEFINE_TYPE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections_permissions = NULL;
static void ews_next_request (EEwsConnection *cnc);
static gint comp_func (gconstpointer a, gconstpointer b);
typedef void (*response_cb) (SoupSession *session, SoupMessage *msg, gpointer user_data);

struct _EEwsConnectionPrivate {
	SoupSession *soup_session;

	gchar *uri;
	gchar *username;
	gchar *password;

	GSList *jobs;
	guint active_jobs;
	GStaticRecMutex queue_lock;
};

enum {
	NEXT_REQUEST,
	SHUTDOWN,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct _EWSNode EWSNode;
struct _EWSNode {
	ESoapMessage *msg;
	GError *error;

	guint32 type;		/* operation type */
	gint pri;		/* the command priority */
	response_cb cb;
};

/* Static Functions */
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


static void
ews_next_request (EEwsConnection *cnc)
{
	GSList *l;
	EWSNode *node;

	QUEUE_LOCK (cnc);

	l = cnc->priv->jobs;
	node = (EWSNode *) l->data;

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (node->msg)->request_body));
		/* print request's body */
		printf ("\n The request headers");
		printf ("\n ===================");
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (node->msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}

	/* Remove the node from the priority queue */
	cnc->priv->jobs = g_slist_remove (cnc->priv->jobs, (gconstpointer *) node);

	/* Add to active job queue */
	cnc->priv->active_jobs++;
	e_ews_connection_queue_message (cnc, node->msg, node->cb, cnc);

	QUEUE_UNLOCK (cnc);
}

/* Response callbacks */

static void
response_parse_generic (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response;
	EEwsConnection *cnc = (EEwsConnection *) data;

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->request_body));
		/* print request's body */
		g_print ("\n------\n");
		fputs (SOUP_MESSAGE (msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}

	response = e_soap_message_parse_response ((ESoapMessage *) msg);

	if (response && g_getenv ("EWS_DEBUG")) {

		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		e_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

	QUEUE_LOCK (cnc);

	/* One more job processed */
	cnc->priv->active_jobs--;

	if (g_slist_length (cnc->priv->jobs))
		g_signal_emit (cnc, signals[NEXT_REQUEST], 0);
	else if (!cnc->priv->active_jobs)
		g_signal_emit (cnc, signals[SHUTDOWN], 0);

	QUEUE_UNLOCK (cnc);

	/* free memory */
	g_object_unref (response);
}

static void
sync_hierarchy_response_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
	ESoapResponse *response = e_soap_message_parse_response ((ESoapMessage *) msg);
	EEwsConnection *cnc = (EEwsConnection *) data;
	ESoapParameter *param, *subparam, *node;
	const gchar *new_sync_state = NULL;

	if (response && g_getenv ("EWS_DEBUG")) {
		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		e_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

	param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
	subparam = e_soap_parameter_get_first_child_by_name (param, "SyncFolderHierarchyResponseMessage");
	node = e_soap_parameter_get_first_child_by_name (subparam, "ResponseCode");

	/* Negative cases */
	if (strcmp (e_soap_parameter_get_string_value(node), "NoError") != E_EWS_CONNECTION_STATUS_OK) {
		/* free memory */
		g_object_unref (response);
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "SyncState");
	new_sync_state = e_soap_parameter_get_string_value (node);
	g_print ("\n The sync state is... \n %s\n", new_sync_state);

	node = e_soap_parameter_get_first_child_by_name (subparam, "IncludesLastFolderInRange");
	if (!strcmp (e_soap_parameter_get_string_value (node), "true")) {
		/* This suggests we have received all the data and no need to make more sync
		 * hierarchy requests.
		 */
	}

	node = e_soap_parameter_get_first_child_by_name (subparam, "Changes");
	
	if (node) {
		ESoapParameter *subparam1;
		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Create");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Create")) {
			EEwsFolder *folder;

			folder = e_ews_folder_new_from_soap_parameter (subparam1);
			/* Add the folders in the "created" list of folders */
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Update");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Update")) {
			EEwsFolder *folder;

			folder = e_ews_folder_new_from_soap_parameter (subparam1);
			/* Add the folders in the "updated" list of folders */
		}

		for (subparam1 = e_soap_parameter_get_first_child_by_name (node, "Delete");
		     subparam1 != NULL;
		     subparam1 = e_soap_parameter_get_next_child_by_name (subparam1, "Delete")) {
			ESoapParameter *folder_param;
			gchar *value;

			folder_param = e_soap_parameter_get_first_child_by_name (subparam1, "FolderId");
			value = e_soap_parameter_get_property (folder_param, "Id");
			g_print("\n The deleted folder id is... %s\n", value);
			g_free (value);

//			/* Should we construct a folder for the delete types? */
//			folder = e_ews_folder_new_from_soap_parameter (subparam1);
			/* Add the folders in the "deleted" list of folders */
		}
	}

	QUEUE_LOCK (cnc);

	/* One more job processed */
	cnc->priv->active_jobs--;

	if (g_slist_length (cnc->priv->jobs))
		g_signal_emit (cnc, signals[NEXT_REQUEST], 0);
	else if (!cnc->priv->active_jobs)
		g_signal_emit (cnc, signals[SHUTDOWN], 0);

	QUEUE_UNLOCK (cnc);

	/* free memory */
	g_object_unref (response);
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

	if (priv) {
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
	printf ("ews connection finalize\n");
	/* clean up */
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
	klass->shutdown = NULL;

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

	/**
	 * EEwsConnection::shutdown
	 **/
	signals[SHUTDOWN] = g_signal_new (
		"shutdown",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EEwsConnectionClass, shutdown),
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

	g_signal_connect (cnc, "next_request", G_CALLBACK (ews_next_request), NULL);

}

static void 
ews_connection_authenticate (SoupSession *sess, SoupMessage *msg,
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

	g_signal_connect (cnc->priv->soup_session, "authenticate",
			  G_CALLBACK(ews_connection_authenticate), cnc);

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
e_ews_autodiscover_ws_url (const gchar *username, const gchar *password, const gchar *email)
{
	/*TODO : Add a GError*/
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

	g_return_val_if_fail (username != NULL, NULL);
	g_return_val_if_fail (password != NULL, NULL);
	g_return_val_if_fail (email != NULL, NULL);

	domain = strchr(email, '@');
	if (!(domain && *domain)) 
		return NULL;
	domain++;

	url = g_strdup_printf("https://%s/autodiscover/autodiscover.xml", domain);
	cnc = e_ews_connection_new (url, username, password, NULL);

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
		fprintf(stderr, "Unexpected response from server: %d\n", status);
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
		fprintf(stderr, "Failed to parse autodiscover response XML\n");
		goto failed;
	}
	node = xmlDocGetRootElement(doc);
	if (strcmp((char *)node->name, "Autodiscover")) {
		fprintf(stderr, "Failed to find <Autodiscover> element\n");
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Response"))
			break;
	}
	if (!node) {
		fprintf(stderr, "Failed to find <Response> element\n");
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Account"))
			break;
	}
	if (!node) {
		fprintf(stderr, "Failed to find <Account> element\n");
		goto failed;
	}
	for (node = node->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !strcmp((char *)node->name, "Protocol") &&
		    (asurl = autodiscover_parse_protocol(node)))
			break;
	}
failed:
	return asurl;
}

void
e_ews_connection_queue_message (EEwsConnection *cnc, ESoapMessage *msg, SoupSessionCallback callback,
			   gpointer user_data)
{
	g_return_if_fail (E_IS_EWS_CONNECTION (cnc));
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	QUEUE_LOCK (cnc);

	soup_session_queue_message (cnc->priv->soup_session, SOUP_MESSAGE (msg), callback, user_data);

	QUEUE_UNLOCK (cnc);
}

void
e_ews_connection_find_item (EEwsConnection *cnc, const gchar *folder_name)
{
	ESoapMessage *msg;
	const gchar *url = g_getenv ("EWS_TEST_URI");
	EWSNode *node;

	msg = e_ews_message_new_with_header (url, "FindItem");
	e_soap_message_add_attribute (msg, "Traversal", "Shallow", NULL, NULL);
	e_soap_message_start_element (msg, "ItemShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", "IdOnly");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "ParentFolderIds", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", folder_name);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	QUEUE_LOCK (cnc);

	node = ews_node_new ();
	node->msg = msg;
	node->pri = EWS_PRIORITY_SYNC_CHANGES;
	node->cb = response_parse_generic;

	if (cnc->priv->active_jobs < EWS_CONNECTIONS_NUMBER) {
		g_print ("\tRequest sent to the server. Response would be handled in a callback...\n");
		cnc->priv->active_jobs++;
		e_ews_connection_queue_message (cnc, msg, response_parse_generic, cnc);
	} else {
		g_print ("\tQueueing this FindItem request since we have %d active parallel requests...\n", EWS_CONNECTIONS_NUMBER);
		cnc->priv->jobs = g_slist_insert_sorted (cnc->priv->jobs, (gpointer *) node, (GCompareFunc) comp_func);
	}
	QUEUE_UNLOCK (cnc);
}

void
e_ews_connection_create_folder (EEwsConnection *cnc)
{
	ESoapMessage *msg;
	EWSNode *node;
	const gchar *url = g_getenv ("EWS_TEST_URI");

	msg = e_ews_message_new_with_header (url, "CreateFolder");

	e_soap_message_start_element (msg, "ParentFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", "msgfolderroot");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Folders", NULL, NULL);
	e_soap_message_start_element (msg, "Folder", "types", NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", "types", "TestBharath");
	e_soap_message_end_element (msg);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	QUEUE_LOCK (cnc);

	node = ews_node_new ();
	node->msg = msg;
	node->pri = EWS_PRIORITY_CREATE_FOLDER;
	node->cb = response_parse_generic;

	if (cnc->priv->active_jobs < EWS_CONNECTIONS_NUMBER) {
		g_print ("\tRequest sent to the server. Response would be handled in a callback...\n");
		cnc->priv->active_jobs++;
		e_ews_connection_queue_message (cnc, msg, response_parse_generic, cnc);
	} else {
		g_print ("\tQueueing this CreateFolder request since we have %d active parallel requests...\n", EWS_CONNECTIONS_NUMBER);
		cnc->priv->jobs = g_slist_insert_sorted (cnc->priv->jobs, (gpointer *) node, (GCompareFunc) comp_func);
	}

	QUEUE_UNLOCK (cnc);
}

void
e_ews_connection_sync_folder_hierarchy (EEwsConnection *cnc, const gchar *sync_state, GList **folder_list)
{
	ESoapMessage *msg;
	const gchar *url = g_getenv ("EWS_TEST_URI");
	EWSNode *node;

	msg = e_ews_message_new_with_header (url, "SyncFolderHierarchy");
	e_soap_message_start_element (msg, "FolderShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", "AllProperties");
	e_soap_message_end_element (msg);

	if (sync_state)
		e_ews_message_write_string_parameter (msg, "SyncState", NULL, sync_state);
	/* Complete the footer and print the request */
	e_ews_message_write_footer (msg);

	QUEUE_LOCK (cnc);

	node = ews_node_new ();
	node->msg = msg;
	node->pri = EWS_PRIORITY_SYNC_CHANGES;
	node->cb = sync_hierarchy_response_cb;

	if (cnc->priv->active_jobs < EWS_CONNECTIONS_NUMBER) {
		g_print ("\tRequest sent to the server. Response would be handled in a callback...\n");
		cnc->priv->active_jobs++;
		e_ews_connection_queue_message (cnc, msg, sync_hierarchy_response_cb, cnc);
	} else {
		g_print ("\tQueueing this SyncFolderHierarchy request since we have %d active parallel requests...\n", EWS_CONNECTIONS_NUMBER);
		cnc->priv->jobs = g_slist_insert_sorted (cnc->priv->jobs, (gpointer *) node, (GCompareFunc) comp_func);
	}

	QUEUE_UNLOCK (cnc);
}


