/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_PART_EWS_SHARING_METADATA_H
#define E_MAIL_PART_EWS_SHARING_METADATA_H

#include <em-format/e-mail-part.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_PART_EWS_SHARING_METADATA \
	(e_mail_part_ews_sharing_metadata_get_type ())
#define E_MAIL_PART_EWS_SHARING_METADATA(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_PART_EWS_SHARING_METADATA, EMailPartEwsSharingMetadata))
#define E_MAIL_PART_EWS_SHARING_METADATA_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_PART_EWS_SHARING_METADATA, EMailPartEwsSharingMetadataClass))
#define E_IS_MAIL_PART_EWS_SHARING_METADATA(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_PART_EWS_SHARING_METADATA))
#define E_IS_MAIL_PART_EWS_SHARING_METADATA_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_PART_EWS_SHARING_METADATA))
#define E_MAIL_PART_EWS_SHARING_METADATA_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_PART_EWS_SHARING_METADATA, EMailPartEwsSharingMetadataClass))

G_BEGIN_DECLS

typedef struct _EMailPartEwsSharingMetadata EMailPartEwsSharingMetadata;
typedef struct _EMailPartEwsSharingMetadataClass EMailPartEwsSharingMetadataClass;
typedef struct _EMailPartEwsSharingMetadataPrivate EMailPartEwsSharingMetadataPrivate;

struct _EMailPartEwsSharingMetadata {
	EMailPart parent;

	gchar *xml;
};

struct _EMailPartEwsSharingMetadataClass {
	EMailPartClass parent_class;
};

GType		e_mail_part_ews_sharing_metadata_get_type	(void) G_GNUC_CONST;
void		e_mail_part_ews_sharing_metadata_type_register	(GTypeModule *type_module);
EMailPart *	e_mail_part_ews_sharing_metadata_new		(CamelMimePart *mime_part,
								 const gchar *id);

G_END_DECLS

#endif /* E_MAIL_PART_EWS_SHARING_METADATA_H */
