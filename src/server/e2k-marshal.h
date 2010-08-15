
#ifndef __e2k_marshal_MARSHAL_H__
#define __e2k_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* NONE:INT,INT (./e2k-marshal.list:1) */
extern void e2k_marshal_VOID__INT_INT (GClosure     *closure,
                                       GValue       *return_value,
                                       guint         n_param_values,
                                       const GValue *param_values,
                                       gpointer      invocation_hint,
                                       gpointer      marshal_data);
#define e2k_marshal_NONE__INT_INT	e2k_marshal_VOID__INT_INT

/* NONE:INT,STRING,STRING (./e2k-marshal.list:2) */
extern void e2k_marshal_VOID__INT_STRING_STRING (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);
#define e2k_marshal_NONE__INT_STRING_STRING	e2k_marshal_VOID__INT_STRING_STRING

G_END_DECLS

#endif /* __e2k_marshal_MARSHAL_H__ */

