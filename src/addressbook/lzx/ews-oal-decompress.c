/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews.h - Ews contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "lzx.h"
#include "ews-oal-decompress.h"

/* endian-neutral reading of little-endian data */
#define __egi32(a,n) ( ((((unsigned char *) a)[n+3]) << 24) | \
                       ((((unsigned char *) a)[n+2]) << 16) | \
                       ((((unsigned char *) a)[n+1]) <<  8) | \
                       ((((unsigned char *) a)[n+0])))
#define EndGetI64(a) ((((unsigned long long int) __egi32(a,4)) << 32) | \
                      ((unsigned int) __egi32(a,0)))
#define EndGetI32(a) __egi32(a,0)
#define EndGetI16(a) ((((a)[1])<<8)|((a)[0]))

typedef struct {
	guint32 h_version;
	guint32 l_version;
	guint32 max_block_size;
	guint32 target_size;
} LzxHeader;

typedef struct {
	guint32 flags;
	guint32 comp_size;
	guint32 ucomp_size;
	guint32 crc;
} LzxBlockHeader;

static gboolean
read_uint32 (FILE *input, guint32 *val)
{
	gchar buf [4];

	if (fread (buf, 1, 4, input) == 4) {
		*val = EndGetI32 (buf);
		return TRUE;	
	} else 
		return FALSE;
}

static LzxHeader *
read_headers (FILE *input, GError **error)
{
	LzxHeader *lzx_h;
	gboolean success;

	lzx_h = g_new0 (LzxHeader, 1);

	success = read_uint32 (input, &lzx_h->h_version);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->l_version);
	if (!success)
		goto exit;

	if (lzx_h->h_version !=  0x00000003 || lzx_h->l_version != 0x00000001) {
		g_free (lzx_h);
		/* set the right domain later */
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1, "wrong version header");
		return NULL;
	}

	success = read_uint32 (input, &lzx_h->max_block_size);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->target_size);
	if (!success)
		goto exit;

exit:
	if (!success) {
		/* set the right domain later */
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1, "Unable to read lzx main header");
		
		g_free (lzx_h);
		lzx_h = NULL;
	}

	return lzx_h;
}

static LzxBlockHeader *
read_block_header (FILE *input, GError **error)
{
	LzxBlockHeader *lzx_b;
	gboolean success;

	lzx_b = g_new0 (LzxBlockHeader, 1);

	success = read_uint32 (input, &lzx_b->flags);
	if (!success)
		goto exit;

	success = read_uint32 (input, &lzx_b->comp_size);
	if (!success)
		goto exit;
	
	success = read_uint32 (input, &lzx_b->ucomp_size);
	if (!success)
		goto exit;
	
	success = read_uint32 (input, &lzx_b->crc);

exit:
	if (!success) {
		/* set the right domain later */
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1, "Unable to read lzx block header");
		
		g_free (lzx_b);
		lzx_b = NULL;
	}

	return	lzx_b; 
}

gboolean
oal_decompress_v4_full_detail_file (const gchar *filename, const gchar *output_filename, GError **error)
{
	LzxHeader *lzx_h = NULL;
	guint total_decomp_size = 0;
	FILE *input, *output = NULL;
	gboolean ret = TRUE;
	GError *err = NULL;

	input = fopen (filename, "rb");
	if (!input) {
		g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "unable to open the input file");
		ret = FALSE;
		goto exit;	
	}
	
	output = fopen (output_filename, "wb");
	if (!input) {
		g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "unable to open the output file");
		ret = FALSE;
		goto exit;	
	}

	lzx_h = read_headers (input, &err);
	if (!lzx_h) {
		ret = FALSE;
		goto exit;
	}
	
	/* TODO decompressing multiple lzx_blocks has not been tested yet. Will need to get a setup and test it. */
	do {
		LzxBlockHeader *lzx_b;
        	struct lzxd_stream *lzs;
		goffset offset;

		lzx_b = read_block_header (input, &err);
		if (err) {
			ret = FALSE;
			goto exit;
		}

		/* note the file offset */
		offset = ftell (input);

		/* lzx_b points to 1, write it directly to file */
		if (lzx_b->flags == 0) {
			gchar *buffer = g_malloc0 (lzx_b->ucomp_size);

			if (!(fread (buffer, 1, lzx_b->ucomp_size, input) == lzx_b->ucomp_size &&
				fwrite (buffer, 1, lzx_b->ucomp_size, output) == lzx_b->ucomp_size)) {
				g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "failed to write data in output file");
				g_free (buffer);
				ret = FALSE;
				goto exit;
			}
			g_free (buffer);
		} else {
			/* round to multiples of 32768 */
			guint mul, round, set, window_bits;
	
			mul = ceil (lzx_b->ucomp_size / 32768.0);
			round = mul * 32768;
			set = g_bit_nth_lsf ((round >> 17), -1);
			
			if (set > 8)
				window_bits = 25;
			else if (set == 0 && !(round >> 17))
				window_bits = 17;
			else {
				window_bits = set + 17;
				if (round % (2^window_bits))
					window_bits++;
			}

			if (window_bits > 25)
				window_bits = 25;

			lzs = lzxd_init (input, output, window_bits, 0, 16, lzx_b->ucomp_size);

			if (lzxd_decompress (lzs, lzs->length) != LZX_ERR_OK) {
				g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed");
				ret = FALSE;
				goto exit;
			}
		}


		/* Set the fp to beggining of next block. This is a HACK, looks like decompress reads beyond the block.
		   Since we can identify the next block start from block header, we just reset the offset */
		offset += lzx_b->comp_size;
		fseek (input, offset, SEEK_SET);

		total_decomp_size += lzx_b->ucomp_size;
		g_free (lzx_b);
	} while (total_decomp_size < lzx_h->target_size);

exit:
	if (input)
		fclose (input);
	
	if (output)
		fclose (output);

	if (err) {
		ret = FALSE;
		g_propagate_error (error, err);
		g_unlink (output_filename);
	}

	g_free (lzx_h);

	return ret;
}

/*
int
main (int argc, char *argv [])
{
	if (argc != 3) {
		g_print ("Pass an lzx file and an output filename as argument \n");
		return;
	}

	g_type_init ();
	g_thread_init (NULL);

	if (oal_decompress_v4_full_detail_file (argv [1], argv [2], NULL))
		g_print ("Successfully decompressed \n");
	else
		g_print ("decompression failed \n");

	return 0;
} */
