/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* e2k-operation.c: Cancellable operations */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e2k-operation.h"

static GStaticMutex op_mutex = G_STATIC_MUTEX_INIT;
static GHashTable *active_ops = NULL;

/**
 * e2k_operation_init:
 * @op: an #E2kOperation
 *
 * This initializes the #E2kOperation pointed to by @op.
 * This should be called before passing @op to a cancellable function.
 **/
void
e2k_operation_init (E2kOperation *op)
{
	g_return_if_fail (op != NULL);

	memset (op, 0, sizeof (E2kOperation));

	g_static_mutex_lock (&op_mutex);
	if (!active_ops)
		active_ops = g_hash_table_new (NULL, NULL);
	g_hash_table_insert (active_ops, op, op);
	g_static_mutex_unlock (&op_mutex);
}

/**
 * e2k_operation_free:
 * @op: an #E2kOperation
 *
 * This frees @op and removes it from the list of active operations.
 * It should be called after the function it was passed to returns.
 **/
void
e2k_operation_free (E2kOperation *op)
{
	g_return_if_fail (op != NULL);

	g_static_mutex_lock (&op_mutex);
	g_hash_table_remove (active_ops, op);
	g_static_mutex_unlock (&op_mutex);
}

/**
 * e2k_operation_start:
 * @op: an #E2kOperation, or %NULL
 * @canceller: the callback to invoke if @op is cancelled
 * @owner: object that owns the operation
 * @data: data to pass to @canceller
 *
 * This starts a single cancellable operation using @op. If @op has
 * already been cancelled, this will invoke @canceller immediately.
 *
 * (If @op is %NULL, e2k_operation_start() is a no-op.)
 **/
void
e2k_operation_start (E2kOperation *op,
		     E2kOperationCancelFunc canceller,
		     gpointer owner,
		     gpointer data)
{
	if (!op)
		return;

	g_static_mutex_lock (&op_mutex);

	op->canceller = canceller;
	op->owner = owner;
	op->data = data;

	if (op->cancelled && op->canceller) {
		g_static_mutex_unlock (&op_mutex);
		op->canceller (op, op->owner, op->data);
		return;
	}

	g_static_mutex_unlock (&op_mutex);
}

/**
 * e2k_operation_finish:
 * @op: an #E2kOperation, or %NULL
 *
 * This finishes the current cancellable operation on @op. Attempting
 * to cancel @op after this point will have no effect until another
 * operation is started on it.
 *
 * (If @op is %NULL, e2k_operation_finish() is a no-op.)
 **/
void
e2k_operation_finish (E2kOperation *op)
{
	if (!op)
		return;

	g_static_mutex_lock (&op_mutex);
	op->canceller = NULL;
	op->owner = NULL;
	op->data = NULL;
	g_static_mutex_unlock (&op_mutex);
}

/**
 * e2k_operation_cancel:
 * @op: an #E2kOperation
 *
 * This cancels @op, invoking its cancellation callback. If @op is not
 * an active operation, or has already been cancelled, this has no
 * effect.
 **/
void
e2k_operation_cancel (E2kOperation *op)
{
	g_return_if_fail (op != NULL);

	g_static_mutex_lock (&op_mutex);

	if (!g_hash_table_lookup (active_ops, op) || op->cancelled) {
		g_static_mutex_unlock (&op_mutex);
		return;
	}

	g_hash_table_remove (active_ops, op);
	op->cancelled = TRUE;
	g_static_mutex_unlock (&op_mutex);

	if (op->canceller)
		op->canceller (op, op->owner, op->data);
}

/**
 * e2k_operation_is_cancelled:
 * @op: an #E2kOperation (or %NULL)
 *
 * Checks if @op has been cancelled. Should only be called while @op
 * is active.
 *
 * Return value: whether or not @op has been cancelled.
 **/
gboolean
e2k_operation_is_cancelled (E2kOperation *op)
{
	return op && op->cancelled;
}
