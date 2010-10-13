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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <ctype.h>
#include <glib/gi18n-lib.h>
#include <libedataserver/e-proxy.h>
#include <libsoup/soup.h>
#include "soup-soap-message.h"
#include "e-ews-connection.h"
#include "e-ews-message.h"
/* #include "e-ews-filter.h" */
#include "build-timestamp.h"

/* For soup sync session timeout */
#define EWS_SOUP_SESSION_TIMEOUT 30

/* Soup session proxy-uri property */
#define SOUP_SESSION_PROXY_URI "proxy-uri"

G_DEFINE_TYPE (EEwsConnection, e_ews_connection, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;
static GHashTable *loaded_connections_permissions = NULL;

struct _EEwsConnectionPrivate {
	SoupSession *soup_session;

	gchar *uri;
	gchar *username;
	gchar *password;
	gchar *session_id;
	gchar *user_name;
	gchar *user_email;
	gchar *user_uuid;
	gchar *version;
	gchar *server_time;
	GHashTable *categories_by_name;
	GHashTable *categories_by_id;
	GList *book_list;
	GMutex *reauth_mutex;
	GMutex *msg_lock;
	EProxy *proxy;
};

static void
update_soup_session_proxy_settings (EProxy *proxy, SoupSession* session,
				    const gchar * uri)
{
	SoupURI *proxy_uri = NULL;

	if (!session || !uri || !proxy)
		return;

	if (e_proxy_require_proxy_for_uri (proxy, uri))
		proxy_uri = e_proxy_peek_uri_for (proxy, uri);

	g_object_set (session, SOUP_SESSION_PROXY_URI,
		      proxy_uri, NULL);
}

static void
proxy_settings_changed (EProxy *proxy, gpointer user_data)
{
	EEwsConnection* conn = (EEwsConnection *)user_data;
	if (!conn || !conn->priv || !conn->priv->soup_session)
		return;

	update_soup_session_proxy_settings (proxy,
					    conn->priv->soup_session,
					    conn->priv->uri);
}

#if 0
static EEwsConnectionStatus
reauthenticate (EEwsConnection *cnc)
{
	EEwsConnectionPrivate  *priv;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EEwsConnectionStatus status = -1;
	gchar *session = NULL;

	priv = cnc->priv;
	if (!priv)
		return E_EWS_CONNECTION_STATUS_INVALID_CONNECTION;

	g_mutex_lock (priv->reauth_mutex);
	msg = e_ews_message_new_with_header (cnc->priv->uri, "getCategoryListRequest");
	e_ews_message_write_footer (msg);

        /* just to make sure we still have invlaid session
	   when multiple e_ews_connection apis see inavlid connection error
	   at the sma time this prevents this function sending login requests multiple times */
	response = e_ews_connection_send_message (cnc, msg);
        if (!response) {
                g_object_unref (msg);
		g_mutex_unlock (priv->reauth_mutex);
                return E_EWS_CONNECTION_STATUS_NO_RESPONSE;
        }
        status = e_ews_connection_parse_response_status (response);
	g_object_unref (response);

	if (status == E_EWS_CONNECTION_STATUS_OK) {
		g_mutex_unlock (priv->reauth_mutex);
		return status;
	}
	/* build the SOAP message */
	msg = e_ews_message_new_with_header (priv->uri, "loginRequest");
	soup_soap_message_start_element (msg, "auth", "types", NULL);
	soup_soap_message_add_attribute (msg, "type", "types:PlainText", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	e_ews_message_write_string_parameter (msg, "username", "types", priv->username);
	e_ews_message_write_string_parameter (msg, "password", "types", priv->password);
	soup_soap_message_end_element (msg);
	e_ews_message_write_footer (msg);

	/* send message to server */
	response = e_ews_connection_send_message (cnc, msg);
	if (response)
		status = e_ews_connection_parse_response_status (response);

	if (status == E_EWS_CONNECTION_STATUS_OK) {
		param = soup_soap_response_get_first_parameter_by_name (response, "session");
		if (param)
			session = soup_soap_parameter_get_string_value (param);

	}

	if (session) {
		g_free (priv->session_id);
		priv->session_id = session;
	}
	g_object_unref (msg);
	if (response)
		g_object_unref (response);
	g_mutex_unlock (priv->reauth_mutex);
	return status;

}

static gboolean
e_ews_connection_response_parse_status_and_description (SoupSoapResponse *response, gint *status, gchar **description)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "status");
	if (!param)
		return FALSE;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "code");
	if (!subparam)
		return FALSE;

	*status = soup_soap_parameter_get_int_value (subparam);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "description");
	if (!subparam)
		return FALSE;

	*description =  soup_soap_parameter_get_string_value (subparam);

	return TRUE;
}
#endif

EEwsConnectionStatus
e_ews_connection_parse_response_status (SoupSoapResponse *response)
{
	SoupSoapParameter *param, *subparam;

	param = soup_soap_response_get_first_parameter_by_name (response, "status");
	if (!param)
		return E_EWS_CONNECTION_STATUS_UNKNOWN;

	subparam = soup_soap_parameter_get_first_child_by_name (param, "code");
	if (!subparam)
		return E_EWS_CONNECTION_STATUS_UNKNOWN;

	switch (soup_soap_parameter_get_int_value (subparam)) {
	case 0 : return E_EWS_CONNECTION_STATUS_OK;
	case 59905 : return E_EWS_CONNECTION_STATUS_BAD_PARAMETER;
	case 53505 : return E_EWS_CONNECTION_STATUS_UNKNOWN_USER;
	case 59914: return E_EWS_CONNECTION_STATUS_ITEM_ALREADY_ACCEPTED;
	case 59910: return E_EWS_CONNECTION_STATUS_INVALID_CONNECTION;
	case 59923: return E_EWS_CONNECTION_STATUS_REDIRECT;
	case 53530: return E_EWS_CONNECTION_STATUS_OTHER;
	/* FIXME: 58652 should be changed with an enum.*/
	case 58652: return E_EWS_CONNECTION_STATUS_OVER_QUOTA;
	case 59922: return 59922; /*Very big attachment, get in chunks*/
		/* FIXME: map all error codes */
	}

	return E_EWS_CONNECTION_STATUS_INVALID_RESPONSE;
}

const gchar *
e_ews_connection_get_error_message (EEwsConnectionStatus status)
{
	switch (status) {
	case E_EWS_CONNECTION_STATUS_OK :
		break;
	case E_EWS_CONNECTION_STATUS_INVALID_CONNECTION :
		return _("Invalid connection");
	case E_EWS_CONNECTION_STATUS_INVALID_OBJECT :
		return _("Invalid object");
	case E_EWS_CONNECTION_STATUS_INVALID_RESPONSE :
		return _("Invalid response from server");
	case E_EWS_CONNECTION_STATUS_NO_RESPONSE:
		return _("No response from the server");
	case E_EWS_CONNECTION_STATUS_OBJECT_NOT_FOUND :
		return _("Object not found");
	case E_EWS_CONNECTION_STATUS_UNKNOWN_USER :
		return _("Unknown User");
	case E_EWS_CONNECTION_STATUS_BAD_PARAMETER :
		return _("Bad parameter");
	case E_EWS_CONNECTION_STATUS_OTHER :
	case E_EWS_CONNECTION_STATUS_UNKNOWN :
	default :
		return _("Unknown error");
	}

	return NULL;
}

static EEwsConnectionStatus
logout (EEwsConnection *cnc)
{
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	EEwsConnectionStatus status;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), E_EWS_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_ews_message_new_with_header (cnc->priv->uri, "logoutRequest");
	e_ews_message_write_string_parameter (msg, "session", "types", cnc->priv->session_id);
	e_ews_message_write_footer (msg);

	/* send message to server */
	response = e_ews_connection_send_message (cnc, msg);
	if (!response) {
		g_object_unref (msg);
		return E_EWS_CONNECTION_STATUS_NO_RESPONSE;
	}

	status = e_ews_connection_parse_response_status (response);

	/* free memory */
	g_object_unref (response);
	g_object_unref (msg);

	return status;
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
		if (priv->session_id) {
			logout (cnc);
			priv->session_id = NULL;
		}

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

		if (priv->user_name) {
			g_free (priv->user_name);
			priv->user_name = NULL;
		}

		if (priv->user_email) {
			g_free (priv->user_email);
			priv->user_email = NULL;
		}

		if (priv->user_uuid) {
			g_free (priv->user_uuid);
			priv->user_uuid = NULL;
		}

		if (priv->reauth_mutex) {
			g_mutex_free (priv->reauth_mutex);
			priv->reauth_mutex = NULL;
		}

		if (priv->msg_lock) {
			g_mutex_free (priv->msg_lock);
			priv->msg_lock = NULL;
		}

		if (priv->categories_by_id) {
			g_hash_table_destroy (priv->categories_by_id);
			priv->categories_by_id = NULL;
		}

		if (priv->categories_by_name) {
			g_hash_table_destroy (priv->categories_by_name);
			priv->categories_by_name = NULL;
		}

		if (priv->book_list) {
			g_list_foreach (priv->book_list, (GFunc) g_object_unref, NULL);
			g_list_free (priv->book_list);
			priv->book_list = NULL;
		}

		if (priv->version) {
			g_free (priv->version);
		}

		if (priv->server_time) {
			g_free (priv->server_time);
			priv->server_time = NULL;
		}

		if (priv->proxy) {
			g_object_unref (priv->proxy);
			priv->proxy = NULL;
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
}

static void
e_ews_connection_init (EEwsConnection *cnc)
{
	EEwsConnectionPrivate *priv;
	guint timeout = EWS_SOUP_SESSION_TIMEOUT;

	/* allocate internal structure */
	priv = g_new0 (EEwsConnectionPrivate, 1);
	cnc->priv = priv;

	/* Set a default timeout value of 30 seconds.
	   FIXME: Make timeout configurable
	*/
	if (g_getenv ("SOUP_SESSION_TIMEOUT"))
		timeout = atoi (g_getenv ("SOUP_SESSION_TIMEOUT"));

        /* Initialize proxy settings */
        priv->proxy = e_proxy_new ();
        e_proxy_setup_proxy (priv->proxy);
        g_signal_connect (priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), cnc);

	/* create the SoupSession for this connection */
	priv->soup_session = soup_session_sync_new_with_options (SOUP_SESSION_USE_NTLM, TRUE, NULL);
	priv->reauth_mutex = g_mutex_new ();
	priv->msg_lock = g_mutex_new ();
	priv->categories_by_id = NULL;
	priv->categories_by_name = NULL;
	priv->book_list = NULL;

	/* README: We do not use libsoup logger and use our own as we need formatted output etc. */
	/*
	   if (g_getenv ("EWS_DEBUG")) {
		if (atoi (g_getenv ("EWS_DEBUG")) == 1) {
			SoupLogger *logger;

			logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
			soup_logger_attach (logger, priv->soup_session);
			g_object_unref (logger);
		}
	}
	*/
}

static void ews_connection_authenticate(SoupSession *sess, SoupMessage *msg,
					SoupAuth *auth, gboolean retrying, 
					gpointer data)
{
	EEwsConnection *cnc = *(EEwsConnection **)data;
	g_print("%s %s : \n\t username : %s\n\t pass : %s\n", G_STRLOC, G_STRFUNC,
		cnc->priv->username, cnc->priv->password);
	if (retrying) {
		g_print ("Authentication failed.");
		return;
	}
	soup_auth_authenticate (auth, cnc->priv->username, cnc->priv->password);
}

EEwsConnection *
e_ews_connection_new_with_error_handler (const gchar *uri, const gchar *username, const gchar *password, EEwsConnectionErrors *errors)
{
	EEwsConnection *cnc;
	gchar *hash_key;
	gchar *redirected_uri = NULL;

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
	
	g_free (cnc->priv->username);
	g_free (cnc->priv->password);
	cnc->priv->username = g_strdup (username);
	cnc->priv->password = g_strdup (password);

	g_signal_connect (cnc->priv->soup_session, "authenticate",
			  G_CALLBACK(ews_connection_authenticate), &cnc);

	/*TODO :Issue a lightweight dummy call to check authentication - optional*/

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
	g_free (redirected_uri);
	return cnc;

}

EEwsConnection *
e_ews_connection_new (const gchar *uri, const gchar *username, const gchar *password)
{
	/* This is where I miss function-overloading and default-parameters */

	return e_ews_connection_new_with_error_handler (uri, username, password, NULL);
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

gchar*
e_ews_autodiscover_ws_url (const gchar *username, const gchar *password, const gchar *email)
{
	/*TODO : Add a GError*/
	gchar *url;
	gchar *domain;
	gchar *asurl = NULL;
	SoupSession *sess;
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

	url = g_strdup_printf("https://autodiscover.%s/autodiscover/autodiscover.xml", domain);

	/*Fixme : We should be using CNC - Sleepy to fix this crash for now*/
	cnc = e_ews_connection_new (url, username, password);

	sess = soup_session_sync_new_with_options(SOUP_SESSION_USE_NTLM, TRUE, NULL);
	g_signal_connect (sess, "authenticate",
			  G_CALLBACK(ews_connection_authenticate), &cnc);

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
				 
	status = soup_session_send_message(sess, msg);

	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	if (status != 200) {
		fprintf(stderr, "Unexpected response from server: %d\n", status);
		goto failed;
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

SoupSoapResponse *
e_ews_connection_send_message (EEwsConnection *cnc, SoupSoapMessage *msg)
{
	SoupSoapResponse *response;
	guint status;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), NULL);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), NULL);

	g_mutex_lock (cnc->priv->msg_lock);
	status = soup_session_send_message (cnc->priv->soup_session, SOUP_MESSAGE (msg));
	g_mutex_unlock (cnc->priv->msg_lock);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		if (g_getenv ("EWS_DEBUG")) {
			const gchar *error = soup_status_get_phrase (status);

			if (!error)
				error = "Unknown error";

			g_debug ("%s: Failed to send message with error %d (%s)", G_STRFUNC, status, error);
		}
		return NULL;
	}

	/* process response */
	response = soup_soap_message_parse_response (msg);

	if (response && g_getenv ("EWS_DEBUG")) {

		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		soup_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

	return response;
}

gchar *
e_ews_connection_format_date_string (const gchar *dtstring)
{
        gchar *str2;
        gint i, j, len = strlen (dtstring);

        str2 = g_malloc0 (len);
	if (len <= 0)
		return str2;

        for (i = 0,j = 0; i < len; i++) {
                if ((dtstring[i] != '-') && (dtstring[i] != ':')) {
			str2[j] = dtstring[i];
			j++;
                }
        }

	str2[j] = '\0';
        return str2;
}

EEwsConnectionStatus
e_ews_connection_logout (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), E_EWS_CONNECTION_STATUS_INVALID_OBJECT);

	g_object_unref (cnc);

	return E_EWS_CONNECTION_STATUS_OK;
}

