/*
 * module-ews-mail-config.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
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
#include "e-ews-ooo-notificator.h"
#include "e-ews-config-lookup.h"
#include "e-ews-photo-source.h"

#include "e-ews-config-ui-extension.h"
#include "server/camel-sasl-xoauth2-office365.h"
#include "server/e-oauth2-service-office365.h"
#include "server/e-source-ews-folder.h"

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

