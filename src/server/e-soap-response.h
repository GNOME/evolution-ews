/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef E_SOAP_RESPONSE_H
#define E_SOAP_RESPONSE_H

#include <glib-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

#define E_TYPE_SOAP_RESPONSE            (e_soap_response_get_type ())
#define E_SOAP_RESPONSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SOAP_RESPONSE, ESoapResponse))
#define E_SOAP_RESPONSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SOAP_RESPONSE, ESoapResponseClass))
#define E_IS_SOAP_RESPONSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOAP_RESPONSE))
#define E_IS_SOAP_RESPONSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_SOAP_RESPONSE))
#define E_SOAP_RESPONSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_SOAP_RESPONSE, ESoapResponseClass))

typedef struct {
	GObject parent;

} ESoapResponse;

typedef struct {
	GObjectClass parent_class;
} ESoapResponseClass;

GType             e_soap_response_get_type (void);

ESoapResponse *e_soap_response_new (void);
ESoapResponse *e_soap_response_new_from_string (const gchar *xmlstr);

gboolean          e_soap_response_from_string (ESoapResponse *response, const gchar *xmlstr);

const gchar       *e_soap_response_get_method_name (ESoapResponse *response);
void              e_soap_response_set_method_name (ESoapResponse *response,
						      const gchar *method_name);

typedef xmlNode ESoapParameter;

const gchar        *e_soap_parameter_get_name (ESoapParameter *param);
gint                e_soap_parameter_get_int_value (ESoapParameter *param);
gchar              *e_soap_parameter_get_string_value (ESoapParameter *param);
ESoapParameter *e_soap_parameter_get_first_child (ESoapParameter *param);
ESoapParameter *e_soap_parameter_get_first_child_by_name (ESoapParameter *param,
								const gchar *name);
ESoapParameter *e_soap_parameter_get_next_child (ESoapParameter *param);
ESoapParameter *e_soap_parameter_get_next_child_by_name (ESoapParameter *param,
							       const gchar *name);
gchar              *e_soap_parameter_get_property (ESoapParameter *param, const gchar *prop_name);

const GList       *e_soap_response_get_parameters (ESoapResponse *response);
ESoapParameter *e_soap_response_get_first_parameter (ESoapResponse *response);
ESoapParameter *e_soap_response_get_first_parameter_by_name (ESoapResponse *response,
								   const gchar *name);
ESoapParameter *e_soap_response_get_next_parameter (ESoapResponse *response,
							  ESoapParameter *from);
ESoapParameter *e_soap_response_get_next_parameter_by_name (ESoapResponse *response,
								  ESoapParameter *from,
								  const gchar *name);

gint e_soap_response_dump_response (ESoapResponse *response, FILE *buffer);

G_END_DECLS

#endif
