/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib-object.h>

#include <composer/e-msg-composer.h>
#include <composer/e-composer-from-header.h>

#include "e-ews-composer-extension.h"

#define E_TYPE_EWS_COMPOSER_EXTENSION (e_ews_composer_extension_get_type ())

GType e_ews_composer_extension_get_type (void);

typedef struct _EEwsComposerExtension {
	EExtension parent;
} EEwsComposerExtension;

typedef struct _EEwsComposerExtensionClass {
	EExtensionClass parent_class;
} EEwsComposerExtensionClass;

G_DEFINE_DYNAMIC_TYPE (EEwsComposerExtension, e_ews_composer_extension, E_TYPE_EXTENSION)

static gboolean
e_ews_composer_extension_is_ews_transport (ESourceRegistry *registry,
					   const gchar *uid)
{
	ESource *source;
	gboolean is_ews_transport = FALSE;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	source = e_source_registry_ref_source (registry, uid);
	if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION)) {
		ESourceMailSubmission *mail_submission;
		const gchar *transport_uid;

		mail_submission = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
		transport_uid = e_source_mail_submission_get_transport_uid (mail_submission);

		if (transport_uid && *transport_uid) {
			ESource *transport_source;

			transport_source = e_source_registry_ref_source (registry, transport_uid);
			if (transport_source && e_source_has_extension (transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT)) {
				ESourceBackend *mail_transport;

				mail_transport = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
				is_ews_transport = g_strcmp0 ("ews", e_source_backend_get_backend_name (mail_transport)) == 0;
			}

			g_clear_object (&transport_source);
		}
	}

	g_clear_object (&source);

	return is_ews_transport;
}

static void
e_ews_composer_extension_from_changed_cb (EComposerHeader *from_header,
					  EComposerHeaderTable *header_table)
{
	EComposerHeader *subject_header;
	gchar *uid;
	gboolean is_ews_transport;

	uid = e_composer_from_header_dup_active_id (E_COMPOSER_FROM_HEADER (from_header), NULL, NULL);
	is_ews_transport = e_ews_composer_extension_is_ews_transport (e_composer_header_get_registry (from_header), uid);
	g_free (uid);

	subject_header = e_composer_header_table_get_header (header_table, E_COMPOSER_HEADER_SUBJECT);
	if (subject_header && GTK_IS_ENTRY (subject_header->input_widget))
		gtk_entry_set_max_length (GTK_ENTRY (subject_header->input_widget), is_ews_transport ? 255 : 0);
}

static void
e_ews_composer_extension_constructed (GObject *object)
{
	EExtensible *extensible;
	EComposerHeaderTable *header_table;
	EComposerHeader *header;

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_ews_composer_extension_parent_class)->constructed (object);

	extensible = e_extension_get_extensible (E_EXTENSION (object));
	header_table = e_msg_composer_get_header_table (E_MSG_COMPOSER (extensible));
	header = e_composer_header_table_get_header (header_table, E_COMPOSER_HEADER_FROM);

	g_signal_connect_object (header, "changed",
		G_CALLBACK (e_ews_composer_extension_from_changed_cb), header_table, 0);
}

static void
e_ews_composer_extension_class_init (EEwsComposerExtensionClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_ews_composer_extension_constructed;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_MSG_COMPOSER;
}

static void
e_ews_composer_extension_class_finalize (EEwsComposerExtensionClass *klass)
{
}

static void
e_ews_composer_extension_init (EEwsComposerExtension *extension)
{
}

void
e_ews_composer_extension_type_register (GTypeModule *type_module)
{
	e_ews_composer_extension_register_type (type_module);
}
