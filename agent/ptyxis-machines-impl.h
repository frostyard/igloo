/* ptyxis-machines-impl.h
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
#include "ptyxis-nsl-provider.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_MACHINES_IMPL (ptyxis_machines_impl_get_type())

G_DECLARE_FINAL_TYPE (PtyxisMachinesImpl, ptyxis_machines_impl, PTYXIS, MACHINES_IMPL, PtyxisIpcMachinesSkeleton)

PtyxisMachinesImpl *ptyxis_machines_impl_new             (PtyxisNslProvider *provider);
char               *ptyxis_machines_impl_dup_config_path (void);
char               *ptyxis_machines_impl_apply_settings  (const char        *contents,
                                                          GVariant          *settings,
                                                          GError           **error);

G_END_DECLS
