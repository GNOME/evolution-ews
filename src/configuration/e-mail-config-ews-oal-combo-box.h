/*
 * e-mail-config-ews-oal-combo-box.h
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

#ifndef E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_H
#define E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_H

#include <gtk/gtk.h>
#include <mail/e-mail-config-service-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX \
	(e_mail_config_ews_oal_combo_box_get_type ())
#define E_MAIL_CONFIG_EWS_OAL_COMBO_BOX(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX, EMailConfigEwsOalComboBox))
#define E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX, EMailConfigEwsOalComboBoxClass))
#define E_IS_MAIL_CONFIG_EWS_OAL_COMBO_BOX(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX))
#define E_IS_MAIL_CONFIG_EWS_OAL_COMBO_BOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX))
#define E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OAL_COMBO_BOX, EMailConfigEwsOalComboBoxClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsOalComboBox EMailConfigEwsOalComboBox;
typedef struct _EMailConfigEwsOalComboBoxClass EMailConfigEwsOalComboBoxClass;
typedef struct _EMailConfigEwsOalComboBoxPrivate EMailConfigEwsOalComboBoxPrivate;

struct _EMailConfigEwsOalComboBox {
	GtkComboBoxText parent;
	EMailConfigEwsOalComboBoxPrivate *priv;
};

struct _EMailConfigEwsOalComboBoxClass {
	GtkComboBoxTextClass parent_class;
};

GType		e_mail_config_ews_oal_combo_box_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_ews_oal_combo_box_type_register
						(GTypeModule *type_module);
GtkWidget *	e_mail_config_ews_oal_combo_box_new
						(EMailConfigServiceBackend *backend);
EMailConfigServiceBackend *
		e_mail_config_ews_oal_combo_box_get_backend
						(EMailConfigEwsOalComboBox *combo_box);
void		e_mail_config_ews_oal_combo_box_update
						(EMailConfigEwsOalComboBox *combo_box,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_mail_config_ews_oal_combo_box_update_finish
						(EMailConfigEwsOalComboBox *combo_box,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_OAL_COMBO_BOX_H */

