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
#include "e-ews-filter.h"
#include "build-timestamp.h"

/* For soup sync session timeout */
#define GW_SOUP_SESSION_TIMEOUT 30

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
	EEwsSendOptions *opts;
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
	msg = e_ews_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "getCategoryListRequest");
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
	msg = e_ews_message_new_with_header (priv->uri, NULL, "loginRequest");
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

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_EWS_CONNECTION_STATUS_INVALID_OBJECT);

	/* build the SOAP message */
	msg = e_ews_message_new_with_header (cnc->priv->uri, cnc->priv->session_id, "logoutRequest");
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

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;
	printf ("gw connection dispose \n");

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

		if (priv->opts) {
			g_object_unref (priv->opts);
			priv->opts = NULL;
		}

		if (priv->version) {
			g_free (priv->version);
			priv->opts = NULL;
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

	g_return_if_fail (E_IS_GW_CONNECTION (cnc));

	priv = cnc->priv;
	printf ("gw connection finalize\n");
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
	guint timeout = GW_SOUP_SESSION_TIMEOUT;

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
	priv->soup_session = soup_session_sync_new_with_options (SOUP_SESSION_TIMEOUT, timeout, NULL);
	priv->reauth_mutex = g_mutex_new ();
	priv->msg_lock = g_mutex_new ();
	priv->categories_by_id = NULL;
	priv->categories_by_name = NULL;
	priv->book_list = NULL;
	priv->opts = NULL;

	/* README: We do not use libsoup logger and use our own as we need formatted output etc. */
	/*
	   if (g_getenv ("GROUPWISE_DEBUG")) {
		if (atoi (g_getenv ("GROUPWISE_DEBUG")) == 1) {
			SoupLogger *logger;

			logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
			soup_logger_attach (logger, priv->soup_session);
			g_object_unref (logger);
		}
	}
	*/
}

static SoupSoapMessage*
form_login_request (const gchar *uri, const gchar * username, const gchar * password)
{
	SoupSoapMessage *msg;
	/* build the SOAP message */
	msg = e_ews_message_new_with_header (uri, NULL, "loginRequest");
	e_ews_message_write_string_parameter (msg, "application", "types", build_timestamp);
	e_ews_message_write_string_parameter (msg, "version", NULL, "1.02");
	soup_soap_message_start_element (msg, "auth", "types", NULL);
	soup_soap_message_add_attribute (msg, "type", "types:PlainText", "xsi",
					 "http://www.w3.org/2001/XMLSchema-instance");
	e_ews_message_write_string_parameter (msg, "username", "types", username);
	if (password && *password)
		e_ews_message_write_string_parameter (msg, "password", "types", password);
	soup_soap_message_end_element (msg);
	e_ews_message_write_footer (msg);
	return msg;
}

EEwsConnection *
e_ews_connection_new_with_error_handler (const gchar *uri, const gchar *username, const gchar *password, EEwsConnectionErrors *errors)
{
	EEwsConnection *cnc;
	SoupSoapMessage *msg;
	SoupSoapResponse *response;
	SoupSoapParameter *param;
	EEwsConnectionStatus status;
	gchar *hash_key;
	gchar *redirected_uri = NULL;
	gint code;
	gchar *description = NULL;

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

		if (E_IS_GW_CONNECTION (cnc)) {
			g_object_ref (cnc);
			g_static_mutex_unlock (&connecting);
			return cnc;
		}
	}

	/* not found, so create a new connection */
	cnc = g_object_new (E_TYPE_EWS_CONNECTION, NULL);

	/* Set proxy details for the Soup session before any
	   communication. */
	update_soup_session_proxy_settings (cnc->priv->proxy,
					    cnc->priv->soup_session,
					    uri);

	msg = form_login_request (uri, username, password);

	/* send message to server */
	response = e_ews_connection_send_message (cnc, msg);

	if (!response) {
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);
		g_object_unref (msg);
		return NULL;
	}

	status = e_ews_connection_parse_response_status (response);
	if (status == E_EWS_CONNECTION_STATUS_REDIRECT) {
		gchar *host, *port;
		gchar **tokens;
		SoupSoapParameter *subparam;

		param = soup_soap_response_get_first_parameter_by_name (response, "redirectToHost");
		subparam = soup_soap_parameter_get_first_child_by_name (param, "ipAddress");
		host = soup_soap_parameter_get_string_value (subparam);
		subparam = soup_soap_parameter_get_first_child_by_name (param, "port");
		port = soup_soap_parameter_get_string_value (subparam);
		if (host && port) {
			tokens = g_strsplit (uri, "://", 2);
			redirected_uri = g_strconcat (tokens[0], "://", host, ":", port, "/soap", NULL);
			g_object_unref (msg);
			g_object_unref (response);
			msg = form_login_request (redirected_uri, username, password);
			uri = redirected_uri;
			response = e_ews_connection_send_message (cnc, msg);
			status = e_ews_connection_parse_response_status (response);
			g_strfreev (tokens);
		}

		g_free (host);
		g_free (port);
	}
	param = soup_soap_response_get_first_parameter_by_name (response, "session");
	if (!param) {
		if (errors && e_ews_connection_response_parse_status_and_description (response, &code, &description) ) {
			errors->status = code;
			errors->description = description;
		}
		g_object_unref (response);
		g_object_unref (msg);
		g_object_unref (cnc);
		g_static_mutex_unlock (&connecting);

		return NULL;
	}

	cnc->priv->uri = g_strdup (uri);
	cnc->priv->username = g_strdup (username);
	cnc->priv->password = g_strdup (password);
	cnc->priv->session_id = soup_soap_parameter_get_string_value (param);

	/* retrieve user information */
	param = soup_soap_response_get_first_parameter_by_name (response, "userinfo");

	if (param) {
		SoupSoapParameter *subparam;
		gchar *param_value;

		subparam = soup_soap_parameter_get_first_child_by_name (param, "email");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_email  = param_value;
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "name");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_name = param_value;
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param, "uuid");
		if (subparam) {
			param_value = soup_soap_parameter_get_string_value (subparam);
			cnc->priv->user_uuid = param_value;
		}
	}

	param = soup_soap_response_get_first_parameter_by_name (response, "gwVersion");
	if (param) {
		gchar *param_value;
		param_value = soup_soap_parameter_get_string_value (param);
		cnc->priv->version = param_value;
	} else
		cnc->priv->version = NULL;

	param = soup_soap_response_get_first_parameter_by_name (response, "serverUTCTime");
	if (param)
		cnc->priv->server_time = soup_soap_parameter_get_string_value (param);

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
	g_object_unref (response);
	g_object_unref (msg);
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

SoupSoapResponse *
e_ews_connection_send_message (EEwsConnection *cnc, SoupSoapMessage *msg)
{
	SoupSoapResponse *response;
	guint status;

	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), NULL);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), NULL);

	g_mutex_lock (cnc->priv->msg_lock);
	status = soup_session_send_message (cnc->priv->soup_session, SOUP_MESSAGE (msg));
	g_mutex_unlock (cnc->priv->msg_lock);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		if (g_getenv ("GROUPWISE_DEBUG")) {
			const gchar *error = soup_status_get_phrase (status);

			if (!error)
				error = "Unknown error";

			g_debug ("%s: Failed to send message with error %d (%s)", G_STRFUNC, status, error);
		}
		return NULL;
	}

	/* process response */
	response = soup_soap_message_parse_response (msg);

	if (response && g_getenv ("GROUPWISE_DEBUG")) {

		/* README: The stdout can be replaced with Evolution's
		Logging framework also */

		soup_soap_response_dump_response (response, stdout);
		g_print ("\n------\n");
	}

	return response;
}

EEwsConnectionStatus
e_ews_connection_logout (EEwsConnection *cnc)
{
	g_return_val_if_fail (E_IS_GW_CONNECTION (cnc), E_EWS_CONNECTION_STATUS_INVALID_OBJECT);

	g_object_unref (cnc);

	return E_EWS_CONNECTION_STATUS_OK;
}

