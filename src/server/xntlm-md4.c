/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include "xntlm-md4.h"

#include <string.h>

/* MD4 encoder. The reference implementation in RFC1320 isn't
 * GPL-compatible.
 */

#define F(X,Y,Z) ( ((X)&(Y)) | ((~(X))&(Z)) )
#define G(X,Y,Z) ( ((X)&(Y)) | ((X)&(Z)) | ((Y)&(Z)) )
#define H(X,Y,Z) ( (X)^(Y)^(Z) )
#define ROT(val, n) ( ((val) << (n)) | ((val) >> (32 - (n))) )

static void
md4sum_round (const guchar *M,
	      guint32 *AA, guint32 *BB,
	      guint32 *CC, guint32 *DD)
{
	guint32 A, B, C, D, X[16];
	gint i;

	for (i = 0; i < 16; i++) {
		X[i] =  (M[i*4]) | (M[i*4 + 1] << 8) |
			(M[i*4 + 2] << 16) | (M[i*4 + 3] << 24);
	}

	A = *AA;
	B = *BB;
	C = *CC;
	D = *DD;

	A = ROT (A + F(B, C, D) + X[0], 3);
	D = ROT (D + F(A, B, C) + X[1], 7);
	C = ROT (C + F(D, A, B) + X[2], 11);
	B = ROT (B + F(C, D, A) + X[3], 19);
	A = ROT (A + F(B, C, D) + X[4], 3);
	D = ROT (D + F(A, B, C) + X[5], 7);
	C = ROT (C + F(D, A, B) + X[6], 11);
	B = ROT (B + F(C, D, A) + X[7], 19);
	A = ROT (A + F(B, C, D) + X[8], 3);
	D = ROT (D + F(A, B, C) + X[9], 7);
	C = ROT (C + F(D, A, B) + X[10], 11);
	B = ROT (B + F(C, D, A) + X[11], 19);
	A = ROT (A + F(B, C, D) + X[12], 3);
	D = ROT (D + F(A, B, C) + X[13], 7);
	C = ROT (C + F(D, A, B) + X[14], 11);
	B = ROT (B + F(C, D, A) + X[15], 19);

	A = ROT (A + G(B, C, D) + X[0]  + 0x5A827999, 3);
	D = ROT (D + G(A, B, C) + X[4]  + 0x5A827999, 5);
	C = ROT (C + G(D, A, B) + X[8]  + 0x5A827999, 9);
	B = ROT (B + G(C, D, A) + X[12] + 0x5A827999, 13);
	A = ROT (A + G(B, C, D) + X[1]  + 0x5A827999, 3);
	D = ROT (D + G(A, B, C) + X[5]  + 0x5A827999, 5);
	C = ROT (C + G(D, A, B) + X[9]  + 0x5A827999, 9);
	B = ROT (B + G(C, D, A) + X[13] + 0x5A827999, 13);
	A = ROT (A + G(B, C, D) + X[2]  + 0x5A827999, 3);
	D = ROT (D + G(A, B, C) + X[6]  + 0x5A827999, 5);
	C = ROT (C + G(D, A, B) + X[10] + 0x5A827999, 9);
	B = ROT (B + G(C, D, A) + X[14] + 0x5A827999, 13);
	A = ROT (A + G(B, C, D) + X[3]  + 0x5A827999, 3);
	D = ROT (D + G(A, B, C) + X[7]  + 0x5A827999, 5);
	C = ROT (C + G(D, A, B) + X[11] + 0x5A827999, 9);
	B = ROT (B + G(C, D, A) + X[15] + 0x5A827999, 13);

	A = ROT (A + H(B, C, D) + X[0]  + 0x6ED9EBA1, 3);
	D = ROT (D + H(A, B, C) + X[8]  + 0x6ED9EBA1, 9);
	C = ROT (C + H(D, A, B) + X[4]  + 0x6ED9EBA1, 11);
	B = ROT (B + H(C, D, A) + X[12] + 0x6ED9EBA1, 15);
	A = ROT (A + H(B, C, D) + X[2]  + 0x6ED9EBA1, 3);
	D = ROT (D + H(A, B, C) + X[10] + 0x6ED9EBA1, 9);
	C = ROT (C + H(D, A, B) + X[6]  + 0x6ED9EBA1, 11);
	B = ROT (B + H(C, D, A) + X[14] + 0x6ED9EBA1, 15);
	A = ROT (A + H(B, C, D) + X[1]  + 0x6ED9EBA1, 3);
	D = ROT (D + H(A, B, C) + X[9]  + 0x6ED9EBA1, 9);
	C = ROT (C + H(D, A, B) + X[5]  + 0x6ED9EBA1, 11);
	B = ROT (B + H(C, D, A) + X[13] + 0x6ED9EBA1, 15);
	A = ROT (A + H(B, C, D) + X[3]  + 0x6ED9EBA1, 3);
	D = ROT (D + H(A, B, C) + X[11] + 0x6ED9EBA1, 9);
	C = ROT (C + H(D, A, B) + X[7]  + 0x6ED9EBA1, 11);
	B = ROT (B + H(C, D, A) + X[15] + 0x6ED9EBA1, 15);

	*AA += A;
	*BB += B;
	*CC += C;
	*DD += D;
}

/**
 * xntlm_md4sum:
 * @in: the input data
 * @nbytes: the length of @in in bytes
 * @digest: buffer to compute the digest
 *
 * Computes the MD4 checksum of @in and puts it in @digest.
 **/
void
xntlm_md4sum (const guchar *in, gint nbytes, guchar digest[16])
{
	guchar M[128];
	guint32 A, B, C, D;
	gint pbytes, nbits = nbytes * 8, remaining_bytes;
	gint total_len, offset;

	pbytes = (120 - (nbytes % 64)) % 64;
	total_len = nbytes + pbytes + 8;

	A = 0x67452301;
	B = 0xEFCDAB89;
	C = 0x98BADCFE;
	D = 0x10325476;

	for (offset = 0; offset < nbytes - 64; offset += 64)
		md4sum_round (in + offset, &A, &B, &C, &D);

	/* Copy the leftover part of the message into M */
	remaining_bytes = nbytes - offset;
	memcpy (M, in + offset, remaining_bytes);

	/* Append a single "1" bit and appropriate padding */
	M[remaining_bytes] = 0x80;
	memset (M + remaining_bytes + 1, 0, pbytes - 1 + 8);

	/* Append length. */
	M[remaining_bytes + pbytes] = nbits & 0xFF;
	M[remaining_bytes + pbytes + 1] = (nbits >> 8) & 0xFF;
	M[remaining_bytes + pbytes + 2] = (nbits >> 16) & 0xFF;
	M[remaining_bytes + pbytes + 3] = (nbits >> 24) & 0xFF;
	/* We assume nbits is less than 2^32 */

	md4sum_round (M, &A, &B, &C, &D);
	if (remaining_bytes > 56)
		md4sum_round (M + 64, &A, &B, &C, &D);

	digest[0]  =  A        & 0xFF;
	digest[1]  = (A >>  8) & 0xFF;
	digest[2]  = (A >> 16) & 0xFF;
	digest[3]  = (A >> 24) & 0xFF;
	digest[4]  =  B        & 0xFF;
	digest[5]  = (B >>  8) & 0xFF;
	digest[6]  = (B >> 16) & 0xFF;
	digest[7]  = (B >> 24) & 0xFF;
	digest[8]  =  C        & 0xFF;
	digest[9]  = (C >>  8) & 0xFF;
	digest[10] = (C >> 16) & 0xFF;
	digest[11] = (C >> 24) & 0xFF;
	digest[12] =  D        & 0xFF;
	digest[13] = (D >>  8) & 0xFF;
	digest[14] = (D >> 16) & 0xFF;
	digest[15] = (D >> 24) & 0xFF;
}
