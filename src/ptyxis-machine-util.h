/* ptyxis-machine-util.h
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

#include <gio/gio.h>

G_BEGIN_DECLS

#define PTYXIS_MACHINE_CONTAINER_PREFIX "nsl:"

gboolean  ptyxis_machine_is_valid_name        (const char         *name);
gboolean  ptyxis_machine_is_valid_user        (const char         *user);
char     *ptyxis_machine_describe_image       (const char         *build);
char     *ptyxis_machine_describe_selectors   (const char * const *selectors);
char     *ptyxis_machine_suggest_name         (const char         *selector,
                                               const char * const *existing);
char     *ptyxis_machine_suggest_import_name  (const char         *file_name,
                                               const char * const *existing);
char     *ptyxis_machine_export_file_name     (const char         *name,
                                               GDateTime          *now);
char     *ptyxis_machine_split_document_path  (const char         *path,
                                               const char         *runtime_dir,
                                               char              **remainder);
char     *ptyxis_machine_compose_host_path    (const char         *host_document_path,
                                               const char         *remainder);
void      ptyxis_machine_host_path_async      (GFile              *file,
                                               GCancellable       *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer            user_data);
char     *ptyxis_machine_host_path_finish     (GAsyncResult       *result,
                                               GError            **error);
char     *ptyxis_machine_dup_container_id     (const char         *name);

char    **ptyxis_machine_logs_argv            (const char         *nsl,
                                               const char         *name);
char    **ptyxis_machine_recover_argv         (const char         *nsl,
                                               const char         *name);
gboolean  ptyxis_machine_can_recover          (const char         *tier,
                                               const char         *state);
gboolean  ptyxis_machine_remove_needs_stop    (const char         *state);
char    **ptyxis_machine_remove_argv          (const char         *nsl,
                                               const char         *name,
                                               const char         *state);

G_END_DECLS
