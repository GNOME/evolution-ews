/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_VALIDATE_H_
#define __E2K_VALIDATE_H_

G_BEGIN_DECLS

typedef enum {
	E2K_AUTOCONFIG_USE_GAL_DEFAULT, /* try ntlm if available and then basic if not or failed */

	E2K_AUTOCONFIG_USE_GAL_BASIC,
	/*E2K_AUTOCONFIG_USE_GAL_SASL,*/
	E2K_AUTOCONFIG_USE_GAL_NTLM
} E2kAutoconfigGalAuthPref;

typedef struct {
        gchar *host;
        gchar *ad_server;
	E2kAutoconfigGalAuthPref ad_auth;
        gchar *mailbox;
        gchar *owa_path;
	gboolean is_ntlm;
}ExchangeParams;

typedef enum {
	E2K_AUTOCONFIG_OK,
	E2K_AUTOCONFIG_REDIRECT,
	E2K_AUTOCONFIG_TRY_SSL,
	E2K_AUTOCONFIG_AUTH_ERROR,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM,
	E2K_AUTOCONFIG_EXCHANGE_5_5,
	E2K_AUTOCONFIG_NOT_EXCHANGE,
	E2K_AUTOCONFIG_NO_OWA,
	E2K_AUTOCONFIG_NO_MAILBOX,
	E2K_AUTOCONFIG_CANT_BPROPFIND,
	E2K_AUTOCONFIG_CANT_RESOLVE,
	E2K_AUTOCONFIG_CANT_CONNECT,
	E2K_AUTOCONFIG_CANCELLED,
	E2K_AUTOCONFIG_FAILED
} E2kAutoconfigResult;

G_END_DECLS

#endif /* __EXCHANGE_VALIDATE_H_ */
