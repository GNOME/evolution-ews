/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef E_SOUP_AUTH_NEGOTIATE_H
#define E_SOUP_AUTH_NEGOTIATE_H

#include "libsoup/soup.h"

#define E_SOUP_TYPE_AUTH_NEGOTIATE \
	(e_soup_auth_negotiate_get_type ())
#define E_SOUP_AUTH_NEGOTIATE(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), E_SOUP_TYPE_AUTH_NEGOTIATE, ESoupAuthNegotiate))
#define E_SOUP_AUTH_NEGOTIATE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_SOUP_TYPE_AUTH_NEGOTIATE, ESoupAuthNegotiate))
#define E_SOUP_IS_AUTH_NEGOTIATE(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), E_SOUP_TYPE_AUTH_NEGOTIATE))
#define E_SOUP_IS_AUTH_NEGOTIATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_SOUP_TYPE_AUTH_NEGOTIATE))
#define E_SOUP_AUTH_NEGOTIATE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_SOUP_TYPE_AUTH_NEGOTIATE, ESoupAuthNegotiateClass))

typedef struct {
	SoupAuth parent;
} ESoupAuthNegotiate;

typedef struct {
	SoupAuthClass parent_class;
} ESoupAuthNegotiateClass;

GType e_soup_auth_negotiate_get_type (void);

#endif /* E_SOUP_AUTH_NEGOTIATE_H */
