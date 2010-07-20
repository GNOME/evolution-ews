/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef SOUP_SOAP_MESSAGE_H
#define SOUP_SOAP_MESSAGE_H 1

#include <time.h>
#include <libxml/tree.h>
#include <libsoup/soup-message.h>
#include "soup-soap-response.h"

G_BEGIN_DECLS

#define SOUP_TYPE_SOAP_MESSAGE            (soup_soap_message_get_type ())
#define SOUP_SOAP_MESSAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SOAP_MESSAGE, SoupSoapMessage))
#define SOUP_SOAP_MESSAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SOAP_MESSAGE, SoupSoapMessageClass))
#define SOUP_IS_SOAP_MESSAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SOAP_MESSAGE))
#define SOUP_IS_SOAP_MESSAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_SOAP_MESSAGE))
#define SOUP_SOAP_MESSAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_SOAP_MESSAGE, SoupSoapMessageClass))

typedef struct {
	SoupMessage parent;

} SoupSoapMessage;

typedef struct {
	SoupMessageClass parent_class;
} SoupSoapMessageClass;

GType             soup_soap_message_get_type (void);

SoupSoapMessage  *soup_soap_message_new (const gchar *method, const gchar *uri_string,
					 gboolean standalone, const gchar *xml_encoding,
					 const gchar *env_prefix, const gchar *env_uri);
SoupSoapMessage  *soup_soap_message_new_from_uri (const gchar *method, SoupURI *uri,
						  gboolean standalone, const gchar *xml_encoding,
						  const gchar *env_prefix, const gchar *env_uri);

void              soup_soap_message_start_envelope (SoupSoapMessage *msg);
void              soup_soap_message_end_envelope (SoupSoapMessage *msg);
void              soup_soap_message_start_body (SoupSoapMessage *msg);
void              soup_soap_message_end_body (SoupSoapMessage *msg);
void              soup_soap_message_start_element (SoupSoapMessage *msg,
						   const gchar *name,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              soup_soap_message_end_element (SoupSoapMessage *msg);
void              soup_soap_message_start_fault (SoupSoapMessage *msg,
						 const gchar *faultcode,
						 const gchar *faultstring,
						 const gchar *faultfactor);
void              soup_soap_message_end_fault (SoupSoapMessage *msg);
void              soup_soap_message_start_fault_detail (SoupSoapMessage *msg);
void              soup_soap_message_end_fault_detail (SoupSoapMessage *msg);
void              soup_soap_message_start_header (SoupSoapMessage *msg);
void              soup_soap_message_end_header (SoupSoapMessage *msg);
void              soup_soap_message_start_header_element (SoupSoapMessage *msg,
							  const gchar *name,
							  gboolean must_understand,
							  const gchar *actor_uri,
							  const gchar *prefix,
							  const gchar *ns_uri);
void              soup_soap_message_end_header_element (SoupSoapMessage *msg);
void              soup_soap_message_write_int (SoupSoapMessage *msg, glong i);
void              soup_soap_message_write_double (SoupSoapMessage *msg, gdouble d);
void              soup_soap_message_write_base64 (SoupSoapMessage *msg, const gchar *string, gint len);
void              soup_soap_message_write_time (SoupSoapMessage *msg, const time_t *timeval);
void              soup_soap_message_write_string (SoupSoapMessage *msg, const gchar *string);
void              soup_soap_message_write_buffer (SoupSoapMessage *msg, const gchar *buffer, gint len);
void              soup_soap_message_set_element_type (SoupSoapMessage *msg, const gchar *xsi_type);
void              soup_soap_message_set_null (SoupSoapMessage *msg);
void              soup_soap_message_add_attribute (SoupSoapMessage *msg,
						   const gchar *name,
						   const gchar *value,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              soup_soap_message_add_namespace (SoupSoapMessage *msg,
						   const gchar *prefix,
						   const gchar *ns_uri);
void              soup_soap_message_set_default_namespace (SoupSoapMessage *msg,
							   const gchar *ns_uri);
void              soup_soap_message_set_encoding_style (SoupSoapMessage *msg, const gchar *enc_style);
void              soup_soap_message_reset (SoupSoapMessage *msg);
void              soup_soap_message_persist (SoupSoapMessage *msg);

const gchar       *soup_soap_message_get_namespace_prefix (SoupSoapMessage *msg, const gchar *ns_uri);

xmlDocPtr         soup_soap_message_get_xml_doc (SoupSoapMessage *msg);

SoupSoapResponse *soup_soap_message_parse_response (SoupSoapMessage *msg);

G_END_DECLS

#endif
