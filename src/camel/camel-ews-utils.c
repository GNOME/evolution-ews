/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* camel-ews-utils.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include "camel-ews-utils.h"

#define SUBFOLDER_DIR_NAME     "subfolders"
#define SUBFOLDER_DIR_NAME_LEN 10
#define RFC_822	"message/rfc822"

static void do_multipart (EEwsConnection *cnc, EEwsItem *item, CamelMultipart *mp, GSList **attach_list);
/**
 * e_path_to_physical:
 * @prefix: a prefix to prepend to the path, or %NULL
 * @path: the virtual path to convert to a filesystem path.
 *
 * This converts the "virtual" path @path into an expanded form that
 * allows a given name to refer to both a file and a directory. The
 * expanded path will have a "subfolders" directory inserted between
 * each path component. If the path ends with "/", the returned
 * physical path will end with "/subfolders"
 *
 * If @prefix is non-%NULL, it will be prepended to the returned path.
 *
 * Returns: the expanded path
 **/
gchar *
e_path_to_physical (const gchar *prefix, const gchar *vpath)
{
	const gchar *p, *newp;
	gchar *dp;
	gchar *ppath;
	gint ppath_len;
	gint prefix_len;

	while (*vpath == '/')
		vpath++;
	if (!prefix)
		prefix = "";

	/* Calculate the length of the real path. */
	ppath_len = strlen (vpath);
	ppath_len++;	/* For the ending zero.  */

	prefix_len = strlen (prefix);
	ppath_len += prefix_len;
	ppath_len++;	/* For the separating slash.  */

	/* Take account of the fact that we need to translate every
	 * separator into 'subfolders/'.
	 */
	p = vpath;
	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL)
			break;

		ppath_len += SUBFOLDER_DIR_NAME_LEN;
		ppath_len++; /* For the separating slash.  */

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	};

	ppath = g_malloc (ppath_len);
	dp = ppath;

	memcpy (dp, prefix, prefix_len);
	dp += prefix_len;
	*(dp++) = '/';

	/* Copy the mangled path.  */
	p = vpath;
	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL) {
			strcpy (dp, p);
			break;
		}

		memcpy (dp, p, newp - p + 1); /* '+ 1' to copy the slash too.  */
		dp += newp - p + 1;

		memcpy (dp, SUBFOLDER_DIR_NAME, SUBFOLDER_DIR_NAME_LEN);
		dp += SUBFOLDER_DIR_NAME_LEN;

		*(dp++) = '/';

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	}

	return ppath;
}

static gboolean
find_folders_recursive (const gchar *physical_path, const gchar *path,
			EPathFindFoldersCallback callback, gpointer data)
{
	GDir *dir;
	gchar *subfolder_directory_path;
	gboolean ok;

	if (*path) {
		if (!callback (physical_path, path, data))
			return FALSE;

		subfolder_directory_path = g_strdup_printf ("%s/%s", physical_path, SUBFOLDER_DIR_NAME);
	} else {
		/* On the top level, we have no folders and,
		 * consequently, no subfolder directory.
		 */

		subfolder_directory_path = g_strdup (physical_path);
	}

	/* Now scan the subfolders and load them. */
	dir = g_dir_open (subfolder_directory_path, 0, NULL);
	if (dir == NULL) {
		g_free (subfolder_directory_path);
		return TRUE;
	}

	ok = TRUE;
	while (ok) {
		struct stat file_stat;
		const gchar *dirent;
		gchar *file_path;
		gchar *new_path;

		dirent = g_dir_read_name (dir);
		if (dirent == NULL)
			break;

		file_path = g_strdup_printf ("%s/%s", subfolder_directory_path, dirent);

		if (g_stat (file_path, &file_stat) < 0 ||
		    !S_ISDIR (file_stat.st_mode)) {
			g_free (file_path);
			continue;
		}

		new_path = g_strdup_printf ("%s/%s", path, dirent);

		ok = find_folders_recursive (file_path, new_path, callback, data);

		g_free (file_path);
		g_free (new_path);
	}

	g_dir_close (dir);
	g_free (subfolder_directory_path);

	return ok;
}

/**
 * e_path_find_folders:
 * @prefix: directory to start from
 * @callback: Callback to invoke on each folder
 * @data: Data for @callback
 *
 * Walks the folder tree starting at @prefix and calls @callback
 * on each folder.
 *
 * Returns: %TRUE on success, %FALSE if an error occurs at any point
 **/
gboolean
e_path_find_folders (const gchar *prefix,
		     EPathFindFoldersCallback callback,
		     gpointer data)
{
	return find_folders_recursive (prefix, "", callback, data);
}

/**
 * e_path_rmdir:
 * @prefix: a prefix to prepend to the path, or %NULL
 * @path: the virtual path to convert to a filesystem path.
 *
 * This removes the directory pointed to by @prefix and @path
 * and attempts to remove its parent "subfolders" directory too
 * if it's empty.
 *
 * Returns: -1 (with errno set) if it failed to rmdir the
 * specified directory. 0 otherwise, whether or not it removed
 * the parent directory.
 **/
gint
e_path_rmdir (const gchar *prefix, const gchar *vpath)
{
	gchar *physical_path, *p;

	/* Remove the directory itself */
	physical_path = e_path_to_physical (prefix, vpath);
	if (g_rmdir (physical_path) == -1) {
		g_free (physical_path);
		return -1;
	}

	/* Attempt to remove its parent "subfolders" directory,
	 * ignoring errors since it might not be empty.
	 */

	p = strrchr (physical_path, '/');
	if (p[1] == '\0') {
		g_free (physical_path);
		return 0;
	}
	*p = '\0';
	p = strrchr (physical_path, '/');
	if (!p || strcmp (p + 1, SUBFOLDER_DIR_NAME) != 0) {
		g_free (physical_path);
		return 0;
	}

	g_rmdir (physical_path);
	g_free (physical_path);
	return 0;
}

static GSList *
add_recipients(GSList *recipient_list, CamelAddress *recipients, gint recipient_type)
{
	gint total_add,i;
	EEwsItemRecipient *recipient;

	total_add = camel_address_length (recipients);
	for (i=0; i<total_add; i++) {
		const gchar *name = NULL, *addr = NULL;
		if (camel_internet_address_get ((CamelInternetAddress *)recipients, i , &name, &addr )) {

			recipient = g_new0 (EEwsItemRecipient, 1);

			recipient->email = g_strdup (addr);
			recipient->display_name = g_strdup (name);
			recipient->type = recipient_type;
			recipient->status = E_EWS_ITEM_STAT_NONE;
			recipient_list = g_slist_prepend (recipient_list, recipient);
		}
	}
	return recipient_list;
}

static void
send_as_attachment (EEwsConnection *cnc, EEwsItem *item, CamelStream *content, CamelContentType *type, CamelDataWrapper *dw, const gchar *filename, const gchar *cid, GSList **attach_list)
{
	EEwsItemLinkInfo *info = NULL;
	EEwsConnectionStatus status;
	EEwsItemAttachment *attachment;
	EEwsItem *temp_item;
	GByteArray *byte_array;

	attachment = g_new0 (EEwsItemAttachment, 1);
	attachment->contentType = camel_content_type_simple (type);

	if (cid)
		attachment->contentid = camel_header_contentid_decode (cid);

	byte_array = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (content));

	if (filename && byte_array->data) {
		if (camel_content_type_is (type, "application", "pgp-signature")) {
			gchar *temp_str;
			gint temp_len;
			temp_str = g_base64_encode (byte_array->data, byte_array->len);
			temp_len = strlen (temp_str);
			attachment->data = g_strdup (temp_str);
			attachment->size = temp_len;
			g_free (temp_str);
			temp_str = NULL;
		} else {
			attachment->data = g_base64_encode(byte_array->data, byte_array->len);
			attachment->size = strlen (attachment->data);
		}
	} else if (byte_array->data) {
		gchar *temp_str;
		gint temp_len;
		if (!strcmp (attachment->contentType, "multipart/digest")) {
			/* FIXME? */
		} else {
			temp_str = g_base64_encode (byte_array->data, byte_array->len);
			temp_len = strlen (temp_str);
			attachment->data = g_strdup (temp_str);
			attachment->size = temp_len;
			g_free (temp_str);
			temp_str = NULL;
		}
	}

	if (camel_content_type_is (type, "text", "html") || camel_content_type_is (type, "multipart", "alternative")) {
		if (!filename)
			filename = "text.htm";
		if (camel_content_type_is (type, "multipart", "alternative")) {
			/* FIXME: this just feels so wrong... */
			g_free (attachment->contentType);
			attachment->contentType = g_strdup ("text/html");
		}
	}

	attachment->name = g_strdup (filename ? filename : "");
	if (camel_content_type_is (type, "message", "rfc822")) {
		gchar *item_id;

		item_id = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (dw), "X-GW-ITEM-ID"));
		/*
		 * XXX: The following code piece is a screwed up way of doing stuff.
		 * But we dont have much choice. Do not use 'camel_header_msgid_decode'
		 * since it removes the container id portion from the id and which the
		 * groupwise server needs.
		 */
		g_strstrip (item_id);
		status = e_ews_connection_forward_item (cnc, item_id, NULL, TRUE, &temp_item);
		g_free (item_id);

		if (status != E_EWS_CONNECTION_STATUS_OK) {
			g_warning ("Could not send a forwardRequest...continuing without!!\n");

			g_free (attachment->name);
			attachment->name = g_strdup ("Mime.822");

			g_free (attachment->contentType);
			attachment->contentType = g_strdup ("Mail");
		} else {
			GSList *attach_list = e_ews_item_get_attach_id_list (temp_item);
			EEwsItemAttachment *temp_attach = (EEwsItemAttachment *)attach_list->data;
			attachment->id = g_strdup (temp_attach->id);
			attachment->item_reference = g_strdup (temp_attach->item_reference);
			g_free (attachment->name);
			attachment->name = g_strdup (temp_attach->name);
			g_free (attachment->contentType);
			attachment->contentType = g_strdup ("Mail");
			g_free (attachment->data);
			attachment->data = NULL;
			attachment->size = 0;
			info = e_ews_item_get_link_info (temp_item);
			e_ews_item_set_link_info (item, info);
		}
	}

	*attach_list = g_slist_append (*attach_list, attachment);
}

EEwsItem *
camel_ews_util_item_from_message (EEwsConnection *cnc, CamelMimeMessage *message, CamelAddress *from)
{
	EEwsItem *item;
	EEwsItemOrganizer *org = g_new0 (EEwsItemOrganizer, 1);
	const gchar *display_name = NULL, *email = NULL;
	gchar *send_options = NULL;
	CamelMultipart *mp;
	GSList *recipient_list = NULL, *attach_list = NULL;
	CamelAddress *recipients;

	/*Egroupwise item*/
	item = e_ews_item_new_empty ();

	/*populate recipient list*/
	recipients = CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO));
	recipient_list=add_recipients(recipient_list,recipients,E_EWS_ITEM_RECIPIENT_TO);

	recipients = CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC));
	recipient_list=add_recipients(recipient_list,recipients,E_EWS_ITEM_RECIPIENT_CC);

	recipients = CAMEL_ADDRESS (camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC));
	recipient_list=add_recipients(recipient_list,recipients,E_EWS_ITEM_RECIPIENT_BC);
	recipient_list = g_slist_reverse (recipient_list);

	/** Get the mime parts from CamelMimemessge **/
	mp = (CamelMultipart *)camel_medium_get_content (CAMEL_MEDIUM (message));
	if (!mp) {
		g_warning ("ERROR: Could not get content object");
		camel_operation_end (NULL);
		return NULL;
	}

	if (CAMEL_IS_MULTIPART (mp)) {
		/*contains multiple parts*/
		do_multipart (cnc, item, mp, &attach_list);
	} else {
		/*only message*/
		CamelStream *content;
		CamelDataWrapper *dw = NULL;
		CamelContentType *type;
		GByteArray *byte_array;

		byte_array = g_byte_array_new ();
		content = camel_stream_mem_new_with_byte_array (byte_array);

		dw = camel_medium_get_content (CAMEL_MEDIUM (message));
		type = camel_mime_part_get_content_type((CamelMimePart *)message);

		if (camel_content_type_is (type, "text", "plain")) {
			CamelStream *filtered_stream;
			CamelMimeFilter *filter;
			const gchar *charset;
			gchar *content_type;

			content_type = camel_content_type_simple (type);
			e_ews_item_set_content_type (item, content_type);
			g_free (content_type);

			charset = camel_content_type_param (type, "charset");
			if (charset && g_ascii_strcasecmp (charset, "US-ASCII") && g_ascii_strcasecmp (charset, "UTF-8")) {
				filter = camel_mime_filter_charset_new (charset, "UTF-8");
				filtered_stream = camel_stream_filter_new (content);
				camel_stream_filter_add ((CamelStreamFilter *) filtered_stream, filter);
				g_object_unref (filter);
			} else {
				/* US-ASCII or UTF-8 */
				filtered_stream = g_object_ref (content);
			}

			camel_data_wrapper_decode_to_stream (dw, filtered_stream, NULL);
			camel_stream_flush (filtered_stream, NULL);
			g_object_unref (filtered_stream);

			camel_stream_write (content, "", 1, NULL);
			e_ews_item_set_message (item, (const gchar *)byte_array->data);
		} else {
			camel_data_wrapper_decode_to_stream (dw, content, NULL);
			send_as_attachment (cnc, item, content, type, dw, NULL, NULL, &attach_list);
		}

		g_object_unref (content);
	}
	/*Populate EEwsItem*/
	/*From Address*/
	camel_internet_address_get ((CamelInternetAddress *)from, 0 , &display_name, &email);
	org->display_name = g_strdup (display_name);
	org->email = g_strdup (email);
	e_ews_item_set_organizer (item, org);
	/*recipient list*/
	e_ews_item_set_recipient_list (item, recipient_list);
	/*Item type is mail*/
	e_ews_item_set_item_type (item, E_EWS_ITEM_TYPE_MAIL);
	/*subject*/
	e_ews_item_set_subject (item, camel_mime_message_get_subject(message));
	/*attachmets*/
	e_ews_item_set_attach_id_list (item, attach_list);

	/*send options*/
	e_ews_item_set_sendoptions (item, TRUE);

	if ((gchar *)camel_medium_get_header (CAMEL_MEDIUM(message), X_REPLY_CONVENIENT))
		e_ews_item_set_reply_request (item, TRUE);

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM(message), X_REPLY_WITHIN);
	if (send_options) {
		e_ews_item_set_reply_request (item, TRUE);
		e_ews_item_set_reply_within (item, send_options);
	}
	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM(message),X_EXPIRE_AFTER);
	if (send_options)
		e_ews_item_set_expires (item, send_options);

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM(message), X_DELAY_UNTIL);
	if (send_options)
		e_ews_item_set_delay_until (item, send_options);

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM(message), X_TRACK_WHEN);

	/*we check if user has modified the status tracking options, if no then we anyway
	 * set status tracking all*/
	if (send_options) {
		switch (atoi(send_options)) {
			case 1: e_ews_item_set_track_info (item, E_EWS_ITEM_DELIVERED);
				break;
			case 2: e_ews_item_set_track_info (item, E_EWS_ITEM_DELIVERED_OPENED);
				break;
			case 3: e_ews_item_set_track_info (item, E_EWS_ITEM_ALL);
				break;
			default: e_ews_item_set_track_info (item, E_EWS_ITEM_NONE);
				 break;
		}
	} else
		e_ews_item_set_track_info (item, E_EWS_ITEM_ALL);

	if ((gchar *)camel_medium_get_header (CAMEL_MEDIUM(message), X_AUTODELETE))
		e_ews_item_set_autodelete (item, TRUE);

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM (message), X_RETURN_NOTIFY_OPEN);
	if (send_options) {
		switch (atoi(send_options)) {
			case 0: e_ews_item_set_notify_opened (item, E_EWS_ITEM_NOTIFY_NONE);
				break;
			case 1: e_ews_item_set_notify_opened (item, E_EWS_ITEM_NOTIFY_MAIL);
		}
	}
	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM (message), X_RETURN_NOTIFY_DELETE);
	if (send_options) {
		switch (atoi(send_options)) {
			case 0: e_ews_item_set_notify_deleted (item, E_EWS_ITEM_NOTIFY_NONE);
				break;
			case 1: e_ews_item_set_notify_deleted (item, E_EWS_ITEM_NOTIFY_MAIL);
		}
	}

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM (message), X_SEND_OPT_PRIORITY);
	if (send_options) {
		switch (atoi(send_options)) {
			case E_EWS_PRIORITY_HIGH: e_ews_item_set_priority(item, "High");
						 break;
			case E_EWS_PRIORITY_LOW:  e_ews_item_set_priority(item, "Low");
						 break;
			case E_EWS_PRIORITY_STANDARD: e_ews_item_set_priority(item, "Standard");
						     break;
		}
	}

	send_options = (gchar *)camel_medium_get_header (CAMEL_MEDIUM (message), X_SEND_OPT_SECURITY);
	if (send_options) {
		switch (atoi(send_options)) {
			case E_EWS_SECURITY_NORMAL : e_ews_item_set_security(item, "Normal");
						    break;
			case E_EWS_SECURITY_PROPRIETARY : e_ews_item_set_security(item, "Proprietary");
							 break;
			case E_EWS_SECURITY_CONFIDENTIAL : e_ews_item_set_security(item, "Confidential");
							  break;
			case E_EWS_SECURITY_SECRET : e_ews_item_set_security(item, "Secret");
						    break;
			case E_EWS_SECURITY_TOP_SECRET : e_ews_item_set_security(item, "TopSecret");
							break;
			case E_EWS_SECURITY_FOR_YOUR_EYES_ONLY : e_ews_item_set_security(item, "ForYourEyesOnly");
								break;
		}
	}
	return item;
}

void
do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new)
{
	diff->changed = old ^ _new;
	diff->bits = _new & diff->changed;
}

gchar *
gw_concat ( const gchar *prefix, const gchar *suffix)
{
	gsize len;

	len = strlen (prefix);
	if (len == 0 || prefix[len - 1] == '/')
		return g_strdup_printf ("%s%s", prefix, suffix);
	else
		return g_strdup_printf ("%s%c%s", prefix, '/', suffix);
}

void
strip_lt_gt (gchar **string, gint s_offset, gint e_offset)
{
	gchar *temp = NULL;
	gint len;

	temp = g_strdup (*string);
	len = strlen (*string);

	*string = (gchar *)g_malloc0 (len-1);
	*string = memcpy(*string, temp+s_offset, len-e_offset);
	g_free (temp);
}

static void
do_multipart (EEwsConnection *cnc, EEwsItem *item, CamelMultipart *mp, GSList **attach_list)
{
	/*contains multiple parts*/
	guint part_count;
	gint i;

	part_count = camel_multipart_get_number (mp);
	for ( i=0; i<part_count; i++) {
		CamelContentType *type;
		CamelMimePart *part;
		CamelStream *content;
		CamelDataWrapper *dw = NULL;
		GByteArray *buffer;
		const gchar *disposition, *filename;
		const gchar *content_id = NULL;
		gboolean is_alternative = FALSE;

		buffer = g_byte_array_new ();
		content = camel_stream_mem_new_with_byte_array (buffer);

		/*
		 * XXX:
		 * Assuming the first part always is the actual message
		 * and an attachment otherwise.....
		 */
		part = camel_multipart_get_part (mp, i);

		if (!part)
			continue;

		type = camel_mime_part_get_content_type(part);
		dw = camel_medium_get_content (CAMEL_MEDIUM (part));

		if (CAMEL_IS_MULTIPART (dw)) {
			do_multipart (cnc, item, (CamelMultipart *) camel_medium_get_content ((CamelMedium *) part), attach_list);
			continue;
		}

		if (type->subtype && !strcmp (type->subtype, "alternative")) {
			/* eh... I don't think this code will ever get hit? */
			CamelMimePart *temp_part;
			const gchar *cid = NULL;
			CamelStream *temp_content;
			CamelDataWrapper *temp_dw = NULL;
			GByteArray *temp_buffer;

			temp_buffer = g_byte_array_new ();
			temp_content = camel_stream_mem_new_with_byte_array (temp_buffer);

			temp_part = camel_multipart_get_part ((CamelMultipart *)dw, 1);
			if (temp_part) {
				is_alternative = TRUE;
				temp_dw = camel_medium_get_content (CAMEL_MEDIUM (temp_part));
				camel_data_wrapper_write_to_stream(temp_dw, temp_content, NULL);
				filename = camel_mime_part_get_filename (temp_part);
				disposition = camel_mime_part_get_disposition (temp_part);
				cid = camel_mime_part_get_content_id (temp_part);
				send_as_attachment (cnc, item, temp_content, type, temp_dw, filename, cid, attach_list);
			}
			g_object_unref (temp_content);
			continue;
		}

		if (i == 0 && camel_content_type_is (type, "text", "plain")) {
			CamelStream *filtered_stream;
			CamelMimeFilter *filter;
			const gchar *charset;
			gchar *content_type;

			content_type = camel_content_type_simple (type);
			e_ews_item_set_content_type (item, content_type);
			g_free (content_type);

			charset = camel_content_type_param (type, "charset");
			if (charset && g_ascii_strcasecmp (charset, "US-ASCII") && g_ascii_strcasecmp (charset, "UTF-8")) {
				filter = camel_mime_filter_charset_new (charset, "UTF-8");
				filtered_stream = camel_stream_filter_new (content);
				camel_stream_filter_add ((CamelStreamFilter *) filtered_stream, filter);
				g_object_unref (filter);
			} else {
				/* US-ASCII or UTF-8 */
				filtered_stream = g_object_ref (content);
			}

			camel_data_wrapper_decode_to_stream (dw, filtered_stream, NULL);
			camel_stream_flush (filtered_stream, NULL);
			g_object_unref (filtered_stream);

			camel_stream_write (content, "", 1, NULL);
			e_ews_item_set_message (item, (const gchar *) buffer->data);
		} else {
			filename = camel_mime_part_get_filename (part);
			disposition = camel_mime_part_get_disposition (part);
			content_id = camel_mime_part_get_content_id (part);

			camel_data_wrapper_decode_to_stream (dw, content, NULL);
			send_as_attachment (cnc, item, content, type, dw, filename, content_id, attach_list);
		}

		g_object_unref (content);
	} /*end of for*/
}
