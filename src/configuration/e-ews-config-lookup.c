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

G_DEFINE_DYNAMIC_TYPE (EEwsConfigLookup, e_ews_config_lookup, E_TYPE_EXTENSION)

static void
ews_config_lookup_thread (EConfigLookup *config_lookup,
			  const ENamedParameters *params,
			  gpointer user_data,
			  GCancellable *cancellable)
{
	CamelEwsSettings *ews_settings = NULL;
	ESource *source;
	const gchar *email_address;
	const gchar *password;
	const gchar *extension_name;

	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));
	g_return_if_fail (params != NULL);

	email_address = e_named_parameters_get (params, E_CONFIG_LOOKUP_PARAM_EMAIL_ADDRESS);

	if (!email_address || !*email_address)
		return;

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

	if (password &&
	    e_ews_autodiscover_ws_url_sync (ews_settings, email_address, password, cancellable, NULL)) {
		const gchar *url;

		url = camel_ews_settings_get_hosturl (ews_settings);
		if (url && *url) {
			EConfigLookupResult *lookup_result;
			GString *description;
			gchar *tmp, *ptr, *user;
			SoupURI *suri;

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

			lookup_result = e_config_lookup_result_simple_new (E_CONFIG_LOOKUP_RESULT_COLLECTION,
				E_CONFIG_LOOKUP_RESULT_PRIORITY_IMAP - 100, /* This is better than IMAP */
				TRUE,
				"ews",
				_("Exchange Web Services"),
				description->str);

			e_config_lookup_result_simple_add_string (lookup_result, extension_name,
				"hosturl", camel_ews_settings_get_hosturl (ews_settings));

			e_config_lookup_result_simple_add_string (lookup_result, extension_name,
				"oaburl", camel_ews_settings_get_oaburl (ews_settings));

			if (user && *user) {
				e_config_lookup_result_simple_add_string (lookup_result,
					E_SOURCE_EXTENSION_AUTHENTICATION,
					"user", user);
			}

			if (suri && suri->host && *suri->host) {
				e_config_lookup_result_simple_add_string (lookup_result,
					E_SOURCE_EXTENSION_AUTHENTICATION,
					"host", suri->host);
			}

			e_config_lookup_add_result (config_lookup, lookup_result);

			g_string_free (description, TRUE);
			g_free (user);
			if (suri)
				soup_uri_free (suri);
		}
	}

	g_clear_object (&ews_settings);
}

static void
ews_config_lookup_run_cb (EConfigLookup *config_lookup,
			  const ENamedParameters *params,
			  EActivity *activity,
			  gpointer user_data)
{
	g_return_if_fail (E_IS_CONFIG_LOOKUP (config_lookup));
	g_return_if_fail (E_IS_EWS_CONFIG_LOOKUP (user_data));
	g_return_if_fail (E_IS_ACTIVITY (activity));

	e_config_lookup_create_thread (config_lookup, params, activity,
		ews_config_lookup_thread, NULL, NULL);
}

static void
ews_config_lookup_constructed (GObject *object)
{
	EConfigLookup *config_lookup;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_ews_config_lookup_parent_class)->constructed (object);

	config_lookup = E_CONFIG_LOOKUP (e_extension_get_extensible (E_EXTENSION (object)));

	g_signal_connect (config_lookup, "run",
		G_CALLBACK (ews_config_lookup_run_cb), object);
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
