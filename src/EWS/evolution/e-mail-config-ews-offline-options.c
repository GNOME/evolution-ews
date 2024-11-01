/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <libebackend/libebackend.h>
#include <libedataserver/libedataserver.h>

#include <e-util/e-util.h>
#include <mail/e-mail-config-provider-page.h>

#include "e-mail-config-ews-offline-options.h"

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS \
	(e_mail_config_ews_offline_options_get_type ())
#define E_MAIL_CONFIG_EWS_OFFLINE_OPTIONS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS, EMailConfigEwsOfflineOptions))
#define E_MAIL_CONFIG_EWS_OFFLINE_OPTIONS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS, EMailConfigEwsOfflineOptionsClass))
#define E_IS_MAIL_CONFIG_EWS_OFFLINE_OPTIONS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS))
#define E_IS_MAIL_CONFIG_EWS_OFFLINE_OPTIONS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS))
#define E_MAIL_CONFIG_EWS_OFFLINE_OPTIONS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OFFLINE_OPTIONS, EMailConfigEwsOfflineOptionsClass))

typedef struct _EMailConfigEwsOfflineOptions EMailConfigEwsOfflineOptions;
typedef struct _EMailConfigEwsOfflineOptionsClass EMailConfigEwsOfflineOptionsClass;

struct _EMailConfigEwsOfflineOptions {
	EExtension parent;
};

struct _EMailConfigEwsOfflineOptionsClass {
	EExtensionClass parent_class;
};

GType		e_mail_config_ews_offline_options_get_type
						(void) G_GNUC_CONST;

G_DEFINE_DYNAMIC_TYPE (EMailConfigEwsOfflineOptions, e_mail_config_ews_offline_options, E_TYPE_EXTENSION)

static void
mail_config_ews_offline_options_constructed (GObject *object)
{
	EMailConfigProviderPage *provider_page;
	EMailConfigServiceBackend *backend;
	CamelProvider *provider;
	CamelSettings *settings;
	GtkBox *placeholder;
	GtkWidget *hbox;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_offline_options_parent_class)->constructed (object);

	provider_page = E_MAIL_CONFIG_PROVIDER_PAGE (e_extension_get_extensible (E_EXTENSION (object)));
	backend = e_mail_config_provider_page_get_backend (provider_page);
	provider = e_mail_config_service_backend_get_provider (backend);
	settings = e_mail_config_service_backend_get_settings (backend);

	if (e_mail_config_provider_page_is_empty (provider_page) ||
	    !provider || g_strcmp0 (provider->protocol, "ews") != 0)
		return;

	g_return_if_fail (CAMEL_IS_OFFLINE_SETTINGS (settings));

	placeholder = e_mail_config_provider_page_get_placeholder (provider_page, "ews-limit-by-age-placeholder");
	g_return_if_fail (placeholder != NULL);

	hbox = e_dialog_offline_settings_new_limit_box (CAMEL_OFFLINE_SETTINGS (settings));
	gtk_box_pack_start (placeholder, hbox, FALSE, FALSE, 0);
	gtk_widget_show (hbox);
}

static void
e_mail_config_ews_offline_options_class_init (EMailConfigEwsOfflineOptionsClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = mail_config_ews_offline_options_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_MAIL_CONFIG_PROVIDER_PAGE;
}

static void
e_mail_config_ews_offline_options_class_finalize (EMailConfigEwsOfflineOptionsClass *class)
{
}

static void
e_mail_config_ews_offline_options_init (EMailConfigEwsOfflineOptions *extension)
{
}

void
e_mail_config_ews_offline_options_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_offline_options_register_type (type_module);
}
