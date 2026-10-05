/* ptyxis-machine-command-dialog.h
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

#include <adwaita.h>

G_BEGIN_DECLS

#define PTYXIS_TYPE_MACHINE_COMMAND_DIALOG (ptyxis_machine_command_dialog_get_type())

G_DECLARE_FINAL_TYPE (PtyxisMachineCommandDialog, ptyxis_machine_command_dialog, PTYXIS, MACHINE_COMMAND_DIALOG, AdwDialog)

PtyxisMachineCommandDialog *ptyxis_machine_command_dialog_new             (const char                 *title,
                                                                           const char * const         *argv);
void                        ptyxis_machine_command_dialog_set_messages    (PtyxisMachineCommandDialog *self,
                                                                           const char                 *running,
                                                                           const char                 *succeeded,
                                                                           const char                 *failed);
void                        ptyxis_machine_command_dialog_set_open_container
                                                                          (PtyxisMachineCommandDialog *self,
                                                                           const char                 *container_id);

G_END_DECLS
