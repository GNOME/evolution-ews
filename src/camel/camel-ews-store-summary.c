#include <glib.h>
#include <gio/gio.h>
#include "camel-ews-store-summary.h"

#define SLOCK(x) (g_static_rec_mutex_lock(&(x)->priv->s_lock))
#define S_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->s_lock))

struct _CamelEwsStoreSummaryPrivate {
	GKeyFile *key_file;
	gboolean dirty;
	GStaticRecMutex s_lock;
};

G_DEFINE_TYPE (CamelEwsStoreSummary, camel_ews_store_summary, CAMEL_TYPE_OBJECT)

static void
ews_store_summary_finalize (GObject *object)
{
	CamelEwsStoreSummary *ews_summary = CAMEL_EWS_STORE_SUMMARY (object);
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;

	g_key_file_free (priv->key_file);
	g_static_rec_mutex_free (&priv->s_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_summary_parent_class)->finalize (object);
}

static void
camel_ews_store_summary_class_init (CamelEwsStoreSummaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ews_store_summary_finalize;
}

static void
camel_ews_store_summary_init (CamelEwsStoreSummary *ews_summary)
{
	CamelEwsStoreSummaryPrivate *priv;

	priv = g_new0 (CamelEwsStoreSummaryPrivate, 1);
	ews_summary->priv = priv;

	priv->key_file = g_key_file_new ();
	priv->dirty = FALSE;
	g_static_rec_mutex_lock (&priv->s_lock);
}

CamelEwsStoreSummary *
camel_ews_store_summary_new (void)
{
	return g_object_new (CAMEL_TYPE_EWS_STORE_SUMMARY, NULL);
}

