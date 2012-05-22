/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __EWS_KERBEROS_H__
#define __EWS_KERBEROS_H__

G_BEGIN_DECLS

typedef enum {
	EWS_KERBEROS_OK,
	EWS_KERBEROS_USER_UNKNOWN,
	EWS_KERBEROS_PASSWORD_INCORRECT,
	EWS_KERBEROS_PASSWORD_EXPIRED,
	EWS_KERBEROS_PASSWORD_TOO_WEAK,

	EWS_KERBEROS_KDC_UNREACHABLE,
	EWS_KERBEROS_TIME_SKEW,

	EWS_KERBEROS_FAILED
} EwsKerberosResult;

EwsKerberosResult ews_kerberos_check_password  (const gchar *user,
						const gchar *domain,
						const gchar *password);

EwsKerberosResult ews_kerberos_change_password (const gchar *user,
						const gchar *domain,
						const gchar *old_password,
						const gchar *new_password);

G_END_DECLS

#endif /* __EWS_KERBEROS_H__ */
