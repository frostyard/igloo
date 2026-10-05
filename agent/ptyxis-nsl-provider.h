/* ptyxis-nsl-provider.h
 *
 * Copyright 2026 Brian Ketelsen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "ptyxis-container-provider.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_NSL_PROVIDER (ptyxis_nsl_provider_get_type())

G_DECLARE_FINAL_TYPE (PtyxisNslProvider, ptyxis_nsl_provider, PTYXIS, NSL_PROVIDER, PtyxisContainerProvider)

PtyxisContainerProvider *ptyxis_nsl_provider_new          (void);
const char              *ptyxis_nsl_provider_get_nsl_path (PtyxisNslProvider    *self);
void                     ptyxis_nsl_provider_rescan       (PtyxisNslProvider    *self);
void                     ptyxis_nsl_provider_queue_update (PtyxisNslProvider    *self);
void                     ptyxis_nsl_provider_list_async   (PtyxisNslProvider    *self,
                                                           GCancellable         *cancellable,
                                                           GAsyncReadyCallback   callback,
                                                           gpointer              user_data);
GPtrArray               *ptyxis_nsl_provider_list_finish  (PtyxisNslProvider    *self,
                                                           GAsyncResult         *result,
                                                           GError              **error);
void                     ptyxis_nsl_provider_load_names   (PtyxisNslProvider    *self);

G_END_DECLS
