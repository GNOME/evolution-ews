/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_HTTP_UTILS_H__
#define __E2K_HTTP_UTILS_H__

#include "e2k-types.h"
#include <time.h>
#include <libsoup/soup-status.h>
#include <libsoup/soup-message-headers.h>

typedef guint E2kHTTPStatus;

time_t         e2k_http_parse_date      (const gchar *date);
E2kHTTPStatus  e2k_http_parse_status    (const gchar *status_line);

const gchar    *e2k_http_accept_language (void);

GSList        *e2k_http_get_headers     (SoupMessageHeaders *hdrs,
					 const gchar         *header_name);

#define E2K_HTTP_STATUS_IS_TRANSPORT_ERROR(status) SOUP_STATUS_IS_TRANSPORT_ERROR(status)
#define E2K_HTTP_CANCELLED                         SOUP_STATUS_CANCELLED
#define E2K_HTTP_CANT_RESOLVE                      SOUP_STATUS_CANT_RESOLVE
#define E2K_HTTP_CANT_CONNECT                      SOUP_STATUS_CANT_CONNECT
#define E2K_HTTP_SSL_FAILED                        SOUP_STATUS_SSL_FAILED
#define E2K_HTTP_IO_ERROR                          SOUP_STATUS_IO_ERROR
#define E2K_HTTP_MALFORMED                         SOUP_STATUS_MALFORMED

#define E2K_HTTP_STATUS_IS_INFORMATIONAL(status)   SOUP_STATUS_IS_INFORMATIONAL(status)
#define E2K_HTTP_CONTINUE                          100

#define E2K_HTTP_STATUS_IS_SUCCESSFUL(status)      SOUP_STATUS_IS_SUCCESSFUL(status)
#define E2K_HTTP_OK                                200
#define E2K_HTTP_CREATED                           201
#define E2K_HTTP_ACCEPTED                          202
#define E2K_HTTP_NO_CONTENT                        204
#define E2K_HTTP_MULTI_STATUS                      207

#define E2K_HTTP_STATUS_IS_REDIRECTION(status)     SOUP_STATUS_IS_REDIRECTION(status)

#define E2K_HTTP_STATUS_IS_CLIENT_ERROR(status)    SOUP_STATUS_IS_CLIENT_ERROR(status)
#define E2K_HTTP_BAD_REQUEST                       400
#define E2K_HTTP_UNAUTHORIZED                      401
#define E2K_HTTP_FORBIDDEN                         403
#define E2K_HTTP_NOT_FOUND                         404
#define E2K_HTTP_METHOD_NOT_ALLOWED                405
#define E2K_HTTP_CONFLICT                          409
#define E2K_HTTP_PRECONDITION_FAILED               412
#define E2K_HTTP_REQUESTED_RANGE_NOT_SATISFIABLE   416
#define E2K_HTTP_UNPROCESSABLE_ENTITY              422
#define E2K_HTTP_LOCKED                            423
#define E2K_HTTP_INSUFFICIENT_SPACE_ON_RESOURCE    425
#define E2K_HTTP_TIMEOUT                           440

#define E2K_HTTP_STATUS_IS_SERVER_ERROR(status)    SOUP_STATUS_IS_SERVER_ERROR(status)
#define E2K_HTTP_INTERNAL_SERVER_ERROR             500
#define E2K_HTTP_BAD_GATEWAY                       502

G_END_DECLS

#endif /* __E2K_HTTP_UTILS_H__ */
