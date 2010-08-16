/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EWS_CONTEXT_H__
#define __EWS_CONTEXT_H__

#include <libsoup/soup-message.h>
#include <libsoup/soup-session.h>
#include <sys/time.h>

#include <glib-object.h>

#include "e-ews-types.h"
#include "e-ews-operation.h"
#include "e-ews-http-utils.h"

G_BEGIN_DECLS

#define EWS_TYPE_CONTEXT            (ews_context_get_type ())
#define EWS_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EWS_TYPE_CONTEXT, EWSContext))
#define EWS_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EWS_TYPE_CONTEXT, EWSContextClass))
#define EWS_IS_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EWS_TYPE_CONTEXT))
#define EWS_IS_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EWS_TYPE_CONTEXT))

struct _EWSContext {
	GObject parent;

	EWSContextPrivate *priv;
};

struct _EWSContextClass {
	GObjectClass parent_class;

	/* signals */
	void (*redirect) (EWSContext *ctx, EWSHTTPStatus status,
			  const gchar *old_uri, const gchar *new_uri);
};

GType          ews_context_get_type         (void);

EWSContext    *ews_context_new              (const gchar  *uri);
void           ews_context_set_auth         (EWSContext  *ctx,
					     const gchar  *username,
					     const gchar  *domain,
					     const gchar  *authmech,
					     const gchar  *password);
gboolean       ews_context_fba              (EWSContext  *ctx,
					     SoupMessage *failed_msg);

time_t        ews_context_get_last_timestamp    (EWSContext *ctx);

typedef gboolean (*EWSContextTestCallback)     (EWSContext *ctx,
						const gchar *test_name,
						gpointer user_data);

EWSHTTPStatus  ews_context_get               (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *uri,
					      gchar **content_type,
					      SoupBuffer **response);
EWSHTTPStatus  ews_context_get_owa           (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *uri,
					      gboolean claim_ie,
					      SoupBuffer **response);

EWSHTTPStatus  ews_context_put               (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *uri,
					      const gchar *content_type,
					      const gchar *body, gint length,
					      gchar **repl_uid);
EWSHTTPStatus  ews_context_put_new           (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *folder_uri,
					      const gchar *object_name,
					      EWSContextTestCallback test_callback,
					      gpointer user_data,
					      const gchar *content_type,
					      const gchar *body, gint length,
					      gchar **location,
					      gchar **repl_uid);
EWSHTTPStatus  ews_context_post              (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *uri,
					      const gchar *content_type,
					      const gchar *body, gint length,
					      gchar **location,
					      gchar **repl_uid);

EWSHTTPStatus  ews_context_delete            (EWSContext *ctx,
					      EWSOperation *op,
					      const gchar *uri);

/* Subscriptions */
typedef enum {
	E2K_CONTEXT_OBJECT_CHANGED,
	E2K_CONTEXT_OBJECT_ADDED,
	E2K_CONTEXT_OBJECT_REMOVED,
	E2K_CONTEXT_OBJECT_MOVED
} EWSContextChangeType;

typedef void (*EWSContextChangeCallback)     (EWSContext *ctx,
					      const gchar *uri,
					      EWSContextChangeType type,
					      gpointer user_data);

void          ews_context_subscribe          (EWSContext *ctx,
					      const gchar *uri,
					      EWSContextChangeType type,
					      gint min_interval,
					      EWSContextChangeCallback callback,
					      gpointer user_data);
void          ews_context_unsubscribe        (EWSContext *ctx,
					      const gchar *uri);

/*
 * Utility functions
 */
SoupMessage   *ews_soup_message_new      (EWSContext *ctx,
					  const gchar *uri,
					  const gchar *method);
SoupMessage   *ews_soup_message_new_full (EWSContext *ctx,
					  const gchar *uri,
					  const gchar *method,
					  const gchar *content_type,
					  SoupMemoryUse use,
					  const gchar *body,
					  gsize length);
void           ews_context_queue_message (EWSContext *ctx,
					  SoupMessage *msg,
					  SoupSessionCallback callback,
					  gpointer user_data);
EWSHTTPStatus  ews_context_send_message  (EWSContext *ctx,
					  EWSOperation *op,
					  SoupMessage *msg);

G_END_DECLS

#endif /* __E2K_CONTEXT_H__ */
