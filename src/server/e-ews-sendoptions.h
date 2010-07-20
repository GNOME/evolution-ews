/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_EWS_SENDOPTIONS_H
#define E_EWS_SENDOPTIONS_H

#include "soup-soap-response.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_SENDOPTIONS            (e_ews_sendoptions_get_type ())
#define E_EWS_SENDOPTIONS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_SENDOPTIONS, EEwsSendOptions))
#define E_EWS_SENDOPTIONS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_SENDOPTIONS, EEwsSendOptionsClass))
#define E_IS_GW_SENDOPTIONS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_SENDOPTIONS))
#define E_IS_GW_SENDOPTIONS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_SENDOPTIONS))

typedef struct _EEwsSendOptions        EEwsSendOptions;
typedef struct _EEwsSendOptionsClass   EEwsSendOptionsClass;
typedef struct _EEwsSendOptionsPrivate EEwsSendOptionsPrivate;

struct _EEwsSendOptions {
	GObject parent;
	EEwsSendOptionsPrivate *priv;
};

struct _EEwsSendOptionsClass {
	GObjectClass parent_class;
};
typedef enum {
	E_EWS_PRIORITY_UNDEFINED,
	E_EWS_PRIORITY_HIGH,
	E_EWS_PRIORITY_STANDARD,
	E_EWS_PRIORITY_LOW
} EEwsSendOptionsPriority;

typedef enum {
	E_EWS_SECURITY_NORMAL,
	E_EWS_SECURITY_PROPRIETARY,
	E_EWS_SECURITY_CONFIDENTIAL,
	E_EWS_SECURITY_SECRET,
	E_EWS_SECURITY_TOP_SECRET,
	E_EWS_SECURITY_FOR_YOUR_EYES_ONLY
} EEwsSendOptionsSecurity;

typedef enum {
	E_EWS_RETURN_NOTIFY_NONE,
	E_EWS_RETURN_NOTIFY_MAIL
} EEwsSendOptionsReturnNotify;

typedef enum {
	E_EWS_DELIVERED = 1,
	E_EWS_DELIVERED_OPENED = 2,
	E_EWS_ALL = 3
} EEwsTrackInfo;

typedef struct {
	EEwsSendOptionsPriority priority;
	gboolean reply_enabled;
	gboolean reply_convenient;
	gint reply_within;
	gboolean expiration_enabled;
	gint expire_after;
	gboolean delay_enabled;
	gint delay_until;
} EEwsSendOptionsGeneral;

typedef struct {
	gboolean tracking_enabled;
	EEwsTrackInfo track_when;
	gboolean autodelete;
	EEwsSendOptionsReturnNotify opened;
	EEwsSendOptionsReturnNotify accepted;
	EEwsSendOptionsReturnNotify declined;
	EEwsSendOptionsReturnNotify completed;
} EEwsSendOptionsStatusTracking;

GType e_ews_sendoptions_get_type (void);
EEwsSendOptions* e_ews_sendoptions_new_from_soap_parameter (SoupSoapParameter *param);
EEwsSendOptionsGeneral* e_ews_sendoptions_get_general_options (EEwsSendOptions *opts);
EEwsSendOptionsStatusTracking* e_ews_sendoptions_get_status_tracking_options (EEwsSendOptions *opts, const gchar *type);
gboolean e_ews_sendoptions_form_message_to_modify (SoupSoapMessage *msg, EEwsSendOptions *n_opts, EEwsSendOptions *o_opts);
EEwsSendOptions * e_ews_sendoptions_new (void);

#endif
