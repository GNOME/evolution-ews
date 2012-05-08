/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Mandy Wu <mandy.wu@intel.com>
 *
 * Copyright © 2011 Intel Corporation. (www.intel.com)
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

/* APIs : e_ews_connection_create_items */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-item.h"
#include "server/e-ews-message.h"

#include "utils.h"

static void op_test_create_item ();
void cuditem_tests_run ();

static GMainLoop *main_loop;

static void
create_item_ready_callback (GObject *object,
                            GAsyncResult *res,
                            gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	GSList *ids = NULL, *l;

	e_ews_connection_create_items_finish	(cnc, res, &ids, &error);

	if (error != NULL) {
		g_print ("Unable to get item: %s :%d \n", error->message, error->code);
		goto quit;
	}

	for (l = ids; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = l->data;
		const EwsId *itemid = e_ews_item_get_id (item);
		g_print ("CreateItem: Id is %s \n", itemid->id);
		break;
	}

quit:
	g_main_loop_quit (main_loop);
}

static void
op_test_create_item_get_soap_msg (ESoapMessage *msg,
                                  gpointer user_data)
{
	xmlDocPtr doc;
	xmlChar *xmlbuff;
	gint size;

	//creating XML node for create item
	e_soap_message_start_element (msg, "CalendarItem", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "Subject", NULL,  "Test Summary");
	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, "Test Body", "BodyType", "Text");
	e_ews_message_write_string_parameter (msg, "Start", NULL, "2011-03-01T15:00:00");
	e_ews_message_write_string_parameter (msg, "End", NULL, "2011-03-01T16:00:00");
	e_ews_message_write_string_parameter (msg, "Location", NULL,  "Test Location");

	e_soap_message_end_element (msg);

	g_printf("\nSOAP Request message:\n");
	doc = e_soap_message_get_xml_doc (msg);
	xmlDocDumpFormatMemory (doc, &xmlbuff,&size,1);
	g_printf("%s\n", (gchar *)xmlbuff);
	xmlFree (xmlbuff);
}

static void
op_test_create_item ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	const gchar *folderid;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL, NULL, NULL);
	g_assert (cnc != NULL);

	//to check how to change to real data
	folderid = "AQASAG1hbmR5Lnd1QGludGVsLmNvbQAuAAADnUl3sZrICU6Xd1qXV+rpVwEAJvRPgQpR30KhJ7wqBr4YXgAAAY6W+gAAAA==";

	e_ews_connection_create_items (
		cnc, EWS_PRIORITY_MEDIUM,
		NULL, "SendToAllAndSaveCopy", folderid,
		op_test_create_item_get_soap_msg, NULL,
		cancellable, create_item_ready_callback, NULL);
}

static gboolean
idle_cb (gpointer data)
{
	g_printf ("Testing create item..... \n");
	op_test_create_item ();

	return FALSE;
}

/*Run tests*/
void cuditem_tests_run ()
{
	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
