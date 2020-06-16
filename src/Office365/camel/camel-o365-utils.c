/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <libemail-engine/libemail-engine.h>

#include "camel-o365-utils.h"

/* Unref with g_object_unref() when done with it */
static ESource *
camel_o365_utils_ref_corresponding_source (CamelService *service,
					   GCancellable *cancellable)
{
	ESourceRegistry *registry = NULL;
	CamelSession *session;
	ESource *source = NULL;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	session = camel_service_ref_session (service);
	if (E_IS_MAIL_SESSION (session)) {
		registry = e_mail_session_get_registry (E_MAIL_SESSION (session));
		if (registry)
			g_object_ref (registry);
	}

	g_clear_object (&session);

	if (!registry)
		registry = e_source_registry_new_sync (cancellable, NULL);

	if (registry) {
		source = e_source_registry_ref_source (registry, camel_service_get_uid (service));

		if (source) {
			ESource *parent;

			parent = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

			g_clear_object (&source);
			source = parent;
		}
	}

	g_clear_object (&registry);

	return source;
}

EO365Connection *
camel_o365_utils_new_connection (CamelService *service,
				 GCancellable *cancellable)
{
	CamelSettings *settings;
	EO365Connection *cnc;
	ESource *source;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	source = camel_o365_utils_ref_corresponding_source (service, cancellable);

	if (!source)
		return NULL;

	settings = camel_service_ref_settings (service);

	cnc = e_o365_connection_new (source, CAMEL_O365_SETTINGS (settings));

	e_binding_bind_property (
		service, "proxy-resolver",
		cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	g_clear_object (&settings);
	g_clear_object (&source);

	return cnc;
}
