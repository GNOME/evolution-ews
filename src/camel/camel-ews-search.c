/*
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <camel/camel.h>
#include <camel/camel-search-private.h>
#include <e-util/e-util.h>

#include "server/e-ews-query-to-restriction.h"

#include "camel-ews-folder.h"
#include "camel-ews-search.h"

#define CAMEL_EWS_SEARCH_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_SEARCH, CamelEwsSearchPrivate))

struct _CamelEwsSearchPrivate {
	GWeakRef ews_store;
	gint *local_data_search; /* not NULL, if testing whether all used headers are all locally available */

	GCancellable *cancellable; /* not referenced */
	GError **error; /* not referenced */
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE (
	CamelEwsSearch,
	camel_ews_search,
	CAMEL_TYPE_FOLDER_SEARCH)

static void
ews_search_set_property (GObject *object,
			 guint property_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			camel_ews_search_set_store (
				CAMEL_EWS_SEARCH (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_search_get_property (GObject *object,
			 guint property_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			g_value_take_object (
				value,
				camel_ews_search_ref_store (
				CAMEL_EWS_SEARCH (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_search_dispose (GObject *object)
{
	CamelEwsSearchPrivate *priv;

	priv = CAMEL_EWS_SEARCH_GET_PRIVATE (object);

	g_weak_ref_set (&priv->ews_store, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_search_parent_class)->dispose (object);
}

static void
ews_search_finalize (GObject *object)
{
	CamelEwsSearchPrivate *priv;

	priv = CAMEL_EWS_SEARCH_GET_PRIVATE (object);

	g_weak_ref_clear (&priv->ews_store);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_search_parent_class)->finalize (object);
}

static CamelSExpResult *
ews_search_result_match_all (CamelSExp *sexp,
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
ews_search_result_match_none (CamelSExp *sexp,
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
ews_search_items_to_ptr_array (const GSList *items)
{
	GPtrArray *matches = NULL;
	const GSList *link;

	for (link = items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const EwsId *id;

		if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		id = e_ews_item_get_id (item);
		if (!id || !id->id)
			continue;

		if (!matches)
			matches = g_ptr_array_new ();

		g_ptr_array_add (matches, (gpointer) camel_pstring_strdup (id->id));
	}

	return matches;
}

static CamelSExpResult *
ews_search_process_criteria (CamelSExp *sexp,
			     CamelFolderSearch *search,
			     CamelEwsStore *ews_store,
			     const GPtrArray *words)
{
	CamelSExpResult *result;
	CamelEwsSearch *ews_search = CAMEL_EWS_SEARCH (search);
	CamelEwsFolder *ews_folder;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;

	ews_folder = CAMEL_EWS_FOLDER (camel_folder_search_get_folder (search));

	/* Sanity check. */
	g_return_val_if_fail (ews_folder != NULL, NULL);

	if (ews_folder != NULL) {
		EEwsConnection *connection = NULL;
		gchar *folder_id = NULL;
		gboolean can_search;

		/* there should always be one, held by one of the callers of this function */
		g_warn_if_fail (ews_store != NULL);

		can_search = ews_store != NULL && words != NULL;

		if (can_search) {
			folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary,
				camel_folder_get_full_name (CAMEL_FOLDER (ews_folder)));
			if (!folder_id)
				can_search = FALSE;
		}

		if (can_search) {
			connection = camel_ews_store_ref_connection (ews_store);
			if (!connection)
				can_search = FALSE;
		}

		if (can_search) {
			EwsFolderId *fid;
			GSList *found_items = NULL;
			gboolean includes_last_item = FALSE;
			GString *expression;
			guint ii;

			fid = e_ews_folder_id_new (folder_id, NULL, FALSE);
			expression = g_string_new ("");

			if (words->len >= 2)
				g_string_append (expression, "(and ");

			for (ii = 0; ii < words->len; ii++) {
				GString *word;

				word = e_str_replace_string (g_ptr_array_index (words, ii), "\"", "\\\"");

				g_string_append (expression, "(body-contains \"");
				g_string_append (expression, word->str);
				g_string_append (expression, "\")");

				g_string_free (word, TRUE);
			}

			/* Close the 'and' */
			if (words->len >= 2)
				g_string_append (expression, ")");

			if (e_ews_connection_find_folder_items_sync (
				connection, EWS_PRIORITY_MEDIUM,
				fid, "IdOnly", NULL, NULL, expression->str, NULL,
				E_EWS_FOLDER_TYPE_MAILBOX, &includes_last_item, &found_items,
				e_ews_query_to_restriction,
				ews_search->priv->cancellable, &local_error)) {

				uids = ews_search_items_to_ptr_array (found_items);
			}

			g_slist_free_full (found_items, g_object_unref);
			g_string_free (expression, TRUE);
			e_ews_folder_id_free (fid);
		}

		g_free (folder_id);
	}

	/* Sanity check. */
	g_warn_if_fail (
		((uids != NULL) && (local_error == NULL)) ||
		((uids == NULL) && (local_error != NULL)));

	if (local_error != NULL)
		g_propagate_error (ews_search->priv->error, local_error);

	if (!uids) {
		/* Make like we've got an empty result */
		uids = g_ptr_array_new ();
	}

	if (camel_folder_search_get_current_message_info (search)) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = (uids && uids->len > 0);
	} else {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_ref (uids);
	}

	g_ptr_array_unref (uids);

	return result;
}

static GPtrArray *
ews_search_gather_words (CamelSExpResult **argv,
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
ews_search_body_contains (CamelSExp *sexp,
			  gint argc,
			  CamelSExpResult **argv,
			  CamelFolderSearch *search)
{
	CamelEwsSearch *ews_search = CAMEL_EWS_SEARCH (search);
	CamelEwsStore *ews_store;
	CamelSExpResult *result;
	GPtrArray *words;

	/* Always do body-search server-side */
	if (ews_search->priv->local_data_search) {
		*ews_search->priv->local_data_search = -1;
		return ews_search_result_match_none (sexp, search);
	}

	/* Match everything if argv = [""] */
	if (argc == 1 && argv[0]->value.string[0] == '\0')
		return ews_search_result_match_all (sexp, search);

	/* Match nothing if empty argv or empty summary. */
	if (argc == 0 || camel_folder_search_get_summary_empty (search))
		return ews_search_result_match_none (sexp, search);

	ews_store = camel_ews_search_ref_store (CAMEL_EWS_SEARCH (search));

	/* This will be NULL if we're offline. Search from cache. */
	if (!ews_store) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_ews_search_parent_class)->
			body_contains (sexp, argc, argv, search);
	}

	words = ews_search_gather_words (argv, 0, argc);

	result = ews_search_process_criteria (sexp, search, ews_store, words);

	g_ptr_array_free (words, TRUE);
	g_object_unref (ews_store);

	return result;
}

static void
camel_ews_search_class_init (CamelEwsSearchClass *class)
{
	GObjectClass *object_class;
	CamelFolderSearchClass *search_class;

	g_type_class_add_private (class, sizeof (CamelEwsSearchPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_search_set_property;
	object_class->get_property = ews_search_get_property;
	object_class->dispose = ews_search_dispose;
	object_class->finalize = ews_search_finalize;

	search_class = CAMEL_FOLDER_SEARCH_CLASS (class);
	search_class->body_contains = ews_search_body_contains;

	g_object_class_install_property (
		object_class,
		PROP_STORE,
		g_param_spec_object (
			"store",
			"EWS Store",
			"EWS Store for server-side searches",
			CAMEL_TYPE_EWS_STORE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_ews_search_init (CamelEwsSearch *search)
{
	search->priv = CAMEL_EWS_SEARCH_GET_PRIVATE (search);
	search->priv->local_data_search = NULL;

	g_weak_ref_init (&search->priv->ews_store, NULL);
}

/**
 * camel_ews_search_new:
 * @ews_store: a #CamelEwsStore to which the search belongs
 *
 * Returns a new #CamelEwsSearch instance.
 *
 * Returns: a new #CamelEwsSearch
 *
 * Since: 3.24
 **/
CamelFolderSearch *
camel_ews_search_new (CamelEwsStore *ews_store)
{
	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), NULL);

	return g_object_new (
		CAMEL_TYPE_EWS_SEARCH,
		"store", ews_store,
		NULL);
}

/**
 * camel_ews_search_ref_store:
 * @search: a #CamelEwsSearch
 *
 * Returns a #CamelEwsStore to use for server-side searches,
 * or %NULL when the store is offline.
 *
 * The returned #CamelEwsStore is referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelEwsStore, or %NULL
 *
 * Since: 3.24
 **/
CamelEwsStore *
camel_ews_search_ref_store (CamelEwsSearch *search)
{
	CamelEwsStore *ews_store;

	g_return_val_if_fail (CAMEL_IS_EWS_SEARCH (search), NULL);

	ews_store = g_weak_ref_get (&search->priv->ews_store);

	if (ews_store && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		g_clear_object (&ews_store);

	return ews_store;
}

/**
 * camel_ews_search_set_store:
 * @search: a #CamelEwsSearch
 * @ews_store: a #CamelEwsStore, or %NULL
 *
 * Sets a #CamelEwsStore to use for server-side searches. Generally
 * this is set for the duration of a single search when online, and then
 * reset to %NULL.
 *
 * Since: 3.24
 **/
void
camel_ews_search_set_store (CamelEwsSearch *search,
			    CamelEwsStore *ews_store)
{
	g_return_if_fail (CAMEL_IS_EWS_SEARCH (search));

	if (ews_store != NULL)
		g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	g_weak_ref_set (&search->priv->ews_store, ews_store);

	g_object_notify (G_OBJECT (search), "store");
}

/**
 * camel_ews_search_set_cancellable_and_error:
 * @search: a #CamelEwsSearch
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
 * Since: 3.24
 **/
void
camel_ews_search_set_cancellable_and_error (CamelEwsSearch *search,
					    GCancellable *cancellable,
					    GError **error)
{
	g_return_if_fail (CAMEL_IS_EWS_SEARCH (search));

	if (cancellable)
		g_return_if_fail (G_IS_CANCELLABLE (cancellable));

	search->priv->cancellable = cancellable;
	search->priv->error = error;
}
