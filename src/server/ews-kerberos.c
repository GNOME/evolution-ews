/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ews-kerberos.h"
#include <krb5.h>

static krb5_context
ews_kerberos_context_new (const gchar *domain)
{
	krb5_context ctx;
	gchar *realm;

	if (krb5_init_context (&ctx) != 0)
		return NULL;

	realm = g_ascii_strup (domain, strlen (domain));
	krb5_set_default_realm (ctx, realm);
	g_free (realm);

	return ctx;
}

static EwsKerberosResult
krb5_result_to_ews_kerberos_result (gint result)
{
	switch (result) {
	case 0:
		return EWS_KERBEROS_OK;

	case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN:
		return EWS_KERBEROS_USER_UNKNOWN;

	case KRB5KRB_AP_ERR_BAD_INTEGRITY:
	case KRB5KDC_ERR_PREAUTH_FAILED:
	case KRB5KDC_ERR_CLIENT_REVOKED:
		return EWS_KERBEROS_PASSWORD_INCORRECT;

	case KRB5KDC_ERR_KEY_EXP:
		return EWS_KERBEROS_PASSWORD_EXPIRED;

	case KRB5_KDC_UNREACH:
		return EWS_KERBEROS_KDC_UNREACHABLE;

	case KRB5KRB_AP_ERR_SKEW:
		return EWS_KERBEROS_TIME_SKEW;

	default:
		g_warning ("Unexpected kerberos error %d", result);

	case KRB5_REALM_UNKNOWN:
		return EWS_KERBEROS_FAILED;
	}
}

static EwsKerberosResult
get_init_cred (krb5_context ctx,
               const gchar *usr_name,
               const gchar *passwd,
               const gchar *in_tkt_service,
               krb5_creds *cred)
{
	krb5_principal principal;
	krb5_get_init_creds_opt opt;
	krb5_error_code result;

	result = krb5_parse_name (ctx, usr_name, &principal);
	if (result)
		return EWS_KERBEROS_USER_UNKNOWN;

	krb5_get_init_creds_opt_init (&opt);
	krb5_get_init_creds_opt_set_tkt_life (&opt, 5 *60);
	krb5_get_init_creds_opt_set_renew_life (&opt, 0);
	krb5_get_init_creds_opt_set_forwardable (&opt, 0);
	krb5_get_init_creds_opt_set_proxiable (&opt, 0);

	result = krb5_get_init_creds_password (
		ctx, cred, principal,
		(gchar *) passwd,
		NULL, NULL, 0,
		(gchar *) in_tkt_service, &opt);
	krb5_free_principal (ctx, principal);

	return krb5_result_to_ews_kerberos_result (result);
}

/**
 * ews_kerberos_change_password
 * @user: username
 * @domain: domain name
 * @old_password: currrent password
 * @new_password: password to be changed to
 *
 * Changes the password for the given user
 *
 * Return value: an #EwsKerberosResult
 **/
EwsKerberosResult
ews_kerberos_change_password (const gchar *user,
                              const gchar *domain,
                              const gchar *old_password,
                              const gchar *new_password)
{
	krb5_context ctx;
	krb5_creds creds;
	krb5_data res_code_string, res_string;
	EwsKerberosResult result;
	gint res_code;

	ctx = ews_kerberos_context_new (domain);
	if (!ctx)
		return EWS_KERBEROS_FAILED;

	result = get_init_cred (
		ctx, user, old_password,
		"kadmin/changepw", &creds);
	if (result != EWS_KERBEROS_OK) {
		krb5_free_context (ctx);
		return result;
	}

	result = krb5_change_password (
		ctx, &creds, (gchar *) new_password,
		&res_code, &res_code_string, &res_string);

	g_print ("%s", res_code_string.data);
	krb5_free_cred_contents (ctx, &creds);
	krb5_free_data_contents (ctx, &res_code_string);
	krb5_free_data_contents (ctx, &res_string);
	krb5_free_context (ctx);

	if (result != 0)
		return krb5_result_to_ews_kerberos_result (result);
	else if (res_code != 0)
		return EWS_KERBEROS_FAILED;
	else
		return EWS_KERBEROS_OK;
	/*Check for res code and pop up res_string*/
}

/**
 * ews_kerberos_check_password:
 * @user: username
 * @domain: domain name
 * @password: current password
 *
 * Checks if the password is valid, invalid, or expired
 *
 * Return value: %EWS_KERBEROS_OK, %EWS_KERBEROS_USER_UNKNOWN,
 * %EWS_KERBEROS_PASSWORD_INCORRECT, %EWS_KERBEROS_PASSWORD_EXPIRED,
 * or %EWS_KERBEROS_FAILED (for unknown errors)
 **/
EwsKerberosResult
ews_kerberos_check_password (const gchar *user,
                             const gchar *domain,
                             const gchar *password)
{
	krb5_context ctx;
	krb5_creds creds;
	EwsKerberosResult result;

	ctx = ews_kerberos_context_new (domain);
	if (!ctx)
		return EWS_KERBEROS_FAILED;

	result = get_init_cred (ctx, user, password, NULL, &creds);

	krb5_free_context (ctx);
	if (result == EWS_KERBEROS_OK)
		krb5_free_cred_contents (ctx, &creds);

	return result;
}
