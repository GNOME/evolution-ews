/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_RESULT_H__
#define __E2K_RESULT_H__

#include <libsoup/soup-message.h>
#include "e2k-properties.h"
#include "e2k-types.h"
#include "e2k-http-utils.h"

typedef struct {
	gchar *href;
	gint status;
	E2kProperties *props;
} E2kResult;

void       e2k_results_from_multistatus           (SoupMessage  *msg,
						   E2kResult   **results,
						   gint          *nresults);

E2kResult *e2k_results_copy                       (E2kResult    *results,
						   gint           nresults);

void       e2k_results_free                       (E2kResult    *results,
						   gint           nresults);

GArray    *e2k_results_array_new                  (void);

void       e2k_results_array_add_from_multistatus (GArray       *results_array,
						   SoupMessage  *msg);

void       e2k_results_array_free                 (GArray       *results_array,
						   gboolean      free_results);

typedef struct E2kResultIter E2kResultIter;

typedef E2kHTTPStatus (*E2kResultIterFetchFunc) (E2kResultIter *iter,
						 E2kContext *ctx,
						 E2kOperation *op,
						 E2kResult **results,
						 gint *nresults,
						 gint *first,
						 gint *total,
						 gpointer user_data);
typedef void          (*E2kResultIterFreeFunc)  (E2kResultIter *iter,
						 gpointer user_data);

E2kResultIter *e2k_result_iter_new         (E2kContext            *ctx,
					    E2kOperation          *op,
					    gboolean               ascending,
					    gint                    total,
					    E2kResultIterFetchFunc fetch_func,
					    E2kResultIterFreeFunc  free_func,
					    gpointer               user_data);

E2kResult     *e2k_result_iter_next        (E2kResultIter         *iter);
gint            e2k_result_iter_get_index   (E2kResultIter         *iter);
gint            e2k_result_iter_get_total   (E2kResultIter         *iter);
E2kHTTPStatus  e2k_result_iter_free        (E2kResultIter         *iter);

#endif /* __E2K_RESULT_H__ */
