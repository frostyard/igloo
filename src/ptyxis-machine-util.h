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

#include <glib.h>

G_BEGIN_DECLS

#define PTYXIS_MACHINE_CONTAINER_PREFIX "nsl:"

gboolean  ptyxis_machine_is_valid_name        (const char         *name);
gboolean  ptyxis_machine_is_valid_user        (const char         *user);
char     *ptyxis_machine_describe_image       (const char         *build);
char     *ptyxis_machine_describe_selectors   (const char * const *selectors);
char     *ptyxis_machine_suggest_name         (const char         *selector,
                                               const char * const *existing);
char     *ptyxis_machine_dup_container_id     (const char         *name);

G_END_DECLS
