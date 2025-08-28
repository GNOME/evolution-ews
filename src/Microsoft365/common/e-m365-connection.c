/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

#include "camel-m365-settings.h"
#include "e-ews-common-utils.h"
#include "e-m365-json-utils.h"

#include "e-m365-connection.h"

#define LOCK(x) g_rec_mutex_lock (&(x->priv->property_lock))
#define UNLOCK(x) g_rec_mutex_unlock (&(x->priv->property_lock))

#define M365_HOSTNAME "graph.microsoft.com"
#define M365_RETRY_IO_ERROR_SECONDS 3
#define X_EVO_M365_DATA "X-EVO-M365-DATA"

#define ORG_CONTACTS_PROPS "addresses,companyName,department,displayName,givenName,id,jobTitle,mail,mailNickname,phones,proxyAddresses,surname"
#define USERS_PROPS	"aboutMe,birthday,businessPhones,city,companyName,country,createdDateTime,department,displayName,faxNumber,givenName," \
			"id,imAddresses,jobTitle,mail,mailNickname,mobilePhone,mySite,officeLocation,otherMails,postalCode,proxyAddresses," \
			"state,streetAddress,surname"

G_DEFINE_QUARK (e-m365-error-quark, e_m365_error)

typedef enum _CSMFlags {
	CSM_DEFAULT		= 0,
	CSM_DISABLE_RESPONSE	= 1 << 0
} CSMFlags;

struct _EM365ConnectionPrivate {
	GRecMutex property_lock;

	ESource *source;
	CamelM365Settings *settings;
	SoupSession *soup_session;
	GProxyResolver *proxy_resolver;

	gchar *user; /* The default user for the URL */
	gchar *impersonate_user;

	gchar *hash_key; /* in the opened connections hash */

	/* How many microseconds to wait, until can execute a new request.
	   This is to cover throttling and server unavailable responses.
	   https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
	gint64 backoff_for_usec;

	guint concurrent_connections;
};

enum {
	PROP_0,
	PROP_PROXY_RESOLVER,
	PROP_SETTINGS,
	PROP_SOURCE,
	PROP_CONCURRENT_CONNECTIONS,
	PROP_USER,			/* This one is hidden, write only */
	PROP_USE_IMPERSONATION,		/* This one is hidden, write only */
	PROP_IMPERSONATE_USER		/* This one is hidden, write only */
};

G_DEFINE_TYPE_WITH_PRIVATE (EM365Connection, e_m365_connection, G_TYPE_OBJECT)

static GHashTable *opened_connections = NULL;
G_LOCK_DEFINE_STATIC (opened_connections);

static gboolean
m365_log_enabled (void)
{
	static gint log_enabled = -1;

	if (log_enabled == -1)
		log_enabled = g_strcmp0 (g_getenv ("M365_DEBUG"), "1") == 0 ? 1 : 0;

	return log_enabled == 1;
}

static gchar *
m365_connection_construct_hash_key (CamelM365Settings *settings)
{
	gchar *user;
	gchar *hash_key = NULL;

	user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));

	if (camel_m365_settings_get_use_impersonation (settings)) {
		gchar *impersonate_user;

		impersonate_user = camel_m365_settings_dup_impersonate_user (settings);

		if (impersonate_user && *impersonate_user)
			hash_key = g_strdup_printf ("%s#%s", impersonate_user, user ? user : "no-user");

		g_free (impersonate_user);
	}

	if (!hash_key)
		hash_key = g_steal_pointer (&user);

	if (!hash_key)
		hash_key = g_strdup ("no-user");

	g_free (user);

	return hash_key;
}

static void
m365_connection_set_settings (EM365Connection *cnc,
			      CamelM365Settings *settings)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));
	g_return_if_fail (cnc->priv->settings == NULL);

	cnc->priv->settings = g_object_ref (settings);

	e_binding_bind_property (
		cnc->priv->settings, "user",
		cnc, "user",
		G_BINDING_DEFAULT |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		cnc->priv->settings, "use-impersonation",
		cnc, "use-impersonation",
		G_BINDING_DEFAULT |
		G_BINDING_SYNC_CREATE);

	/* No need to G_BINDING_SYNC_CREATE, because the 'use-impersonation' already updated the value */
	e_binding_bind_property (
		cnc->priv->settings, "impersonate-user",
		cnc, "impersonate-user",
		G_BINDING_DEFAULT);

	e_binding_bind_property (
		cnc->priv->settings, "concurrent-connections",
		cnc, "concurrent-connections",
		G_BINDING_DEFAULT |
		G_BINDING_SYNC_CREATE);
}

static void
m365_connection_set_source (EM365Connection *cnc,
			    ESource *source)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (cnc->priv->source == NULL);

	cnc->priv->source = g_object_ref (source);
}

static void
m365_connection_take_user (EM365Connection *cnc,
			   gchar *user)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!user || !*user)
		g_clear_pointer (&user, g_free);

	g_free (cnc->priv->user);
	cnc->priv->user = user;

	UNLOCK (cnc);
}

static void
m365_connection_take_impersonate_user (EM365Connection *cnc,
				       gchar *impersonate_user)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!impersonate_user || !*impersonate_user ||
	    !camel_m365_settings_get_use_impersonation (cnc->priv->settings)) {
		g_clear_pointer (&impersonate_user, g_free);
	}

	if (g_strcmp0 (impersonate_user, cnc->priv->impersonate_user) != 0) {
		g_free (cnc->priv->impersonate_user);
		cnc->priv->impersonate_user = impersonate_user;
	} else {
		g_clear_pointer (&impersonate_user, g_free);
	}

	UNLOCK (cnc);
}

static void
m365_connection_set_use_impersonation (EM365Connection *cnc,
				       gboolean use_impersonation)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!use_impersonation)
		m365_connection_take_impersonate_user (cnc, NULL);
	else
		m365_connection_take_impersonate_user (cnc, camel_m365_settings_dup_impersonate_user (cnc->priv->settings));

	UNLOCK (cnc);
}

static void
m365_connection_set_property (GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			e_m365_connection_set_proxy_resolver (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SETTINGS:
			m365_connection_set_settings (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE:
			m365_connection_set_source (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			e_m365_connection_set_concurrent_connections (
				E_M365_CONNECTION (object),
				g_value_get_uint (value));
			return;

		case PROP_USER:
			m365_connection_take_user (
				E_M365_CONNECTION (object),
				g_value_dup_string (value));
			return;

		case PROP_USE_IMPERSONATION:
			m365_connection_set_use_impersonation (
				E_M365_CONNECTION (object),
				g_value_get_boolean (value));
			return;

		case PROP_IMPERSONATE_USER:
			m365_connection_take_impersonate_user (
				E_M365_CONNECTION (object),
				g_value_dup_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_connection_get_property (GObject *object,
			      guint property_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			g_value_take_object (
				value,
				e_m365_connection_ref_proxy_resolver (
				E_M365_CONNECTION (object)));
			return;

		case PROP_SETTINGS:
			g_value_set_object (
				value,
				e_m365_connection_get_settings (
				E_M365_CONNECTION (object)));
			return;

		case PROP_SOURCE:
			g_value_set_object (
				value,
				e_m365_connection_get_source (
				E_M365_CONNECTION (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				e_m365_connection_get_concurrent_connections (
				E_M365_CONNECTION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_connection_constructed (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);
	ESourceExtension *extension;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->constructed (object);

	cnc->priv->soup_session = g_object_new (E_TYPE_SOUP_SESSION,
		"source", cnc->priv->source,
		"handle-backoff-responses", FALSE,
		"max-conns", cnc->priv->concurrent_connections,
		"max-conns-per-host", cnc->priv->concurrent_connections,
		NULL);

	if (m365_log_enabled ()) {
		SoupLogger *logger = soup_logger_new (SOUP_LOGGER_LOG_BODY);

		soup_session_add_feature (cnc->priv->soup_session, SOUP_SESSION_FEATURE (logger));

		g_object_unref (logger);
	}

	soup_session_add_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_COOKIE_JAR);
	soup_session_add_feature_by_type (cnc->priv->soup_session, E_TYPE_SOUP_AUTH_BEARER);

	/* This can use only OAuth2 authentication, which should be set on the ESource */
	if (soup_session_has_feature (cnc->priv->soup_session, SOUP_TYPE_AUTH_BASIC))
		soup_session_remove_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_AUTH_BASIC);
	if (soup_session_has_feature (cnc->priv->soup_session, SOUP_TYPE_AUTH_NTLM))
		soup_session_remove_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_AUTH_NTLM);
	if (soup_session_has_feature (cnc->priv->soup_session, SOUP_TYPE_AUTH_NEGOTIATE))
		soup_session_remove_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_AUTH_NEGOTIATE);
	soup_session_add_feature_by_type (cnc->priv->soup_session, E_TYPE_SOUP_AUTH_BEARER);

	cnc->priv->hash_key = m365_connection_construct_hash_key (cnc->priv->settings);

	e_binding_bind_property (
		cnc, "proxy-resolver",
		cnc->priv->soup_session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	/* the ESoupSession can read timeout from the WebDAV Backend extension,
	   thus make sure the options are in sync */
	extension = e_source_get_extension (cnc->priv->source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	e_binding_bind_property (
		cnc->priv->settings, "timeout",
		extension, "timeout",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		cnc->priv->settings, "timeout",
		cnc->priv->soup_session, "timeout",
		G_BINDING_SYNC_CREATE);
}

static void
m365_connection_dispose (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);

	G_LOCK (opened_connections);

	/* Remove the connection from the opened connections */
	if (opened_connections &&
	    g_hash_table_lookup (opened_connections, cnc->priv->hash_key) == (gpointer) object) {
		g_hash_table_remove (opened_connections, cnc->priv->hash_key);
		if (g_hash_table_size (opened_connections) == 0) {
			g_hash_table_destroy (opened_connections);
			opened_connections = NULL;
		}
	}

	G_UNLOCK (opened_connections);

	LOCK (cnc);

	g_clear_object (&cnc->priv->source);
	g_clear_object (&cnc->priv->settings);
	g_clear_object (&cnc->priv->soup_session);
	g_clear_object (&cnc->priv->proxy_resolver);

	UNLOCK (cnc);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->dispose (object);
}

static void
m365_connection_finalize (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);

	g_rec_mutex_clear (&cnc->priv->property_lock);
	g_clear_pointer (&cnc->priv->user, g_free);
	g_clear_pointer (&cnc->priv->impersonate_user, g_free);
	g_free (cnc->priv->hash_key);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->finalize (object);
}

static void
e_m365_connection_class_init (EM365ConnectionClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = m365_connection_set_property;
	object_class->get_property = m365_connection_get_property;
	object_class->constructed = m365_connection_constructed;
	object_class->dispose = m365_connection_dispose;
	object_class->finalize = m365_connection_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PROXY_RESOLVER,
		g_param_spec_object (
			"proxy-resolver",
			"Proxy Resolver",
			"The proxy resolver for this backend",
			G_TYPE_PROXY_RESOLVER,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SETTINGS,
		g_param_spec_object (
			"settings",
			"Settings",
			"Connection settings",
			CAMEL_TYPE_M365_SETTINGS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"Corresponding ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CONCURRENT_CONNECTIONS,
		g_param_spec_uint (
			"concurrent-connections",
			"Concurrent Connections",
			"Number of concurrent connections to use",
			MIN_CONCURRENT_CONNECTIONS,
			MAX_CONCURRENT_CONNECTIONS,
			1,
			/* Do not construct it, otherwise it overrides the value derived from CamelM365Settings */
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USER,
		g_param_spec_string (
			"user",
			NULL,
			NULL,
			NULL,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_IMPERSONATION,
		g_param_spec_boolean (
			"use-impersonation",
			NULL,
			NULL,
			FALSE,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_IMPERSONATE_USER,
		g_param_spec_string (
			"impersonate-user",
			NULL,
			NULL,
			NULL,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_m365_connection_init (EM365Connection *cnc)
{
	cnc->priv = e_m365_connection_get_instance_private (cnc);

	g_rec_mutex_init (&cnc->priv->property_lock);

	cnc->priv->backoff_for_usec = 0;
}

gboolean
e_m365_connection_util_delta_token_failed (const GError *error)
{
	return g_error_matches (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_BAD_REQUEST) ||
	       g_error_matches (error, E_M365_ERROR, E_M365_ERROR_ID_MALFORMED) ||
	       g_error_matches (error, E_M365_ERROR, E_M365_ERROR_SYNC_STATE_NOT_FOUND);
}

void
e_m365_connection_util_set_message_status_code (SoupMessage *message,
						gint status_code)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	g_object_set_data (G_OBJECT (message), "m365-batch-status-code",
		GINT_TO_POINTER (status_code));
}

gint
e_m365_connection_util_get_message_status_code (SoupMessage *message)
{
	gint status_code;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), -1);

	status_code = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (message), "m365-batch-status-code"));

	if (!status_code)
		status_code = soup_message_get_status (message);

	return status_code;
}

/* An EM365ConnectionRawDataFunc function, which writes the received data into the provided CamelStream */
gboolean
e_m365_connection_util_read_raw_data_cb (EM365Connection *cnc,
					 SoupMessage *message,
					 GInputStream *raw_data_stream,
					 gpointer user_data, /* CamelStream * */
					 GCancellable *cancellable,
					 GError **error)
{
	CamelStream *cache_stream = user_data;
	gssize expected_size = 0, wrote_size = 0, last_percent = -1;
	gint last_progress_notify = 0;
	gsize buffer_size = 65535;
	gchar *buffer;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STREAM (cache_stream), FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (raw_data_stream), FALSE);

	if (message && soup_message_get_response_headers (message)) {
		const gchar *content_length_str;

		content_length_str = soup_message_headers_get_one (soup_message_get_response_headers (message), "Content-Length");

		if (content_length_str && *content_length_str)
			expected_size = (gssize) g_ascii_strtoll (content_length_str, NULL, 10);
	}

	buffer = g_malloc (buffer_size);

	do {
		success = !g_cancellable_set_error_if_cancelled (cancellable, error);

		if (success) {
			gssize n_read, n_wrote;

			n_read = g_input_stream_read (raw_data_stream, buffer, buffer_size, cancellable, error);

			if (n_read == -1) {
				success = FALSE;
			} else if (!n_read) {
				break;
			} else {
				n_wrote = camel_stream_write (cache_stream, buffer, n_read, cancellable, error);
				success = n_read == n_wrote;

				if (success && expected_size > 0) {
					gssize percent;

					wrote_size += n_wrote;

					percent = wrote_size * 100.0 / expected_size;

					if (percent > 100)
						percent = 100;

					if (percent != last_percent) {
						gint64 now = g_get_monotonic_time ();

						/* Notify only 10 times per second, not more */
						if (percent == 100 || now - last_progress_notify > G_USEC_PER_SEC / 10) {
							last_progress_notify = now;
							last_percent = percent;

							camel_operation_progress (cancellable, percent);
						}
					}
				}
			}
		}
	} while (success);

	g_free (buffer);

	if (success)
		camel_stream_flush (cache_stream, cancellable, NULL);

	return success;
}

static gboolean
m365_connection_util_reencode_one_part_to_base64_sync (CamelMimePart *part,
						       CamelDataWrapper *containee,
						       GCancellable *cancellable,
						       GError **error)
{
	CamelStream *stream;
	gboolean success;

	if (!CAMEL_IS_MIME_MESSAGE (containee)) {
		switch (camel_mime_part_get_encoding (part)) {
		case CAMEL_TRANSFER_ENCODING_DEFAULT:
		case CAMEL_TRANSFER_ENCODING_BASE64:
			/* no need to re-encode */
			return TRUE;
		default:
			break;
		}
	}

	stream = camel_stream_mem_new ();
	success = camel_data_wrapper_decode_to_stream_sync (containee, stream, cancellable, error) != -1;

	if (success) {
		GByteArray *byte_array;
		CamelContentType *ct;
		gchar *mime_type;

		ct = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (part));
		mime_type = camel_content_type_format (ct);
		byte_array = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));

		camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_BASE64);
		camel_mime_part_set_content (part, (const gchar *) byte_array->data, (gint) byte_array->len, mime_type);

		g_free (mime_type);
	}

	g_object_unref (stream);

	return success;
}

/* The server re-encodes the HTML messages into base64, but as of 2024-07-11 they do not
   decode quoted-printable encoding properly, thus re-encode to base64 instead.
   The multipart/signed messages are not affected, luckily (it would break the signature anyway). */
gboolean
e_m365_connection_util_reencode_parts_to_base64_sync (CamelMimePart *part, /* it can be a CamelMimeMessage */
						      GCancellable *cancellable,
						      GError **error)
{
	CamelDataWrapper *containee;

	if (CAMEL_IS_MULTIPART_SIGNED (part)) {
		/* Microsoft does not re-encode these */
		return TRUE;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	containee = camel_medium_get_content (CAMEL_MEDIUM (part));

	if (containee == NULL)
		return TRUE;

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART_SIGNED (containee)) {
		/* Microsoft does not re-encode these */
		return TRUE;
	} else if (CAMEL_IS_MULTIPART (containee)) {
		gint ii, parts;

		parts = camel_multipart_get_number (CAMEL_MULTIPART (containee));
		for (ii = 0; ii < parts; ii++) {
			CamelMimePart *mpart = camel_multipart_get_part (CAMEL_MULTIPART (containee), ii);

			if (!e_m365_connection_util_reencode_parts_to_base64_sync (mpart, cancellable, error))
				return FALSE;
		}
	} else if (CAMEL_IS_MIME_MESSAGE (containee)) {
		if (!e_m365_connection_util_reencode_parts_to_base64_sync (CAMEL_MIME_PART (containee), cancellable, error))
			return FALSE;
	} else {
		if (!m365_connection_util_reencode_one_part_to_base64_sync (part, containee, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

EM365Connection *
e_m365_connection_new (ESource *source,
		       CamelM365Settings *settings)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return e_m365_connection_new_full (source, settings, TRUE);
}

EM365Connection *
e_m365_connection_new_for_backend (EBackend *backend,
				   ESourceRegistry *registry,
				   ESource *source,
				   CamelM365Settings *settings)
{
	ESource *backend_source, *parent_source;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	backend_source = e_backend_get_source (backend);

	if (!backend_source)
		return e_m365_connection_new (source, settings);

	parent_source = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (parent_source) {
		EM365Connection *cnc;

		cnc = e_m365_connection_new (parent_source, settings);

		g_object_unref (parent_source);

		return cnc;
	}

	return e_m365_connection_new (source, settings);
}

EM365Connection *
e_m365_connection_new_full (ESource *source,
			    CamelM365Settings *settings,
			    gboolean allow_reuse)
{
	EM365Connection *cnc;

	if (allow_reuse) {
		gchar *hash_key = m365_connection_construct_hash_key (settings);

		if (hash_key) {
			G_LOCK (opened_connections);

			if (opened_connections) {
				cnc = g_hash_table_lookup (opened_connections, hash_key);

				if (cnc) {
					g_object_ref (cnc);
					G_UNLOCK (opened_connections);

					g_free (hash_key);

					return cnc;
				}
			}

			G_UNLOCK (opened_connections);
		}

		g_free (hash_key);
	}

	cnc = g_object_new (E_TYPE_M365_CONNECTION,
		"source", source,
		"settings", settings,
		NULL);

	if (allow_reuse && cnc->priv->hash_key) {
		G_LOCK (opened_connections);

		if (!opened_connections)
			opened_connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		g_hash_table_insert (opened_connections, g_strdup (cnc->priv->hash_key), cnc);

		G_UNLOCK (opened_connections);
	}

	return cnc;
}

ESource *
e_m365_connection_get_source (EM365Connection *cnc)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	return cnc->priv->source;
}

CamelM365Settings *
e_m365_connection_get_settings (EM365Connection *cnc)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	return cnc->priv->settings;
}

guint
e_m365_connection_get_concurrent_connections (EM365Connection *cnc)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), 1);

	return cnc->priv->concurrent_connections;
}

void
e_m365_connection_set_concurrent_connections (EM365Connection *cnc,
					      guint concurrent_connections)
{
	guint current_cc;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	concurrent_connections = CLAMP (concurrent_connections, MIN_CONCURRENT_CONNECTIONS, MAX_CONCURRENT_CONNECTIONS);

	current_cc = e_m365_connection_get_concurrent_connections (cnc);

	if (current_cc == concurrent_connections)
		return;

	/* Will be updated in the priv->soup_session the next time it's created,
	   because "max-conns" is a construct-only property */
	cnc->priv->concurrent_connections = concurrent_connections;

	g_object_notify (G_OBJECT (cnc), "concurrent-connections");
}

GProxyResolver *
e_m365_connection_ref_proxy_resolver (EM365Connection *cnc)
{
	GProxyResolver *proxy_resolver = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->proxy_resolver)
		proxy_resolver = g_object_ref (cnc->priv->proxy_resolver);

	UNLOCK (cnc);

	return proxy_resolver;
}

void
e_m365_connection_set_proxy_resolver (EM365Connection *cnc,
				      GProxyResolver *proxy_resolver)
{
	gboolean notify = FALSE;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	/* Emitting a "notify" signal unnecessarily might have
	 * unwanted side effects like cancelling a SoupMessage.
	 * Only emit if we now have a different GProxyResolver. */

	if (proxy_resolver != cnc->priv->proxy_resolver) {
		g_clear_object (&cnc->priv->proxy_resolver);
		cnc->priv->proxy_resolver = proxy_resolver;

		if (proxy_resolver)
			g_object_ref (proxy_resolver);

		notify = TRUE;
	}

	UNLOCK (cnc);

	if (notify)
		g_object_notify (G_OBJECT (cnc), "proxy-resolver");
}

static void
m365_connection_request_cancelled_cb (GCancellable *cancellable,
				      gpointer user_data)
{
	EFlag *flag = user_data;

	g_return_if_fail (flag != NULL);

	e_flag_set (flag);
}

/* An example error response:

  {
    "error": {
      "code": "BadRequest",
      "message": "Parsing Select and Expand failed.",
      "innerError": {
        "request-id": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
        "date": "2020-06-10T13:44:43"
      }
    }
  }

 */
static gboolean
m365_connection_extract_error (JsonNode *node,
			       guint status_code,
			       GError **error)
{
	JsonObject *object;
	GQuark domain = E_SOUP_SESSION_ERROR;
	const gchar *code, *message;

	if (!node || !JSON_NODE_HOLDS_OBJECT (node))
		return FALSE;

	object = e_m365_json_get_object_member (json_node_get_object (node), "error");

	if (!object)
		return FALSE;

	code = e_m365_json_get_string_member (object, "code", NULL);
	message = e_m365_json_get_string_member (object, "message", NULL);

	if (!code && !message)
		return FALSE;

	if (!status_code || status_code == -1 || SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		domain = G_IO_ERROR;
		status_code = G_IO_ERROR_INVALID_DATA;
	} else if (g_strcmp0 (code, "ErrorInvalidUser") == 0) {
		status_code = SOUP_STATUS_UNAUTHORIZED;
	} else if (g_strcmp0 (code, "ErrorItemNotFound") == 0) {
		domain = E_M365_ERROR;
		status_code = E_M365_ERROR_ITEM_NOT_FOUND;
	} else if (g_strcmp0 (code, "ErrorInvalidIdMalformed") == 0) {
		domain = E_M365_ERROR;
		status_code = E_M365_ERROR_ID_MALFORMED;
	} else if (g_strcmp0 (code, "SyncStateNotFound") == 0) {
		domain = E_M365_ERROR;
		status_code = E_M365_ERROR_SYNC_STATE_NOT_FOUND;
	}

	if (code && message)
		g_set_error (error, domain, status_code, "%s: %s", code, message);
	else
		g_set_error_literal (error, domain, status_code, code ? code : message);

	return TRUE;
}

typedef gboolean (* EM365ResponseFunc)	(EM365Connection *cnc,
					 SoupMessage *message,
					 GInputStream *input_stream,
					 JsonNode *node,
					 gpointer user_data,
					 gchar **out_next_link,
					 GCancellable *cancellable,
					 GError **error);

/* (transfer full) (nullable): Free the *out_node with json_node_unref(), if not NULL;
   It can return 'success', even when the *out_node is NULL. */
gboolean
e_m365_connection_json_node_from_message (SoupMessage *message,
					  GInputStream *input_stream,
					  JsonNode **out_node,
					  GCancellable *cancellable,
					  GError **error)
{
	JsonObject *message_json_object;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_node != NULL, FALSE);

	*out_node = NULL;

	message_json_object = g_object_get_data (G_OBJECT (message), X_EVO_M365_DATA);

	if (message_json_object) {
		*out_node = json_node_init_object (json_node_new (JSON_NODE_OBJECT), message_json_object);

		success = !m365_connection_extract_error (*out_node, e_m365_connection_util_get_message_status_code (message), &local_error);
	} else {
		const gchar *content_type;

		content_type = soup_message_get_response_headers (message) ?
			soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL) : NULL;

		if (content_type && g_ascii_strcasecmp (content_type, "application/json") == 0) {
			JsonParser *json_parser;
			GByteArray *msg_bytes = NULL;

			json_parser = json_parser_new_immutable ();

			if (!input_stream)
				msg_bytes = e_soup_session_util_get_message_bytes (message);

			if (input_stream) {
				success = json_parser_load_from_stream (json_parser, input_stream, cancellable, error);
			} else if (msg_bytes) {
				success = json_parser_load_from_data (json_parser, (const gchar *) msg_bytes->data, msg_bytes->len, error);
			} else {
				/* This should not happen, it's for safety check only, thus the string is not localized */
				success = FALSE;
				g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED, "No JSON data found");
			}

			if (success) {
				*out_node = json_parser_steal_root (json_parser);

				success = !m365_connection_extract_error (*out_node, e_m365_connection_util_get_message_status_code (message), &local_error);
			}

			g_object_unref (json_parser);
		}
	}

	if (!success && *out_node) {
		json_node_unref (*out_node);
		*out_node = NULL;
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static gboolean
m365_connection_send_request_sync (EM365Connection *cnc,
				   SoupMessage *message,
				   EM365ResponseFunc response_func,
				   EM365ConnectionRawDataFunc raw_data_func,
				   gpointer func_user_data,
				   GCancellable *cancellable,
				   GError **error)
{
	SoupSession *soup_session;
	gint need_retry_seconds = 5;
	gboolean did_io_error_retry = FALSE;
	gboolean success = FALSE, need_retry = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (response_func != NULL || raw_data_func != NULL, FALSE);
	g_return_val_if_fail (response_func == NULL || raw_data_func == NULL, FALSE);

	while (need_retry && !g_cancellable_is_cancelled (cancellable)) {
		need_retry = FALSE;

		LOCK (cnc);

		if (cnc->priv->backoff_for_usec) {
			EFlag *flag;
			gint64 wait_ms;
			gulong handler_id = 0;

			wait_ms = cnc->priv->backoff_for_usec / G_TIME_SPAN_MILLISECOND;

			UNLOCK (cnc);

			flag = e_flag_new ();

			if (cancellable) {
				handler_id = g_cancellable_connect (cancellable, G_CALLBACK (m365_connection_request_cancelled_cb),
					flag, NULL);
			}

			while (wait_ms > 0 && !g_cancellable_is_cancelled (cancellable)) {
				gint64 now = g_get_monotonic_time ();
				gint left_minutes, left_seconds;

				left_minutes = wait_ms / 60000;
				left_seconds = (wait_ms / 1000) % 60;

				if (left_minutes > 0) {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Microsoft 365 server is busy, waiting to retry (%d:%02d minute)",
							"Microsoft 365 server is busy, waiting to retry (%d:%02d minutes)", left_minutes),
						left_minutes, left_seconds);
				} else {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Microsoft 365 server is busy, waiting to retry (%d second)",
							"Microsoft 365 server is busy, waiting to retry (%d seconds)", left_seconds),
						left_seconds);
				}

				e_flag_wait_until (flag, now + (G_TIME_SPAN_MILLISECOND * (wait_ms > 1000 ? 1000 : wait_ms)));
				e_flag_clear (flag);

				now = g_get_monotonic_time () - now;
				now = now / G_TIME_SPAN_MILLISECOND;

				if (now >= wait_ms)
					wait_ms = 0;
				wait_ms -= now;

				camel_operation_pop_message (cancellable);
			}

			if (handler_id)
				g_cancellable_disconnect (cancellable, handler_id);

			e_flag_free (flag);

			LOCK (cnc);

			cnc->priv->backoff_for_usec = 0;
		}

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			UNLOCK (cnc);

			e_m365_connection_util_set_message_status_code (message, -1);

			return FALSE;
		}

		soup_session = cnc->priv->soup_session ? g_object_ref (cnc->priv->soup_session) : NULL;

		UNLOCK (cnc);

		if (soup_session) {
			GInputStream *input_stream;
			GError *local_error = NULL;
			gboolean is_io_error;

			input_stream = e_soup_session_send_message_sync (E_SOUP_SESSION (soup_session), message, cancellable, &local_error);

			success = input_stream != NULL;
			is_io_error = !did_io_error_retry && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT);

			if (local_error) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}

			if (is_io_error ||
			    /* Throttling - https://docs.microsoft.com/en-us/graph/throttling  */
			    e_m365_connection_util_get_message_status_code (message) == 429 ||
			    /* https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
			    e_m365_connection_util_get_message_status_code (message) == SOUP_STATUS_SERVICE_UNAVAILABLE) {
				did_io_error_retry = did_io_error_retry || is_io_error;
				need_retry = TRUE;
			}

			if (need_retry) {
				const gchar *retry_after_str;
				gint64 retry_after;

				retry_after_str = soup_message_get_response_headers (message) ?
					soup_message_headers_get_one (soup_message_get_response_headers (message), "Retry-After") : NULL;

				if (retry_after_str && *retry_after_str)
					retry_after = g_ascii_strtoll (retry_after_str, NULL, 10);
				else if (is_io_error)
					retry_after = M365_RETRY_IO_ERROR_SECONDS;
				else
					retry_after = 0;

				if (retry_after > 0)
					need_retry_seconds = retry_after;
				else if (need_retry_seconds < 120)
					need_retry_seconds *= 2;

				LOCK (cnc);

				if (cnc->priv->backoff_for_usec < need_retry_seconds * G_USEC_PER_SEC)
					cnc->priv->backoff_for_usec = need_retry_seconds * G_USEC_PER_SEC;

				if (e_m365_connection_util_get_message_status_code (message) == SOUP_STATUS_SERVICE_UNAVAILABLE)
					soup_session_abort (soup_session);

				UNLOCK (cnc);

				success = FALSE;
			} else if (success && raw_data_func && SOUP_STATUS_IS_SUCCESSFUL (e_m365_connection_util_get_message_status_code (message))) {
				success = raw_data_func (cnc, message, input_stream, func_user_data, cancellable, error);
			} else if (success) {
				JsonNode *node = NULL;

				success = e_m365_connection_json_node_from_message (message, input_stream, &node, cancellable, error);

				if (success) {
					gchar *next_link = NULL;

					success = response_func && response_func (cnc, message, input_stream, node, func_user_data, &next_link, cancellable, error);

					if (success && next_link && *next_link) {
						GUri *uri;

						uri = g_uri_parse (next_link, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);

						/* Check whether the server returned correct nextLink URI */
						g_warn_if_fail (uri != NULL);

						if (uri) {
							need_retry = TRUE;

							soup_message_set_uri (message, uri);
							g_uri_unref (uri);
						}
					}

					g_free (next_link);
				} else if (error && !*error && e_m365_connection_util_get_message_status_code (message) && !SOUP_STATUS_IS_SUCCESSFUL (e_m365_connection_util_get_message_status_code (message))) {
					if (e_m365_connection_util_get_message_status_code (message) == -1)
						g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Invalid data"));
					else
						g_set_error_literal (error, E_SOUP_SESSION_ERROR, e_m365_connection_util_get_message_status_code (message),
							soup_message_get_reason_phrase (message) ? soup_message_get_reason_phrase (message) :
							soup_status_get_phrase (e_m365_connection_util_get_message_status_code (message)));
				}

				if (node)
					json_node_unref (node);
			} else if (e_soup_session_util_get_message_bytes (message)) {
				/* Try to extract detailed error */
				JsonNode *node = NULL;

				if (!e_m365_connection_json_node_from_message (message, input_stream, &node, cancellable, &local_error) && local_error) {
					g_clear_error (error);
					g_propagate_error (error, local_error);
					local_error = NULL;
				}

				g_clear_pointer (&node, json_node_unref);
			}

			g_clear_object (&input_stream);
		} else {
			if (!e_m365_connection_util_get_message_status_code (message)) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, _("Operation was cancelled"));
			} else if (e_m365_connection_util_get_message_status_code (message) == -1) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Invalid data"));
			} else {
				g_set_error_literal (error, E_SOUP_SESSION_ERROR, e_m365_connection_util_get_message_status_code (message),
					soup_message_get_reason_phrase (message) ? soup_message_get_reason_phrase (message) :
					soup_status_get_phrase (e_m365_connection_util_get_message_status_code (message)));
			}
		}

		g_clear_object (&soup_session);

		if (need_retry) {
			success = FALSE;
			g_clear_error (error);
		}
	}

	return success;
}

static gboolean
e_m365_read_no_response_cb (EM365Connection *cnc,
			    SoupMessage *message,
			    GInputStream *raw_data_stream,
			    gpointer user_data,
			    GCancellable *cancellable,
			    GError **error)
{
	/* This is used when no response is expected from the server.
	   Read the data stream only if debugging is on, in case
	   the server returns anything interesting. */

	if (m365_log_enabled ()) {
		gchar buffer[4096];

		while (g_input_stream_read (raw_data_stream, buffer, sizeof (buffer), cancellable, error) > 0) {
			/* Do nothing, just read it, thus it's shown in the debug output */
		}
	}

	return TRUE;
}

static gboolean
e_m365_read_to_byte_array_cb (EM365Connection *cnc,
			      SoupMessage *message,
			      GInputStream *raw_data_stream,
			      gpointer user_data,
			      GCancellable *cancellable,
			      GError **error)
{
	GByteArray **out_byte_array = user_data;
	gchar buffer[4096];
	gssize n_read;

	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (out_byte_array != NULL, FALSE);

	if (!*out_byte_array) {
		goffset content_length;

		content_length = soup_message_headers_get_content_length (soup_message_get_response_headers (message));

		if (!content_length || content_length > 65536)
			content_length = 65535;

		*out_byte_array = g_byte_array_sized_new (content_length);
	}

	while (n_read = g_input_stream_read (raw_data_stream, buffer, sizeof (buffer), cancellable, error), n_read > 0) {
		g_byte_array_append (*out_byte_array, (const guint8 *) buffer, n_read);
	}

	return !n_read;
}

typedef struct _EM365ResponseData {
	EM365ConnectionJsonFunc json_func;
	gpointer func_user_data;
	gboolean read_only_once; /* To be able to just try authentication */
	GSList **out_items; /* JsonObject * */
	GPtrArray *out_array_items; /* JsonObject * */
	gchar **out_delta_link; /* set only if available and not NULL */
	GPtrArray *inout_requests; /* SoupMessage *, for the batch request */
} EM365ResponseData;

static gboolean
e_m365_read_valued_response_cb (EM365Connection *cnc,
				SoupMessage *message,
				GInputStream *input_stream,
				JsonNode *node,
				gpointer user_data,
				gchar **out_next_link,
				GCancellable *cancellable,
				GError **error)
{
	EM365ResponseData *response_data = user_data;
	JsonObject *object;
	JsonArray *value;
	const gchar *delta_link;
	GSList *items = NULL;
	gboolean can_continue = TRUE;
	guint ii, len;

	g_return_val_if_fail (response_data != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	if (!response_data->read_only_once)
		*out_next_link = g_strdup (e_m365_json_get_string_member (object, "@odata.nextLink", NULL));

	delta_link = e_m365_json_get_string_member (object, "@odata.deltaLink", NULL);

	if (delta_link && response_data->out_delta_link)
		*response_data->out_delta_link = g_strdup (delta_link);

	value = e_m365_json_get_array_member (object, "value");
	g_return_val_if_fail (value != NULL, FALSE);

	len = json_array_get_length (value);

	for (ii = 0; ii < len; ii++) {
		JsonNode *elem = json_array_get_element (value, ii);

		g_warn_if_fail (JSON_NODE_HOLDS_OBJECT (elem));

		if (JSON_NODE_HOLDS_OBJECT (elem)) {
			JsonObject *elem_object = json_node_get_object (elem);

			if (elem_object) {
				if (response_data->out_items)
					*response_data->out_items = g_slist_prepend (*response_data->out_items, json_object_ref (elem_object));
				else if (response_data->out_array_items)
					g_ptr_array_add (response_data->out_array_items, json_object_ref (elem_object));
				else
					items = g_slist_prepend (items, json_object_ref (elem_object));
			}
		}
	}

	if (response_data->json_func)
		can_continue = response_data->json_func (cnc, items, response_data->func_user_data, cancellable, error);

	g_slist_free_full (items, (GDestroyNotify) json_object_unref);

	return can_continue;
}

static gboolean
e_m365_read_json_object_response_cb (EM365Connection *cnc,
				     SoupMessage *message,
				     GInputStream *input_stream,
				     JsonNode *node,
				     gpointer user_data,
				     gchar **out_next_link,
				     GCancellable *cancellable,
				     GError **error)
{
	JsonObject **out_json_object = user_data;
	JsonObject *object;

	g_return_val_if_fail (out_json_object != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_json_object = json_object_ref (object);

	return TRUE;
}

static SoupMessage *
m365_connection_new_soup_message (const gchar *method,
				  const gchar *uri,
				  CSMFlags csm_flags,
				  GError **error)
{
	SoupMessage *message;

	g_return_val_if_fail (method != NULL, NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	message = soup_message_new (method, uri);

	if (message) {
		SoupMessageHeaders *request_headers = soup_message_get_request_headers (message);

		soup_message_headers_append (request_headers, "Connection", "Close");
		soup_message_headers_append (request_headers, "User-Agent", "Evolution-M365/" VERSION);

		/* Disable caching for proxies (RFC 4918, section 10.4.5) */
		soup_message_headers_append (request_headers, "Cache-Control", "no-cache");
		soup_message_headers_append (request_headers, "Pragma", "no-cache");

		if ((csm_flags & CSM_DISABLE_RESPONSE) != 0)
			soup_message_headers_append (request_headers, "Prefer", "return=minimal");
	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Malformed URI: “%s”"), uri);
	}

	return message;
}

ESourceAuthenticationResult
e_m365_connection_authenticate_sync (EM365Connection *cnc,
				     const gchar *user_override,
				     EM365FolderKind kind,
				     const gchar *group_id,
				     const gchar *folder_id,
				     gchar **out_certificate_pem,
				     GTlsCertificateFlags *out_certificate_errors,
				     GCancellable *cancellable,
				     GError **error)
{
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;
	JsonObject *object = NULL;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), result);

	switch (kind) {
	default:
		g_warn_if_reached ();
		/* Falls through */
	case E_M365_FOLDER_KIND_UNKNOWN:
	case E_M365_FOLDER_KIND_MAIL:
		if (!folder_id || !*folder_id)
			folder_id = "inbox";

		success = e_m365_connection_get_mail_folder_sync (cnc, user_override, folder_id, "displayName", &object, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_CONTACTS:
		if (!folder_id || !*folder_id)
			folder_id = "contacts";

		success = e_m365_connection_get_contacts_folder_sync (cnc, user_override, folder_id, "displayName", &object, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_ORG_CONTACTS:
		success = e_m365_connection_get_org_contacts_accessible_sync (cnc, user_override, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_USERS:
		success = e_m365_connection_get_users_accessible_sync (cnc, user_override, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_PEOPLE:
		success = e_m365_connection_get_people_accessible_sync (cnc, user_override, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_CALENDAR:
		if (folder_id && !*folder_id)
			folder_id = NULL;

		success = e_m365_connection_get_calendar_folder_sync (cnc, user_override, group_id, folder_id, "name", &object, cancellable, error);
		break;
	case E_M365_FOLDER_KIND_TASKS:
		if (!folder_id || !*folder_id)
			folder_id = "tasks";

		success = e_m365_connection_get_task_list_sync (cnc, user_override, folder_id, &object, cancellable, error);
		break;
	}

	if (success) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			/* Nothing to do */
		} else if (e_soup_session_get_ssl_error_details (E_SOUP_SESSION (cnc->priv->soup_session), out_certificate_pem, out_certificate_errors)) {
			result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
			   /* Returned by the OAuth2 service when the token cannot be refreshed */
			   g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED)) {
			LOCK (cnc);

			if (cnc->priv->impersonate_user) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			} else {
				result = E_SOURCE_AUTHENTICATION_REJECTED;
			}

			UNLOCK (cnc);

			g_clear_error (&local_error);
		}

		if (local_error) {
			g_propagate_error (error, local_error);
			local_error = NULL;
		}
	}

	if (object)
		json_object_unref (object);

	g_clear_error (&local_error);

	return result;
}

gboolean
e_m365_connection_disconnect_sync (EM365Connection *cnc,
				   GCancellable *cancellable,
				   GError **error)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	LOCK (cnc);

	soup_session_abort (cnc->priv->soup_session);

	UNLOCK (cnc);

	return TRUE;
}

/* Expects NULL-terminated pair of parameters 'name', 'value'; if 'value' is NULL, the parameter is skipped.
   An empty 'name' can add the 'value' into the path. These can be only before query parameters. */
gchar *
e_m365_connection_construct_uri (EM365Connection *cnc,
				 gboolean include_user,
				 const gchar *user_override,
				 EM365ApiVersion api_version,
				 const gchar *api_part,
				 const gchar *resource,
				 const gchar *id,
				 const gchar *path,
				 ...)
{
	va_list args;
	const gchar *name, *value;
	gboolean first_param = TRUE;
	GString *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	if (!api_part)
		api_part = "users";

	uri = g_string_sized_new (128);

	/* https://graph.microsoft.com/v1.0/users/XUSERX/mailFolders */

	g_string_append (uri, "https://" M365_HOSTNAME);

	switch (api_version) {
	case E_M365_API_V1_0:
		g_string_append_c (uri, '/');
		g_string_append (uri, "v1.0");
		break;
	case E_M365_API_BETA:
		g_string_append_c (uri, '/');
		g_string_append (uri, "beta");
		break;
	}

	if (*api_part) {
		g_string_append_c (uri, '/');
		g_string_append (uri, api_part);
	}

	if (include_user) {
		const gchar *use_user;

		LOCK (cnc);

		if (user_override)
			use_user = user_override;
		else if (cnc->priv->impersonate_user)
			use_user = cnc->priv->impersonate_user;
		else
			use_user = cnc->priv->user;

		if (use_user) {
			gchar *encoded;

			encoded = g_uri_escape_string (use_user, NULL, FALSE);

			g_string_append_c (uri, '/');
			g_string_append (uri, encoded);

			g_free (encoded);
		}

		UNLOCK (cnc);
	}

	if (resource && *resource) {
		g_string_append_c (uri, '/');
		g_string_append (uri, resource);
	}

	if (id && *id) {
		g_string_append_c (uri, '/');
		g_string_append (uri, id);
	}

	if (path && *path) {
		g_string_append_c (uri, '/');
		g_string_append (uri, path);
	}

	va_start (args, path);

	name = va_arg (args, const gchar *);

	while (name) {
		value = va_arg (args, const gchar *);

		if (*name && value) {
			g_string_append_c (uri, first_param ? '?' : '&');

			first_param = FALSE;

			g_string_append (uri, name);
			g_string_append_c (uri, '=');

			if (*value) {
				gchar *encoded;

				encoded = g_uri_escape_string (value, NULL, FALSE);

				g_string_append (uri, encoded);

				g_free (encoded);
			}
		} else if (!*name && value && *value) {
			/* Warn when adding path after additional query parameters */
			g_warn_if_fail (first_param);

			g_string_append_c (uri, '/');
			g_string_append (uri, value);
		}

		name = va_arg (args, const gchar *);
	}

	va_end (args);

	return g_string_free (uri, FALSE);
}

static void
e_m365_connection_set_json_body (SoupMessage *message,
				 JsonBuilder *builder)
{
	JsonGenerator *generator;
	JsonNode *node;
	gchar *data;
	gsize data_length = 0;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (builder != NULL);

	node = json_builder_get_root (builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, node);

	data = json_generator_to_data (generator, &data_length);

	soup_message_headers_set_content_type (soup_message_get_request_headers (message), "application/json", NULL);

	if (data)
		e_soup_session_util_set_message_request_body_from_data (message, FALSE, "application/json", data, data_length, g_free);

	g_object_unref (generator);
	json_node_unref (node);
}

static void
e_m365_fill_message_headers_cb (JsonObject *object,
				const gchar *member_name,
				JsonNode *member_node,
				gpointer user_data)
{
	SoupMessage *message = user_data;

	g_return_if_fail (message != NULL);
	g_return_if_fail (member_name != NULL);
	g_return_if_fail (member_node != NULL);

	if (JSON_NODE_HOLDS_VALUE (member_node)) {
		const gchar *value;

		value = json_node_get_string (member_node);

		if (value)
			soup_message_headers_replace (soup_message_get_response_headers (message), member_name, value);
	}
}

static void
e_m365_connection_fill_batch_response (SoupMessage *message,
				       JsonObject *object)
{
	JsonObject *subobject;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (object != NULL);

	e_m365_connection_util_set_message_status_code (message, e_m365_json_get_int_member (object, "status", -1));

	subobject = e_m365_json_get_object_member (object, "headers");

	if (subobject)
		json_object_foreach_member (subobject, e_m365_fill_message_headers_cb, message);

	subobject = e_m365_json_get_object_member (object, "body");

	if (subobject)
		g_object_set_data_full (G_OBJECT (message), X_EVO_M365_DATA, json_object_ref (subobject), (GDestroyNotify) json_object_unref);
}

static gboolean
e_m365_read_batch_response_cb (EM365Connection *cnc,
			       SoupMessage *message,
			       GInputStream *input_stream,
			       JsonNode *node,
			       gpointer user_data,
			       gchar **out_next_link,
			       GCancellable *cancellable,
			       GError **error)
{
	GPtrArray *requests = user_data;
	JsonObject *object;
	JsonArray *responses;
	guint ii, len;

	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_next_link = g_strdup (e_m365_json_get_string_member (object, "@odata.nextLink", NULL));

	responses = e_m365_json_get_array_member (object, "responses");
	g_return_val_if_fail (responses != NULL, FALSE);

	len = json_array_get_length (responses);

	for (ii = 0; ii < len; ii++) {
		JsonNode *elem = json_array_get_element (responses, ii);

		g_warn_if_fail (JSON_NODE_HOLDS_OBJECT (elem));

		if (JSON_NODE_HOLDS_OBJECT (elem)) {
			JsonObject *elem_object = json_node_get_object (elem);

			if (elem_object) {
				const gchar *id_str;

				id_str = e_m365_json_get_string_member (elem_object, "id", NULL);

				if (id_str) {
					guint id;

					id = (guint) g_ascii_strtoull (id_str, NULL, 10);

					if (id < requests->len)
						e_m365_connection_fill_batch_response (g_ptr_array_index (requests, id), elem_object);
				}
			}
		}
	}

	return TRUE;
}

/* https://docs.microsoft.com/en-us/graph/json-batching */

static gboolean
e_m365_connection_batch_request_internal_sync (EM365Connection *cnc,
					       EM365ApiVersion api_version,
					       GPtrArray *requests, /* SoupMessage * */
					       GCancellable *cancellable,
					       GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success = TRUE;
	gchar *uri, buff[128];
	guint ii;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (requests->len > 0, FALSE);
	g_return_val_if_fail (requests->len <= E_M365_BATCH_MAX_REQUESTS, FALSE);

	uri = e_m365_connection_construct_uri (cnc, FALSE, NULL, api_version, "",
		"$batch", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_begin_array_member (builder, "requests");

	for (ii = 0; success && ii < requests->len; ii++) {
		SoupMessageHeadersIter iter;
		SoupMessage *submessage;
		GUri *guri;
		GInputStream *request_body;
		gssize request_body_length = 0;
		gboolean has_headers = FALSE;
		const gchar *hdr_name, *hdr_value, *use_uri;
		gboolean is_application_json = FALSE;

		submessage = g_ptr_array_index (requests, ii);

		if (!submessage)
			continue;

		e_m365_connection_util_set_message_status_code (submessage, -1);

		guri = soup_message_get_uri (submessage);
		uri = guri ? g_uri_to_string_partial (guri, G_URI_HIDE_PASSWORD) : NULL;

		if (!uri) {
			g_warning ("%s: Batch message ignored due to no URI", G_STRFUNC);
			continue;
		}

		/* Only the path with the query can be used in the batch request */
		use_uri = strstr (uri, M365_HOSTNAME);
		if (use_uri)
			use_uri = strchr (use_uri, '/');
		if (!use_uri)
			use_uri = uri;

		/* The 'url' is without the API part, it is derived from the main request */
		if (g_str_has_prefix (use_uri, "/v1.0/") ||
		    g_str_has_prefix (use_uri, "/beta/"))
			use_uri += 5;

		g_snprintf (buff, sizeof (buff), "%d", ii);

		e_m365_json_begin_object_member (builder, NULL);

		e_m365_json_add_string_member (builder, "id", buff);
		e_m365_json_add_string_member (builder, "method", soup_message_get_method (submessage));
		e_m365_json_add_string_member (builder, "url", use_uri);

		g_free (uri);

		soup_message_headers_iter_init (&iter, soup_message_get_request_headers (submessage));

		while (soup_message_headers_iter_next (&iter, &hdr_name, &hdr_value)) {
			if (hdr_name && *hdr_name && hdr_value &&
			    !camel_strcase_equal (hdr_name, "Connection") &&
			    !camel_strcase_equal (hdr_name, "User-Agent")) {
				if (camel_strcase_equal (hdr_name, "Content-Type") &&
				    camel_strcase_equal (hdr_value, "application/json"))
					is_application_json = TRUE;

				if (!has_headers) {
					has_headers = TRUE;

					e_m365_json_begin_object_member (builder, "headers");
				}

				e_m365_json_add_string_member (builder, hdr_name, hdr_value);
			}
		}

		if (has_headers)
			e_m365_json_end_object_member (builder); /* headers */

		request_body = e_soup_session_util_ref_message_request_body (submessage, &request_body_length);

		if (request_body && request_body_length > 0) {
			if (is_application_json) {
				/* The server needs it unpacked, not as a plain string */
				JsonParser *parser;
				JsonNode *node;

				parser = json_parser_new_immutable ();

				success = json_parser_load_from_stream (parser, request_body, cancellable, error);

				if (!success)
					g_prefix_error (error, "%s", _("Failed to parse own Json data"));

				node = success ? json_parser_steal_root (parser) : NULL;

				if (node) {
					json_builder_set_member_name (builder, "body");
					json_builder_add_value (builder, node);
				}

				g_clear_object (&parser);
			} else {
				GByteArray *array;
				guint8 *buffer;
				gsize n_buffer_sz = 16384;
				gsize n_read;

				buffer = g_new0 (guint8, n_buffer_sz);
				array = g_byte_array_sized_new (request_body_length + 1);

				while (g_input_stream_read_all (request_body, buffer, n_buffer_sz, &n_read, cancellable, NULL)) {
					if (n_read > 0)
						g_byte_array_append (array, buffer, n_read);
				}

				/* null-terminate the data, to use it as a string */
				buffer[0] = '\0';
				g_byte_array_append (array, buffer, 1);

				e_m365_json_add_string_member (builder, "body", (const gchar *) array->data);

				g_byte_array_unref (array);
				g_free (buffer);
			}
		}

		e_m365_json_end_object_member (builder); /* unnamed object */

		g_clear_object (&request_body);
	}

	e_m365_json_end_array_member (builder);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	soup_message_headers_append (soup_message_get_request_headers (message), "Accept", "application/json");

	g_object_unref (builder);

	success = success && m365_connection_send_request_sync (cnc, message, e_m365_read_batch_response_cb, NULL, requests, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* The 'requests' contains a SoupMessage * objects, from which are read
   the request data and on success the SoupMessage's 'response' properties
   are filled accordingly.
 */
gboolean
e_m365_connection_batch_request_sync (EM365Connection *cnc,
				      EM365ApiVersion api_version,
				      GPtrArray *requests, /* SoupMessage * */
				      GCancellable *cancellable,
				      GError **error)
{
	GPtrArray *use_requests;
	gint need_retry_seconds = 5;
	gboolean did_io_error_retry = FALSE;
	gboolean success, need_retry = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (requests->len > 0, FALSE);
	g_return_val_if_fail (requests->len <= E_M365_BATCH_MAX_REQUESTS, FALSE);

	use_requests = requests;

	while (need_retry) {
		GError *local_error = NULL;

		need_retry = FALSE;
		g_clear_error (error);

		success = e_m365_connection_batch_request_internal_sync (cnc, api_version, use_requests, cancellable, &local_error);

		if (!did_io_error_retry && g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT)) {
			did_io_error_retry = TRUE;
			success = FALSE;
			need_retry = TRUE;

			LOCK (cnc);

			if (cnc->priv->backoff_for_usec < M365_RETRY_IO_ERROR_SECONDS * G_USEC_PER_SEC)
				cnc->priv->backoff_for_usec = M365_RETRY_IO_ERROR_SECONDS * G_USEC_PER_SEC;

			UNLOCK (cnc);
		}

		if (local_error) {
			g_propagate_error (error, local_error);
			local_error = NULL;
		}

		if (success) {
			GPtrArray *new_requests = NULL;
			gint delay_seconds = 0;
			gint ii;

			for (ii = 0; ii < use_requests->len; ii++) {
				SoupMessage *message = g_ptr_array_index (use_requests, ii);

				if (!message)
					continue;

				/* Throttling - https://docs.microsoft.com/en-us/graph/throttling  */
				if (e_m365_connection_util_get_message_status_code (message) == 429 ||
				    /* https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
				    e_m365_connection_util_get_message_status_code (message) == SOUP_STATUS_SERVICE_UNAVAILABLE) {
					const gchar *retry_after_str;
					gint64 retry_after;

					need_retry = TRUE;

					if (!new_requests)
						new_requests = g_ptr_array_sized_new (use_requests->len);

					g_ptr_array_add (new_requests, message);

					retry_after_str = soup_message_get_response_headers (message) ?
						soup_message_headers_get_one (soup_message_get_response_headers (message), "Retry-After") : NULL;

					if (retry_after_str && *retry_after_str)
						retry_after = g_ascii_strtoll (retry_after_str, NULL, 10);
					else
						retry_after = 0;

					if (retry_after > 0)
						delay_seconds = MAX (delay_seconds, retry_after);
					else
						delay_seconds = MAX (delay_seconds, need_retry_seconds);
				}
			}

			if (new_requests) {
				if (delay_seconds)
					need_retry_seconds = delay_seconds;
				else if (need_retry_seconds < 120)
					need_retry_seconds *= 2;

				LOCK (cnc);

				if (cnc->priv->backoff_for_usec < need_retry_seconds * G_USEC_PER_SEC)
					cnc->priv->backoff_for_usec = need_retry_seconds * G_USEC_PER_SEC;

				UNLOCK (cnc);

				if (use_requests != requests)
					g_ptr_array_free (use_requests, TRUE);

				use_requests = new_requests;
			}
		}
	}

	if (use_requests != requests)
		g_ptr_array_free (use_requests, TRUE);

	return success;
}

/* This can be used as an EM365ConnectionJsonFunc function, it only
   copies items of 'results' into 'user_data', which is supposed
   to be a pointer to a GSList *. */
gboolean
e_m365_connection_call_gather_into_slist (EM365Connection *cnc,
					  const GSList *results, /* JsonObject * - the returned objects from the server */
					  gpointer user_data, /* expects GSList **, aka pointer to a GSList *, where it copies the 'results' */
					  GCancellable *cancellable,
					  GError **error)
{
	GSList **out_results = user_data, *link;

	g_return_val_if_fail (out_results != NULL, FALSE);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *obj = link->data;

		if (obj)
			*out_results = g_slist_prepend (*out_results, json_object_ref (obj));
	}

	return TRUE;
}

/* https://docs.microsoft.com/en-us/graph/api/outlookuser-list-mastercategories?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_categories_sync (EM365Connection *cnc,
				       const gchar *user_override,
				       GSList **out_categories, /* EM365Category * */
				       GCancellable *cancellable,
				       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_categories != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"outlook",
		"masterCategories",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_categories;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-mailfolders?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_mail_folders_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  const gchar *from_path, /* path for the folder to read, NULL for top user folder */
					  const gchar *select, /* properties to select, nullable */
					  GSList **out_folders, /* EM365MailFolder * - the returned mailFolder objects */
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folders != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		NULL,
		from_path,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_folders;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_folders_delta_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  EM365FolderKind kind,
					  const gchar *select, /* properties to select, nullable */
					  const gchar *delta_link, /* previous delta link */
					  guint max_page_size, /* 0 for default by the server */
					  EM365ConnectionJsonFunc func, /* function to call with each result set */
					  gpointer func_user_data, /* user data passed into the 'func' */
					  gchar **out_delta_link,
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link)
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);

	if (!message) {
		const gchar *kind_str = NULL;
		gchar *uri;

		switch (kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			kind_str = "contactFolders";
			break;
		case E_M365_FOLDER_KIND_MAIL:
			kind_str = "mailFolders";
			break;
		default:
			g_warn_if_reached ();
			break;
		}

		g_return_val_if_fail (kind_str != NULL, FALSE);

		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			kind_str,
			NULL,
			"delta",
			"$select", select,
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_mail_folder_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *folder_id, /* nullable - then the 'inbox' is used */
					const gchar *select, /* nullable - properties to select */
					EM365MailFolder **out_folder,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		folder_id ? folder_id : "inbox",
		NULL,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-mailfolders?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/mailfolder-post-childfolders?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *parent_folder_id, /* NULL for the folder root */
					   const gchar *display_name,
					   EM365MailFolder **out_mail_folder,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);
	g_return_val_if_fail (out_mail_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		parent_folder_id,
		parent_folder_id ? "childFolders" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "displayName", display_name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *folder_id,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders", folder_id, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-copy?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/mailfolder-move?view=graph-rest-1.0&tabs=http
 */
gboolean
e_m365_connection_copy_move_mail_folder_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *src_folder_id,
					      const gchar *des_folder_id,
					      gboolean do_copy,
					      EM365MailFolder **out_mail_folder,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (src_folder_id != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		src_folder_id,
		do_copy ? "copy" : "move",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "destinationId", des_folder_id);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_rename_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *folder_id,
					   const gchar *display_name,
					   EM365MailFolder **out_mail_folder,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		folder_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "displayName", display_name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_list_messages_sync (EM365Connection *cnc,
				      const gchar *user_override, /* for which user, NULL to use the account user */
				      const gchar *folder_id,
				      const gchar *select, /* nullable - properties to select */
				      const gchar *filter, /* nullable - filter which events to list */
				      GSList **out_messages, /* EM365MailMessage * - the returned objects */
				      GCancellable *cancellable,
				      GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (out_messages != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		folder_id,
		"messages",
		"$select", select,
		"$filter", filter,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_messages;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-delta?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/contact-delta?view=graph-rest-1.0&tabs=http
   https://learn.microsoft.com/en-us/graph/api/orgcontact-delta?view=graph-rest-1.0&tabs=http
   https://learn.microsoft.com/en-us/graph/api/user-delta?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_objects_delta_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  EM365FolderKind kind,
					  const gchar *folder_id, /* folder ID to get delta messages in, nullable for orgContacts, users */
					  const gchar *select, /* properties to select, nullable */
					  const gchar *delta_link, /* previous delta link */
					  guint max_page_size, /* 0 for default by the server */
					  EM365ConnectionJsonFunc func, /* function to call with each result set */
					  gpointer func_user_data, /* user data passed into the 'func' */
					  gchar **out_delta_link,
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link)
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);

	if (!message) {
		const gchar *api_part = NULL;
		const gchar *kind_str = NULL;
		const gchar *kind_path_str = NULL;
		gchar *uri;
		gboolean with_size;

		switch (kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			g_return_val_if_fail (folder_id != NULL, FALSE);
			kind_str = "contactFolders";
			kind_path_str = "contacts";
			break;
		case E_M365_FOLDER_KIND_ORG_CONTACTS:
			api_part = "contacts";
			break;
		case E_M365_FOLDER_KIND_USERS:
			api_part = "users";
			break;
		case E_M365_FOLDER_KIND_MAIL:
			g_return_val_if_fail (folder_id != NULL, FALSE);
			kind_str = "mailFolders";
			kind_path_str = "messages";
			break;
		default:
			g_warn_if_reached ();
			break;
		}

		with_size = select && g_str_has_prefix (select, "|size|");
		if (with_size)
			select += 6;

		uri = e_m365_connection_construct_uri (cnc, api_part == NULL, user_override, E_M365_API_V1_0, api_part,
			kind_str,
			folder_id,
			kind_path_str,
			"", "delta",
			"$select", select,
			with_size ? "expand" : NULL,
			with_size ? "singleValueExtendedProperties($filter=id eq '" E_M365_PT_MESSAGE_SIZE_NAME "')" : NULL,
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_mail_message_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *folder_id,
					 const gchar *message_id,
					 EM365ConnectionRawDataFunc func,
					 gpointer func_user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, func, func_user_data, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-messages?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_mail_message_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
					    JsonBuilder *mail_message, /* filled mailMessage object */
					    EM365MailMessage **out_created_message, /* free with json_object_unref() */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (mail_message != NULL, FALSE);
	g_return_val_if_fail (out_created_message != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "mailFolders" : "messages",
		folder_id,
		folder_id ? "messages" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, mail_message);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_message, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_upload_mail_message_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
					    CamelMimeMessage *mime_message,
					    EM365MailMessage **out_created_message, /* free with json_object_unref() */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	CamelStream *mem_stream;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (mime_message), FALSE);
	g_return_val_if_fail (out_created_message != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "mailFolders" : "messages",
		folder_id,
		folder_id ? "messages" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	mem_stream = camel_stream_mem_new ();

	success = camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (mime_message), mem_stream, cancellable, error) >= 0 &&
		  camel_stream_flush (mem_stream, cancellable, error) != -1;

	if (success) {
		GInputStream *input_stream;
		GByteArray *mime_data;
		gchar *base64_data;
		gssize base64_data_len;

		mime_data = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (mem_stream));
		base64_data = g_base64_encode (mime_data->data, mime_data->len);
		base64_data_len = strlen (base64_data);

		input_stream = g_memory_input_stream_new_from_data (g_steal_pointer (&base64_data), base64_data_len, g_free);

		e_soup_session_util_set_message_request_body (message, "text/plain", input_stream, base64_data_len);

		success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_message, cancellable, error);

		g_clear_object (&input_stream);
	}

	g_clear_object (&mem_stream);
	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-post-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_add_mail_message_attachment_sync (EM365Connection *cnc,
						    const gchar *user_override, /* for which user, NULL to use the account user */
						    const gchar *message_id, /* the message to add it to */
						    JsonBuilder *attachment, /* filled attachment object */
						    gchar **out_attachment_id,
						    GCancellable *cancellable,
						    GError **error)
{
	SoupMessage *message;
	JsonObject *added_attachment = NULL;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (attachment != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"attachments",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, attachment);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &added_attachment, cancellable, error);

	if (success && added_attachment && out_attachment_id)
		*out_attachment_id = g_strdup (e_m365_attachment_get_id (added_attachment));

	if (added_attachment)
		json_object_unref (added_attachment);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-update?view=graph-rest-1.0&tabs=http */

SoupMessage *
e_m365_connection_prepare_update_mail_message (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *message_id,
					       JsonBuilder *mail_message, /* values to update, as a mailMessage object */
					       GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);
	g_return_val_if_fail (mail_message != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		NULL,
		NULL);

	/* The server returns the mailMessage object back, but it can be ignored here */
	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, mail_message);

	return message;
}

gboolean
e_m365_connection_update_mail_message_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *message_id,
					    JsonBuilder *mail_message, /* values to update, as a mailMessage object */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);
	g_return_val_if_fail (mail_message != NULL, FALSE);

	message = e_m365_connection_prepare_update_mail_message (cnc, user_override, message_id, mail_message, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-copy?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/message-move?view=graph-rest-1.0&tabs=http
 */
static SoupMessage *
e_m365_connection_prepare_copy_move_mail_message (EM365Connection *cnc,
						  const gchar *user_override,
						  const gchar *message_id,
						  const gchar *des_folder_id,
						  gboolean do_copy,
						  GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		do_copy ? "copy" : "move",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "destinationId", des_folder_id);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	return message;
}

/* out_des_message_ids: Camel-pooled gchar *, new ids, in the same order as in message_ids; can be partial */
gboolean
e_m365_connection_copy_move_mail_messages_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						const GSList *message_ids, /* const gchar * */
						const gchar *des_folder_id,
						gboolean do_copy,
						GSList **out_des_message_ids, /* Camel-pooled gchar * */
						GCancellable *cancellable,
						GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_ids != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);
	g_return_val_if_fail (out_des_message_ids != NULL, FALSE);

	*out_des_message_ids = NULL;

	if (g_slist_next (message_ids)) {
		GPtrArray *requests;
		GSList *link;
		guint total, done = 0;

		total = g_slist_length ((GSList *) message_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) message_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_copy_move_mail_message (cnc, user_override, id, des_folder_id, do_copy, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					JsonObject *response = NULL;

					success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &response, cancellable, error);

					if (response) {
						*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
							(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
						json_object_unref (response);
					} else {
						success = FALSE;
					}
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

					if (success) {
						guint ii;

						for (ii = 0; success && ii < requests->len; ii++) {
							JsonNode *node = NULL;

							message = g_ptr_array_index (requests, ii);

							success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

							if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
								JsonObject *response;

								response = json_node_get_object (node);

								if (response) {
									*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
										(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
								} else {
									success = FALSE;
								}
							} else {
								success = FALSE;
							}

							if (node)
								json_node_unref (node);
						}
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_copy_move_mail_message (cnc, user_override, message_ids->data, des_folder_id, do_copy, error);

		if (message) {
			JsonObject *response = NULL;

			success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &response, cancellable, error);

			if (response) {
				*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
					(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
				json_object_unref (response);
			} else {
				success = FALSE;
			}

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	*out_des_message_ids = g_slist_reverse (*out_des_message_ids);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-delete?view=graph-rest-1.0&tabs=http */

static SoupMessage *
e_m365_connection_prepare_delete_mail_message (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *message_id,
					       GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	return message;
}

gboolean
e_m365_connection_delete_mail_messages_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const GSList *message_ids, /* const gchar * */
					     GSList **out_deleted_ids, /* (transfer container): const gchar *, borrowed from message_ids */
					     GCancellable *cancellable,
					     GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_ids != NULL, FALSE);

	if (g_slist_next (message_ids)) {
		GPtrArray *requests;
		GSList *link, *from_link = (GSList *) message_ids;
		guint total, done = 0;

		total = g_slist_length ((GSList *) message_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) message_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_delete_mail_message (cnc, user_override, id, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);
				}

				if (success && out_deleted_ids) {
					while (from_link) {
						*out_deleted_ids = g_slist_prepend (*out_deleted_ids, from_link->data);

						if (from_link == link)
							break;

						from_link = g_slist_next (from_link);
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);
				from_link = g_slist_next (link);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_delete_mail_message (cnc, user_override, message_ids->data, error);

		if (message) {
			success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

			if (success && out_deleted_ids)
				*out_deleted_ids = g_slist_prepend (*out_deleted_ids, message_ids->data);

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	if (out_deleted_ids && *out_deleted_ids && g_slist_next (*out_deleted_ids))
		*out_deleted_ids = g_slist_reverse (*out_deleted_ids);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-send?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_send_mail_message_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *message_id,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"send",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	soup_message_headers_append (soup_message_get_request_headers (message), "Content-Length", "0");

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-sendmail?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_send_mail_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  JsonBuilder *request, /* filled sendMail object */
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (request != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"sendMail", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, request);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_send_mail_mime_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *base64_mime,
				       gssize base64_mime_length,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (base64_mime != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"sendMail", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	if (base64_mime_length < 0)
		base64_mime_length = strlen (base64_mime);

	soup_message_headers_set_content_type (soup_message_get_request_headers (message), "text/plain", NULL);
	e_soup_session_util_set_message_request_body_from_data (message, FALSE, "text/plain", base64_mime, base64_mime_length, NULL);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contactfolder-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_contacts_folder_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *folder_id, /* nullable - then the default 'contacts' folder is returned */
					    const gchar *select, /* nullable - properties to select */
					    EM365Folder **out_folder,
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id ? folder_id : "contacts",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/profilephoto-get?view=graph-rest-1.0 */

gboolean
e_m365_connection_get_contact_photo_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  const gchar *folder_id,
					  const gchar *contact_id,
					  GByteArray **out_photo,
					  GCancellable *cancellable,
					  GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (out_photo != NULL, FALSE);

	if (folder_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"contactFolders",
			folder_id,
			"contacts",
			"", contact_id,
			"", "photo",
			"", "$value",
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, FALSE, user_override, E_M365_API_V1_0, NULL,
			contact_id,
			"photo",
			"$value",
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_to_byte_array_cb, out_photo, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/profilephoto-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_contact_photo_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *folder_id,
					     const gchar *contact_id,
					     const GByteArray *jpeg_photo, /* nullable, to remove the photo */
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id,
		"contacts",
		"", contact_id,
		"", "photo",
		"", "$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_PUT, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	soup_message_headers_set_content_type (soup_message_get_request_headers (message), "image/jpeg", NULL);
	soup_message_headers_set_content_length (soup_message_get_request_headers (message), jpeg_photo ? jpeg_photo->len : 0);

	if (jpeg_photo)
		e_soup_session_util_set_message_request_body_from_data (message, FALSE, "image/jpeg", jpeg_photo->data, jpeg_photo->len, NULL);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-get?view=graph-rest-1.0&tabs=http */

static SoupMessage *
e_m365_connection_prepare_get_contact (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id,
				       const gchar *contact_id,
				       GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (folder_id != NULL, NULL);
	g_return_val_if_fail (contact_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id,
		"contacts",
		"", contact_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	g_free (uri);

	return message;
}

gboolean
e_m365_connection_get_contact_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *folder_id,
				    const gchar *contact_id,
				    EM365Contact **out_contact,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);

	message = e_m365_connection_prepare_get_contact (cnc, user_override, folder_id, contact_id, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/orgcontact-get?view=graph-rest-1.0&tabs=http */

static SoupMessage *
e_m365_connection_prepare_get_org_contact (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *contact_id,
					   GError **error)
{
	SoupMessage *message;
	gchar *uri;

	uri = e_m365_connection_construct_uri (cnc, FALSE, user_override, E_M365_API_V1_0,
		"contacts",
		NULL,
		NULL,
		contact_id,
		"$select", ORG_CONTACTS_PROPS,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	g_free (uri);

	return message;
}

/* https://learn.microsoft.com/en-us/graph/api/user-get?view=graph-rest-1.0&tabs=http */

static SoupMessage *
e_m365_connection_prepare_get_user (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *user_id,
				    GError **error)
{
	SoupMessage *message;
	gchar *uri;

	uri = e_m365_connection_construct_uri (cnc, FALSE, user_override, E_M365_API_V1_0,
		"users",
		NULL,
		NULL,
		user_id,
		"$select", USERS_PROPS,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	g_free (uri);

	return message;
}

static gboolean
e_m365_connection_get_contacts_internal_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      EM365FolderKind folder_kind,
					      const gchar *folder_id,
					      GPtrArray *ids, /* const gchar * */
					      GPtrArray **out_contacts, /* EM365Contact * */
					      GCancellable *cancellable,
					      GError **error)
{
	GPtrArray *requests;
	guint total, done = 0, ii;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_kind == E_M365_FOLDER_KIND_CONTACTS ||
			      folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS ||
			      folder_kind == E_M365_FOLDER_KIND_USERS, FALSE);
	if (folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (ids != NULL, FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	*out_contacts = NULL;
	total = ids->len;
	requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

	for (ii = 0; ii < ids->len && success; ii++) {
		const gchar *id = g_ptr_array_index (ids, ii);
		SoupMessage *message = NULL;

		switch (folder_kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			message = e_m365_connection_prepare_get_contact (cnc, user_override, folder_id, id, error);
			break;
		case E_M365_FOLDER_KIND_ORG_CONTACTS:
			message = e_m365_connection_prepare_get_org_contact (cnc, user_override, id, error);
			break;
		case E_M365_FOLDER_KIND_USERS:
			message = e_m365_connection_prepare_get_user (cnc, user_override, id, error);
			break;
		default:
			break;
		}

		if (!message) {
			success = FALSE;
			break;
		}

		g_ptr_array_add (requests, message);

		if (requests->len == E_M365_BATCH_MAX_REQUESTS || ii + 1 >= ids->len) {
			if (!*out_contacts)
				*out_contacts = g_ptr_array_new_full (ids->len, (GDestroyNotify) json_object_unref);

			if (requests->len == 1) {
				EM365Contact *contact = NULL;

				success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &contact, cancellable, error);

				if (success)
					g_ptr_array_add (*out_contacts, contact);
			} else {
				success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

				if (success) {
					guint jj;

					for (jj = 0; jj < requests->len && success; jj++) {
						JsonNode *node = NULL;

						message = g_ptr_array_index (requests, jj);
						success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

						if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
							JsonObject *response;

							response = json_node_get_object (node);

							if (response)
								g_ptr_array_add (*out_contacts, json_object_ref (response));
							else
								success = FALSE;
						} else {
							success = FALSE;
						}

						if (node)
							json_node_unref (node);
					}
				}
			}

			g_ptr_array_remove_range (requests, 0, requests->len);

			done += requests->len;

			camel_operation_progress (cancellable, done * 100.0 / total);
		}
	}

	g_ptr_array_free (requests, TRUE);

	if (!success && *out_contacts && !(*out_contacts)->len)
		g_clear_pointer (out_contacts, g_ptr_array_unref);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/aad-advanced-queries?tabs=http
   https://learn.microsoft.com/en-us/graph/search-query-parameter?tabs=http */

static gboolean
e_m365_connection_search_contacts_internal_sync (EM365Connection *cnc,
						 const gchar *user_override, /* for which user, NULL to use the account user */
						 EM365FolderKind folder_kind,
						 const gchar *folder_id,
						 const gchar *search_text,
						 GSList **out_contacts, /* transfer full, EM365Contact * */
						 GCancellable *cancellable,
						 GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	const gchar *api_part = NULL;
	const gchar *kind_str = NULL;
	const gchar *kind_path_str = NULL;
	GString *search_str = NULL;
	gchar *uri, *filter = NULL, *mail_filter = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_kind == E_M365_FOLDER_KIND_CONTACTS ||
			      folder_kind == E_M365_FOLDER_KIND_ORG_CONTACTS ||
			      folder_kind == E_M365_FOLDER_KIND_USERS, FALSE);
	if (folder_kind == E_M365_FOLDER_KIND_CONTACTS)
		g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (search_text != NULL, FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	*out_contacts = NULL;

	if (strchr (search_text, '\'')) {
		search_str = e_ews_common_utils_str_replace_string (search_text, "'", "''");
		search_text = search_str->str;
	}

	if (strchr (search_text, '\"')) {
		GString *tmp;

		/* remove double quotes */
		tmp = e_ews_common_utils_str_replace_string (search_text, "\"", "");
		if (search_str)
			g_string_free (search_str, TRUE);

		search_str = tmp;
		search_text = search_str->str;
	}

	switch (folder_kind) {
	case E_M365_FOLDER_KIND_CONTACTS:
		g_return_val_if_fail (folder_id != NULL, FALSE);
		kind_str = "contactFolders";
		kind_path_str = "contacts";
		mail_filter = g_strconcat ("\"emailAddresses:", search_text, "\"", NULL);
		break;
	case E_M365_FOLDER_KIND_ORG_CONTACTS:
		api_part = "contacts";
		mail_filter = g_strconcat ("\"mail:", search_text, "\"", NULL);
		break;
	case E_M365_FOLDER_KIND_USERS:
		api_part = "users";
		mail_filter = g_strconcat ("\"mail:", search_text, "\"", NULL);
		break;
	default:
		g_warn_if_reached ();
		break;
	}

	filter = g_strconcat (
		"\"displayName:", search_text, "\""
		" OR "
		"\"givenName:", search_text, "\""
		" OR "
		"\"surname:", search_text, "\"",
		mail_filter ? " OR " : NULL,
		mail_filter,
		NULL);

	uri = e_m365_connection_construct_uri (cnc, api_part == NULL, user_override, E_M365_API_V1_0, api_part,
		kind_str,
		folder_id,
		kind_path_str,
		"$top", "50", /* no need for too many; server default is currently 250 */
		"$count", "true", /* required */
		"$search", filter,
		NULL);

	if (search_str)
		g_string_free (search_str, TRUE);
	g_free (mail_filter);
	g_free (filter);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	/* required */
	soup_message_headers_append (soup_message_get_request_headers (message), "ConsistencyLevel", "eventual");

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_contacts;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	if (!success && *out_contacts) {
		g_slist_free_full (*out_contacts, (GDestroyNotify) json_object_unref);
		*out_contacts = NULL;
	}

	return success;
}

gboolean
e_m365_connection_get_contacts_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *folder_id,
				     GPtrArray *ids, /* const gchar * */
				     GPtrArray **out_contacts, /* EM365Contact * */
				     GCancellable *cancellable,
				     GError **error)
{
	return e_m365_connection_get_contacts_internal_sync (cnc, user_override, E_M365_FOLDER_KIND_CONTACTS,
		folder_id, ids, out_contacts, cancellable, error);
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-contacts?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
				       JsonBuilder *contact, /* filled contact object */
				       EM365Contact **out_created_contact, /* free with json_object_unref() */
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (out_created_contact != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, contact);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id,
				       const gchar *contact_id,
				       JsonBuilder *contact, /* values to update, as a contact object */
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : contact_id,
		"", folder_id ? contact_id : NULL,
		NULL);

	/* The server returns the contact object back, but it can be ignored here */
	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, contact);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id,
				       const gchar *contact_id,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : contact_id,
		"", folder_id ? contact_id : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/orgcontact-list?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_org_contacts_accessible_sync (EM365Connection *cnc,
						    const gchar *user_override, /* for which user, NULL to use the account user */
						    GCancellable *cancellable,
						    GError **error)
{
	EM365ResponseData rd;
	GSList *contacts = NULL;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	uri = e_m365_connection_construct_uri (cnc, FALSE, user_override, E_M365_API_V1_0,
		"contacts",
		NULL, NULL, NULL,
		"$top", "1",
		"$select", "id",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.read_only_once = TRUE;
	rd.out_items = &contacts;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	/* ignore returned contact, the call is used only to check whether the end point can be accessed */
	g_slist_free_full (contacts, (GDestroyNotify) json_object_unref);
	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/orgcontact-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_org_contact_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *contact_id,
					EM365Contact **out_contact,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);

	message = e_m365_connection_prepare_get_org_contact (cnc, user_override, contact_id, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_org_contacts_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 GPtrArray *ids, /* const gchar * */
					 GPtrArray **out_contacts, /* EM365Contact * */
					 GCancellable *cancellable,
					 GError **error)
{
	return e_m365_connection_get_contacts_internal_sync (cnc, user_override, E_M365_FOLDER_KIND_ORG_CONTACTS,
		NULL, ids, out_contacts, cancellable, error);
}

/* https://learn.microsoft.com/en-us/graph/api/user-list?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_users_accessible_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     GCancellable *cancellable,
					     GError **error)
{
	EM365ResponseData rd;
	GSList *contacts = NULL;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	uri = e_m365_connection_construct_uri (cnc, FALSE, user_override, E_M365_API_V1_0,
		"users",
		NULL, NULL, NULL,
		"$top", "1",
		"$select", "id",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.read_only_once = TRUE;
	rd.out_items = &contacts;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	/* ignore returned contact, the call is used only to check whether the end point can be accessed */
	g_slist_free_full (contacts, (GDestroyNotify) json_object_unref);
	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/user-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_user_sync (EM365Connection *cnc,
				 const gchar *user_override, /* for which user, NULL to use the account user */
				 const gchar *user_id,
				 EM365Contact **out_contact,
				 GCancellable *cancellable,
				 GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (user_id != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);

	message = e_m365_connection_prepare_get_user (cnc, user_override, user_id, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_users_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  GPtrArray *ids, /* const gchar * */
				  GPtrArray **out_contacts, /* EM365Contact * */
				  GCancellable *cancellable,
				  GError **error)
{
	return e_m365_connection_get_contacts_internal_sync (cnc, user_override, E_M365_FOLDER_KIND_USERS,
		NULL, ids, out_contacts, cancellable, error);
}

gboolean
e_m365_connection_search_contacts_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					EM365FolderKind kind,
					const gchar *folder_id,
					const gchar *search_text,
					GSList **out_contacts, /* transfer full, EM365Contact * */
					GCancellable *cancellable,
					GError **error)
{
	return e_m365_connection_search_contacts_internal_sync (cnc, user_override, kind,
		folder_id, search_text, out_contacts, cancellable, error);
}

/* https://learn.microsoft.com/en-us/graph/api/user-list-people?view=graph-rest-1.0&tabs=http */

static gboolean
e_m365_connection_get_people_internal_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    gboolean id_only,
					    guint max_entries,
					    GPtrArray **out_contacts,
					    GCancellable *cancellable,
					    GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri, *max_entries_str = NULL;
	const gchar *param_name1 = NULL, *param_value1 = NULL;
	const gchar *param_name2 = NULL, *param_value2 = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	if (max_entries > 0) {
		max_entries_str = g_strdup_printf ("%u", max_entries);

		param_name1 = "$top";
		param_value1 = max_entries_str;
	}

	if (id_only) {
		param_name2 = "$select";
		param_value2 = "id";
	}

	if (!param_name1) {
		param_name1 = param_name2;
		param_value1 = param_value2;
		param_name2 = NULL;
		param_value2 = NULL;
	}

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"people", NULL, NULL,
		param_name1, param_value1,
		param_name2, param_value2,
		NULL);

	g_free (max_entries_str);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.read_only_once = id_only;
	rd.out_array_items = g_ptr_array_new_with_free_func ((GDestroyNotify) json_object_unref);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	if (success)
		*out_contacts = rd.out_array_items;
	else
		g_ptr_array_unref (rd.out_array_items);

	return success;
}


gboolean
e_m365_connection_get_people_accessible_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      GCancellable *cancellable,
					      GError **error)
{
	GPtrArray *contacts = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	if (!e_m365_connection_get_people_internal_sync (cnc, user_override, TRUE, 1, &contacts, cancellable, error))
		return FALSE;

	g_clear_pointer (&contacts, g_ptr_array_unref);

	return TRUE;
}

gboolean
e_m365_connection_get_people_sync (EM365Connection *cnc,
				   const gchar *user_override, /* for which user, NULL to use the account user */
				   guint max_entries,
				   GPtrArray **out_contacts, /* EM365Contact * */
				   GCancellable *cancellable,
				   GError **error)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_contacts != NULL, FALSE);

	return e_m365_connection_get_people_internal_sync (cnc, user_override, FALSE, max_entries, out_contacts, cancellable, error);
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-calendargroups?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_calendar_groups_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     GSList **out_groups, /* EM365CalendarGroup * - the returned calendarGroup objects */
					     GCancellable *cancellable,
					     GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_groups != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_groups;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-calendargroups?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *name,
					      EM365CalendarGroup **out_created_group,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (out_created_group != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "name", name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_group, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_calendar_group_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *group_id,
					   EM365CalendarGroup **out_group,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);
	g_return_val_if_fail (out_group != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups",
		group_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_group, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *group_id,
					      const gchar *name,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups",
		group_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "name", name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *group_id,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", group_id, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendar?view=graph-rest-1.0 */

gboolean
e_m365_connection_list_calendars_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *group_id, /* nullable, calendar group id for group calendars */
				       const gchar *select, /* properties to select, nullable */
				       GSList **out_calendars, /* EM365Calendar * - the returned calendar objects */
				       GCancellable *cancellable,
				       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_calendars != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_calendars;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-post-calendars?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable, then the default group is used */
					JsonBuilder *calendar,
					EM365Calendar **out_created_calendar,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar != NULL, FALSE);
	g_return_val_if_fail (out_created_calendar != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		"calendars",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, calendar);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_calendar, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_calendar_folder_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *group_id, /* nullable - then the default group is used */
					    const gchar *calendar_id, /* nullable - then the default calendar is used */
					    const gchar *select, /* nullable - properties to select */
					    EM365Calendar **out_calendar,
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_calendar != NULL, FALSE);

	if (group_id && calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			"$select", select,
			NULL);
	} else if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "groups",
			group_id,
			"calendar",
			NULL,
			"$select", select,
			NULL);
	} else if (calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			"$select", select,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendar",
			NULL,
			NULL,
			"$select", select,
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_calendar, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable - then the default group is used */
					const gchar *calendar_id,
					const gchar *name, /* nullable - to keep the existing name */
					EM365CalendarColorType color,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);

	/* Nothing to change */
	if (!name && (color == E_M365_CALENDAR_COLOR_NOT_SET || color == E_M365_CALENDAR_COLOR_UNKNOWN))
		return TRUE;

	if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			NULL);
	}

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_calendar_add_name (builder, name);
	e_m365_calendar_add_color (builder, color);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable - then the default group is used */
					const gchar *calendar_id,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);

	if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/calendar-list-calendarpermissions?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_calendar_permissions_sync (EM365Connection *cnc,
						  const gchar *user_override, /* for which user, NULL to use the account user */
						  const gchar *group_id, /* nullable - calendar group for group calendars */
						  const gchar *calendar_id,
						  GSList **out_permissions, /* EM365CalendarPermission * */
						  GCancellable *cancellable,
						  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (out_permissions != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "calendarPermissions",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_permissions;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/calendar-post-calendarpermissions?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_calendar_permission_sync (EM365Connection *cnc,
						   const gchar *user_override, /* for which user, NULL to use the account user */
						   const gchar *group_id, /* nullable, then the default group is used */
						   const gchar *calendar_id,
						   JsonBuilder *permission,
						   EM365CalendarPermission **out_created_permission,
						   GCancellable *cancellable,
						   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (permission != NULL, FALSE);
	g_return_val_if_fail (out_created_permission != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "calendarPermissions",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, permission);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_permission, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/calendarpermission-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_calendar_permission_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						const gchar *group_id, /* nullable, then the default group is used */
						const gchar *calendar_id,
						const gchar *permission_id,
						EM365CalendarPermission **out_permission,
						GCancellable *cancellable,
						GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (permission_id != NULL, FALSE);
	g_return_val_if_fail (out_permission != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "calendarPermissions",
		"", permission_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_permission, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/calendarpermission-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_calendar_permission_sync (EM365Connection *cnc,
						   const gchar *user_override, /* for which user, NULL to use the account user */
						   const gchar *group_id, /* nullable - then the default group is used */
						   const gchar *calendar_id,
						   const gchar *permission_id,
						   JsonBuilder *permission,
						   GCancellable *cancellable,
						   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (permission_id != NULL, FALSE);
	g_return_val_if_fail (permission != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "calendarPermissions",
		"", permission_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, permission);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/calendarpermission-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_calendar_permission_sync (EM365Connection *cnc,
						   const gchar *user_override, /* for which user, NULL to use the account user */
						   const gchar *group_id, /* nullable - then the default group is used */
						   const gchar *calendar_id,
						   const gchar *permission_id,
						   GCancellable *cancellable,
						   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (permission_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "calendarPermissions",
		"", permission_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

static void
m365_connection_prefer_outlook_timezone (SoupMessage *message,
					 const gchar *prefer_outlook_timezone)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	if (prefer_outlook_timezone && *prefer_outlook_timezone) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("outlook.timezone=\"%s\"", prefer_outlook_timezone);

		soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", prefer_value);

		g_free (prefer_value);
	}
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-events?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_events_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* nullable, calendar group id for group calendars */
				    const gchar *calendar_id,
				    const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				    const gchar *select, /* nullable - properties to select */
				    const gchar *filter, /* nullable - filter which events to list */
				    GSList **out_events, /* EM365Event * - the returned event objects */
				    GCancellable *cancellable,
				    GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (out_events != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"$select", select,
		"$filter", filter,
		select ? NULL : "$expand",
		select ? NULL : "singleValueExtendedProperties($filter=id eq '" E_M365_RECURRENCE_BLOB_NAME "')",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	m365_connection_prefer_outlook_timezone (message, prefer_outlook_timezone);

	soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", "outlook.body-content-type=\"text\"");

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_events;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-events?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/group-post-events?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable, then the default group is used */
				     const gchar *calendar_id, /* nullable, then the default user calendar is used */
				     JsonBuilder *event,
				     EM365Event **out_created_event,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (out_created_event != NULL, FALSE);

	if (calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			group_id ? "calendarGroups" : "calendars",
			group_id,
			group_id ? "calendars" : NULL,
			"", calendar_id,
			"", "events",
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "users",
			"events", NULL, NULL, NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, event);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_event, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0&tabs=http */

SoupMessage *
e_m365_connection_prepare_get_event (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable, then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				     const gchar *select, /* nullable - properties to select */
				     GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (calendar_id != NULL, NULL);
	g_return_val_if_fail (event_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"$select", select,
		select ? NULL : "$expand",
		select ? NULL : "singleValueExtendedProperties($filter=id eq '" E_M365_RECURRENCE_BLOB_NAME "')",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	m365_connection_prefer_outlook_timezone (message, prefer_outlook_timezone);
	soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", "outlook.body-content-type=\"text\"");

	return message;
}

gboolean
e_m365_connection_get_event_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  const gchar *group_id, /* nullable, then the default group is used */
				  const gchar *calendar_id,
				  const gchar *event_id,
				  const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				  const gchar *select, /* nullable - properties to select */
				  EM365Event **out_event,
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (out_event != NULL, FALSE);

	message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, event_id, prefer_outlook_timezone, select, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_event, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_events_sync (EM365Connection *cnc,
				   const gchar *user_override, /* for which user, NULL to use the account user */
				   const gchar *group_id, /* nullable, then the default group is used */
				   const gchar *calendar_id,
				   const GSList *event_ids, /* const gchar * */
				   const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				   const gchar *select, /* nullable - properties to select */
				   GSList **out_events, /* EM365Event *, in the same order as event_ids; can return partial list */
				   GCancellable *cancellable,
				   GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_ids != NULL, FALSE);
	g_return_val_if_fail (out_events != NULL, FALSE);

	if (g_slist_next (event_ids)) {
		GPtrArray *requests;
		GSList *link;
		guint total, done = 0;

		total = g_slist_length ((GSList *) event_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) event_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, id, prefer_outlook_timezone, select, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					EM365Event *event = NULL;

					success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &event, cancellable, error);

					if (success)
						*out_events = g_slist_prepend (*out_events, event);
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

					if (success) {
						guint ii;

						for (ii = 0; ii < requests->len && success; ii++) {
							JsonNode *node = NULL;

							message = requests->pdata[ii];
							success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

							if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
								JsonObject *response;

								response = json_node_get_object (node);

								if (response) {
									*out_events = g_slist_prepend (*out_events, json_object_ref (response));
								} else {
									success = FALSE;
								}
							} else {
								success = FALSE;
							}

							if (node)
								json_node_unref (node);
						}
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, event_ids->data, prefer_outlook_timezone, select, error);

		if (message) {
			EM365Event *event = NULL;

			success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &event, cancellable, error);

			if (success)
				*out_events = g_slist_prepend (*out_events, event);

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	*out_events = g_slist_reverse (*out_events);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/event-list-instances?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_event_instance_id_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *group_id, /* nullable, then the default group is used */
					      const gchar *calendar_id,
					      const gchar *event_id,
					      ICalTime *instance_time,
					      gchar **out_instance_id,
					      GCancellable *cancellable,
					      GError **error)
{
	EM365ResponseData rd;
	GSList *items = NULL;
	SoupMessage *message;
	gchar *uri, *start_date_time, *end_date_time;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (instance_time != NULL, FALSE);
	g_return_val_if_fail (out_instance_id != NULL, FALSE);

	start_date_time = g_strdup_printf ("%04d-%02d-%02dT00:00:00",
		i_cal_time_get_year (instance_time),
		i_cal_time_get_month (instance_time),
		i_cal_time_get_day (instance_time));
	end_date_time = g_strdup_printf ("%04d-%02d-%02dT23:59:59.999",
		i_cal_time_get_year (instance_time),
		i_cal_time_get_month (instance_time),
		i_cal_time_get_day (instance_time));

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "instances",
		"$select", "id,start",
		"startDateTime", start_date_time,
		"endDateTime", end_date_time,
		NULL);

	g_free (start_date_time);
	g_free (end_date_time);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	*out_instance_id = NULL;

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = &items;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	if (success && items) {
		EM365Event *event;

		/* more than one event returned; unlikely, but just in case */
		if (items->next) {
			time_t instance_tt = i_cal_time_as_timet (instance_time);
			GSList *link;

			for (link = items; link; link = g_slist_next (link)) {
				EM365DateTimeWithZone *start_datetime;

				event = link->data;
				if (event)
					continue;

				start_datetime = e_m365_event_get_start (event);
				if (!start_datetime)
					continue;

				/* this is going to fail with timezones */
				if (instance_tt == e_m365_date_time_get_date_time (start_datetime)) {
					*out_instance_id = g_strdup (e_m365_event_get_id (event));
					break;
				}
			}
		} else if (items->data) {
			/* easy case, only one hit found */
			event = items->data;
			*out_instance_id = g_strdup (e_m365_event_get_id (event));
		}
	}

	if (success && !*out_instance_id) {
		gchar *str = i_cal_time_as_ical_string (instance_time);

		/* Translators: the '%s' is replaced with the date/time, which the instance was looked up for */
		g_set_error (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND, _("Cannot find instance at '%s'"), str);
		success = FALSE;

		g_free (str);
	}

	g_clear_object (&message);
	g_slist_free_full (items, (GDestroyNotify) json_object_unref);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable - then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     JsonBuilder *event,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, event);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable - then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-accept?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/event-tentativelyaccept?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/event-decline?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_response_event_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *group_id, /* nullable - then the default group is used */
				       const gchar *calendar_id,
				       const gchar *event_id,
				       EM365ResponseType response, /* uses only accepted/tentatively accepted/declined values */
				       const gchar *comment, /* nullable */
				       gboolean send_response,
				       GCancellable *cancellable,
				       GError **error)
{
	JsonBuilder *builder;
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (response == E_M365_RESPONSE_ACCEPTED || response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED || response == E_M365_RESPONSE_DECLINED, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED ? "tentativelyAccept" :
		    response == E_M365_RESPONSE_DECLINED ? "decline" : "accept",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_nonempty_string_member (builder, "comment", comment);
	e_m365_json_add_boolean_member (builder, "sendResponse", send_response);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/event-cancel?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_cancel_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable - then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     const gchar *comment, /* nullable */
				     GCancellable *cancellable,
				     GError **error)
{
	JsonBuilder *builder;
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "cancel",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_nonempty_string_member (builder, "comment", comment);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-dismissreminder?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_dismiss_reminder_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *group_id, /* nullable - then the default group is used */
					 const gchar *calendar_id,
					 const gchar *event_id,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "dismissReminder",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-list-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_event_attachments_sync (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *group_id, /* nullable, then the default group is used */
					       const gchar *calendar_id,
					       const gchar *event_id,
					       const gchar *select, /* nullable - properties to select */
					       GSList **out_attachments, /* EM365Attachment * */
					       GCancellable *cancellable,
					       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (out_attachments != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_attachments;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/attachment-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_event_attachment_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *group_id, /* nullable, then the default group is used */
					     const gchar *calendar_id,
					     const gchar *event_id,
					     const gchar *attachment_id,
					     EM365ConnectionRawDataFunc func,
					     gpointer func_user_data,
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (attachment_id != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"", attachment_id,
		"", "$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, func, func_user_data, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-post-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_add_event_attachment_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *group_id, /* nullable, then the default group is used */
					     const gchar *calendar_id, /* nullable, then the default user calendar is used */
					     const gchar *event_id,
					     JsonBuilder *in_attachment,
					     EM365Attachment **out_attachment, /* nullable */
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (in_attachment != NULL, FALSE);

	if (calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			group_id ? "calendarGroups" : "calendars",
			group_id,
			group_id ? "calendars" : NULL,
			"", calendar_id,
			"", "events",
			"", event_id,
			"", "attachments",
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "users",
			"events", NULL, NULL,
			"", event_id,
			"", "attachments",
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, out_attachment ? CSM_DEFAULT : CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_attachment);

	success = m365_connection_send_request_sync (cnc, message, out_attachment ? e_m365_read_json_object_response_cb : NULL,
		out_attachment ? NULL : e_m365_read_no_response_cb, out_attachment, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/attachment-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_event_attachment_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						const gchar *group_id, /* nullable, then the default group is used */
						const gchar *calendar_id,
						const gchar *event_id,
						const gchar *attachment_id,
						GCancellable *cancellable,
						GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (attachment_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"", attachment_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-getschedule?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_schedule_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     gint interval_minutes, /* between 5 and 1440, -1 to use the default (30) */
				     time_t start_time,
				     time_t end_time,
				     const GSList *email_addresses, /* const gchar * - SMTP addresses to query */
				     GSList **out_infos, /* EM365ScheduleInformation * */
				     GCancellable *cancellable,
				     GError **error)
{
	EM365ResponseData rd;
	JsonBuilder *builder;
	SoupMessage *message;
	GSList *link;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (email_addresses != NULL, FALSE);
	g_return_val_if_fail (out_infos != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendar",
		"getSchedule",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);

	if (interval_minutes > 0)
		e_m365_json_add_int_member (builder, "interval", interval_minutes);

	e_m365_add_date_time (builder, "startTime", start_time, "UTC");
	e_m365_add_date_time (builder, "endTime", end_time, "UTC");
	e_m365_json_begin_array_member (builder, "schedules");

	for (link = (GSList *) email_addresses; link; link = g_slist_next (link)) {
		const gchar *addr = link->data;

		if (addr && *addr)
			json_builder_add_string_value (builder, addr);
	}

	e_m365_json_end_array_member (builder); /* "schedules" */
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_infos;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todo-list-lists?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_task_lists_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					GSList **out_task_lists, /* EM365TaskList * - the returned todoTaskList objects */
					GCancellable *cancellable,
					GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_task_lists != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_task_lists;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todo-post-lists?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_task_list_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 JsonBuilder *task_list,
					 EM365TaskList **out_created_task_list,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list != NULL, FALSE);
	g_return_val_if_fail (out_created_task_list != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, task_list);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_task_list, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_task_list_sync (EM365Connection *cnc,
				      const gchar *user_override, /* for which user, NULL to use the account user */
				      const gchar *task_list_id,
				      EM365TaskList **out_task_list,
				      GCancellable *cancellable,
				      GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (out_task_list != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_task_list, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_task_list_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *task_list_id,
					 const gchar *display_name,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "displayName", display_name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_task_list_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *task_list_id,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-delta?view=graph-rest-1.0 */

gboolean
e_m365_connection_get_task_lists_delta_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *delta_link, /* previous delta link */
					     guint max_page_size, /* 0 for default by the server */
					     EM365ConnectionJsonFunc func, /* function to call with each result set */
					     gpointer func_user_data, /* user data passed into the 'func' */
					     gchar **out_delta_link,
					     GCancellable *cancellable,
					     GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link) {
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);
	} else {
		gchar *uri;

		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"todo",
			"lists",
			"delta",
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-list-tasks?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_tasks_sync (EM365Connection *cnc,
				   const gchar *user_override, /* for which user, NULL to use the account user */
				   const gchar *group_id, /* unused, always NULL */
				   const gchar *task_list_id,
				   const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				   const gchar *select, /* nullable - properties to select */
				   const gchar *filter, /* nullable - filter which tasks to list */
				   GSList **out_tasks, /* EM365Task * - the returned task objects */
				   GCancellable *cancellable,
				   GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (out_tasks != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"$select", select,
		"$filter", filter,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	m365_connection_prefer_outlook_timezone (message, prefer_outlook_timezone);
	soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", "outlook.body-content-type=\"text\"");

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_tasks;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotasklist-post-tasks?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_task_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* unused, always NULL */
				    const gchar *task_list_id,
				    JsonBuilder *task,
				    EM365Task **out_created_task,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (out_created_task != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, task);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_task, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-get?view=graph-rest-1.0&tabs=http */

SoupMessage *
e_m365_connection_prepare_get_task (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* unused, always NULL */
				    const gchar *task_list_id,
				    const gchar *task_id,
				    const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				    const gchar *select, /* nullable - properties to select */
				    GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (task_list_id != NULL, NULL);
	g_return_val_if_fail (task_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	m365_connection_prefer_outlook_timezone (message, prefer_outlook_timezone);
	soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", "outlook.body-content-type=\"text\"");

	return message;
}

gboolean
e_m365_connection_get_task_sync (EM365Connection *cnc,
				 const gchar *user_override, /* for which user, NULL to use the account user */
				 const gchar *group_id, /* unused, always NULL */
				 const gchar *task_list_id,
				 const gchar *task_id,
				 const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				 const gchar *select, /* nullable - properties to select */
				 EM365Task **out_task,
				 GCancellable *cancellable,
				 GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (out_task != NULL, FALSE);

	message = e_m365_connection_prepare_get_task (cnc, user_override, group_id, task_list_id, task_id, prefer_outlook_timezone, select, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_task, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_tasks_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  const gchar *group_id, /* unused, always NULL */
				  const gchar *task_list_id,
				  const GSList *task_ids, /* const gchar * */
				  const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				  const gchar *select, /* nullable - properties to select */
				  GSList **out_tasks, /* EM365Task *, in the same order as task_ids; can return partial list */
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_ids != NULL, FALSE);
	g_return_val_if_fail (out_tasks != NULL, FALSE);

	if (g_slist_next (task_ids)) {
		GPtrArray *requests;
		GSList *link;
		guint total, done = 0;

		total = g_slist_length ((GSList *) task_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) task_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_get_task (cnc, user_override, group_id, task_list_id, id, prefer_outlook_timezone, select, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					EM365Task *task = NULL;

					success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &task, cancellable, error);

					if (success)
						*out_tasks = g_slist_prepend (*out_tasks, task);
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

					if (success) {
						guint ii;

						for (ii = 0; ii < requests->len && success; ii++) {
							JsonNode *node = NULL;

							message = requests->pdata[ii];
							success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

							if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
								JsonObject *response;

								response = json_node_get_object (node);

								if (response) {
									*out_tasks = g_slist_prepend (*out_tasks, json_object_ref (response));
								} else {
									success = FALSE;
								}
							} else {
								success = FALSE;
							}

							if (node)
								json_node_unref (node);
						}
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_get_task (cnc, user_override, group_id, task_list_id, task_ids->data, prefer_outlook_timezone, select, error);

		if (message) {
			EM365Task *task = NULL;

			success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &task, cancellable, error);

			if (success)
				*out_tasks = g_slist_prepend (*out_tasks, task);

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	*out_tasks = g_slist_reverse (*out_tasks);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_task_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* unused, always NULL */
				    const gchar *task_list_id,
				    const gchar *task_id,
				    JsonBuilder *task,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (task != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, task);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_task_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* unused, always NULL */
				    const gchar *task_list_id,
				    const gchar *task_id,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-delta?view=graph-rest-1.0 */

gboolean
e_m365_connection_get_tasks_delta_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *task_list_id,
					const gchar *delta_link, /* previous delta link */
					guint max_page_size, /* 0 for default by the server */
					EM365ConnectionJsonFunc func, /* function to call with each result set */
					gpointer func_user_data, /* user data passed into the 'func' */
					gchar **out_delta_link,
					GCancellable *cancellable,
					GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link) {
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);
	} else {
		gchar *uri;

		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"todo",
			"lists",
			"task_list_id",
			"", "tasks",
			"", "delta",
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (soup_message_get_request_headers (message), "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-list-checklistitems?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_checklist_items_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *task_list_id,
					     const gchar *task_id,
					     const gchar *select, /* nullable - properties to select */
					     GSList **out_items, /* EM365ChecklistItem * */
					     GCancellable *cancellable,
					     GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (out_items != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "checklistItems",
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_items;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/checklistitem-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_checklist_item_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *task_list_id,
					   const gchar *task_id,
					   const gchar *item_id,
					   EM365ChecklistItem **out_item, /* nullable */
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (item_id != NULL, FALSE);
	g_return_val_if_fail (out_item != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "checklistItems",
		"", item_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_item, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-post-checklistitems?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_checklist_item_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *task_list_id,
					      const gchar *task_id,
					      JsonBuilder *in_item,
					      EM365ChecklistItem **out_item, /* nullable */
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (in_item != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "checklistItems",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, out_item ? CSM_DEFAULT : CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_item);

	success = m365_connection_send_request_sync (cnc, message, out_item ? e_m365_read_json_object_response_cb : NULL,
		out_item ? NULL : e_m365_read_no_response_cb, out_item, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/checklistitem-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_checklist_item_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *task_list_id,
					      const gchar *task_id,
					      const gchar *item_id,
					      JsonBuilder *in_item,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (item_id != NULL, FALSE);
	g_return_val_if_fail (in_item != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "checklistItems",
		"", item_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_item);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/checklistitem-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_checklist_item_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *task_list_id,
					      const gchar *task_id,
					      const gchar *item_id,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (item_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "checklistItems",
		"", item_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-list-linkedresources?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_linked_resources_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *task_list_id,
					      const gchar *task_id,
					      const gchar *select, /* nullable - properties to select */
					      GSList **out_resources, /* EM365LinkedResource * */
					      GCancellable *cancellable,
					      GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (out_resources != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "linkedResources",
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_resources;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/linkedresource-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_linked_resource_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *task_list_id,
					    const gchar *task_id,
					    const gchar *resource_id,
					    EM365LinkedResource **out_resource, /* nullable */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (resource_id != NULL, FALSE);
	g_return_val_if_fail (out_resource != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "linkedResources",
		"", resource_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_resource, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/todotask-post-linkedresources?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_linked_resource_sync (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *task_list_id,
					       const gchar *task_id,
					       JsonBuilder *in_resource,
					       EM365LinkedResource **out_resource, /* nullable */
					       GCancellable *cancellable,
					       GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (in_resource != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "linkedResources",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, out_resource ? CSM_DEFAULT : CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_resource);

	success = m365_connection_send_request_sync (cnc, message, out_resource ? e_m365_read_json_object_response_cb : NULL,
		out_resource ? NULL : e_m365_read_no_response_cb, out_resource, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/linkedresource-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_linked_resource_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *task_list_id,
					      const gchar *task_id,
					      const gchar *resource_id,
					      JsonBuilder *in_resource,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (resource_id != NULL, FALSE);
	g_return_val_if_fail (in_resource != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "linkedResources",
		"", resource_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_resource);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/linkedresource-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_linked_resource_sync (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *task_list_id,
					       const gchar *task_id,
					       const gchar *resource_id,
					       GCancellable *cancellable,
					       GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (task_list_id != NULL, FALSE);
	g_return_val_if_fail (task_id != NULL, FALSE);
	g_return_val_if_fail (resource_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"todo",
		"lists",
		task_list_id,
		"", "tasks",
		"", task_id,
		"", "linkedResources",
		"", resource_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/user-get-mailboxsettings?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_mailbox_settings_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     EM365MailboxSettings **out_mailbox_settings,
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_mailbox_settings != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "users",
		"mailboxSettings", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mailbox_settings, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/user-update-mailboxsettings?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_mailbox_settings_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						JsonBuilder *in_mailbox_settings,
						GCancellable *cancellable,
						GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (in_mailbox_settings != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "users",
		"mailboxSettings", NULL, NULL, NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_mailbox_settings);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://learn.microsoft.com/en-us/graph/api/user-get-mailboxsettings?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_automatic_replies_setting_sync (EM365Connection *cnc,
						      const gchar *user_override, /* for which user, NULL to use the account user */
						      EM365AutomaticRepliesSetting **out_automatic_replies_setting,
						      GCancellable *cancellable,
						      GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_automatic_replies_setting != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "users",
		"mailboxSettings", "automaticRepliesSetting", NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_automatic_replies_setting, cancellable, error);

	g_clear_object (&message);

	return success;
}
