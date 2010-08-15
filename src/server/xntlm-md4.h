/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _XNTLM_MD4_H
#define _XNTLM_MD4_H

void xntlm_md4sum (const guchar *in, gint nbytes,
		   guchar digest[16]);

#endif /* _XNTLM_MD4_H */
