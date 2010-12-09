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

GType                        camel_ews_store_summary_get_type      (void);
CamelEwsStoreSummary      *camel_ews_store_summary_new        (void);

G_END_DECLS

#endif /* CAMEL_EWS_STORE_SUMMARY_H */
