/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <camel/camel.h>
#include <camel/camel-search-private.h>

#include "camel-m365-folder.h"
#include "camel-m365-folder-search.h"

struct _CamelM365FolderSearchPrivate {
	GWeakRef m365_store;

	GHashTable *cached_results; /* gchar * (search description) ~> GHashTable { gchar * (uid {from string pool}) ~> NULL } */
	GCancellable *cancellable; /* not referenced */
	GError **error; /* not referenced */
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelM365FolderSearch, camel_m365_folder_search, CAMEL_TYPE_FOLDER_SEARCH)

static void
m365_folder_search_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			camel_m365_folder_search_set_store (
				CAMEL_M365_FOLDER_SEARCH (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_folder_search_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			g_value_take_object (
				value,
				camel_m365_folder_search_ref_store (
				CAMEL_M365_FOLDER_SEARCH (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_folder_search_dispose (GObject *object)
{
	CamelM365FolderSearch *self = CAMEL_M365_FOLDER_SEARCH (object);

	g_weak_ref_set (&self->priv->m365_store, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_folder_search_parent_class)->dispose (object);
}

static void
m365_folder_search_finalize (GObject *object)
{
	CamelM365FolderSearch *self = CAMEL_M365_FOLDER_SEARCH (object);

	g_weak_ref_clear (&self->priv->m365_store);
	g_hash_table_destroy (self->priv->cached_results);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_folder_search_parent_class)->finalize (object);
}

static CamelSExpResult *
m365_folder_search_result_match_all (CamelSExp *sexp,
				     CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (camel_folder_search_get_current_message_info (search)) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = TRUE;
	} else {
		GPtrArray *summary;
		gint ii;

		summary = camel_folder_search_get_summary (search);

		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();

		for (ii = 0; summary && ii < summary->len; ii++) {
			g_ptr_array_add (
				result->value.ptrarray,
				(gpointer) summary->pdata[ii]);
		}
	}

	return result;
}

static CamelSExpResult *
m365_folder_search_result_match_none (CamelSExp *sexp,
				      CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (camel_folder_search_get_current_message_info (search)) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = FALSE;
	} else {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();
	}

	return result;
}

static GPtrArray *
m365_folder_search_messages_to_ptr_array (const GSList *messages)
{
	GPtrArray *matches = NULL;
	const GSList *link;

	for (link = messages; link; link = g_slist_next (link)) {
		EM365MailMessage *msg = link->data;

		if (!msg || !e_m365_mail_message_get_id (msg))
			continue;

		if (!matches)
			matches = g_ptr_array_new ();

		g_ptr_array_add (matches, (gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (msg)));
	}

	return matches;
}

static gchar *
m365_folder_search_describe_criteria (const GPtrArray *words)
{
	GString *desc;

	desc = g_string_sized_new (64);

	if (words && words->len) {
		guint ii;

		for (ii = 0; ii < words->len; ii++) {
			const gchar *word = words->pdata[ii];

			if (word) {
				g_string_append (desc, word);
				g_string_append_c (desc, '\n');
			}
		}
	}

	g_string_append_c (desc, '\n');

	return g_string_free (desc, FALSE);
}

/* This is copy of e_str_replace_string(), to not depend on the evolution code
   in the library code (and to not bring gtk+ into random processes). */
static GString *
m365_str_replace_string (const gchar *text,
			 const gchar *before,
			 const gchar *after)
{
	const gchar *p, *next;
	GString *str;
	gint find_len;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (before != NULL, NULL);
	g_return_val_if_fail (*before, NULL);

	find_len = strlen (before);
	str = g_string_new ("");

	p = text;
	while (next = strstr (p, before), next) {
		if (p < next)
			g_string_append_len (str, p, next - p);

		if (after && *after)
			g_string_append (str, after);

		p = next + find_len;
	}

	return g_string_append (str, p);
}

static CamelSExpResult *
m365_folder_search_process_criteria (CamelSExp *sexp,
				     CamelFolderSearch *search,
				     CamelM365Store *m365_store,
				     const GPtrArray *words)
{
	CamelM365FolderSearch *self = CAMEL_M365_FOLDER_SEARCH (search);
	CamelSExpResult *result;
	CamelMessageInfo *info;
	GHashTable *cached_results;
	gchar *criteria_desc;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;

	criteria_desc = m365_folder_search_describe_criteria (words);
	cached_results = g_hash_table_lookup (self->priv->cached_results, criteria_desc);

	if (!cached_results) {
		CamelM365Folder *m365_folder;

		m365_folder = CAMEL_M365_FOLDER (camel_folder_search_get_folder (search));

		/* Sanity check. */
		g_return_val_if_fail (m365_folder != NULL, NULL);

		if (m365_folder != NULL) {
			EM365Connection *connection = NULL;
			gboolean can_search;

			/* there should always be one, held by one of the callers of this function */
			g_warn_if_fail (m365_store != NULL);

			can_search = m365_store != NULL && words != NULL;

			if (can_search) {
				connection = camel_m365_store_ref_connection (m365_store);
				if (!connection)
					can_search = FALSE;
			}

			if (can_search) {
				GSList *found_messages = NULL;
				GString *expression;
				guint ii;

				expression = g_string_new ("");

				for (ii = 0; ii < words->len; ii++) {
					GString *word;

					if (ii > 0)
						g_string_append (expression, " and ");

					word = m365_str_replace_string (g_ptr_array_index (words, ii), "'", "''");

					g_string_append (expression, "contains(body/content, '");
					g_string_append (expression, word->str);
					g_string_append (expression, "')");

					g_string_free (word, TRUE);
				}

				if (e_m365_connection_list_messages_sync (connection, NULL, camel_m365_folder_get_id (m365_folder),
					"id", expression->str, &found_messages, self->priv->cancellable, &local_error)) {
					uids = m365_folder_search_messages_to_ptr_array (found_messages);
				}

				g_slist_free_full (found_messages, (GDestroyNotify) json_object_unref);
				g_string_free (expression, TRUE);
			}

			g_clear_object (&connection);
		}

		if (local_error != NULL)
			g_propagate_error (self->priv->error, local_error);

		if (!uids) {
			/* Make like we've got an empty result */
			uids = g_ptr_array_new ();
		}
	}

	if (!cached_results) {
		cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);

		if (uids) {
			guint ii;

			for (ii = 0; ii < uids->len; ii++) {
				g_hash_table_insert (cached_results, (gpointer) camel_pstring_strdup (uids->pdata[ii]), NULL);
			}
		}

		g_hash_table_insert (self->priv->cached_results, criteria_desc, cached_results);
	} else {
		g_free (criteria_desc);
	}

	info = camel_folder_search_get_current_message_info (search);

	if (info) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = g_hash_table_contains (cached_results, camel_message_info_get_uid (info));
	} else {
		if (!uids) {
			GHashTableIter iter;
			gpointer key;

			uids = g_ptr_array_sized_new (g_hash_table_size (cached_results));

			g_hash_table_iter_init (&iter, cached_results);

			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				g_ptr_array_add (uids, (gpointer) camel_pstring_strdup (key));
			}
		}

		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_ref (uids);
	}

	if (uids)
		g_ptr_array_unref (uids);

	return result;
}

static GPtrArray *
m365_folder_search_gather_words (CamelSExpResult **argv,
				 gint from_index,
				 gint argc)
{
	GPtrArray *ptrs;
	GHashTable *words_hash;
	GHashTableIter iter;
	gpointer key, value;
	gint ii, jj;

	g_return_val_if_fail (argv != 0, NULL);

	words_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (ii = from_index; ii < argc; ii++) {
		struct _camel_search_words *words;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		/* Handle multiple search words within a single term. */
		words = camel_search_words_split ((const guchar *) argv[ii]->value.string);

		for (jj = 0; jj < words->len; jj++) {
			const gchar *word = words->words[jj]->word;

			g_hash_table_insert (words_hash, g_strdup (word), NULL);
		}

		camel_search_words_free (words);
	}

	ptrs = g_ptr_array_new_full (g_hash_table_size (words_hash), g_free);

	g_hash_table_iter_init (&iter, words_hash);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_ptr_array_add (ptrs, g_strdup (key));
	}

	if (ptrs->len == 0) {
		g_ptr_array_free (ptrs, TRUE);
		ptrs = NULL;
	}

	g_hash_table_destroy (words_hash);

	return ptrs;
}

static CamelSExpResult *
m365_folder_search_body_contains (CamelSExp *sexp,
				  gint argc,
				  CamelSExpResult **argv,
				  CamelFolderSearch *search)
{
	CamelM365FolderSearch *self = CAMEL_M365_FOLDER_SEARCH (search);
	CamelM365Store *m365_store;
	CamelSExpResult *result;
	GPtrArray *words;

	/* Match everything if argv = [""] */
	if (argc == 1 && argv[0]->value.string[0] == '\0')
		return m365_folder_search_result_match_all (sexp, search);

	/* Match nothing if empty argv or empty summary. */
	if (argc == 0 || camel_folder_search_get_summary_empty (search))
		return m365_folder_search_result_match_none (sexp, search);

	m365_store = camel_m365_folder_search_ref_store (self);

	/* This will be NULL if we're offline. Search from cache. */
	if (!m365_store) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_m365_folder_search_parent_class)->body_contains (sexp, argc, argv, search);
	}

	words = m365_folder_search_gather_words (argv, 0, argc);

	result = m365_folder_search_process_criteria (sexp, search, m365_store, words);

	g_ptr_array_free (words, TRUE);
	g_object_unref (m365_store);

	return result;
}

static void
camel_m365_folder_search_class_init (CamelM365FolderSearchClass *klass)
{
	GObjectClass *object_class;
	CamelFolderSearchClass *search_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = m365_folder_search_set_property;
	object_class->get_property = m365_folder_search_get_property;
	object_class->dispose = m365_folder_search_dispose;
	object_class->finalize = m365_folder_search_finalize;

	search_class = CAMEL_FOLDER_SEARCH_CLASS (klass);
	search_class->body_contains = m365_folder_search_body_contains;

	g_object_class_install_property (
		object_class,
		PROP_STORE,
		g_param_spec_object (
			"store",
			"M365 Store",
			"M365 Store for server-side searches",
			CAMEL_TYPE_M365_STORE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_m365_folder_search_init (CamelM365FolderSearch *search)
{
	search->priv = camel_m365_folder_search_get_instance_private (search);
	search->priv->cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);

	g_weak_ref_init (&search->priv->m365_store, NULL);
}

/**
 * camel_m365_folder_search_new:
 * @m365_store: a #CamelM365Store to which the search belongs
 *
 * Returns a new #CamelM365FolderSearch instance.
 *
 * Returns: a new #CamelM365FolderSearch
 *
 * Since: 3.54
 **/
CamelFolderSearch *
camel_m365_folder_search_new (CamelM365Store *m365_store)
{
	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), NULL);

	return g_object_new (
		CAMEL_TYPE_M365_FOLDER_SEARCH,
		"store", m365_store,
		NULL);
}

/**
 * camel_m365_folder_search_ref_store:
 * @self: a #CamelM365FolderSearch
 *
 * Returns a #CamelM365Store to use for server-side searches,
 * or %NULL when the store is offline.
 *
 * The returned #CamelM365Store is referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (nullable): a #CamelM365Store, or %NULL
 *
 * Since: 3.54
 **/
CamelM365Store *
camel_m365_folder_search_ref_store (CamelM365FolderSearch *self)
{
	CamelM365Store *m365_store;

	g_return_val_if_fail (CAMEL_IS_M365_FOLDER_SEARCH (self), NULL);

	m365_store = g_weak_ref_get (&self->priv->m365_store);

	if (m365_store && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (m365_store)))
		g_clear_object (&m365_store);

	return m365_store;
}

/**
 * camel_m365_folder_search_set_store:
 * @self: a #CamelM365FolderSearch
 * @m365_store: a #CamelM365Store, or %NULL
 *
 * Sets a #CamelM365Store to use for server-side searches. Generally
 * this is set for the duration of a single search when online, and then
 * reset to %NULL.
 *
 * Since: 3.54
 **/
void
camel_m365_folder_search_set_store (CamelM365FolderSearch *self,
				    CamelM365Store *m365_store)
{
	g_return_if_fail (CAMEL_IS_M365_FOLDER_SEARCH (self));

	if (m365_store != NULL)
		g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));

	g_weak_ref_set (&self->priv->m365_store, m365_store);

	g_object_notify (G_OBJECT (self), "store");
}

void
camel_m365_folder_search_clear_cached_results (CamelM365FolderSearch *self)
{
	g_return_if_fail (CAMEL_IS_M365_FOLDER_SEARCH (self));

	g_hash_table_remove_all (self->priv->cached_results);
}

/**
 * camel_m365_folder_search_set_cancellable_and_error:
 * @self: a #CamelM365FolderSearch
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Sets @cancellable and @error to use for server-side searches. This way
 * the search can return accurate errors and be eventually cancelled by
 * a user.
 *
 * Note: The caller is responsible to keep alive both @cancellable and @error
 * for the whole run of the search and reset them both to NULL after
 * the search is finished.
 *
 * Since: 3.54
 **/
void
camel_m365_folder_search_set_cancellable_and_error (CamelM365FolderSearch *self,
						    GCancellable *cancellable,
						    GError **error)
{
	g_return_if_fail (CAMEL_IS_M365_FOLDER_SEARCH (self));

	if (cancellable)
		g_return_if_fail (G_IS_CANCELLABLE (cancellable));

	self->priv->cancellable = cancellable;
	self->priv->error = error;
}
