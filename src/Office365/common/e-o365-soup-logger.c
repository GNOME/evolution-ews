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

#include <glib.h>

#include "e-o365-soup-logger.h"

/* Standard GObject macros */
#define E_TYPE_O365_SOUP_LOGGER \
	(e_o365_soup_logger_get_type ())
#define E_O365_SOUP_LOGGER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_O365_SOUP_LOGGER, EO365SoupLogger))
#define E_O365_SOUP_LOGGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_O365_SOUP_LOGGER, EO365SoupLoggerClass))
#define E_IS_O365_SOUP_LOGGER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_O365_SOUP_LOGGER))
#define E_IS_O365_SOUP_LOGGER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_O365_SOUP_LOGGER))
#define E_O365_SOUP_LOGGER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_O365_SOUP_LOGGER))

G_BEGIN_DECLS

typedef struct _EO365SoupLogger EO365SoupLogger;
typedef struct _EO365SoupLoggerClass EO365SoupLoggerClass;

struct _EO365SoupLogger {
	GObject parent;

	GString *data;
};

struct _EO365SoupLoggerClass {
	GObjectClass parent_class;
};

GType		e_o365_soup_logger_get_type	(void) G_GNUC_CONST;

static void	e_o365_soup_logger_converter_interface_init
						(GConverterIface *iface);

G_DEFINE_TYPE_WITH_CODE (EO365SoupLogger, e_o365_soup_logger, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER, e_o365_soup_logger_converter_interface_init))

static GConverterResult
e_o365_soup_logger_convert (GConverter *converter,
			    gconstpointer inbuf,
			    gsize inbuf_size,
			    gpointer outbuf,
			    gsize outbuf_size,
			    GConverterFlags flags,
			    gsize *bytes_read,
			    gsize *bytes_written,
			    GError **error)
{
	EO365SoupLogger *logger = E_O365_SOUP_LOGGER (converter);
	GConverterResult result;
	gsize min_size;

	min_size = MIN (inbuf_size, outbuf_size);

	if (inbuf && min_size)
		memcpy (outbuf, inbuf, min_size);
	*bytes_read = *bytes_written = min_size;

	if (!logger->data)
		logger->data = g_string_sized_new (10240);

	g_string_append_len (logger->data, (const gchar *) outbuf, (gssize) min_size);

	if ((flags & G_CONVERTER_INPUT_AT_END) != 0)
		result = G_CONVERTER_FINISHED;
	else if ((flags & G_CONVERTER_FLUSH) != 0)
		result = G_CONVERTER_FLUSHED;
	else
		result = G_CONVERTER_CONVERTED;

	return result;
}

static void
e_o365_soup_logger_reset (GConverter *converter)
{
	/* Nothing to do. */
}

static void
e_o365_soup_logger_print_data (EO365SoupLogger *logger)
{
	if (logger->data) {
		g_print ("%s\n", logger->data->str);
		g_string_free (logger->data, TRUE);
		logger->data = NULL;
	}
}

static void
e_o365_soup_logger_message_finished_cb (SoupMessage *msg,
					gpointer user_data)
{
	EO365SoupLogger *logger = user_data;

	g_return_if_fail (E_IS_O365_SOUP_LOGGER (logger));

	e_o365_soup_logger_print_data (logger);
}

static void
o365_soup_logger_finalize (GObject *object)
{
	EO365SoupLogger *logger = E_O365_SOUP_LOGGER (object);

	e_o365_soup_logger_print_data (logger);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_soup_logger_parent_class)->finalize (object);
}

static void
e_o365_soup_logger_class_init (EO365SoupLoggerClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = o365_soup_logger_finalize;
}

static void
e_o365_soup_logger_converter_interface_init (GConverterIface *iface)
{
	iface->convert = e_o365_soup_logger_convert;
	iface->reset = e_o365_soup_logger_reset;
}

static void
e_o365_soup_logger_init (EO365SoupLogger *logger)
{
}

GInputStream *
e_o365_soup_logger_attach (SoupMessage *message,
			   GInputStream *input_stream)
{
	GConverter *logger;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), input_stream);
	g_return_val_if_fail (G_IS_INPUT_STREAM (input_stream), input_stream);

	logger = g_object_new (E_TYPE_O365_SOUP_LOGGER, NULL);

	input_stream = g_converter_input_stream_new (input_stream, logger);
	g_object_set_data_full (G_OBJECT (message), "EO365SoupLogger", logger, g_object_unref);

	g_signal_connect_object (message, "finished",
		G_CALLBACK (e_o365_soup_logger_message_finished_cb), logger, G_CONNECT_AFTER);

	return input_stream;
}
