/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_AUTOCONFIG_H__
#define __E2K_AUTOCONFIG_H__

#include "e2k-types.h"
#include "e2k-operation.h"
#include "e2k-validate.h"

G_BEGIN_DECLS

typedef enum {
	E2K_EXCHANGE_UNKNOWN,
	E2K_EXCHANGE_2000,
	E2K_EXCHANGE_2003,

	E2K_EXCHANGE_FUTURE
} E2kExchangeVersion;

typedef enum {
	E2K_AUTOCONFIG_USE_BASIC,
	E2K_AUTOCONFIG_USE_NTLM,
	E2K_AUTOCONFIG_USE_EITHER
} E2kAutoconfigAuthPref;

typedef struct {
	/* Input data. (gc_server is optional) */
	gchar *owa_uri, *gc_server;
	gchar *username, *password;
	gint gal_limit;
	E2kAutoconfigGalAuthPref gal_auth;

	/* Output data */
	E2kExchangeVersion version;
	gchar *display_name, *email;
	gchar *account_uri, *exchange_server;
	gchar *timezone;

	/* Private-ish members */
	gchar *nt_domain, *w2k_domain;
	gchar *home_uri, *exchange_dn;
	gchar *pf_server;
	E2kAutoconfigAuthPref auth_pref;
	gboolean require_ntlm, use_ntlm;
	gboolean saw_basic, saw_ntlm;
	gboolean nt_domain_defaulted, gc_server_autodetected;
} E2kAutoconfig;

E2kAutoconfig       *e2k_autoconfig_new                  (const gchar *owa_uri,
							  const gchar *username,
							  const gchar *password,
							  E2kAutoconfigAuthPref auth_pref);
void                 e2k_autoconfig_free                 (E2kAutoconfig *ac);

void                 e2k_autoconfig_set_owa_uri          (E2kAutoconfig *ac,
							  const gchar *owa_uri);
void                 e2k_autoconfig_set_gc_server        (E2kAutoconfig *ac,
							  const gchar *gc_server,
							  gint gal_limit,
							  E2kAutoconfigGalAuthPref gal_auth);
void                 e2k_autoconfig_set_username         (E2kAutoconfig *ac,
							  const gchar *username);
void                 e2k_autoconfig_set_password         (E2kAutoconfig *ac,
							  const gchar *password);

E2kContext          *e2k_autoconfig_get_context          (E2kAutoconfig *ac,
							  E2kOperation *op,
							  E2kAutoconfigResult *result);
E2kAutoconfigResult  e2k_autoconfig_check_exchange       (E2kAutoconfig *ac,
							  E2kOperation *op);
E2kGlobalCatalog    *e2k_autoconfig_get_global_catalog   (E2kAutoconfig *ac,
							  E2kOperation *op);
E2kAutoconfigResult  e2k_autoconfig_check_global_catalog (E2kAutoconfig *ac,
							  E2kOperation *op);

const gchar          *e2k_autoconfig_lookup_option        (const gchar *option);

G_END_DECLS

#endif /* __EXCHANGE_AUTOCONFIG_H__ */
