/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __EWS_OPERATION_H__
#define __EWS_OPERATION_H__

#include <string.h>
#include "e-ews-types.h"

G_BEGIN_DECLS

typedef void (*EWSOperationCancelFunc) (EWSOperation *op, gpointer owner, gpointer data);

struct _EWSOperation {
	/*< private >*/
	gboolean cancelled;

	EWSOperationCancelFunc canceller;
	gpointer owner, data;
};

/* These are called by the caller of the cancellable function. */
void     ews_operation_init           (EWSOperation           *op);
void     ews_operation_free           (EWSOperation           *op);

/* These are called by the cancellable function itself. */
void     ews_operation_start          (EWSOperation           *op,
				       EWSOperationCancelFunc  canceller,
				       gpointer                owner,
				       gpointer                data);
void     ews_operation_finish         (EWSOperation           *op);

void     ews_operation_cancel         (EWSOperation           *op);
gboolean ews_operation_is_cancelled   (EWSOperation           *op);

G_END_DECLS

#endif /* __EWS_OPERATION_H__ */
