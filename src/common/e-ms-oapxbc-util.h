/*
 * SPDX-FileCopyrightText: (C) 2024 Siemens AG
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MS_OAPXBC_UTIL_H
#define E_MS_OAPXBC_UTIL_H

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

SoupCookie *	e_ms_oapxbc_util_get_prt_sso_cookie_sync	(EOAuth2Service *service,
								 ESource *source,
								 const gchar *client_id,
								 const gchar *sso_uri,
								 const gchar *redirect_uri,
								 const gchar *auth_user,
								 GCancellable *cancellable,
								 GError **error);

G_END_DECLS

#endif /* E_MS_OAPXBC_UTIL_H */
