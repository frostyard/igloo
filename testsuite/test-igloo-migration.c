/* test-igloo-migration.c
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

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "ptyxis-igloo-migration.h"

#define OLD_ID "io.github.frostyard.Igloo"
#define NEW_ID "org.frostyard.Igloo"
#define OLD_PATH "/io/github/frostyard/Igloo/"
#define NEW_PATH "/org/frostyard/Igloo/"

/* ptyxis-util.c, which owns this function, needs VTE and GTK, and this test
 * links neither. It stubs the real one: no test here calls
 * ptyxis_igloo_migrate(); the tests hand their own list paths to
 * ptyxis_igloo_migration_run().
 */
char **ptyxis_get_xdg_terminals_list_paths (void);

char **
ptyxis_get_xdg_terminals_list_paths (void)
{
  return g_new0 (char *, 1);
}

static void
remove_tree (const char *path)
{
  GDir *dir;
  const char *name;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return;

  if (!g_file_test (path, G_FILE_TEST_IS_DIR))
    {
      g_remove (path);
      return;
    }

  dir = g_dir_open (path, 0, NULL);

  if (dir != NULL)
    {
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          g_autofree char *child = g_build_filename (path, name, NULL);

          remove_tree (child);
        }

      g_dir_close (dir);
    }

  g_rmdir (path);
}

static void
write_file (const char *path,
            const char *contents)
{
  g_autofree char *parent = g_path_get_dirname (path);
  g_autoptr(GError) error = NULL;

  g_mkdir_with_parents (parent, 0700);
  g_file_set_contents (path, contents, -1, &error);
  g_assert_no_error (error);
}

static char *
read_file (const char *path)
{
  g_autofree char *contents = NULL;
  g_autoptr(GError) error = NULL;

  g_file_get_contents (path, &contents, NULL, &error);
  g_assert_no_error (error);

  return g_steal_pointer (&contents);
}

static void
assert_file_contents (const char *path,
                      const char *expected)
{
  g_autofree char *contents = read_file (path);

  g_assert_cmpstr (contents, ==, expected);
}

static void
test_rename_groups (void)
{
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_autofree char *value = NULL;

  g_key_file_set_value (key_file, "io/github/frostyard/Igloo", "color-scheme", "'dark'");
  g_key_file_set_value (key_file, "io/github/frostyard/Igloo/Shortcuts", "new-tab", "'<Control>t'");
  g_key_file_set_value (key_file, "io/github/frostyard/Igloo/Profiles/aaaa", "label", "'One'");
  g_key_file_set_value (key_file, "io/github/frostyard/Igloo/Profiles/bbbb", "label", "'Two'");
  g_key_file_set_value (key_file, "org/gnome/desktop/interface", "color-scheme", "'prefer-dark'");
  g_key_file_set_value (key_file, "io/github/frostyard/IglooX", "keep", "'yes'");

  g_assert_true (ptyxis_igloo_migration_rename_groups (key_file, OLD_PATH, NEW_PATH));

  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo"));
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo/Shortcuts"));
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo/Profiles/aaaa"));
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo/Profiles/bbbb"));

  g_assert_false (g_key_file_has_group (key_file, "io/github/frostyard/Igloo"));
  g_assert_false (g_key_file_has_group (key_file, "io/github/frostyard/Igloo/Shortcuts"));
  g_assert_false (g_key_file_has_group (key_file, "io/github/frostyard/Igloo/Profiles/aaaa"));

  /* Groups that only share the characters of the old prefix, or belong to
   * someone else, stay as they are.
   */
  g_assert_true (g_key_file_has_group (key_file, "io/github/frostyard/IglooX"));
  g_assert_true (g_key_file_has_group (key_file, "org/gnome/desktop/interface"));

  /* Values move as raw strings: a value is not reformatted. */
  value = g_key_file_get_value (key_file, "org/frostyard/Igloo", "color-scheme", NULL);
  g_assert_cmpstr (value, ==, "'dark'");
  g_clear_pointer (&value, g_free);
  value = g_key_file_get_value (key_file, "org/frostyard/Igloo/Shortcuts", "new-tab", NULL);
  g_assert_cmpstr (value, ==, "'<Control>t'");
  g_clear_pointer (&value, g_free);
  value = g_key_file_get_value (key_file, "org/frostyard/Igloo/Profiles/bbbb", "label", NULL);
  g_assert_cmpstr (value, ==, "'Two'");
  g_clear_pointer (&value, g_free);
  value = g_key_file_get_value (key_file, "io/github/frostyard/IglooX", "keep", NULL);
  g_assert_cmpstr (value, ==, "'yes'");

  /* A second run finds its own work: nothing to change. */
  g_assert_false (ptyxis_igloo_migration_rename_groups (key_file, OLD_PATH, NEW_PATH));
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo"));

  /* A group of the new name means the migration ran before, or the renamed
   * application wrote settings: nothing changes then.
   */
  g_key_file_set_value (key_file, "io/github/frostyard/Igloo", "color-scheme", "'light'");
  g_assert_false (ptyxis_igloo_migration_rename_groups (key_file, OLD_PATH, NEW_PATH));
  g_assert_true (g_key_file_has_group (key_file, "io/github/frostyard/Igloo"));
  g_clear_pointer (&value, g_free);
  value = g_key_file_get_value (key_file, "org/frostyard/Igloo", "color-scheme", NULL);
  g_assert_cmpstr (value, ==, "'dark'");
}

static void
test_rewrite_lines (void)
{
  g_autofree char *rewritten = NULL;

  /* The whole line changes, and a line with more text stays. */
  rewritten = ptyxis_igloo_migration_rewrite_lines ("first\n"
                                                    "io.github.frostyard.Igloo.desktop:new-window\n"
                                                    "io.github.frostyard.Igloo.desktop:new-window extra\n"
                                                    "last\n",
                                                    OLD_ID ".desktop:new-window",
                                                    NEW_ID ".desktop:new-window");
  g_assert_cmpstr (rewritten, ==, "first\n"
                                  "org.frostyard.Igloo.desktop:new-window\n"
                                  "io.github.frostyard.Igloo.desktop:new-window extra\n"
                                  "last\n");

  /* No final newline stays without one. */
  g_clear_pointer (&rewritten, g_free);
  rewritten = ptyxis_igloo_migration_rewrite_lines ("io.github.frostyard.Igloo.desktop:new-window",
                                                    OLD_ID ".desktop:new-window",
                                                    NEW_ID ".desktop:new-window");
  g_assert_cmpstr (rewritten, ==, "org.frostyard.Igloo.desktop:new-window");

  /* An empty file, and a file with no matching line, change nothing. */
  g_clear_pointer (&rewritten, g_free);
  g_assert_null (ptyxis_igloo_migration_rewrite_lines ("",
                                                      OLD_ID ".desktop:new-window",
                                                      NEW_ID ".desktop:new-window"));
  g_assert_null (ptyxis_igloo_migration_rewrite_lines ("org.gnome.TextEditor.desktop:new-window\n",
                                                      OLD_ID ".desktop:new-window",
                                                      NEW_ID ".desktop:new-window"));
}

static void
test_run (void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *tmp = g_dir_make_tmp ("igloo-migration-XXXXXX", &error);
  g_autofree char *config_dir = NULL;
  g_autofree char *data_dir = NULL;
  g_autofree char *old_config = NULL;
  g_autofree char *new_config = NULL;
  g_autofree char *old_data = NULL;
  g_autofree char *new_data = NULL;
  g_autofree char *keyfile_path = NULL;
  g_autofree char *list_path = NULL;
  g_autofree char *session_path = NULL;
  g_autofree char *palette_path = NULL;
  g_autoptr(GKeyFile) key_file = NULL;
  const char *lists[2];

  g_assert_no_error (error);
  g_assert_nonnull (tmp);

  config_dir = g_build_filename (tmp, "config", NULL);
  data_dir = g_build_filename (tmp, "data", NULL);
  old_config = g_build_filename (config_dir, OLD_ID, NULL);
  new_config = g_build_filename (config_dir, NEW_ID, NULL);
  old_data = g_build_filename (data_dir, OLD_ID, NULL);
  new_data = g_build_filename (data_dir, NEW_ID, NULL);
  keyfile_path = g_build_filename (config_dir, "glib-2.0", "settings", "keyfile", NULL);
  list_path = g_build_filename (tmp, "xdg-terminals.list", NULL);
  session_path = g_build_filename (new_config, "session.gvariant", NULL);
  palette_path = g_build_filename (new_data, "palettes", "x.palette", NULL);

  write_file (g_build_filename (old_config, "session.gvariant", NULL), "session");
  write_file (g_build_filename (old_data, "palettes", "x.palette", NULL), "[Palette]\nname=x\n");
  write_file (keyfile_path,
              "[io/github/frostyard/Igloo]\n"
              "color-scheme='dark'\n"
              "\n"
              "[io/github/frostyard/Igloo/Profiles/aaaa]\n"
              "label='One'\n"
              "\n"
              "[org/gnome/desktop/interface]\n"
              "color-scheme='prefer-dark'\n");
  write_file (list_path,
              OLD_ID ".desktop:new-window\n"
              "org.gnome.TextEditor.desktop:new-window\n");

  lists[0] = list_path;
  lists[1] = NULL;

  ptyxis_igloo_migration_run (config_dir, data_dir, lists, OLD_ID, NEW_ID, OLD_PATH, NEW_PATH);

  /* The folders named by the application ID move with their files. */
  g_assert_false (g_file_test (old_config, G_FILE_TEST_EXISTS));
  g_assert_false (g_file_test (old_data, G_FILE_TEST_EXISTS));
  g_assert_true (g_file_test (session_path, G_FILE_TEST_EXISTS));
  g_assert_true (g_file_test (palette_path, G_FILE_TEST_EXISTS));
  assert_file_contents (session_path, "session");

  /* The settings groups move; the other group stays. */
  key_file = g_key_file_new ();
  g_assert_true (g_key_file_load_from_file (key_file, keyfile_path, G_KEY_FILE_NONE, &error));
  g_assert_no_error (error);
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo"));
  g_assert_true (g_key_file_has_group (key_file, "org/frostyard/Igloo/Profiles/aaaa"));
  g_assert_false (g_key_file_has_group (key_file, "io/github/frostyard/Igloo"));
  g_assert_true (g_key_file_has_group (key_file, "org/gnome/desktop/interface"));
  g_clear_pointer (&key_file, g_key_file_unref);

  /* The terminals list line changes; the other line stays. */
  assert_file_contents (list_path, NEW_ID ".desktop:new-window\n"
                                      "org.gnome.TextEditor.desktop:new-window\n");

  /* A second run finds nothing of the old name and changes nothing. */
  ptyxis_igloo_migration_run (config_dir, data_dir, lists, OLD_ID, NEW_ID, OLD_PATH, NEW_PATH);

  g_assert_false (g_file_test (old_config, G_FILE_TEST_EXISTS));
  g_assert_false (g_file_test (old_data, G_FILE_TEST_EXISTS));
  assert_file_contents (session_path, "session");
  assert_file_contents (list_path, NEW_ID ".desktop:new-window\n"
                                    "org.gnome.TextEditor.desktop:new-window\n");

  /* A new folder that already holds data stays; the old folder stays too. */
  write_file (g_build_filename (old_config, "session.gvariant", NULL), "old session");
  write_file (g_build_filename (new_config, "session.gvariant", NULL), "new session");

  ptyxis_igloo_migration_run (config_dir, data_dir, lists, OLD_ID, NEW_ID, OLD_PATH, NEW_PATH);

  g_assert_true (g_file_test (old_config, G_FILE_TEST_EXISTS));
  assert_file_contents (g_build_filename (old_config, "session.gvariant", NULL), "old session");
  assert_file_contents (session_path, "new session");

  remove_tree (tmp);
}

static void
test_run_nothing_old (void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *tmp = g_dir_make_tmp ("igloo-migration-XXXXXX", &error);
  g_autofree char *config_dir = NULL;
  g_autofree char *data_dir = NULL;
  g_autofree char *keyfile_path = NULL;
  g_autofree char *list_path = NULL;
  const char *lists[2];

  g_assert_no_error (error);
  g_assert_nonnull (tmp);

  config_dir = g_build_filename (tmp, "config", NULL);
  data_dir = g_build_filename (tmp, "data", NULL);
  keyfile_path = g_build_filename (config_dir, "glib-2.0", "settings", "keyfile", NULL);
  list_path = g_build_filename (tmp, "xdg-terminals.list", NULL);

  lists[0] = list_path;
  lists[1] = NULL;

  ptyxis_igloo_migration_run (config_dir, data_dir, lists, OLD_ID, NEW_ID, OLD_PATH, NEW_PATH);

  g_assert_false (g_file_test (g_build_filename (config_dir, NEW_ID, NULL), G_FILE_TEST_EXISTS));
  g_assert_false (g_file_test (g_build_filename (data_dir, NEW_ID, NULL), G_FILE_TEST_EXISTS));
  g_assert_false (g_file_test (keyfile_path, G_FILE_TEST_EXISTS));
  g_assert_false (g_file_test (list_path, G_FILE_TEST_EXISTS));

  remove_tree (tmp);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/igloo-migration/rename-groups", test_rename_groups);
  g_test_add_func ("/igloo-migration/rewrite-lines", test_rewrite_lines);
  g_test_add_func ("/igloo-migration/run", test_run);
  g_test_add_func ("/igloo-migration/run-nothing-old", test_run_nothing_old);

  return g_test_run ();
}
