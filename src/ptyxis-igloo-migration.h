/* ptyxis-igloo-migration.h
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

#include <glib.h>

G_BEGIN_DECLS

gboolean ptyxis_igloo_migration_rename_groups (GKeyFile   *key_file,
                                               const char *old_path,
                                               const char *new_path);
char    *ptyxis_igloo_migration_rewrite_lines (const char *contents,
                                               const char *old_line,
                                               const char *new_line);
void     ptyxis_igloo_migration_run           (const char        *config_dir,
                                               const char        *data_dir,
                                               const char * const *terminal_lists,
                                               const char        *old_id,
                                               const char        *new_id,
                                               const char        *old_path,
                                               const char        *new_path);
void     ptyxis_igloo_migrate                 (void);

/* for tests only: called with the migration lock held, before it reads
 * anything; pass NULL to remove it again */
void     ptyxis_igloo_migration_set_locked_hook (GFunc       hook,
                                                 gpointer    data);
/* for tests only: the locked descriptor, which the caller closes, or -1 when
 * another process holds the migration lock */
int      ptyxis_igloo_migration_try_lock        (const char *config_dir);

G_END_DECLS
