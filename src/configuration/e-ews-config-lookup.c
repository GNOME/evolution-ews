/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <e-util/e-util.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"
#include "e-ews-config-lookup.h"

#define E_TYPE_EWS_CONFIG_LOOKUP_RESULT \
	(e_ews_config_lookup_result_get_type ())
#define E_EWS_CONFIG_LOOKUP_RESULT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_CONFIG_LOOKUP_RESULT, EEwsConfigLookupResult))
#define E_IS_EWS_CONFIG_LOOKUP_RESULT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_CONFIG_LOOKUP_RESULT))

typedef struct _EEwsConfigLookupResult EEwsConfigLookupResult;
typedef struct _EEwsConfigLookupResultClass EEwsConfigLookupResultClass;

struct _EEwsConfigLookupResult {
	/*< private >*/
	EConfigLookupResultSimple parent;

	gchar *host;
};

struct _EEwsConfigLookupResultClass {
	/*< private >*/
	EConfigLookupResultSimpleClass parent_class;
};

GType e_ews_config_lookup_result_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (EEwsConfigLookupResult, e_ews_config_lookup_result, E_TYPE_CONFIG_LOOKUP_RESULT_SIMPLE)

static void
ews_config_lookup_result_copy_authentication (ESourceAuthentication *src_authentication_extension,
					      ESource *des_source)
{
	ESourceAuthentication *des_authentication_extension;

	g_return_if_fail (E_IS_SOURCE_AUTHENTICATION (src_authentication_extension));
	g_return_if_fail (E_IS_SOURCE (des_source));

	des_authentication_extension = e_source_get_extension (des_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	e_source_authentication_set_host (des_authentication_extension,
		e_source_authentication_get_host (src_authentication_extension));

	e_source_authentication_set_port (des_authentication_extension,
		e_source_authentication_get_port (src_authentication_extension));

	e_source_authentication_set_user (des_authentication_extension,
		e_source_authentication_get_user (src_authentication_extension));
}

static gboolean
ews_config_lookup_result_configure_source (EConfigLookupResult *lookup_result,
					   EConfigLookup *config_lookup,
					   ESource *source)
{
	ESource *other_source;
	ESourceAuthentication *src_authentication_extension;
	ESourceMailAccount *mail_account_extension;
	ESourceMailTransport *mail_transport_extension;

	g_return_val_if_fail (E_IS_EWS_CONFIG_LOOKUP_RESULT (lookup_result), FALSE);
	g_return_val_if_fail (E_IS_CONFIG_LOOKUP (config_lookup), FALSE);

	/* Chain up to parent's method. */
	if (!E_CONFIG_LOOKUP_RESULT_SIMPLE_CLASS (e_ews_config_lookup_result_parent_class)->configure_source (lookup_result, config_lookup, source))
		return FALSE;

	src_authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	other_source = e_config_lookup_get_source (config_lookup, E_CONFIG_LOOKUP_SOURCE_MAIL_ACCOUNT);
	mail_account_extension = e_source_get_extension (other_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	ews_config_lookup_result_copy_authentication (src_authentication_extension, other_source);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_account_extension), "ews");

	other_source = e_config_lookup_get_source (config_lookup, E_CONFIG_LOOKUP_SOURCE_MAIL_TRANSPORT);
	mail_transport_extension = e_source_get_extension (other_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
	ews_config_lookup_result_copy_authentication (src_authentication_extension, other_source);
	e_source_backend_set_backend_name (E_SOURCE_BACKEND (mail_transport_extension), "ews");

	return TRUE;
}

static void
e_ews_config_lookup_result_class_init (EEwsConfigLookupResultClass *klass)
{
	EConfigLookupResultSimpleClass *simple_result_class;

	simple_result_class = E_CONFIG_LOOKUP_RESULT_SIMPLE_CLASS (klass);
	simple_result_class->configure_source = ews_config_lookup_result_configure_source;
}

static void
e_ews_config_lookup_result_init (EEwsConfigLookupResult *ews_result)
{
}

static EConfigLookupResult *
e_ews_config_lookup_result_new (EConfigLookupResultKind kind,
				gint priority,
				const gchar *protocol,
				const gchar *display_name,
				const gchar *description,
				const gchar *password)
{
	EEwsConfigLookupResult *ews_result;

	g_return_val_if_fail (protocol != NULL, NULL);
	g_return_val_if_fail (display_name != NULL, NULL);
	g_return_val_if_fail (description != NULL, NULL);

	ews_result = g_object_new (E_TYPE_EWS_CONFIG_LOOKUP_RESULT,
		"kind", kind,
		"priority", priority,
		"is-complete", TRUE,
		"protocol", protocol,
		"display-name", display_name,
		"description", description,
		"password", password,
		NULL);

	return E_CONFIG_LOOKUP_RESULT (ews_result);
}

/* ------------------------------------------------------------------------- */

/* Standard GObject macros */
#define E_TYPE_EWS_CONFIG_LOOKUP \
	(e_ews_config_lookup_get_type ())
#define E_EWS_CONFIG_LOOKUP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_CONFIG_LOOKUP, EEwsConfigLookup))
#define E_EWS_CONFIG_LOOKUP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_CONFIG_LOOKUP, EEwsConfigLookupClass))
#define E_IS_EWS_CONFIG_LOOKUP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_CONFIG_LOOKUP))
#define E_IS_EWS_CONFIG_LOOKUP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_CONFIG_LOOKUP))
#define E_EWS_CONFIG_LOOKUP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_CONFIG_LOOKUP, EEwsConfigLookupClass))

typedef struct _EEwsConfigLookup EEwsConfigLookup;
typedef struct _EEwsConfigLookupClass EEwsConfigLookupClass;

struct _EEwsConfigLookup {
	EExtension parent;
};

struct _EEwsConfigLookupClass {
	EExtensionClass parent_class;
};

GType e_ews_config_lookup_get_type (void) G_GNUC_CONST;

static void ews_config_lookup_worker_iface_init (EConfigLookupWorkerInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEwsConfigLookup, e_ews_config_lookup, E_TYPE_EXTENSION, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_CONFIG_LOOKUP_WORKER, ews_config_lookup_worker_iface_init))

static const gchar *
ews_config_lookup_worker_get_display_name (EConfigLookupWorker *worker)
{
	return _("Exchange Web Services autodiscovery");
}

static void
ews_config_lookup_worker_result_from_settings (EConfigLookupWorker *lookup_worker,
					       EConfigLookup *config_lookup,
					       const gchar *email_address,
					       CamelEwsSettings *ews_settings,
					       const ENamedParameters *params)
{
	const gchar *url;

	g_return_if_fail (E_IS_EWS_CONFIG_LOOKUP (lookup_worker));
	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (ews_settings));

	url = camel_ews_settings_get_hosturl (ews_settings);
	if (url && *url) {
		EConfigLookupResult *lookup_result;
		GString *description;
		gchar *tmp, *ptr, *user;
		const gchar *extension_name;
		SoupURI *suri;

		extension_name = e_source_camel_get_extension_name ("ews");

		tmp = g_strdup (email_address);
		ptr = tmp ? strchr (tmp, '@') : NULL;
		if (ptr)
			*ptr = '\0';

		if (!tmp || !*tmp) {
			g_free (tmp);
			tmp = NULL;
		}

		user = tmp;

		suri = soup_uri_new (url);

		description = g_string_new ("");

		if (user && *user)
			g_string_append_printf (description, _("User: %s"), user);

		if (description->len)
			g_string_append_c (description, '\n');

		g_string_append_printf (description, _("Host URL: %s"), url);

		url = camel_ews_settings_get_oaburl (ews_settings);
		if (url && *url) {
			g_string_append_c (description, '\n');
			g_string_append_printf (description, _("OAB URL: %s"), url);
		}

		lookup_result = e_ews_config_lookup_result_new (E_CONFIG_LOOKUP_RESULT_COLLECTION,
			E_CONFIG_LOOKUP_RESULT_PRIORITY_IMAP - 100, /* This is better than IMAP */
			"ews",
			_("Exchange Web Services"),
			description->str,
			params && e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_PASSWORD) &&
			e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_REMEMBER_PASSWORD) ?
			e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_PASSWORD) : NULL);

		e_config_lookup_result_simple_add_string (lookup_result, E_SOURCE_EXTENSION_COLLECTION,
			"backend-name", "ews");

		e_config_lookup_result_simple_add_string (lookup_result, extension_name,
			"hosturl", camel_ews_settings_get_hosturl (ews_settings));

		e_config_lookup_result_simple_add_string (lookup_result, extension_name,
			"oaburl", camel_ews_settings_get_oaburl (ews_settings));

		if (user && *user) {
			e_config_lookup_result_simple_add_string (lookup_result,
				E_SOURCE_EXTENSION_COLLECTION,
				"identity", user);

			e_config_lookup_result_simple_add_string (lookup_result,
				E_SOURCE_EXTENSION_AUTHENTICATION,
				"user", user);
		}

		if (suri && suri->host && *suri->host) {
			e_config_lookup_result_simple_add_string (lookup_result,
				E_SOURCE_EXTENSION_AUTHENTICATION,
				"host", suri->host);
		}

		if (suri && suri->port) {
			e_config_lookup_result_simple_add_uint (lookup_result,
				E_SOURCE_EXTENSION_AUTHENTICATION,
				"port", suri->port);
		}

		e_config_lookup_add_result (config_lookup, lookup_result);

		g_string_free (description, TRUE);
		g_free (user);
		if (suri)
			soup_uri_free (suri);
	}
}

static void
ews_config_lookup_worker_run (EConfigLookupWorker *lookup_worker,
			      EConfigLookup *config_lookup,
			      const ENamedParameters *params,
			      ENamedParameters **out_restart_params,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelEwsSettings *ews_settings = NULL;
	ESource *source;
	const gchar *email_address;
	const gchar *password;
	const gchar *extension_name;

	g_return_if_fail (E_IS_EWS_CONFIG_LOOKUP (lookup_worker));
	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));
	g_return_if_fail (params != NULL);

	email_address = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_EMAIL_ADDRESS);

	if (!email_address || !*email_address)
		return;

	if (!e_named_parameters_exists (params, E_CONFIG_LOOKUP_PARAM_PASSWORD)) {
		g_set_error (error, E_CONFIG_LOOKUP_WORKER_ERROR, E_CONFIG_LOOKUP_WORKER_ERROR_REQUIRES_PASSWORD,
			_("Requires user password to continue"));
		return;
	}

	ews_settings = g_object_new (CAMEL_TYPE_EWS_SETTINGS, NULL);
	camel_ews_settings_set_email (ews_settings, email_address);

	extension_name = e_source_camel_get_extension_name ("ews");
	source = e_config_lookup_get_source (config_lookup, E_CONFIG_LOOKUP_SOURCE_COLLECTION);
	if (source && e_source_has_extension (source, extension_name)) {
		ESourceCamel *camel_extension;
		CamelSettings *settings;

		camel_extension = e_source_get_extension (source, extension_name);
		settings = e_source_camel_get_settings (camel_extension);

		/* Copy only the host url */
		if (CAMEL_IS_EWS_SETTINGS (settings))
			camel_ews_settings_set_hosturl (ews_settings, camel_ews_settings_get_hosturl (CAMEL_EWS_SETTINGS (settings)));
	}

	password = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_PASSWORD);

	if (password) {
		const gchar *servers;

		if (e_ews_autodiscover_ws_url_sync (source, ews_settings, email_address, password, cancellable, NULL)) {
			ews_config_lookup_worker_result_from_settings (lookup_worker, config_lookup, email_address, ews_settings, params);
		}

		servers = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_SERVERS);

		if (servers && *servers) {
			gchar **servers_strv;
			gint ii;

			servers_strv = g_strsplit (servers, ";", 0);

			for (ii = 0; servers_strv && servers_strv[ii] && !g_cancellable_is_cancelled (cancellable); ii++) {
				const gchar *server = servers_strv[ii];
				gchar *tmp = NULL;

				if (*server && !strstr (server, "://")) {
					tmp = g_strconcat ("https://", server, "/EWS/Exchange.asmx", NULL);
					server = tmp;
				}

				camel_ews_settings_set_hosturl (ews_settings, server);

				if (e_ews_autodiscover_ws_url_sync (source, ews_settings, email_address, password, cancellable, NULL)) {
					ews_config_lookup_worker_result_from_settings (lookup_worker, config_lookup, email_address, ews_settings, params);
				}

				g_free (tmp);
			}

			g_strfreev (servers_strv);
		}

		if (out_restart_params)
			*out_restart_params = e_named_parameters_new_clone (params);
	}

	g_clear_object (&ews_settings);
}

static void
ews_config_lookup_constructed (GObject *object)
{
	EConfigLookup *config_lookup;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_config_lookup_parent_class)->constructed (object);

	config_lookup = E_CONFIG_LOOKUP (e_extension_get_extensible (E_EXTENSION (object)));

	e_config_lookup_register_worker (config_lookup, E_CONFIG_LOOKUP_WORKER (object));
}

static void
e_ews_config_lookup_class_init (EEwsConfigLookupClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = ews_config_lookup_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CONFIG_LOOKUP;
}

static void
e_ews_config_lookup_class_finalize (EEwsConfigLookupClass *class)
{
}

static void
ews_config_lookup_worker_iface_init (EConfigLookupWorkerInterface *iface)
{
	iface->get_display_name = ews_config_lookup_worker_get_display_name;
	iface->run = ews_config_lookup_worker_run;
}

static void
e_ews_config_lookup_init (EEwsConfigLookup *extension)
{
}

void
e_ews_config_lookup_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_config_lookup_register_type (type_module);
}
