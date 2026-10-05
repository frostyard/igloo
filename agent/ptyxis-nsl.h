/* ptyxis-nsl.h
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

/* Machines see shared host trees under this prefix. */
#define PTYXIS_NSL_HOST_PREFIX "/mnt/host"

typedef struct _PtyxisNslMachine
{
  char     *name;
  char     *state;
  char     *image;
  char     *tier;
  gboolean  is_default;
} PtyxisNslMachine;

typedef struct _PtyxisNslImage
{
  char     **selectors;
  char      *build;
  gboolean   cached;
} PtyxisNslImage;

typedef struct _PtyxisNslSetting
{
  char *key;
  char *value;
  char *source;
} PtyxisNslSetting;

void       ptyxis_nsl_machine_free         (PtyxisNslMachine  *machine);
void       ptyxis_nsl_image_free           (PtyxisNslImage    *image);
void       ptyxis_nsl_setting_free         (PtyxisNslSetting  *setting);
char      *ptyxis_nsl_find_program         (void);
char      *ptyxis_nsl_dup_home             (void);
gboolean   ptyxis_nsl_is_valid_name        (const char        *name);
GPtrArray *ptyxis_nsl_parse_machines       (const char        *text);
GPtrArray *ptyxis_nsl_parse_images         (const char        *text);
GPtrArray *ptyxis_nsl_parse_config         (const char        *text,
                                            char             **path);
char      *ptyxis_nsl_translate_directory  (const char        *directory);
char      *ptyxis_nsl_translate_uri        (const char        *uri);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PtyxisNslMachine, ptyxis_nsl_machine_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PtyxisNslImage, ptyxis_nsl_image_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PtyxisNslSetting, ptyxis_nsl_setting_free)

G_END_DECLS
