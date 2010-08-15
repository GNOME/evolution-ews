/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _XNTLM_DES_H
#define _XNTLM_DES_H

#include <glib.h>

typedef guint32 XNTLM_DES_KS[16][2];

enum {
	XNTLM_DES_ENCRYPT = 0,
	XNTLM_DES_DECRYPT = 1
};

void xntlm_deskey (XNTLM_DES_KS ks, const guchar *key, gint decrypt);

void xntlm_des (XNTLM_DES_KS ks, guchar block[8]);

#endif /* _XNTLM_DES_H */
