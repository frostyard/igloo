/* ptyxis-nsl.c
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

#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ptyxis-nsl.h"

/* nsl prints its tables with text/tabwriter: each column starts where the
 * header has a word preceded by at least two spaces, and every row of the
 * table is aligned to those columns. Cells may be empty, so rows are split
 * at the header's column offsets rather than at whitespace.
 */

typedef gboolean (*RowValidator) (char **cells);

void
ptyxis_nsl_machine_free (PtyxisNslMachine *machine)
{
  g_clear_pointer (&machine->name, g_free);
  g_clear_pointer (&machine->state, g_free);
  g_clear_pointer (&machine->image, g_free);
  g_clear_pointer (&machine->tier, g_free);
  g_free (machine);
}

void
ptyxis_nsl_image_free (PtyxisNslImage *image)
{
  g_clear_pointer (&image->selectors, g_strfreev);
  g_clear_pointer (&image->build, g_free);
  g_free (image);
}

void
ptyxis_nsl_setting_free (PtyxisNslSetting *setting)
{
  g_clear_pointer (&setting->key, g_free);
  g_clear_pointer (&setting->value, g_free);
  g_clear_pointer (&setting->source, g_free);
  g_free (setting);
}

static GArray *
column_offsets (const char *header)
{
  GArray *offsets = g_array_new (FALSE, FALSE, sizeof (guint));
  guint zero = 0;

  g_array_append_val (offsets, zero);

  for (guint i = 2; header[i]; i++)
    {
      if (header[i] != ' ' && header[i-1] == ' ' && header[i-2] == ' ')
        g_array_append_val (offsets, i);
    }

  return offsets;
}

static char **
split_row (const char *line,
           GArray     *offsets)
{
  gsize len = strlen (line);
  char **cells = g_new0 (char *, offsets->len + 1);

  for (guint i = 0; i < offsets->len; i++)
    {
      guint begin = g_array_index (offsets, guint, i);
      guint end = i + 1 < offsets->len ? g_array_index (offsets, guint, i + 1) : len;

      if (begin >= len)
        cells[i] = g_strdup ("");
      else
        cells[i] = g_strstrip (g_strndup (line + begin, MIN (end, len) - begin));
    }

  return cells;
}

/* Returns an array of rows, each a NULL-terminated array of cells, for the
 * table whose header line begins with @first_header. Rows end at the first
 * blank line or the first line @validator rejects.
 */
static GPtrArray *
parse_table (const char   *text,
             const char   *first_header,
             RowValidator  validator)
{
  g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func ((GDestroyNotify)g_strfreev);
  g_autoptr(GArray) offsets = NULL;
  g_auto(GStrv) lines = NULL;
  gsize first_len;

  g_return_val_if_fail (first_header != NULL, NULL);

  if (text == NULL)
    return g_steal_pointer (&rows);

  first_len = strlen (first_header);
  lines = g_strsplit (text, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    {
      const char *line = lines[i];

      if (offsets == NULL)
        {
          if (strncmp (line, first_header, first_len) == 0 &&
              (line[first_len] == ' ' || line[first_len] == 0))
            offsets = column_offsets (line);
          continue;
        }

      if (line[0] == 0)
        break;

      {
        char **cells = split_row (line, offsets);

        if (validator != NULL && !validator (cells))
          {
            g_strfreev (cells);
            break;
          }

        g_ptr_array_add (rows, cells);
      }
    }

  return g_steal_pointer (&rows);
}

gboolean
ptyxis_nsl_is_valid_name (const char *name)
{
  gsize len;

  if (name == NULL)
    return FALSE;

  len = strlen (name);

  /* A lowercase ASCII letter, then lowercase letters, digits or interior
   * hyphens, at most 24 characters.
   */
  if (len == 0 || len > 24 || !g_ascii_islower (name[0]) || name[len-1] == '-')
    return FALSE;

  for (gsize i = 1; i < len; i++)
    {
      if (!g_ascii_islower (name[i]) && !g_ascii_isdigit (name[i]) && name[i] != '-')
        return FALSE;
    }

  return TRUE;
}

static gboolean
machine_row_is_valid (char **cells)
{
  return cells[0] != NULL && ptyxis_nsl_is_valid_name (cells[0]);
}

GPtrArray *
ptyxis_nsl_parse_machines (const char *text)
{
  g_autoptr(GPtrArray) machines = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_machine_free);
  g_autoptr(GPtrArray) rows = parse_table (text, "MACHINE", machine_row_is_valid);

  for (guint i = 0; i < rows->len; i++)
    {
      char **cells = g_ptr_array_index (rows, i);
      guint n_cells = g_strv_length (cells);
      PtyxisNslMachine *machine;

      /* MACHINE STATE IMAGE TIER DEFAULT */
      if (n_cells < 5)
        continue;

      machine = g_new0 (PtyxisNslMachine, 1);
      machine->name = g_strdup (cells[0]);
      machine->state = g_strdup (cells[1]);
      machine->image = g_strdup (cells[2]);
      machine->tier = g_strdup (cells[3]);
      machine->is_default = g_strcmp0 (cells[4], "*") == 0;

      g_ptr_array_add (machines, machine);
    }

  return g_steal_pointer (&machines);
}

static gboolean
image_row_is_valid (char **cells)
{
  return g_strcmp0 (cells[0], "vm") == 0 || g_strcmp0 (cells[0], "machine") == 0;
}

GPtrArray *
ptyxis_nsl_parse_images (const char *text)
{
  g_autoptr(GPtrArray) images = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_image_free);
  g_autoptr(GPtrArray) rows = parse_table (text, "KIND", image_row_is_valid);

  for (guint i = 0; i < rows->len; i++)
    {
      char **cells = g_ptr_array_index (rows, i);
      g_auto(GStrv) selectors = NULL;
      g_autoptr(GPtrArray) trimmed = NULL;
      PtyxisNslImage *image;

      /* KIND SELECTORS BUILD CACHED; only machine images can be created */
      if (g_strv_length (cells) < 4 || g_strcmp0 (cells[0], "machine") != 0)
        continue;

      selectors = g_strsplit (cells[1], ",", 0);
      trimmed = g_ptr_array_new ();

      for (guint j = 0; selectors[j]; j++)
        {
          g_strstrip (selectors[j]);

          if (selectors[j][0] != 0)
            g_ptr_array_add (trimmed, g_strdup (selectors[j]));
        }

      if (trimmed->len == 0)
        continue;

      g_ptr_array_add (trimmed, NULL);

      image = g_new0 (PtyxisNslImage, 1);
      image->selectors = (char **)g_ptr_array_free (g_steal_pointer (&trimmed), FALSE);
      image->build = g_strdup (cells[2]);
      image->cached = g_strcmp0 (cells[3], "yes") == 0;

      g_ptr_array_add (images, image);
    }

  return g_steal_pointer (&images);
}

static gboolean
setting_row_is_valid (char **cells)
{
  const char *key = cells[0];
  const char *dot;

  if (key == NULL || !(dot = strchr (key, '.')) || dot == key || dot[1] == 0)
    return FALSE;

  for (const char *c = key; *c; c++)
    {
      if (!g_ascii_islower (*c) && *c != '_' && *c != '.')
        return FALSE;
    }

  return TRUE;
}

GPtrArray *
ptyxis_nsl_parse_config (const char  *text,
                         char       **path)
{
  g_autoptr(GPtrArray) settings = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_setting_free);
  g_autoptr(GPtrArray) rows = parse_table (text, "SETTING", setting_row_is_valid);

  if (path != NULL)
    {
      static const char prefix[] = "Configuration file: ";
      const char *line = text ? strstr (text, prefix) : NULL;

      *path = NULL;

      if (line != NULL)
        {
          const char *begin = line + strlen (prefix);
          const char *end = strchr (begin, '\n');
          g_autofree char *value = end ? g_strndup (begin, end - begin) : g_strdup (begin);

          if (g_str_has_suffix (value, " (absent)"))
            value[strlen (value) - strlen (" (absent)")] = 0;

          *path = g_steal_pointer (&value);
        }
    }

  for (guint i = 0; i < rows->len; i++)
    {
      char **cells = g_ptr_array_index (rows, i);
      PtyxisNslSetting *setting;

      /* SETTING VALUE SOURCE */
      if (g_strv_length (cells) < 3)
        continue;

      setting = g_new0 (PtyxisNslSetting, 1);
      setting->key = g_strdup (cells[0]);
      setting->value = g_strdup (cells[1]);
      setting->source = g_strdup (cells[2]);

      g_ptr_array_add (settings, setting);
    }

  return g_steal_pointer (&settings);
}

static gboolean
is_executable_file (const char *path)
{
  return g_file_test (path, G_FILE_TEST_IS_EXECUTABLE) &&
         !g_file_test (path, G_FILE_TEST_IS_DIR);
}

/**
 * ptyxis_nsl_find_program:
 *
 * Locates nsl on the host. The agent may inherit a minimal PATH from the
 * session (for example through flatpak-spawn), so the install locations
 * nsl documents are checked as well: Homebrew and release tarballs.
 *
 * Returns: (transfer full) (nullable): the path to nsl, or %NULL
 */
char *
ptyxis_nsl_find_program (void)
{
  const char *home = g_get_home_dir ();
  g_autofree char *path = NULL;
  g_autofree char *local_bin = g_build_filename (home, ".local", "bin", "nsl", NULL);
  g_autofree char *user_brew = g_build_filename (home, ".linuxbrew", "bin", "nsl", NULL);
  const char *candidates[] = {
    local_bin,
    "/home/linuxbrew/.linuxbrew/bin/nsl",
    user_brew,
    "/usr/local/bin/nsl",
    "/usr/bin/nsl",
  };

  if ((path = g_find_program_in_path ("nsl")))
    return g_steal_pointer (&path);

  for (guint i = 0; i < G_N_ELEMENTS (candidates); i++)
    {
      if (is_executable_file (candidates[i]))
        return g_strdup (candidates[i]);
    }

  return NULL;
}

/**
 * ptyxis_nsl_dup_home:
 *
 * Gets nsl's state directory, NSL_HOME, which defaults to
 * `$XDG_DATA_HOME/nsl`.
 *
 * Returns: (transfer full): the state directory
 */
char *
ptyxis_nsl_dup_home (void)
{
  const char *home = g_getenv ("NSL_HOME");

  if (home != NULL && g_path_is_absolute (home))
    return g_strdup (home);

  return g_build_filename (g_get_user_data_dir (), "nsl", NULL);
}

static gboolean
same_file (const char *a,
           const char *b)
{
  struct stat x, y;

  return stat (a, &x) == 0 &&
         stat (b, &y) == 0 &&
         x.st_dev == y.st_dev &&
         x.st_ino == y.st_ino;
}

static char *
canonicalize (const char *path)
{
  char resolved[PATH_MAX];

  if (realpath (path, resolved) == NULL)
    return NULL;

  return g_strdup (resolved);
}

static GPtrArray *
host_shares (void)
{
  GPtrArray *shares = g_ptr_array_new_with_free_func (g_free);
  g_autofree char *media = g_build_filename ("/run/media", g_get_user_name (), NULL);
  const char *trees[] = { g_get_home_dir (), media, "/mnt" };

  /* The same trees nsl shares with machines that are not isolated:
   * the home directory, removable media and /mnt.
   */
  for (guint i = 0; i < G_N_ELEMENTS (trees); i++)
    {
      g_autofree char *canonical = NULL;
      gboolean seen = FALSE;

      if (!g_file_test (trees[i], G_FILE_TEST_IS_DIR) ||
          !(canonical = canonicalize (trees[i])))
        continue;

      for (guint j = 0; j < shares->len; j++)
        seen |= g_strcmp0 (g_ptr_array_index (shares, j), canonical) == 0;

      if (!seen)
        g_ptr_array_add (shares, g_steal_pointer (&canonical));
    }

  return shares;
}

/**
 * ptyxis_nsl_translate_directory:
 * @directory: an absolute host directory
 *
 * Maps a host directory to the path a machine that is not isolated sees,
 * as nsl does: ancestors are matched to the shared trees by device and
 * inode so that symlink and bind-mount aliases such as /home for /var/home
 * translate.
 *
 * Returns: (transfer full) (nullable): the machine path below /mnt/host,
 *   or %NULL when @directory is not in a shared tree
 */
char *
ptyxis_nsl_translate_directory (const char *directory)
{
  g_autoptr(GPtrArray) shares = NULL;
  g_autofree char *current = NULL;
  g_autofree char *rest = NULL;

  if (directory == NULL || !g_path_is_absolute (directory))
    return NULL;

  shares = host_shares ();
  current = g_strdup (directory);
  rest = g_strdup ("");

  /* Drop trailing slashes, other than the root */
  for (gsize len = strlen (current); len > 1 && current[len-1] == '/'; len--)
    current[len-1] = 0;

  for (;;)
    {
      g_autofree char *parent = NULL;
      g_autofree char *base = NULL;
      char *next_rest;

      for (guint i = 0; i < shares->len; i++)
        {
          const char *share = g_ptr_array_index (shares, i);

          if (same_file (current, share))
            {
              g_autofree char *guest = g_strconcat (PTYXIS_NSL_HOST_PREFIX, share, NULL);

              if (rest[0] == 0)
                return g_steal_pointer (&guest);

              return g_build_filename (guest, rest, NULL);
            }
        }

      if (g_strcmp0 (current, "/") == 0)
        return NULL;

      parent = g_path_get_dirname (current);
      base = g_path_get_basename (current);

      if (rest[0] == 0)
        next_rest = g_strdup (base);
      else
        next_rest = g_build_filename (base, rest, NULL);

      g_free (rest);
      rest = next_rest;

      g_free (current);
      current = g_steal_pointer (&parent);
    }
}

/**
 * ptyxis_nsl_translate_uri:
 * @uri: a URI reported from inside a machine
 *
 * Maps `file://` URIs below /mnt/host to the host path they name. Other
 * URIs are returned unchanged.
 *
 * Returns: (transfer full): the translated URI
 */
char *
ptyxis_nsl_translate_uri (const char *uri)
{
  g_autofree char *path = NULL;
  static const gsize prefix_len = sizeof PTYXIS_NSL_HOST_PREFIX - 1;

  if (uri == NULL)
    return NULL;

  if (!g_str_has_prefix (uri, "file://") ||
      !(path = g_filename_from_uri (uri, NULL, NULL)) ||
      strncmp (path, PTYXIS_NSL_HOST_PREFIX, prefix_len) != 0 ||
      (path[prefix_len] != '/' && path[prefix_len] != 0))
    return g_strdup (uri);

  if (path[prefix_len] == 0)
    return g_filename_to_uri ("/", NULL, NULL);

  return g_filename_to_uri (path + prefix_len, NULL, NULL);
}
