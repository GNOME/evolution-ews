/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_OPERATION_H__
#define __E2K_OPERATION_H__

#include <string.h>
#include "e2k-types.h"

G_BEGIN_DECLS

typedef void (*E2kOperationCancelFunc) (E2kOperation *op, gpointer owner, gpointer data);

struct _E2kOperation {
	/*< private >*/
	gboolean cancelled;

	E2kOperationCancelFunc canceller;
	gpointer owner, data;
};

/* These are called by the caller of the cancellable function. */
void     e2k_operation_init           (E2kOperation           *op);
void     e2k_operation_free           (E2kOperation           *op);

/* These are called by the cancellable function itself. */
void     e2k_operation_start          (E2kOperation           *op,
				       E2kOperationCancelFunc  canceller,
				       gpointer                owner,
				       gpointer                data);
void     e2k_operation_finish         (E2kOperation           *op);

void     e2k_operation_cancel         (E2kOperation           *op);
gboolean e2k_operation_is_cancelled   (E2kOperation           *op);

G_END_DECLS

#endif /* __E2k_OPERATION_H__ */
