/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_OOO_NOTIFICATOR_H
#define E_M365_OOO_NOTIFICATOR_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

#define E_TYPE_M365_OOO_NOTIFICATOR (e_m365_ooo_notificator_get_type ())
G_DECLARE_FINAL_TYPE (EM365OooNotificator, e_m365_ooo_notificator, E, M365_OOO_NOTIFICATOR, EExtension)

void		e_m365_ooo_notificator_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* E_M365_OOO_NOTIFICATOR_H */
