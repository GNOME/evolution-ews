/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "lzx.h"
#include "../ews-oab-decompress.h"

/* endian-neutral reading of little-endian data */
#define __egi32(a,n) ( ((((unsigned char *) a)[n+3]) << 24) | \
                       ((((guchar *) a)[n + 2]) << 16) | \
                       ((((guchar *) a)[n + 1]) <<  8) | \
                       ((((guchar *) a)[n + 0])))
#define EndGetI64(a) ((((unsigned long long int) __egi32(a,4)) << 32) | \
                      ((guint) __egi32 (a,0)))
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
read_uint32 (FILE *input,
             guint32 *val)
{
	gchar buf[4];

	if (fread (buf, 1, 4, input) == 4) {
		*val = EndGetI32 (buf);
		return TRUE;
	} else
		return FALSE;
}

static LzxHeader *
read_headers (FILE *input,
              GError **error)
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
read_block_header (FILE *input,
                   GError **error)
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
ews_oab_decompress_full (const gchar *filename, const gchar *output_filename,
			 GError **error)
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
	if (!output) {
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
			/* The window size should be the smallest power of two between 2^17 and 2^25 that is
			   greater than or equal to the sum of the size of the reference data rounded up to
			   a multiple of 32768 and the size of the subject data. Since we have no reference
			   data, forget that and the rounding. Just the smallest power of two which is large
			   enough to cover the subject data (lzx_b->ucomp_size). */

			guint window_bits = g_bit_nth_msf(lzx_b->ucomp_size - 1, -1) + 1;

			if (window_bits < 17)
				window_bits = 17;
			else if (window_bits > 25)
				window_bits = 25;

			lzs = ews_lzxd_init (input, output, window_bits,
					 0, 4096, lzx_b->ucomp_size, 1);
			if (!lzs) {
				g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed (lzxd_init)");
				ret = FALSE;
				goto exit;
			}
			if (ews_lzxd_decompress (lzs, lzx_b->ucomp_size) != LZX_ERR_OK) {
				g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed (lzxd_decompress)");
				ret = FALSE;
				goto exit;
			}
		}

		/* Set the fp to beggining of next block. This is a HACK, looks like decompress reads beyond the block.
		 * Since we can identify the next block start from block header, we just reset the offset */
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

typedef struct {
	guint32 h_version;
	guint32 l_version;
	guint32 max_block_size;
	guint32 source_size;
	guint32 target_size;
	guint32 source_crc;
	guint32 target_crc;
} LzxPatchHeader;

typedef struct {
	guint32 patch_size;
	guint32 target_size;
	guint32 source_size;
	guint32 crc;
} LzxPatchBlockHeader;


static LzxPatchHeader *
read_patch_headers (FILE *input,
              GError **error)
{
	LzxPatchHeader *lzx_h;
	gboolean success;

	lzx_h = g_new0 (LzxPatchHeader, 1);

	success = read_uint32 (input, &lzx_h->h_version);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->l_version);
	if (!success)
		goto exit;

	if (lzx_h->h_version !=  0x00000003 || lzx_h->l_version != 0x00000002) {
		g_free (lzx_h);
		/* set the right domain later */
		g_set_error_literal (error, g_quark_from_string ("lzx"), 1, "wrong version header");
		return NULL;
	}

	success = read_uint32 (input, &lzx_h->max_block_size);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->source_size);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->target_size);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->source_crc);
	if (!success)
		goto exit;
	success = read_uint32 (input, &lzx_h->target_crc);
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

static LzxPatchBlockHeader *
read_patch_block_header (FILE *input,
			 GError **error)
{
	LzxPatchBlockHeader *lzx_b;
	gboolean success;

	lzx_b = g_new0 (LzxPatchBlockHeader, 1);

	success = read_uint32 (input, &lzx_b->patch_size);
	if (!success)
		goto exit;

	success = read_uint32 (input, &lzx_b->target_size);
	if (!success)
		goto exit;

	success = read_uint32 (input, &lzx_b->source_size);
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
ews_oab_decompress_patch (const gchar *filename, const gchar *orig_filename,
			  const gchar *output_filename, GError **error)
{
	LzxPatchHeader *lzx_h = NULL;
	guint total_decomp_size = 0;
	FILE *input = NULL, *output = NULL, *orig_input = NULL;
	gboolean ret = TRUE;
	GError *err = NULL;

	input = fopen (filename, "rb");
	if (!input) {
		g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "unable to open the input file");
		ret = FALSE;
		goto exit;
	}

	orig_input = fopen (orig_filename, "rb");
	if (!orig_input) {
		g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "unable to open the reference input file");
		ret = FALSE;
		goto exit;
	}

	output = fopen (output_filename, "wb");
	if (!output) {
		g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "unable to open the output file");
		ret = FALSE;
		goto exit;
	}

	lzx_h = read_patch_headers (input, &err);
	if (!lzx_h) {
		ret = FALSE;
		goto exit;
	}

	/* TODO decompressing multiple lzx_blocks has not been tested yet. Will need to get a setup and test it. */
	do {
		LzxPatchBlockHeader *lzx_b;
		struct lzxd_stream *lzs;
		goffset offset;
		guint ref_size, window_bits;

		lzx_b = read_patch_block_header (input, &err);
		if (err) {
			printf("err block header\n");
			ret = FALSE;
			goto exit;
		}

		/* note the file offset */
		offset = ftell(input);

		/* The window size should be the smallest power of two
		   between 2^17 and 2^25 that is greater than or equal
		   to the sum of the size of the reference data
		   rounded up to a multiple of 32768 and the size of
		   the subject data. */
		ref_size = (lzx_b->source_size + 32767) & ~32767;
		window_bits = g_bit_nth_msf(ref_size + lzx_b->target_size - 1, -1) + 1;

		if (window_bits < 17)
			window_bits = 17;
		else if (window_bits > 25)
			window_bits = 25;

		lzs = ews_lzxd_init (input, output, window_bits,
				 0, 4096, lzx_b->target_size, 1);
		if (!lzs) {
			g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed (lzxd_init)");
			ret = FALSE;
			goto exit;
		}
		if (ews_lzxd_set_reference_data(lzs, orig_input, lzx_b->source_size)) {
			g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed (lzxd_set_reference_data)");
			ret = FALSE;
			goto exit;
		}
		if (ews_lzxd_decompress (lzs, lzs->length) != LZX_ERR_OK) {
			g_set_error_literal (&err, g_quark_from_string ("lzx"), 1, "decompression failed (lzxd_decompress)");
			ret = FALSE;
			goto exit;
		}

		/* Set the fp to beggining of next block. This is a HACK, looks like decompress reads beyond the block.
		 * Since we can identify the next block start from block header, we just reset the offset */
		offset += lzx_b->patch_size;
		fseek (input, offset, SEEK_SET);

		total_decomp_size += lzx_b->target_size;
		g_free (lzx_b);
	} while (total_decomp_size < lzx_h->target_size);

exit:
	if (input)
		fclose (input);

	if (orig_input)
		fclose (orig_input);

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

