/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "e-cal-config-ews.h"
#include "e-book-config-ews.h"
#include "e-mail-config-ews-autodiscover.h"
#include "e-mail-config-ews-backend.h"
#include "e-mail-config-ews-gal.h"
#include "e-mail-config-ews-notebook.h"
#include "e-mail-config-ews-oal-combo-box.h"
#include "e-mail-config-ews-delegates-page.h"
#include "e-mail-config-ews-offline-options.h"
#include "e-mail-config-ews-ooo-page.h"
#include "e-mail-config-ews-folder-sizes-page.h"
#include "e-mail-formatter-ews-sharing-metadata.h"
#include "e-mail-parser-ews-multipart-mixed.h"
#include "e-mail-parser-ews-sharing-metadata.h"
#include "e-mail-part-ews-sharing-metadata.h"
#include "e-ews-ooo-notificator.h"
#include "e-ews-config-lookup.h"
#include "e-ews-photo-source.h"

#include "e-ews-comp-editor-extension.h"
#include "e-ews-composer-extension.h"
#include "e-ews-config-ui-extension.h"
#include "common/camel-sasl-xoauth2-office365.h"
#include "common/e-oauth2-service-office365.h"
#include "common/e-source-ews-folder.h"

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, EXCHANGE_EWS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_cal_config_ews_type_register (type_module);
	e_book_config_ews_type_register (type_module);
	e_mail_config_ews_autodiscover_type_register (type_module);
	e_mail_config_ews_backend_type_register (type_module);
	e_mail_config_ews_offline_options_type_register (type_module);
	e_mail_config_ews_gal_type_register (type_module);
	e_mail_config_ews_notebook_type_register (type_module);
	e_mail_config_ews_oal_combo_box_type_register (type_module);
	e_mail_config_ews_delegates_page_type_register (type_module);
	e_mail_config_ews_ooo_page_type_register (type_module);
	e_mail_config_ews_folder_sizes_page_type_register (type_module);
	e_mail_formatter_ews_sharing_metadata_type_register (type_module);
	e_mail_parser_ews_multipart_mixed_type_register (type_module);
	e_mail_parser_ews_sharing_metadata_type_register (type_module);
	e_mail_part_ews_sharing_metadata_type_register (type_module);
	e_ews_comp_editor_extension_type_register (type_module);
	e_ews_composer_extension_type_register (type_module);
	e_ews_config_lookup_type_register (type_module);
	e_ews_config_ui_extension_type_register (type_module);
	e_ews_ooo_notificator_type_register (type_module);
	e_ews_photo_source_type_register (type_module);
	camel_sasl_xoauth2_office365_type_register (type_module);
	e_oauth2_service_office365_type_register (type_module);

	e_source_ews_folder_type_register (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

