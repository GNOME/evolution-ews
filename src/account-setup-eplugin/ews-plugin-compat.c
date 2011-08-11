/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <libedataserver/eds-version.h>
#include "ews-plugin-compat.h"

#if EDS_CHECK_VERSION(2,33,0)
#include <mail/e-mail-session.h>
#else
#include <mail/mail-session.h>
#endif

#if EDS_CHECK_VERSION(2,29,0)
#include <mail/e-mail-backend.h>
#include <shell/e-shell.h>
#endif

CamelSession *
ews_plugin_get_default_camel_session ()
{
#if EDS_CHECK_VERSION(2,33,0)
	EShell *shell;
	EShellBackend *shell_backend;
	CamelSession *session;

	shell = e_shell_get_default ();
	shell_backend = e_shell_get_backend_by_name (shell, "mail");
	session = (CamelSession *) e_mail_backend_get_session (E_MAIL_BACKEND (shell_backend));

	return session;
#else	
	return session;
#endif	
}


gboolean
ews_plugin_get_online_status (void)
{
#if EDS_CHECK_VERSION(2,29,0)
	EShell *shell;
	shell = e_shell_get_default ();
	
	return e_shell_get_online (shell);
#else
	return TRUE;
#endif	
}
