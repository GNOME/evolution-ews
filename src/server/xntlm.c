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

#include "xntlm.h"
#include "xntlm-des.h"
#include "xntlm-md4.h"

#include <ctype.h>
#include <string.h>

static guchar NTLM_NEGOTIATE_MESSAGE[] = {
	 'N',  'T',  'L',  'M',  'S',  'S',  'P', 0x00,
	0x01, 0x00, 0x00, 0x00, 0x06, 0x82, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00
};

/**
 * xntlm_negotiate:
 *
 * Creates an NTLM Type 1 (Negotiate) message
 *
 * Return value: the message
 **/
GByteArray *
xntlm_negotiate (void)
{
	GByteArray *message;

	message = g_byte_array_new ();
	g_byte_array_append (message, NTLM_NEGOTIATE_MESSAGE,
			     sizeof (NTLM_NEGOTIATE_MESSAGE));
	return message;
}

#define GET_SHORTY(p) ((p)[0] + ((p)[1] << 8))

static gchar *
strip_dup (guchar *mem, gint len)
{
	gchar *buf = g_malloc (len / 2 + 1), *p = buf;

	while (len > 0) {
		*p = (gchar)*mem;
		p++;
		mem += 2;
		len -= 2;
	}

	*p = '\0';
	return buf;
}

#define NTLM_CHALLENGE_NONCE_POS       24
#define NTLM_CHALLENGE_NONCE_LEN        8

#define NTLM_CHALLENGE_DATA_OFFSET_POS 44
#define NTLM_CHALLENGE_DATA_LENGTH_POS 40

#define NTLM_CHALLENGE_DATA_NT_DOMAIN   2
#define NTLM_CHALLENGE_DATA_W2K_DOMAIN  4

#define NTLM_CHALLENGE_BASE_SIZE       48

/**
 * xntlm_parse_challenge:
 * @challenge: buffer containing an NTLM Type 2 (Challenge) message
 * @len: the length of @challenge
 * @nonce: return variable for the challenge nonce, or %NULL
 * @nt_domain: return variable for the server NT domain, or %NULL
 * @w2k_domain: return variable for the server W2k domain, or %NULL
 *
 * Attempts to parse the challenge in @challenge. If @nonce is
 * non-%NULL, the 8-byte nonce from @challenge will be returned in it.
 * Likewise, if @nt_domain and/or @w2k_domain are non-%NULL, the
 * server's domain names will be returned in them. The strings
 * returned must be freed with g_free().
 *
 * Return value: %TRUE if the challenge could be parsed,
 * %FALSE otherwise.
 **/
gboolean
xntlm_parse_challenge (gpointer challenge, gint len, gchar **nonce,
		       gchar **nt_domain, gchar **w2k_domain)
{
	guchar *chall = (guchar *)challenge;
	gint off, dlen, doff, type;

	if (len < NTLM_CHALLENGE_BASE_SIZE)
		return FALSE;

	off = GET_SHORTY (chall + NTLM_CHALLENGE_DATA_OFFSET_POS);
	dlen = GET_SHORTY (chall + NTLM_CHALLENGE_DATA_LENGTH_POS);
	if (len < off + dlen)
		return FALSE;

	if (nonce) {
		*nonce = g_memdup (chall + NTLM_CHALLENGE_NONCE_POS,
				   NTLM_CHALLENGE_NONCE_LEN);
	}

	if (!nt_domain && !w2k_domain)
		return TRUE;

	while (off < len - 4) {
		type = GET_SHORTY (chall + off);
		dlen = GET_SHORTY (chall + off + 2);
		doff = off + 4;
		if (doff + dlen > len)
			break;

		switch (type) {
		case NTLM_CHALLENGE_DATA_NT_DOMAIN:
			if (nt_domain)
				*nt_domain = strip_dup (chall + doff, dlen);
			break;
		case NTLM_CHALLENGE_DATA_W2K_DOMAIN:
			if (w2k_domain)
				*w2k_domain = strip_dup (chall + doff, dlen);
			break;
		}

		off = doff + dlen;
	}

	return TRUE;
}

static void
ntlm_set_string (GByteArray *ba, gint offset, const gchar *data, gint len)
{
	ba->data[offset    ] = ba->data[offset + 2] =  len       & 0xFF;
	ba->data[offset + 1] = ba->data[offset + 3] = (len >> 8) & 0xFF;
	ba->data[offset + 4] =  ba->len       & 0xFF;
	ba->data[offset + 5] = (ba->len >> 8) & 0xFF;
	g_byte_array_append (ba, (guint8 *) data, len);
}

static void ntlm_lanmanager_hash (const gchar *password, gchar hash[21]);
static void ntlm_nt_hash         (const gchar *password, gchar hash[21]);
static void ntlm_calc_response   (const guchar key[21],
				  const guchar plaintext[8],
				  guchar results[24]);

static guchar NTLM_RESPONSE_MESSAGE_HEADER[] = {
	 'N',  'T',  'L',  'M',  'S',  'S',  'P', 0x00,
	0x03, 0x00, 0x00, 0x00, 0x02, 0x82, 0x00, 0x00
};

#define NTLM_RESPONSE_BASE_SIZE             64
#define NTLM_RESPONSE_LM_RESP_OFFSET        12
#define NTLM_RESPONSE_NT_RESP_OFFSET        20
#define NTLM_RESPONSE_DOMAIN_OFFSET         28
#define NTLM_RESPONSE_USER_OFFSET           36
#define NTLM_RESPONSE_WORKSTATION_OFFSET    44

/**
 * xntlm_authenticate:
 * @nonce: the nonce from an NTLM Type 2 (Challenge) message
 * @domain: the NT domain to authenticate against
 * @user: the name of the user in @domain
 * @password: @user's password
 * @workstation: the name of the local workstation authenticated
 * against, or %NULL.
 *
 * Generates an NTLM Type 3 (Authenticate) message from the given
 * data. @workstation is provided for completeness, but can basically
 * always be left %NULL.
 *
 * Return value: the NTLM Type 3 message
 **/
GByteArray *
xntlm_authenticate (const gchar *nonce, const gchar *domain,
		    const gchar *user, const gchar *password,
		    const gchar *workstation)
{
	GByteArray *message;
	guchar hash[21], lm_resp[24], nt_resp[24];

	if (!workstation)
		workstation = "";

	message = g_byte_array_new ();

	ntlm_lanmanager_hash (password, (gchar *) hash);
	ntlm_calc_response (hash, (guchar *) nonce, lm_resp);
	ntlm_nt_hash (password, (gchar *) hash);
	ntlm_calc_response (hash, (guchar *) nonce, nt_resp);

	g_byte_array_set_size (message, NTLM_RESPONSE_BASE_SIZE);
	memset (message->data, 0, NTLM_RESPONSE_BASE_SIZE);
	memcpy (message->data, NTLM_RESPONSE_MESSAGE_HEADER,
		sizeof (NTLM_RESPONSE_MESSAGE_HEADER));

	ntlm_set_string (message, NTLM_RESPONSE_DOMAIN_OFFSET,
			 domain, strlen (domain));
	ntlm_set_string (message, NTLM_RESPONSE_USER_OFFSET,
			 user, strlen (user));
	ntlm_set_string (message, NTLM_RESPONSE_WORKSTATION_OFFSET,
			 workstation, strlen (workstation));
	ntlm_set_string (message, NTLM_RESPONSE_LM_RESP_OFFSET,
			 (gchar *) lm_resp, sizeof (lm_resp));
	ntlm_set_string (message, NTLM_RESPONSE_NT_RESP_OFFSET,
			 (gchar *) nt_resp, sizeof (nt_resp));

	return message;
}

static void
setup_schedule (const guchar *key_56, XNTLM_DES_KS ks)
{
	guchar key[8];
	gint i, c, bit;

	key[0] = (key_56[0]);
	key[1] = (key_56[1] >> 1) | ((key_56[0] << 7) & 0xFF);
	key[2] = (key_56[2] >> 2) | ((key_56[1] << 6) & 0xFF);
	key[3] = (key_56[3] >> 3) | ((key_56[2] << 5) & 0xFF);
	key[4] = (key_56[4] >> 4) | ((key_56[3] << 4) & 0xFF);
	key[5] = (key_56[5] >> 5) | ((key_56[4] << 3) & 0xFF);
	key[6] = (key_56[6] >> 6) | ((key_56[5] << 2) & 0xFF);
	key[7] =                    ((key_56[6] << 1) & 0xFF);

	/* Fix parity */
	for (i = 0; i < 8; i++) {
		for (c = bit = 0; bit < 8; bit++)
			if (key [i] & (1 << bit))
				c++;
		if (!(c & 1))
			key [i] ^= 0x01;
	}

        xntlm_deskey (ks, key, XNTLM_DES_ENCRYPT);
}

static guchar LM_PASSWORD_MAGIC[] = {
	0x4B, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25,
	0x4B, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25,
	0x00, 0x00, 0x00, 0x00, 0x00
};

static void
ntlm_lanmanager_hash (const gchar *password, gchar hash[21])
{
	guchar lm_password [15];
	XNTLM_DES_KS ks;
	guint i;

	for (i = 0; i < 14 && password [i]; i++)
		lm_password [i] = toupper ((guchar) password [i]);

	for (; i < sizeof (lm_password); i++)
		lm_password [i] = '\0';

	memcpy (hash, LM_PASSWORD_MAGIC, sizeof (LM_PASSWORD_MAGIC));

	setup_schedule (lm_password, ks);
	xntlm_des (ks, (guchar *) hash);

	setup_schedule (lm_password + 7, ks);
	xntlm_des (ks, (guchar *) hash + 8);
}

static void
ntlm_nt_hash (const gchar *password, gchar hash[21])
{
	guchar *buf, *p;

	p = buf = g_malloc (strlen (password) * 2);

	while (*password) {
		*p++ = *password++;
		*p++ = '\0';
	}

	xntlm_md4sum (buf, p - buf, (guchar *) hash);
	memset (hash + 16, 0, 5);

	g_free (buf);
}

static void
ntlm_calc_response (const guchar key[21], const guchar plaintext[8],
		    guchar results[24])
{
        XNTLM_DES_KS ks;

	memcpy (results, plaintext, 8);
	memcpy (results + 8, plaintext, 8);
	memcpy (results + 16, plaintext, 8);

        setup_schedule (key, ks);
	xntlm_des (ks, results);

        setup_schedule (key + 7, ks);
	xntlm_des (ks, results + 8);

        setup_schedule (key + 14, ks);
        xntlm_des (ks, results + 16);
}

