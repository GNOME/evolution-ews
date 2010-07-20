/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Sankar P <psankar@novell.com>
 *  Shreyas Srinivasan <sshreyas@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include "e-ews-proxy.h"
#include "e-ews-message.h"

void
e_ews_proxy_construct_proxy_access_list (SoupSoapParameter *param, GList **proxy_list)
{
	/* parse the response and create the individual proxy accounts */
	SoupSoapParameter *subparam;
	SoupSoapParameter *type_param;
	SoupSoapParameter *individual_rights;
	gchar *value;

	*proxy_list = NULL;
	for (subparam = soup_soap_parameter_get_first_child_by_name (param, "entry");
			subparam != NULL;
			subparam = soup_soap_parameter_get_next_child_by_name (subparam, "entry")) {

		proxyHandler *aclInstance;
		aclInstance = (proxyHandler *) g_malloc0 (sizeof(proxyHandler));
		aclInstance->permissions = 0;
		aclInstance->flags = 0;
		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "email");
		value = NULL;
		if (type_param)	{
			value = soup_soap_parameter_get_string_value (type_param);
			aclInstance->proxy_email = value;
		}

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "displayName");
		value = NULL;
		if (type_param)	{
			value = soup_soap_parameter_get_string_value (type_param);
			aclInstance->proxy_name = value;
		}
		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "id");
		value = NULL;
		if (type_param)	{
			value = soup_soap_parameter_get_string_value (type_param);
			aclInstance->uniqueid = value;
		} else
			aclInstance->uniqueid = NULL;

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "mail");
		value = NULL;
		if (type_param)	{
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"read");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_MAIL_READ;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"write");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_MAIL_WRITE;
			}
		}

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "appointment");
		if (type_param) {
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"read");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_APPOINTMENT_READ;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"write");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_APPOINTMENT_WRITE;
			}
		}

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "task");
		if (type_param)	{
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"read");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_TASK_READ;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"write");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_TASK_WRITE;
			}
		}

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "note");
		if (type_param)	{
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"read");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_NOTES_READ;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"write");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_NOTES_WRITE;
			}
		}

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "misc");
		if (type_param)	{
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"alarms");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_GET_ALARMS;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"notify");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_GET_NOTIFICATIONS;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"setup");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_MODIFY_FOLDERS;
			}
			individual_rights= soup_soap_parameter_get_first_child_by_name (type_param,"readHidden");
			if (individual_rights) {
				aclInstance->permissions |= E_EWS_PROXY_READ_PRIVATE;
			}
		}

		*proxy_list = g_list_append(*proxy_list, aclInstance);
	}
}

void
e_ews_proxy_construct_proxy_list (SoupSoapParameter *param, GList **proxy_info)
{
	SoupSoapParameter *subparam;
	SoupSoapParameter *type_param;
	gchar *value;

	for (subparam = soup_soap_parameter_get_first_child_by_name (param, "proxy");
			subparam != NULL;
			subparam = soup_soap_parameter_get_next_child_by_name (subparam, "proxy"))
	{

		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "displayName");
		value = NULL;
		if (type_param)	{
			value = soup_soap_parameter_get_string_value (type_param);
			*proxy_info = g_list_append(*proxy_info, value);
		}
		type_param = soup_soap_parameter_get_first_child_by_name (subparam, "email");
		value = NULL;
		if (type_param)	{
			value = soup_soap_parameter_get_string_value (type_param);
			*proxy_info = g_list_append(*proxy_info, value);
		}
	}
}

static void
e_ews_proxy_form_soap_request_from_proxyHandler (SoupSoapMessage *msg, proxyHandler *new_proxy)
{
	gboolean added = FALSE;
	e_ews_message_write_string_parameter (msg, "email", NULL, new_proxy->proxy_email);
	e_ews_message_write_string_parameter (msg, "displayName", NULL, new_proxy->proxy_name);

	if (new_proxy->permissions & E_EWS_PROXY_MAIL_READ) {
		added = TRUE;
		soup_soap_message_start_element (msg, "mail", NULL, NULL);
		e_ews_message_write_int_parameter (msg, "read", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_MAIL_WRITE) {
		if (added == FALSE) {
			added=TRUE;
			soup_soap_message_start_element (msg, "mail", NULL, NULL);
		}
		e_ews_message_write_int_parameter (msg, "write", NULL, 1);
	}
	if (added == TRUE)
		soup_soap_message_end_element(msg);

	added = FALSE;
	if (new_proxy->permissions & E_EWS_PROXY_APPOINTMENT_READ) {
		added=TRUE;
		soup_soap_message_start_element (msg, "appointment", NULL, NULL);
		e_ews_message_write_int_parameter (msg, "read", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_APPOINTMENT_WRITE) {
		if (added == FALSE)
		{
			added=TRUE;
			soup_soap_message_start_element (msg, "appointment", NULL, NULL);
		}
		e_ews_message_write_int_parameter (msg, "write", NULL, 1);
	}
	if (added == TRUE)
		soup_soap_message_end_element  (msg);

	added = FALSE;
	if (new_proxy->permissions & E_EWS_PROXY_TASK_READ) {
		added=TRUE;
		soup_soap_message_start_element (msg, "task", NULL, NULL);
		e_ews_message_write_int_parameter (msg, "read", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_TASK_WRITE) {
		if (added == FALSE)
		{
			added=TRUE;
			soup_soap_message_start_element (msg, "task", NULL, NULL);
		}
		e_ews_message_write_int_parameter (msg, "write", NULL, 1);
	}
	if (added == TRUE)
		soup_soap_message_end_element(msg);

	added = FALSE;
	if (new_proxy->permissions & E_EWS_PROXY_NOTES_READ) {
		added=TRUE;
		soup_soap_message_start_element (msg, "note", NULL, NULL);
		e_ews_message_write_int_parameter (msg, "read", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_NOTES_WRITE) {
		if (added==FALSE)
		{
			added=TRUE;
			soup_soap_message_start_element (msg, "note", NULL, NULL);
		}
		e_ews_message_write_int_parameter (msg, "write", NULL, 1);
	}
	if (added == TRUE)
		soup_soap_message_end_element(msg);

	added = FALSE;
	if (new_proxy->permissions & E_EWS_PROXY_GET_ALARMS) {
		added=TRUE;
		soup_soap_message_start_element(msg,"misc",NULL,NULL);
		e_ews_message_write_int_parameter (msg, "alarms", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_GET_NOTIFICATIONS) {
		if (added!=TRUE)
		{
			added=TRUE;
			soup_soap_message_start_element(msg,"misc",NULL,NULL);
		}
		e_ews_message_write_int_parameter (msg, "notify", NULL, 1);
	}

	if (new_proxy->permissions & E_EWS_PROXY_MODIFY_FOLDERS) {
		if (added!=TRUE)
		{
			added=TRUE;
			soup_soap_message_start_element(msg,"misc",NULL,NULL);
		}
		e_ews_message_write_int_parameter (msg, "setup", NULL, 1);
	}
	if (new_proxy->permissions & E_EWS_PROXY_READ_PRIVATE) {
		if (added!=TRUE)
		{
			added=TRUE;
			soup_soap_message_start_element(msg,"misc",NULL,NULL);
		}
		e_ews_message_write_int_parameter (msg, "readHidden", NULL, 1);
	}
	if (added==TRUE)
		soup_soap_message_end_element(msg);

}

void
e_ews_proxy_form_proxy_add_msg (SoupSoapMessage *msg, proxyHandler *new_proxy)
{
	soup_soap_message_start_element (msg, "entry", NULL, NULL);

	e_ews_proxy_form_soap_request_from_proxyHandler (msg, new_proxy);
}

void
e_ews_proxy_form_proxy_remove_msg (SoupSoapMessage *msg, proxyHandler *removeProxy)
{
	e_ews_message_write_string_parameter (msg, "id", NULL, removeProxy->uniqueid);
}

void
e_ews_proxy_form_modify_proxy_msg (SoupSoapMessage *msg, proxyHandler *new_proxy)
{
	soup_soap_message_start_element (msg, "updates", NULL, NULL);

	soup_soap_message_start_element (msg, "delete", NULL, NULL);
	soup_soap_message_end_element (msg);

	soup_soap_message_start_element (msg, "add", NULL, NULL);
	e_ews_proxy_form_soap_request_from_proxyHandler (msg, new_proxy);
	soup_soap_message_end_element (msg);

	soup_soap_message_end_element (msg);
}

void
e_ews_proxy_parse_proxy_login_response (SoupSoapParameter *param, gint *permissions)
{
	SoupSoapParameter *subparam;
	SoupSoapParameter *individual_rights;

	*permissions = 0;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "mail");
	if (subparam) {
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"read");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_MAIL_READ;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"write");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_MAIL_WRITE;
		}
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "appointment");
	if (subparam) {
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"read");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_APPOINTMENT_READ;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"write");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_APPOINTMENT_WRITE;
		}
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "task");
	if (subparam)	{
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"read");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_TASK_READ;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"write");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_TASK_WRITE;
		}
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "note");
	if (subparam)	{
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"read");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_NOTES_READ;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"write");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_NOTES_WRITE;
		}
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "misc");
	if (subparam)	{
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"alarms");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_GET_ALARMS;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"notify");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_GET_NOTIFICATIONS;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"setup");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_MODIFY_FOLDERS;
		}
		individual_rights= soup_soap_parameter_get_first_child_by_name (subparam,"readHidden");
		if (individual_rights) {
			*permissions |= E_EWS_PROXY_READ_PRIVATE;
		}
	}
}
