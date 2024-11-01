/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EWS_SOAP_REQUEST_H
#define EWS_SOAP_REQUEST_H

#include <time.h>
#include <libxml/tree.h>
#include <libsoup/soup-message.h>
#include <libedataserver/libedataserver.h>
#include "camel-ews-settings.h"
#include "e-soap-response.h"

/* Standard GObject macros */
#define E_TYPE_SOAP_REQUEST \
	(e_soap_request_get_type ())
#define E_SOAP_REQUEST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOAP_REQUEST, ESoapRequest))
#define E_SOAP_REQUEST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOAP_REQUEST, ESoapRequestClass))
#define E_IS_SOAP_REQUEST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOAP_REQUEST))
#define E_IS_SOAP_REQUEST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOAP_REQUEST))
#define E_SOAP_REQUEST_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOAP_REQUEST, ESoapRequestClass))

G_BEGIN_DECLS

typedef struct _ESoapRequest ESoapRequest;
typedef struct _ESoapRequestClass ESoapRequestClass;
typedef struct _ESoapRequestPrivate ESoapRequestPrivate;

typedef void	(* ESoapRequestCustomProcessFn) (ESoapRequest *request,
						 SoupMessage *message,
						 GInputStream *input_stream,
						 gpointer user_data,
						 gboolean *out_repeat,
						 GCancellable *cancellable,
						 GError **error);

struct _ESoapRequest {
	GObject parent;
	ESoapRequestPrivate *priv;
};

struct _ESoapRequestClass {
	GObjectClass parent_class;
};

GType		e_soap_request_get_type		(void) G_GNUC_CONST;
ESoapRequest *	e_soap_request_new		(const gchar *method,
						 const gchar *uri_string,
						 gboolean standalone,
						 const gchar *xml_encoding,
						 const gchar *env_prefix,
						 const gchar *env_uri,
						 GError **error);
ESoapRequest *	e_soap_request_new_from_uri	(const gchar *method,
						 GUri *uri,
						 gboolean standalone,
						 const gchar *xml_encoding,
						 const gchar *env_prefix,
						 const gchar *env_uri);
void		e_soap_request_start_envelope	(ESoapRequest *req);
void		e_soap_request_end_envelope	(ESoapRequest *req);
void		e_soap_request_start_body	(ESoapRequest *req);
void		e_soap_request_end_body		(ESoapRequest *req);
void		e_soap_request_start_element	(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *ns_uri);
void		e_soap_request_end_element	(ESoapRequest *req);
void		e_soap_request_start_fault	(ESoapRequest *req,
						 const gchar *faultcode,
						 const gchar *faultstring,
						 const gchar *faultfactor);
void		e_soap_request_end_fault	(ESoapRequest *req);
void		e_soap_request_start_fault_detail
						(ESoapRequest *req);
void		e_soap_request_end_fault_detail	(ESoapRequest *req);
void		e_soap_request_start_header	(ESoapRequest *req);
void		e_soap_request_replace_header	(ESoapRequest *req,
						 const gchar *name,
						 const gchar *value);
void		e_soap_request_end_header	(ESoapRequest *req);
void		e_soap_request_start_header_element
						(ESoapRequest *req,
						 const gchar *name,
						 gboolean must_understand,
						 const gchar *actor_uri,
						 const gchar *prefix,
						 const gchar *ns_uri);
void		e_soap_request_end_header_element
						(ESoapRequest *req);
void		e_soap_request_write_int	(ESoapRequest *req,
						 glong i);
void		e_soap_request_write_double	(ESoapRequest *req,
						 gdouble d);
void		e_soap_request_write_base64	(ESoapRequest *req,
						 const gchar *string,
						 gint len);
void		e_soap_request_write_time	(ESoapRequest *req,
						 time_t timeval);
void		e_soap_request_write_string	(ESoapRequest *req,
						 const gchar *string);
void		e_soap_request_write_buffer	(ESoapRequest *req,
						 const gchar *buffer,
						 gint len);
void		e_soap_request_set_element_type	(ESoapRequest *req,
						 const gchar *xsi_type);
void		e_soap_request_set_null		(ESoapRequest *req);
void		e_soap_request_add_attribute	(ESoapRequest *req,
						 const gchar *name,
						 const gchar *value,
						 const gchar *prefix,
						 const gchar *ns_uri);
void		e_soap_request_add_namespace	(ESoapRequest *req,
						 const gchar *prefix,
						 const gchar *ns_uri);
void		e_soap_request_set_default_namespace
						(ESoapRequest *req,
						 const gchar *ns_uri);
void		e_soap_request_set_encoding_style
						(ESoapRequest *req,
						 const gchar *enc_style);
void		e_soap_request_reset		(ESoapRequest *req);
const gchar *	e_soap_request_get_namespace_prefix
						(ESoapRequest *req,
						 const gchar *ns_uri);
xmlDocPtr	e_soap_request_get_xml_doc	(ESoapRequest *req);
void		e_soap_request_set_progress_fn	(ESoapRequest *req,
						 ESoapResponseProgressFn fn,
						 gpointer user_data);
void		e_soap_request_get_progress_fn	(ESoapRequest *req,
						 ESoapResponseProgressFn *out_fn,
						 gpointer *out_user_data);
void		e_soap_request_set_store_node_data
						(ESoapRequest *req,
						 const gchar *nodename,
						 const gchar *directory,
						 gboolean base64);
void		e_soap_request_get_store_node_data
						(ESoapRequest *req,
						 const gchar **out_nodename,
						 const gchar **out_directory,
						 gboolean *out_base64);
void		e_soap_request_set_custom_body	(ESoapRequest *req,
						 const gchar *content_type,
						 gconstpointer body,
						 gssize body_len);
void		e_soap_request_set_custom_process_fn
						(ESoapRequest *req,
						 ESoapRequestCustomProcessFn fn,
						 gpointer user_data);
void		e_soap_request_get_custom_process_fn
						(ESoapRequest *req,
						 ESoapRequestCustomProcessFn *out_fn,
						 gpointer *out_user_data);
void		e_soap_request_take_tls_error_details
						(ESoapRequest *req,
						 gchar *certificate_pem,
						 GTlsCertificateFlags certificate_errors);
gboolean	e_soap_request_get_tls_error_details
						(ESoapRequest *req,
						 const gchar **out_certificate_pem,
						 GTlsCertificateFlags *out_certificate_errors);
void		e_soap_request_set_etag		(ESoapRequest *req,
						 const gchar *etag);
const gchar *	e_soap_request_get_etag		(ESoapRequest *req);
SoupMessage *	e_soap_request_persist		(ESoapRequest *req,
						 ESoupSession *soup_session,
						 CamelEwsSettings *settings,
						 GError **error);
void		e_soap_request_setup_response	(ESoapRequest *req,
						 ESoapResponse *response);
G_END_DECLS

#endif /* E_SOAP_REQUEST_H */
