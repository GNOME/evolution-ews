/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EWS_HTTP_UTILS_H__
#define __EWS_HTTP_UTILS_H__

#include "e-ews-types.h"
#include <time.h>
#include <libsoup/soup-status.h>
#include <libsoup/soup-message-headers.h>

typedef guint EWSHTTPStatus;

#define EWS_HTTP_STATUS_IS_TRANSPORT_ERROR(status) SOUP_STATUS_IS_TRANSPORT_ERROR(status)
#define EWS_HTTP_CANCELLED                         SOUP_STATUS_CANCELLED
#define EWS_HTTP_CANT_RESOLVE                      SOUP_STATUS_CANT_RESOLVE
#define EWS_HTTP_CANT_CONNECT                      SOUP_STATUS_CANT_CONNECT
#define EWS_HTTP_SSL_FAILED                        SOUP_STATUS_SSL_FAILED
#define EWS_HTTP_IO_ERROR                          SOUP_STATUS_IO_ERROR
#define EWS_HTTP_MALFORMED                         SOUP_STATUS_MALFORMED

#define EWS_HTTP_STATUS_IS_INFORMATIONAL(status)   SOUP_STATUS_IS_INFORMATIONAL(status)
#define EWS_HTTP_CONTINUE                          100

#define EWS_HTTP_STATUS_IS_SUCCESSFUL(status)      SOUP_STATUS_IS_SUCCESSFUL(status)
#define EWS_HTTP_OK                                200
#define EWS_HTTP_CREATED                           201
#define EWS_HTTP_ACCEPTED                          202
#define EWS_HTTP_NO_CONTENT                        204
#define EWS_HTTP_MULTI_STATUS                      207

#define EWS_HTTP_STATUS_IS_REDIRECTION(status)     SOUP_STATUS_IS_REDIRECTION(status)

#define EWS_HTTP_STATUS_IS_CLIENT_ERROR(status)    SOUP_STATUS_IS_CLIENT_ERROR(status)
#define EWS_HTTP_BAD_REQUEST                       400
#define EWS_HTTP_UNAUTHORIZED                      401
#define EWS_HTTP_FORBIDDEN                         403
#define EWS_HTTP_NOT_FOUND                         404
#define EWS_HTTP_METHOD_NOT_ALLOWED                405
#define EWS_HTTP_CONFLICT                          409
#define EWS_HTTP_PRECONDITION_FAILED               412
#define EWS_HTTP_REQUESTED_RANGE_NOT_SATISFIABLE   416
#define EWS_HTTP_UNPROCESSABLE_ENTITY              422
#define EWS_HTTP_LOCKED                            423
#define EWS_HTTP_INSUFFICIENT_SPACE_ON_RESOURCE    425
#define EWS_HTTP_TIMEOUT                           440

#define EWS_HTTP_STATUS_IS_SERVER_ERROR(status)    SOUP_STATUS_IS_SERVER_ERROR(status)
#define EWS_HTTP_INTERNAL_SERVER_ERROR             500
#define EWS_HTTP_BAD_GATEWAY                       502

G_END_DECLS

#endif /* __EWS_HTTP_UTILS_H__ */
