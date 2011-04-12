/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EXCHANGE_EWS_ACCOUNT_SETUP_H
#define EXCHANGE_EWS_ACCOUNT_SETUP_H

#include "exchange-ews-account-listener.h"

/* This definition should be in-sync with the definition in camel-ews-store.c */
#define EXCHANGE_EWS_PASSWORD_COMPONENT "ExchangeEWS"

#define EWS_URI_PREFIX   "ews://"
#define EWS_PREFIX_LENGTH 6

ExchangeEWSAccountListener *
exchange_ews_accounts_peek_config_listener (void);

#endif /* EXCHANGE_EWS_ACCOUNT_SETUP_H */
