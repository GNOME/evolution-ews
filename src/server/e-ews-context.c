/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <glib.h>

#ifndef G_OS_WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#endif

#include "e-ews-context.h"

#include <libedataserver/e-proxy.h>
#include <libsoup/soup.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#ifdef G_OS_WIN32
/* The strtok() in Microsoft's C library is MT-safe (not stateless,
 * but that is not needed here).
 */
#define strtok_r(s,sep,lasts ) (*(lasts) = strtok((s),(sep)))
#endif

#ifdef G_OS_WIN32
#define CLOSE_SOCKET(socket) closesocket (socket)
#define STATUS_IS_SOCKET_ERROR(status) ((status) == SOCKET_ERROR)
#define SOCKET_IS_INVALID(socket) ((socket) == INVALID_SOCKET)
#define BIND_STATUS_IS_ADDRINUSE() (WSAGetLastError () == WSAEADDRINUSE)
#else
#define CLOSE_SOCKET(socket) close (socket)
#define STATUS_IS_SOCKET_ERROR(status) ((status) == -1)
#define SOCKET_IS_INVALID(socket) ((socket) < 0)
#define BIND_STATUS_IS_ADDRINUSE() (errno == EADDRINUSE)
#endif

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

enum {
	REDIRECT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _EWSContextPrivate {
	SoupSession *session, *async_session;
	gchar *owa_uri, *username, *password;
	time_t last_timestamp;

	/* Notification listener */
	SoupSocket *get_local_address_sock;
	GIOChannel *listener_channel;
	gint listener_watch_id;

	gchar *notification_uri;
	GHashTable *subscriptions_by_id, *subscriptions_by_uri;

	/* Forms-based authentication */
	gchar *cookie;
	gboolean cookie_verified;
	EProxy* proxy;
};

/* For operations with progress */
#define EWS_CONTEXT_MIN_BATCH_SIZE 25
#define EWS_CONTEXT_MAX_BATCH_SIZE 100

/* For soup sync session timeout */
#define EWS_SOUP_SESSION_TIMEOUT 30

/* Soup session proxy-uri property */
#define SOUP_SESSION_PROXY_URI "proxy-uri"

#ifdef EWS_DEBUG
gchar *ews_debug;
gint ews_debug_level;

static SoupLoggerLogLevel ews_debug_request_filter  (SoupLogger  *logger,
						     SoupMessage *msg,
						     gpointer     user_data);
static SoupLoggerLogLevel ews_debug_response_filter (SoupLogger  *logger,
						     SoupMessage *msg,
						     gpointer     user_data);
#endif

static void setup_message (SoupSession *session, SoupMessage *msg, SoupSocket *socket, gpointer user_data);
static void proxy_settings_changed (EProxy *proxy, gpointer user_data);

static void
proxy_settings_changed (EProxy *proxy, gpointer user_data)
{
	SoupURI *proxy_uri = NULL;
	EWSContext* ctx = (EWSContext *)user_data;
	if (!ctx || !ctx->priv ||
	    (!ctx->priv->session && !ctx->priv->async_session) ||
	    (!ctx->priv->owa_uri))
		return;

	if (!e_proxy_require_proxy_for_uri (proxy, ctx->priv->owa_uri))
		proxy_uri = NULL;
	else
		proxy_uri = e_proxy_peek_uri_for (proxy, ctx->priv->owa_uri);

	if (ctx->priv->session)
		g_object_set (ctx->priv->session, SOUP_SESSION_PROXY_URI,
			      proxy_uri, NULL);
	if (ctx->priv->async_session)
		g_object_set (ctx->priv->async_session, SOUP_SESSION_PROXY_URI,
			      proxy_uri, NULL);
}

static void
init (GObject *object)
{
	EWSContext *ctx = EWS_CONTEXT (object);

	ctx->priv = g_new0 (EWSContextPrivate, 1);
	ctx->priv->subscriptions_by_id =
		g_hash_table_new (g_str_hash, g_str_equal);
	ctx->priv->subscriptions_by_uri =
		g_hash_table_new (g_str_hash, g_str_equal);
	ctx->priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (ctx->priv->proxy);
	g_signal_connect (ctx->priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), ctx);
}

static void
dispose (GObject *object)
{
	EWSContext *ctx = EWS_CONTEXT (object);

	if (ctx->priv) {
		if (ctx->priv->owa_uri)
			g_free (ctx->priv->owa_uri);
		if (ctx->priv->username)
			g_free (ctx->priv->username);
		if (ctx->priv->password)
			g_free (ctx->priv->password);

		if (ctx->priv->get_local_address_sock)
			g_object_unref (ctx->priv->get_local_address_sock);

		if (ctx->priv->listener_watch_id)
			g_source_remove (ctx->priv->listener_watch_id);
		if (ctx->priv->listener_channel) {
			g_io_channel_shutdown (ctx->priv->listener_channel,
					       FALSE, NULL);
			g_io_channel_unref (ctx->priv->listener_channel);
		}

		if (ctx->priv->session)
			g_object_unref (ctx->priv->session);
		if (ctx->priv->async_session)
			g_object_unref (ctx->priv->async_session);

		g_free (ctx->priv->cookie);
		g_free (ctx->priv->notification_uri);

		if (ctx->priv->proxy) {
			g_object_unref (ctx->priv->proxy);
			ctx->priv->proxy = NULL;
		}
		g_free (ctx->priv);
		ctx->priv = NULL;

	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;

	signals[REDIRECT] =
		g_signal_new ("redirect",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EWSContextClass, redirect),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE, 3,
			      G_TYPE_INT,
			      G_TYPE_STRING,
			      G_TYPE_STRING);

#ifdef EWS_DEBUG
	ews_debug = getenv ("EWS_DEBUG");
	if (ews_debug)
		ews_debug_level = atoi (ews_debug);
#endif
}

EWS_MAKE_TYPE (ews_context, EWSContext, class_init, init, PARENT_TYPE)

static void
got_connection (SoupSocket *sock, guint status, gpointer user_data)
{
	EWSContext *ctx = user_data;
	SoupAddress *addr;
	struct sockaddr_in sin;
	const gchar *local_ipaddr;
	unsigned short port;
	gint s, ret;

	ctx->priv->get_local_address_sock = NULL;

	if (status != SOUP_STATUS_OK)
		goto done;

	addr = soup_socket_get_local_address (sock);
	local_ipaddr = soup_address_get_physical (addr);

	s = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (SOCKET_IS_INVALID (s))
		goto done;

	memset (&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;

	port = (short)getpid ();
	do {
		port++;
		if (port < 1024)
			port += 1024;
		sin.sin_port = htons (port);
		ret = bind (s, (struct sockaddr *)&sin, sizeof (sin));
	} while (STATUS_IS_SOCKET_ERROR (ret) && BIND_STATUS_IS_ADDRINUSE ());

	if (ret == -1) {
		CLOSE_SOCKET (s);
		goto done;
	}

#ifndef G_OS_WIN32
	ctx->priv->listener_channel = g_io_channel_unix_new (s);
#else
	ctx->priv->listener_channel = g_io_channel_win32_new_socket (s);
#endif
	g_io_channel_set_encoding (ctx->priv->listener_channel, NULL, NULL);
	g_io_channel_set_buffered (ctx->priv->listener_channel, FALSE);

	ctx->priv->notification_uri = g_strdup_printf ("httpu://%s:%u/",
							local_ipaddr,
							port);

done:
	if (sock)
		g_object_unref (sock);
	g_object_unref (ctx);
}

	
/**
 * ews_context_new:
 * @uri: OWA uri to connect to
 *
 * Creates a new #EWSContext based at @uri
 *
 * Return value: the new context
 **/
EWSContext *
ews_context_new (const gchar *uri)
{
	EWSContext *ctx;
	SoupURI *suri;
	SoupAddress *addr;

	suri = soup_uri_new (uri);
	if (!suri)
		return NULL;

	if (!suri->host) {
		soup_uri_free (suri);
		return NULL;
	}

	addr = soup_address_new (suri->host, suri->port);
	soup_uri_free (suri);

	ctx = g_object_new (EWS_TYPE_CONTEXT, NULL);
	ctx->priv->owa_uri = g_strdup (uri);

	ctx->priv->get_local_address_sock =
		soup_socket_new (SOUP_SOCKET_REMOTE_ADDRESS, addr,
				 NULL);
	soup_socket_connect_async (ctx->priv->get_local_address_sock, NULL,
				   got_connection, g_object_ref (ctx));
	g_object_unref (addr);

	return ctx;
}

static void
session_authenticate (SoupSession *session, SoupMessage *msg,
		      SoupAuth *auth, gboolean retrying, gpointer user_data)
{
	EWSContext *ctx = user_data;

	if (!retrying) {
		soup_auth_authenticate (auth, ctx->priv->username,
					ctx->priv->password);
	}
}

/**
 * ews_context_set_auth:
 * @ctx: the context
 * @username: the Windows username (not including domain) of the user
 * @domain: the NT domain, or %NULL to use the default (if using NTLM)
 * @authmech: the HTTP Authorization type to use; either "Basic" or "NTLM"
 * @password: the user's password
 *
 * Sets the authentication information on @ctx. This will have the
 * side effect of cancelling any pending requests on @ctx.
 **/
void
ews_context_set_auth (EWSContext *ctx, const gchar *username,
		      const gchar *domain, const gchar *authmech,
		      const gchar *password)
{
	guint timeout = EWS_SOUP_SESSION_TIMEOUT;
	SoupURI* uri = NULL;
#ifdef EWS_DEBUG
	SoupLogger *logger;
	SoupLoggerLogLevel level;
#endif

	g_return_if_fail (EWS_IS_CONTEXT (ctx));

	if (username) {
		g_free (ctx->priv->username);
		if (domain) {
			ctx->priv->username =
				g_strdup_printf ("%s\\%s", domain,
						 username);
		} else
			ctx->priv->username = g_strdup (username);
	}

	if (password) {
		g_free (ctx->priv->password);
		ctx->priv->password = g_strdup (password);
	}

	/* Destroy the old sessions so we don't reuse old auths */
	if (ctx->priv->session)
		g_object_unref (ctx->priv->session);
	if (ctx->priv->async_session)
		g_object_unref (ctx->priv->async_session);

	/* Set a default timeout value of 30 seconds.
	   FIXME: Make timeout configurable
	*/
	if (g_getenv ("SOUP_SESSION_TIMEOUT"))
		timeout = atoi (g_getenv ("SOUP_SESSION_TIMEOUT"));

	/* Check do we need a proxy to contact the server? */
        if (e_proxy_require_proxy_for_uri (ctx->priv->proxy, ctx->priv->owa_uri))
                uri = e_proxy_peek_uri_for (ctx->priv->proxy, ctx->priv->owa_uri);

	ctx->priv->session = soup_session_sync_new_with_options (
		SOUP_SESSION_USE_NTLM, !authmech || !strcmp (authmech, "NTLM"),
		SOUP_SESSION_TIMEOUT, timeout,
		SOUP_SESSION_PROXY_URI, uri,
		NULL);
	g_signal_connect (ctx->priv->session, "authenticate",
			  G_CALLBACK (session_authenticate), ctx);
	g_signal_connect (ctx->priv->session, "request_started",
			  G_CALLBACK (setup_message), ctx);

	ctx->priv->async_session = soup_session_async_new_with_options (
		SOUP_SESSION_USE_NTLM, !authmech || !strcmp (authmech, "NTLM"),
		SOUP_SESSION_PROXY_URI, uri, NULL);
	g_signal_connect (ctx->priv->async_session, "authenticate",
			  G_CALLBACK (session_authenticate), ctx);
	g_signal_connect (ctx->priv->async_session, "request_started",
			  G_CALLBACK (setup_message), ctx);

#ifdef EWS_DEBUG
	if (ews_debug_level <= 0)
		return;

	if (ews_debug_level < 4)
		level = (SoupLoggerLogLevel)ews_debug_level;
	else
		level = SOUP_LOGGER_LOG_BODY;
	logger = soup_logger_new (level, -1);
	if (level == SOUP_LOGGER_LOG_BODY && ews_debug_level < 5) {
		soup_logger_set_request_filter (logger, ews_debug_request_filter, NULL, NULL);
		soup_logger_set_response_filter (logger, ews_debug_response_filter, NULL, NULL);
	}
	soup_session_add_feature (
		ctx->priv->session, SOUP_SESSION_FEATURE (logger));
	soup_session_add_feature (
		ctx->priv->async_session, SOUP_SESSION_FEATURE (logger));
#endif
}

/**
 * ews_context_get_last_timestamp:
 * @ctx: the context
 *
 * Returns a %time_t corresponding to the last "Date" header
 * received from the server.
 *
 * Return value: the timestamp
 **/
time_t
ews_context_get_last_timestamp (EWSContext *ctx)
{
	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), -1);

	return ctx->priv->last_timestamp;
}

#ifdef EWS_DEBUG
/* Debug levels:
 * 0 - None
 * 1 - Basic request and response (SOUP_LOGGER_LOG_MINIMAL)
 * 2 - 1 plus all headers (SOUP_LOGGER_LOG_HEADERS)
 * 3 - 2 plus most bodies (SOUP_LOGGER_LOG_BODY with filters)
 * 4 - 3 plus Global Catalog debug too
 * 5 - 4 plus all bodies (SOUP_LOGGER_LOG_BODY)
 */

/* The filters are only used when ews_debug_level is 3 or 4,
 * meaning we want to show most, but not all, bodies.
 */
static SoupLoggerLogLevel
ews_debug_request_filter (SoupLogger *logger, SoupMessage *msg,
			  gpointer user_data)
{
	if (msg->method == SOUP_METHOD_POST)
		return SOUP_LOGGER_LOG_HEADERS;
	else
		return SOUP_LOGGER_LOG_BODY;
}

static SoupLoggerLogLevel
ews_debug_response_filter (SoupLogger *logger, SoupMessage *msg,
			   gpointer user_data)
{
	const gchar *content_type;

	if (!EWS_HTTP_STATUS_IS_SUCCESSFUL (msg->status_code))
		return SOUP_LOGGER_LOG_HEADERS;

	content_type = soup_message_headers_get (msg->response_headers,
						 "Content-Type");
	if (!content_type || g_ascii_strncasecmp (content_type, "text/html", 9))
		return SOUP_LOGGER_LOG_BODY;
	else
		return SOUP_LOGGER_LOG_HEADERS;
}
#endif

static void
timestamp_handler (SoupMessage *msg, gpointer user_data)
{
	EWSContext *ctx = user_data;
	const gchar *date;

	date = soup_message_headers_get (msg->response_headers, "Date");
//	if (date)
//		ctx->priv->last_timestamp = ews_http_parse_date (date);
}

static void
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	EWSContext *ctx = user_data;
	const gchar *new_uri;
	SoupURI *soup_uri;
	gchar *old_uri;

	if (!SOUP_STATUS_IS_REDIRECTION (msg->status_code) ||
	    (soup_message_get_flags (msg) & SOUP_MESSAGE_NO_REDIRECT))
		return;

	new_uri = soup_message_headers_get (msg->response_headers, "Location");
	soup_uri = soup_uri_copy (soup_message_get_uri (msg));
	old_uri = soup_uri_to_string (soup_uri, FALSE);

	g_signal_emit (ctx, signals[REDIRECT], 0,
		       msg->status_code, old_uri, new_uri);
	soup_uri_free (soup_uri);
	g_free (old_uri);
}

static void
setup_message (SoupSession *session, SoupMessage *msg,
	       SoupSocket *socket, gpointer user_data)
{
	EWSContext *ctx = user_data;

	if (ctx->priv->cookie) {
		soup_message_headers_replace (msg->request_headers,
					      "Cookie", ctx->priv->cookie);
	}

	/* Only do this the first time through */
	if (!soup_message_headers_get (msg->request_headers, "User-Agent")) {
		g_signal_connect (msg, "got-headers",
				  G_CALLBACK (timestamp_handler), ctx);
		soup_message_add_header_handler (msg, "got-headers",
						 "Location",
						 G_CALLBACK (redirect_handler),
						 ctx);
/*		soup_message_add_status_code_handler (msg, "got-headers",
						      EWS_HTTP_TIMEOUT,
						      G_CALLBACK (fba_timeout_handler),
						      ctx);
*/		soup_message_headers_append (msg->request_headers, "User-Agent",
					     "Evolution/" VERSION);

	}
}

/**
 * ews_soup_message_new:
 * @ctx: the context
 * @uri: the URI
 * @method: the HTTP method
 *
 * Creates a new %SoupMessage for @ctx.
 *
 * Return value: a new %SoupMessage, set up for connector use
 **/
SoupMessage *
ews_soup_message_new (EWSContext *ctx, const gchar *uri, const gchar *method)
{
	SoupMessage *msg;

/*	if (method[0] == 'B') {
		gchar *slash_uri = ews_strdup_with_trailing_slash (uri);
		msg = soup_message_new (method, slash_uri);
		if (!msg)
			g_warning ("Invalid uri '%s'", slash_uri ? slash_uri : "[null]");
		g_free (slash_uri);
	} else {
*/		msg = soup_message_new (method, uri);
		if (!msg)
			g_warning ("Invalid uri '%s'", uri ? uri : "[null]");
//	}

//	return msg;
}

/**
 * ews_soup_message_new_full:
 * @ctx: the context
 * @uri: the URI
 * @method: the HTTP method
 * @content_type: MIME Content-Type of @body
 * @use: use policy of @body
 * @body: request body
 * @length: length of @body
 *
 * Creates a new %SoupMessage with the given body.
 *
 * Return value: a new %SoupMessage with a request body, set up for
 * connector use
 **/
SoupMessage *
ews_soup_message_new_full (EWSContext *ctx, const gchar *uri,
			   const gchar *method, const gchar *content_type,
			   SoupMemoryUse use, const gchar *body,
			   gsize length)
{
	SoupMessage *msg;

	msg = ews_soup_message_new (ctx, uri, method);
	g_return_val_if_fail (msg != NULL, NULL);
	soup_message_set_request (msg, content_type, use, body, length);

	return msg;
}

/**
 * ews_context_queue_message:
 * @ctx: the context
 * @msg: the message to queue
 * @callback: callback to invoke when @msg is done
 * @user_data: data for @callback
 *
 * Asynchronously queues @msg in @ctx's session.
 **/
void
ews_context_queue_message (EWSContext *ctx, SoupMessage *msg,
			   SoupSessionCallback callback,
			   gpointer user_data)
{
	g_return_if_fail (EWS_IS_CONTEXT (ctx));

	soup_session_queue_message (ctx->priv->async_session, msg,
				    callback, user_data);
}

static void
context_canceller (EWSOperation *op, gpointer owner, gpointer data)
{
	EWSContext *ctx = owner;
	SoupMessage *msg = data;

	soup_session_cancel_message (ctx->priv->session, msg,
				     SOUP_STATUS_CANCELLED);
}

/**
 * ews_context_send_message:
 * @ctx: the context
 * @op: an #EWSOperation to use for cancellation
 * @msg: the message to send
 *
 * Synchronously sends @msg in @ctx's session.
 *
 * Return value: the HTTP status of the message
 **/
EWSHTTPStatus
ews_context_send_message (EWSContext *ctx, EWSOperation *op, SoupMessage *msg)
{
	EWSHTTPStatus status;

	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), EWS_HTTP_MALFORMED);

	if (ews_operation_is_cancelled (op)) {
		soup_message_set_status (msg, EWS_HTTP_CANCELLED);
		return EWS_HTTP_CANCELLED;
	}

	ews_operation_start (op, context_canceller, ctx, msg);
	status = soup_session_send_message (ctx->priv->session, msg);
	ews_operation_finish (op);

	return status;
}

static void
update_unique_uri (EWSContext *ctx, SoupMessage *msg,
		   const gchar *folder_uri, const gchar *encoded_name, gint *count,
		   EWSContextTestCallback test_callback, gpointer user_data)
{
	SoupURI *suri;
	gchar *uri = NULL;

	do {
		g_free (uri);
		if (*count == 1) {
			uri = g_strdup_printf ("%s%s.EML", folder_uri,
					       encoded_name);
		} else {
			uri = g_strdup_printf ("%s%s-%d.EML", folder_uri,
					       encoded_name, *count);
		}
		(*count)++;
	} while (test_callback && !test_callback (ctx, uri, user_data));

	suri = soup_uri_new (uri);
	soup_message_set_uri (msg, suri);
	soup_uri_free (suri);
	g_free (uri);
}

/* GET */

static SoupMessage *
get_msg (EWSContext *ctx, const gchar *uri, gboolean owa, gboolean claim_ie)
{
	SoupMessage *msg;

	msg = ews_soup_message_new (ctx, uri, "GET");
	if (!owa)
		soup_message_headers_append (msg->request_headers, "Translate", "F");
	if (claim_ie) {
		soup_message_headers_replace (msg->request_headers, "User-Agent",
					      "MSIE 6.0b (Windows NT 5.0; compatible; "
					      "Evolution/" VERSION ")");
	}

	return msg;
}

/**
 * ews_context_get:
 * @ctx: the context
 * @op: pointer to an #EWSOperation to use for cancellation
 * @uri: URI of the object to GET
 * @content_type: if not %NULL, will contain the Content-Type of the
 * response on return.
 * @response: if not %NULL, will contain the response on return
 *
 * Performs a GET on @ctx for @uri. If successful (2xx status code),
 * the Content-Type, and response body will be returned. The body is not
 * terminated by a '\0'. If the GET is not successful, @content_type and
 * @response will be untouched (even if the error response
 * included a body).
 *
 * Return value: the HTTP status
 **/
EWSHTTPStatus
ews_context_get (EWSContext *ctx, EWSOperation *op, const gchar *uri,
		 gchar **content_type, SoupBuffer **response)
{
	SoupMessage *msg;
	EWSHTTPStatus status;

	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), EWS_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, EWS_HTTP_MALFORMED);

	msg = get_msg (ctx, uri, FALSE, FALSE);
	status = ews_context_send_message (ctx, op, msg);

	if (EWS_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (content_type) {
			const gchar *header;
			header = soup_message_headers_get (msg->response_headers,
							   "Content-Type");
			*content_type = g_strdup (header);
		}
		if (response)
			*response = soup_message_body_flatten (msg->response_body);
	}

	g_object_unref (msg);
	return status;
}

/**
 * ews_context_get_owa:
 * @ctx: the context
 * @op: pointer to an #EWSOperation to use for cancellation
 * @uri: URI of the object to GET
 * @claim_ie: whether or not to claim to be IE
 * @response: if not %NULL, will contain the response on return
 *
 * As with ews_context_get(), but used when you need the HTML or XML
 * data that would be returned to OWA rather than the raw object data.
 *
 * Return value: the HTTP status
 **/
EWSHTTPStatus
ews_context_get_owa (EWSContext *ctx, EWSOperation *op,
		     const gchar *uri, gboolean claim_ie,
		     SoupBuffer **response)
{
	SoupMessage *msg;
	EWSHTTPStatus status;

	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), EWS_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, EWS_HTTP_MALFORMED);

	msg = get_msg (ctx, uri, TRUE, claim_ie);
	status = ews_context_send_message (ctx, op, msg);

	if (EWS_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (response)
			*response = soup_message_body_flatten (msg->response_body);
	}

	g_object_unref (msg);
	return status;
}

/* PUT / POST */

static SoupMessage *
put_msg (EWSContext *ctx, const gchar *uri, const gchar *content_type,
	 SoupMemoryUse buffer_type, const gchar *body, gint length)
{
	SoupMessage *msg;

	msg = ews_soup_message_new_full (ctx, uri, "PUT", content_type,
					 buffer_type, body, length);
	soup_message_headers_append (msg->request_headers, "Translate", "f");

	return msg;
}

static SoupMessage *
post_msg (EWSContext *ctx, const gchar *uri, const gchar *content_type,
	  SoupMemoryUse buffer_type, const gchar *body, gint length)
{
	SoupMessage *msg;

	msg = ews_soup_message_new_full (ctx, uri, "POST", content_type,
					 buffer_type, body, length);
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	return msg;
}

static void
extract_put_results (SoupMessage *msg, gchar **location, gchar **repl_uid)
{
	const gchar *header;

	if (!EWS_HTTP_STATUS_IS_SUCCESSFUL (msg->status_code))
		return;

	if (repl_uid) {
		header = soup_message_headers_get (msg->response_headers,
						   "Repl-UID");
		*repl_uid = g_strdup (header);
	}
	if (location) {
		header = soup_message_headers_get (msg->response_headers,
						   "Location");
		*location = g_strdup (header);
	}
}

/**
 * ews_context_put:
 * @ctx: the context
 * @op: pointer to an #EWSOperation to use for cancellation
 * @uri: the URI to PUT to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @repl_uid: if not %NULL, will contain the Repl-UID of the PUT
 * object on return
 *
 * Performs a PUT operation on @ctx for @uri.
 *
 * Return value: the HTTP status
 **/
EWSHTTPStatus
ews_context_put (EWSContext *ctx, EWSOperation *op, const gchar *uri,
		 const gchar *content_type, const gchar *body, gint length,
		 gchar **repl_uid)
{
	SoupMessage *msg;
	EWSHTTPStatus status;

	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), EWS_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, EWS_HTTP_MALFORMED);
	g_return_val_if_fail (content_type != NULL, EWS_HTTP_MALFORMED);
	g_return_val_if_fail (body != NULL, EWS_HTTP_MALFORMED);

	msg = put_msg (ctx, uri, content_type,
		       SOUP_MEMORY_COPY, body, length);
	status = ews_context_send_message (ctx, op, msg);
	extract_put_results (msg, NULL, repl_uid);

	g_object_unref (msg);
	return status;
}

/**
 * ews_context_post:
 * @ctx: the context
 * @op: pointer to an #EWSOperation to use for cancellation
 * @uri: the URI to POST to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @location: if not %NULL, will contain the Location of the POSTed
 * object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the POSTed
 * object on return
 *
 * Performs a POST operation on @ctx for @uri.
 *
 * Note that POSTed objects will be irrevocably(?) marked as "unsent",
 * If you open a POSTed message in Outlook, it will open in the
 * composer rather than in the message viewer.
 *
 * Return value: the HTTP status
 **/
EWSHTTPStatus
ews_context_post (EWSContext *ctx, EWSOperation *op, const gchar *uri,
		  const gchar *content_type, const gchar *body, gint length,
		  gchar **location, gchar **repl_uid)
{
	SoupMessage *msg;
	EWSHTTPStatus status;

	g_return_val_if_fail (EWS_IS_CONTEXT (ctx), EWS_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, EWS_HTTP_MALFORMED);
	g_return_val_if_fail (content_type != NULL, EWS_HTTP_MALFORMED);
	g_return_val_if_fail (body != NULL, EWS_HTTP_MALFORMED);

	msg = post_msg (ctx, uri, content_type,
			SOUP_MEMORY_COPY, body, length);

	status = ews_context_send_message (ctx, op, msg);
	extract_put_results (msg, location, repl_uid);

	g_object_unref (msg);
	return status;
}
