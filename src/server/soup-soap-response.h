/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef SOUP_SOAP_RESPONSE_H
#define SOUP_SOAP_RESPONSE_H

#include <glib-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

#define SOUP_TYPE_SOAP_RESPONSE            (soup_soap_response_get_type ())
#define SOUP_SOAP_RESPONSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SOAP_RESPONSE, SoupSoapResponse))
#define SOUP_SOAP_RESPONSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SOAP_RESPONSE, SoupSoapResponseClass))
#define SOUP_IS_SOAP_RESPONSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SOAP_RESPONSE))
#define SOUP_IS_SOAP_RESPONSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_SOAP_RESPONSE))
#define SOUP_SOAP_RESPONSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_SOAP_RESPONSE, SoupSoapResponseClass))

typedef struct {
	GObject parent;

} SoupSoapResponse;

typedef struct {
	GObjectClass parent_class;
} SoupSoapResponseClass;

GType             soup_soap_response_get_type (void);

SoupSoapResponse *soup_soap_response_new (void);
SoupSoapResponse *soup_soap_response_new_from_string (const gchar *xmlstr);

gboolean          soup_soap_response_from_string (SoupSoapResponse *response, const gchar *xmlstr);

const gchar       *soup_soap_response_get_method_name (SoupSoapResponse *response);
void              soup_soap_response_set_method_name (SoupSoapResponse *response,
						      const gchar *method_name);

typedef xmlNode SoupSoapParameter;

const gchar        *soup_soap_parameter_get_name (SoupSoapParameter *param);
gint                soup_soap_parameter_get_int_value (SoupSoapParameter *param);
gchar              *soup_soap_parameter_get_string_value (SoupSoapParameter *param);
SoupSoapParameter *soup_soap_parameter_get_first_child (SoupSoapParameter *param);
SoupSoapParameter *soup_soap_parameter_get_first_child_by_name (SoupSoapParameter *param,
								const gchar *name);
SoupSoapParameter *soup_soap_parameter_get_next_child (SoupSoapParameter *param);
SoupSoapParameter *soup_soap_parameter_get_next_child_by_name (SoupSoapParameter *param,
							       const gchar *name);
gchar              *soup_soap_parameter_get_property (SoupSoapParameter *param, const gchar *prop_name);

const GList       *soup_soap_response_get_parameters (SoupSoapResponse *response);
SoupSoapParameter *soup_soap_response_get_first_parameter (SoupSoapResponse *response);
SoupSoapParameter *soup_soap_response_get_first_parameter_by_name (SoupSoapResponse *response,
								   const gchar *name);
SoupSoapParameter *soup_soap_response_get_next_parameter (SoupSoapResponse *response,
							  SoupSoapParameter *from);
SoupSoapParameter *soup_soap_response_get_next_parameter_by_name (SoupSoapResponse *response,
								  SoupSoapParameter *from,
								  const gchar *name);

gint soup_soap_response_dump_response (SoupSoapResponse *response, FILE *buffer);

G_END_DECLS

#endif
