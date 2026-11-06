/* ptyxis-igloo-migration.c
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "ptyxis-igloo-migration.h"

/* Igloo was named io.github.frostyard.Igloo before it followed core ADR-0016
 * (application IDs are org.frostyard.<AppName>). Flatpak moves ~/.var/app/OLD
 * to ~/.var/app/NEW after an end-of-life rebase, but inside that folder 3
 * things keep the old name: the GSettings groups of the GLib keyfile backend,
 * the folders named by the application ID, and the xdg-terminals.list lines.
 * This file renames them once. Every step is idempotent and no step stops the
 * program: a failure is logged and the rest goes on.
 */

/* ptyxis-util.c is the home of the xdg-terminals list paths, but its header
 * pulls in VTE and GTK, which the unit test does not link. The declaration is
 * repeated here so that this file compiles with GLib and GIO alone.
 */
char **ptyxis_get_xdg_terminals_list_paths (void);

/* The migration holds this file's lock while it reads and writes, so that only
 * 1 process migrates at a time.
 */
#define MIGRATION_LOCK_FILE ".igloo-migration.lock"

static GFunc migration_locked_hook;
static gpointer migration_locked_hook_data;

void
ptyxis_igloo_migration_set_locked_hook (GFunc    hook,
                                        gpointer data)
{
  migration_locked_hook = hook;
  migration_locked_hook_data = data;
}

static char *
migration_lock_path (const char *config_dir)
{
  return g_build_filename (config_dir, MIGRATION_LOCK_FILE, NULL);
}

/* Create @config_dir when needed and open the lock file. The caller owns the
 * returned descriptor and the lock it takes on it. This is the 1 place that
 * names the lock file.
 */
static int
migration_lock_open (const char *config_dir,
                     GError    **error)
{
  g_autofree char *path = migration_lock_path (config_dir);
  int fd;

  if (g_mkdir_with_parents (config_dir, 0700) != 0)
    {
      int saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Could not create %s: %s", config_dir, g_strerror (saved_errno));
      return -1;
    }

  do
    fd = open (path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  while (fd == -1 && errno == EINTR);

  if (fd == -1)
    {
      int saved_errno = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Could not open %s: %s", path, g_strerror (saved_errno));
    }

  return fd;
}

/* Take the migration lock without waiting. Returns the locked descriptor, which
 * the caller closes, or -1 when another process holds the lock. For tests only:
 * the migration itself holds the lock for the whole run.
 */
int
ptyxis_igloo_migration_try_lock (const char *config_dir)
{
  g_autoptr(GError) error = NULL;
  int fd;

  g_return_val_if_fail (config_dir != NULL, -1);

  fd = migration_lock_open (config_dir, &error);

  if (fd == -1)
    {
      g_warning ("%s", error->message);
      return -1;
    }

  if (flock (fd, LOCK_EX | LOCK_NB) != 0)
    {
      close (fd);
      return -1;
    }

  return fd;
}

/* The group name of the keyfile backend is the settings path without the
 * leading and trailing "/": "/io/github/frostyard/Igloo/" is the group prefix
 * "io/github/frostyard/Igloo".
 */
static char *
settings_path_to_group_prefix (const char *path)
{
  const char *start;
  gsize len;

  g_return_val_if_fail (path != NULL, NULL);

  start = path;
  len = strlen (path);

  if (len > 0 && start[0] == '/')
    {
      start++;
      len--;
    }

  while (len > 0 && start[len - 1] == '/')
    len--;

  return g_strndup (start, len);
}

/* TRUE when @group is @prefix itself or a child of it. A group that only
 * starts with the characters of @prefix, such as
 * "io/github/frostyard/IglooX", is not a child.
 */
static gboolean
group_has_prefix (const char *group,
                  const char *prefix)
{
  return strcmp (group, prefix) == 0 ||
         (g_str_has_prefix (group, prefix) && group[strlen (prefix)] == '/');
}

gboolean
ptyxis_igloo_migration_rename_groups (GKeyFile   *key_file,
                                      const char *old_path,
                                      const char *new_path)
{
  g_autofree char *old_prefix = NULL;
  g_autofree char *new_prefix = NULL;
  g_auto(GStrv) groups = NULL;
  gboolean changed = FALSE;

  g_return_val_if_fail (key_file != NULL, FALSE);
  g_return_val_if_fail (old_path != NULL, FALSE);
  g_return_val_if_fail (new_path != NULL, FALSE);

  old_prefix = settings_path_to_group_prefix (old_path);
  new_prefix = settings_path_to_group_prefix (new_path);

  if (old_prefix[0] == 0 || new_prefix[0] == 0)
    return FALSE;

  groups = g_key_file_get_groups (key_file, NULL);

  /* A group of the new name means the migration already ran, or the renamed
   * application wrote settings. Leave both names alone then.
   */
  for (guint i = 0; groups[i] != NULL; i++)
    {
      if (group_has_prefix (groups[i], new_prefix))
        return FALSE;
    }

  for (guint i = 0; groups[i] != NULL; i++)
    {
      const char *group = groups[i];
      g_autofree char *new_group = NULL;
      g_auto(GStrv) keys = NULL;

      if (!group_has_prefix (group, old_prefix))
        continue;

      new_group = g_strconcat (new_prefix, group + strlen (old_prefix), NULL);
      keys = g_key_file_get_keys (key_file, group, NULL, NULL);

      if (keys == NULL)
        continue;

      /* Values are copied as they are written: g_key_file_get_value() returns
       * the raw string, so nothing is reformatted.
       */
      for (guint k = 0; keys[k] != NULL; k++)
        {
          g_autofree char *value = g_key_file_get_value (key_file, group, keys[k], NULL);

          if (value != NULL)
            g_key_file_set_value (key_file, new_group, keys[k], value);
        }

      g_key_file_remove_group (key_file, group, NULL);
      changed = TRUE;
    }

  return changed;
}

char *
ptyxis_igloo_migration_rewrite_lines (const char *contents,
                                      const char *old_line,
                                      const char *new_line)
{
  g_autoptr(GString) result = NULL;
  const char *iter;
  gsize old_len;
  gboolean matched = FALSE;

  g_return_val_if_fail (contents != NULL, NULL);
  g_return_val_if_fail (old_line != NULL, NULL);
  g_return_val_if_fail (new_line != NULL, NULL);

  result = g_string_new (NULL);
  old_len = strlen (old_line);

  for (iter = contents; *iter != 0; )
    {
      const char *end = strchr (iter, '\n');
      gsize len = end != NULL ? (gsize) (end - iter) : strlen (iter);

      /* Only a whole line matches; a line with more text stays as it is. */
      if (len == old_len && strncmp (iter, old_line, len) == 0)
        {
          g_string_append (result, new_line);
          matched = TRUE;
        }
      else
        {
          g_string_append_len (result, iter, len);
        }

      if (end == NULL)
        break;

      g_string_append_c (result, '\n');
      iter = end + 1;
    }

  if (!matched)
    return NULL;

  return g_string_free (g_steal_pointer (&result), FALSE);
}

/* Move @old_dir to @new_dir when the old one exists and the new one does not.
 * The application's files stay where they are; only the name that holds them
 * changes.
 */
static void
move_directory (const char *old_dir,
                const char *new_dir)
{
  if (!g_file_test (old_dir, G_FILE_TEST_EXISTS))
    return;

  if (g_file_test (new_dir, G_FILE_TEST_EXISTS))
    return;

  if (g_rename (old_dir, new_dir) != 0)
    g_warning ("Could not move %s to %s: %s", old_dir, new_dir, g_strerror (errno));
}

static void
migrate_settings_keyfile (const char *keyfile_path,
                          const char *old_path,
                          const char *new_path)
{
  g_autoptr(GKeyFile) key_file = NULL;
  g_autofree char *contents = NULL;
  gsize contents_len = 0;
  g_autoptr(GError) error = NULL;

  if (!g_file_test (keyfile_path, G_FILE_TEST_EXISTS))
    return;

  key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, keyfile_path, G_KEY_FILE_KEEP_COMMENTS, &error))
    {
      g_warning ("Could not read %s: %s", keyfile_path, error->message);
      return;
    }

  if (!ptyxis_igloo_migration_rename_groups (key_file, old_path, new_path))
    return;

  contents = g_key_file_to_data (key_file, &contents_len, &error);

  if (contents == NULL)
    {
      g_warning ("Could not convert %s: %s", keyfile_path, error->message);
      return;
    }

  if (!g_file_set_contents (keyfile_path, contents, contents_len, &error))
    g_warning ("Could not write %s: %s", keyfile_path, error->message);
}

static void
migrate_terminals_list (const char *path,
                        const char *old_id,
                        const char *new_id)
{
  g_autofree char *old_line = NULL;
  g_autofree char *new_line = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *rewritten = NULL;
  g_autoptr(GError) error = NULL;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return;

  if (!g_file_get_contents (path, &contents, NULL, &error))
    {
      g_warning ("Could not read %s: %s", path, error->message);
      return;
    }

  old_line = g_strdup_printf ("%s.desktop:new-window", old_id);
  new_line = g_strdup_printf ("%s.desktop:new-window", new_id);
  rewritten = ptyxis_igloo_migration_rewrite_lines (contents, old_line, new_line);

  if (rewritten == NULL)
    return;

  if (!g_file_set_contents (path, rewritten, -1, &error))
    g_warning ("Could not write %s: %s", path, error->message);
}

static gboolean
any_terminals_list_exists (const char * const *terminal_lists)
{
  for (guint i = 0; terminal_lists != NULL && terminal_lists[i] != NULL; i++)
    {
      if (g_file_test (terminal_lists[i], G_FILE_TEST_EXISTS))
        return TRUE;
    }

  return FALSE;
}

void
ptyxis_igloo_migration_run (const char        *config_dir,
                            const char        *data_dir,
                            const char * const *terminal_lists,
                            const char        *old_id,
                            const char        *new_id,
                            const char        *old_path,
                            const char        *new_path)
{
  g_autofree char *old_config = NULL;
  g_autofree char *new_config = NULL;
  g_autofree char *old_data = NULL;
  g_autofree char *new_data = NULL;
  g_autofree char *keyfile_path = NULL;
  g_autoptr(GError) error = NULL;
  gboolean have_old = FALSE;
  int lock_fd;

  g_return_if_fail (config_dir != NULL);
  g_return_if_fail (data_dir != NULL);
  g_return_if_fail (old_id != NULL);
  g_return_if_fail (new_id != NULL);
  g_return_if_fail (old_path != NULL);
  g_return_if_fail (new_path != NULL);

  /* The folders named by the application ID hold session.gvariant and the
   * user palettes. Flatpak already moved ~/.var/app, so these are relative to
   * the new folder.
   */
  old_config = g_build_filename (config_dir, old_id, NULL);
  new_config = g_build_filename (config_dir, new_id, NULL);
  old_data = g_build_filename (data_dir, old_id, NULL);
  new_data = g_build_filename (data_dir, new_id, NULL);
  keyfile_path = g_build_filename (config_dir, "glib-2.0", "settings", "keyfile", NULL);

  /* Check cheaply, before the lock costs anything, that something of the old
   * name can exist. After a migration nothing does, and no file is created.
   */
  have_old = g_file_test (old_config, G_FILE_TEST_EXISTS) ||
             g_file_test (old_data, G_FILE_TEST_EXISTS) ||
             g_file_test (keyfile_path, G_FILE_TEST_EXISTS) ||
             any_terminals_list_exists (terminal_lists);

  if (!have_old)
    return;

  /* Two Igloo processes can start together on the first run after the update,
   * and each one would read the keyfile, migrate its own copy and write it
   * back. The later write would then undo the settings the other process saved
   * in between (atomic replacement does not help: the snapshot is stale). So
   * only 1 process migrates, and it reads every input after it holds the lock.
   * Without the lock the migration does not run at all.
   */
  lock_fd = migration_lock_open (config_dir, &error);

  if (lock_fd == -1)
    {
      g_warning ("Could not lock the Igloo migration: %s", error->message);
      return;
    }

  while (flock (lock_fd, LOCK_EX) != 0)
    {
      if (errno == EINTR)
        continue;

      g_warning ("Could not lock the Igloo migration: %s", g_strerror (errno));
      close (lock_fd);
      return;
    }

  if (migration_locked_hook != NULL)
    migration_locked_hook (NULL, migration_locked_hook_data);

  move_directory (old_config, new_config);
  move_directory (old_data, new_data);

  /* The Flatpak uses the GLib keyfile backend for GSettings. */
  migrate_settings_keyfile (keyfile_path, old_path, new_path);

  for (guint i = 0; terminal_lists != NULL && terminal_lists[i] != NULL; i++)
    migrate_terminals_list (terminal_lists[i], old_id, new_id);

  close (lock_fd);
}

void
ptyxis_igloo_migrate (void)
{
#ifdef IGLOO_PREVIOUS_APP_ID
  g_auto(GStrv) terminal_lists = ptyxis_get_xdg_terminals_list_paths ();

  ptyxis_igloo_migration_run (g_get_user_config_dir (),
                              g_get_user_data_dir (),
                              (const char * const *)terminal_lists,
                              IGLOO_PREVIOUS_APP_ID,
                              APP_ID,
                              IGLOO_PREVIOUS_SCHEMA_PATH,
                              APP_SCHEMA_PATH);
#endif
}
