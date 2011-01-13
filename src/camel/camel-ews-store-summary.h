#ifndef CAMEL_EWS_STORE_SUMMARY_H
#define CAMEL_EWS_STORE_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_STORE_SUMMARY \
	(camel_ews_store_summary_get_type ())
#define CAMEL_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummary))
#define CAMEL_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))
#define CAMEL_IS_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_IS_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_EWS_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelEwsStoreSummary CamelEwsStoreSummary;
typedef struct _CamelEwsStoreSummaryClass CamelEwsStoreSummaryClass;
typedef struct _CamelEwsStoreSummaryPrivate CamelEwsStoreSummaryPrivate;

struct _CamelEwsStoreSummary {
	CamelObject parent;
	CamelEwsStoreSummaryPrivate *priv;
};

struct _CamelEwsStoreSummaryClass {
	CamelObjectClass parent_class;
};

GType		camel_ews_store_summary_get_type	(void);

CamelEwsStoreSummary *	
		camel_ews_store_summary_new	(const gchar *path);
gboolean	camel_ews_store_summary_load	(CamelEwsStoreSummary *ews_summary,
						 GError **error);
gboolean	camel_ews_store_summary_save	(CamelEwsStoreSummary *ews_summary,
						 GError **error);
gboolean	camel_ews_store_summary_clear	(CamelEwsStoreSummary *ews_summary);

void		camel_ews_store_summary_set_folder_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 const gchar *display_name);
void		camel_ews_store_summary_set_parent_folder_id	
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 const gchar *parent_fid);
void		camel_ews_store_summary_set_change_key	
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 const gchar *change_key);
void		camel_ews_store_summary_set_folder_flags
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 flags);
void		camel_ews_store_summary_set_folder_unread
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 unread);
void		camel_ews_store_summary_set_folder_total
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 guint64 total);

const gchar *	camel_ews_store_summary_get_folder_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
const gchar *	camel_ews_store_summary_get_folder_id
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
const gchar *	camel_ews_store_summary_get_parent_folder_id
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
const gchar *	camel_ews_store_summary_get_change_key
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_flags
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_unread
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_total
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);
GSList *	camel_ews_store_summary_get_folders	
						(CamelEwsStoreSummary *ews_summary);


void		camel_ews_store_summary_store_string_val
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *key, 
						 const gchar *value);

const gchar *	camel_ews_store_summary_get_string_val
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *key,
						 GError **error);

gboolean	camel_ews_store_summary_remove_folder
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name,
						 GError **error);

void		camel_ews_store_summary_new_folder
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_full_name, 
						 const gchar *folder_id);

const gchar *	camel_ews_store_summary_get_folder_name_from_id	
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id);

G_END_DECLS

#endif /* CAMEL_EWS_STORE_SUMMARY_H */
