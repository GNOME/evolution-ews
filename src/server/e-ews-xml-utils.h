/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EWS_XML_UTILS_H__
#define __EWS_XML_UTILS_H__

#include <string.h>

#include "e-ews-types.h"
#include <libxml/parser.h>

#define EWS_XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"

xmlDoc *ews_parse_xml  (const gchar *buf, gint len);
xmlDoc *ews_parse_html (const gchar *buf, gint len);

#define EWS_IS_NODE(node, nspace, nname) \
	(!xmlStrcmp ((node)->name, (xmlChar *) (nname)) && \
	(node)->ns && !xmlStrcmp ((node)->ns->href, (xmlChar *) (nspace)))

void  ews_g_string_append_xml_escaped (GString *string, const gchar *value);

xmlNode *ews_xml_find    (xmlNode *node, const gchar *name);
xmlNode *ews_xml_find_in (xmlNode *node, xmlNode *top, const gchar *name);

#endif
