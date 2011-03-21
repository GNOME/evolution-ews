/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef EWS_SOAP_MESSAGE_H
#define EWS_SOAP_MESSAGE_H 1

#include <libedataserver/eds-version.h>

#include <time.h>
#include <libxml/tree.h>
#include <libsoup/soup-message.h>
#include "e-soap-response.h"

G_BEGIN_DECLS

#define E_TYPE_SOAP_MESSAGE            (e_soap_message_get_type ())
#define E_SOAP_MESSAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SOAP_MESSAGE, ESoapMessage))
#define E_SOAP_MESSAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SOAP_MESSAGE, ESoapMessageClass))
#define E_IS_SOAP_MESSAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOAP_MESSAGE))
#define E_IS_SOAP_MESSAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_SOAP_MESSAGE))
#define E_SOAP_MESSAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_SOAP_MESSAGE, ESoapMessageClass))

typedef struct {
	SoupMessage parent;

} ESoapMessage;

typedef struct {
	SoupMessageClass parent_class;
} ESoapMessageClass;

GType             e_soap_message_get_type (void);

ESoapMessage  *e_soap_message_new (const gchar *method, const gchar *uri_string,
					 gboolean standalone, const gchar *xml_encoding,
					 const gchar *env_prefix, const gchar *env_uri);
ESoapMessage  *e_soap_message_new_from_uri (const gchar *method, SoupURI *uri,
						  gboolean standalone, const gchar *xml_encoding,
						  const gchar *env_prefix, const gchar *env_uri);

void              e_soap_message_start_envelope (ESoapMessage *msg);
void              e_soap_message_end_envelope (ESoapMessage *msg);
void              e_soap_message_start_body (ESoapMessage *msg);
void              e_soap_message_end_body (ESoapMessage *msg);
void              e_soap_message_start_element (ESoapMessage *msg,
						   const gchar *name,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              e_soap_message_end_element (ESoapMessage *msg);
void              e_soap_message_start_fault (ESoapMessage *msg,
						 const gchar *faultcode,
						 const gchar *faultstring,
						 const gchar *faultfactor);
void              e_soap_message_end_fault (ESoapMessage *msg);
void              e_soap_message_start_fault_detail (ESoapMessage *msg);
void              e_soap_message_end_fault_detail (ESoapMessage *msg);
void              e_soap_message_start_header (ESoapMessage *msg);
void              e_soap_message_end_header (ESoapMessage *msg);
void              e_soap_message_start_header_element (ESoapMessage *msg,
							  const gchar *name,
							  gboolean must_understand,
							  const gchar *actor_uri,
							  const gchar *prefix,
							  const gchar *ns_uri);
void              e_soap_message_end_header_element (ESoapMessage *msg);
void              e_soap_message_write_int (ESoapMessage *msg, glong i);
void              e_soap_message_write_double (ESoapMessage *msg, gdouble d);
void              e_soap_message_write_base64 (ESoapMessage *msg, const gchar *string, gint len);
void              e_soap_message_write_time (ESoapMessage *msg, const time_t *timeval);
void              e_soap_message_write_string (ESoapMessage *msg, const gchar *string);
void              e_soap_message_write_buffer (ESoapMessage *msg, const gchar *buffer, gint len);
void              e_soap_message_set_element_type (ESoapMessage *msg, const gchar *xsi_type);
void              e_soap_message_set_null (ESoapMessage *msg);
void              e_soap_message_add_attribute (ESoapMessage *msg,
						   const gchar *name,
						   const gchar *value,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              e_soap_message_add_namespace (ESoapMessage *msg,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              e_soap_message_set_default_namespace (ESoapMessage *msg,
							   const gchar *ns_uri);
void              e_soap_message_set_encoding_style (ESoapMessage *msg, const gchar *enc_style);
void              e_soap_message_reset (ESoapMessage *msg);
void              e_soap_message_persist (ESoapMessage *msg);

const gchar       *e_soap_message_get_namespace_prefix (ESoapMessage *msg, const gchar *ns_uri);

xmlDocPtr         e_soap_message_get_xml_doc (ESoapMessage *msg);

ESoapResponse *e_soap_message_parse_response (ESoapMessage *msg);

/* By an amazing coincidence, this looks a lot like camel_progress() */
typedef void (*ESoapProgressFn) (gpointer object, gint percent);

void		  e_soap_message_set_progress_fn (ESoapMessage *msg,
						  ESoapProgressFn fn,
						  gpointer object);
G_END_DECLS

#endif
