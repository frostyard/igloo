/* ptyxis-nsl-container.h
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

#include "ptyxis-agent-ipc.h"

G_BEGIN_DECLS

#define PTYXIS_NSL_CONTAINER_ID_PREFIX "nsl:"

#define PTYXIS_TYPE_NSL_CONTAINER (ptyxis_nsl_container_get_type())

G_DECLARE_FINAL_TYPE (PtyxisNslContainer, ptyxis_nsl_container, PTYXIS, NSL_CONTAINER, PtyxisIpcContainerSkeleton)

PtyxisNslContainer *ptyxis_nsl_container_new          (const char         *nsl_path,
                                                       const char         *name,
                                                       gboolean            isolated);
const char         *ptyxis_nsl_container_get_name     (PtyxisNslContainer *self);
void                ptyxis_nsl_container_set_nsl_path (PtyxisNslContainer *self,
                                                       const char         *nsl_path);
void                ptyxis_nsl_container_set_isolated (PtyxisNslContainer *self,
                                                       gboolean            isolated);
char               *ptyxis_nsl_container_dup_guest_directory
                                                      (PtyxisNslContainer *self,
                                                       const char         *cwd);

G_END_DECLS
