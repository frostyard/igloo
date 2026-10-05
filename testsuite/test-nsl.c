/* test-nsl.c
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

#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "ptyxis-machines-impl.h"
#include "ptyxis-nsl.h"
#include "ptyxis-nsl-container.h"

static char *
load (const char *name)
{
  g_autofree char *path = g_build_filename (g_getenv ("G_TEST_SRCDIR"), "nsl", name, NULL);
  g_autoptr(GError) error = NULL;
  char *contents = NULL;

  g_file_get_contents (path, &contents, NULL, &error);
  g_assert_no_error (error);

  return contents;
}

static const PtyxisNslMachine *
machine_at (GPtrArray *machines,
            guint      i)
{
  g_assert_cmpuint (i, <, machines->len);
  return g_ptr_array_index (machines, i);
}

static void
test_nsl_list (void)
{
  g_autofree char *json = load ("list.json");
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) machines = ptyxis_nsl_parse_machines (json, &error);
  const PtyxisNslMachine *m;

  g_assert_no_error (error);
  g_assert_cmpuint (machines->len, ==, 1);

  m = machine_at (machines, 0);
  g_assert_cmpstr (m->name, ==, "trixie");
  g_assert_cmpstr (m->state, ==, "stopped");
  g_assert_cmpstr (m->image, ==, "nsl-machine-debian-trixie-x86-64-r4");
  g_assert_cmpstr (m->tier, ==, "shared");
  g_assert_true (m->is_default);
}

static void
test_nsl_list_mixed (void)
{
  g_autofree char *json = load ("list-mixed.json");
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) machines = ptyxis_nsl_parse_machines (json, &error);
  const PtyxisNslMachine *m;

  g_assert_no_error (error);
  g_assert_cmpuint (machines->len, ==, 5);

  m = machine_at (machines, 0);
  g_assert_cmpstr (m->name, ==, "trixie");
  g_assert_cmpstr (m->state, ==, "running");
  g_assert_true (m->is_default);

  /* An imported machine has no image build */
  m = machine_at (machines, 1);
  g_assert_cmpstr (m->name, ==, "imported");
  g_assert_cmpstr (m->image, ==, "");
  g_assert_cmpstr (m->tier, ==, "shared");
  g_assert_false (m->is_default);

  m = machine_at (machines, 2);
  g_assert_cmpstr (m->name, ==, "sandbox");
  g_assert_cmpstr (m->tier, ==, "isolated");

  m = machine_at (machines, 3);
  g_assert_cmpstr (m->state, ==, "incomplete");

  /* Machines being removed have no image or tier; unknown fields are ignored */
  m = machine_at (machines, 4);
  g_assert_cmpstr (m->name, ==, "gone");
  g_assert_cmpstr (m->state, ==, "removing");
  g_assert_cmpstr (m->tier, ==, "");
  g_assert_false (m->is_default);
}

static void
test_nsl_list_invalid (void)
{
  const char *documents[] = {
    "",
    "not json",
    "[]",
    "{\"vms\": []}",
    "{\"machines\": {}}",
  };
  g_autoptr(GPtrArray) empty = NULL;
  g_autoptr(GError) error = NULL;

  for (guint i = 0; i < G_N_ELEMENTS (documents); i++)
    {
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GPtrArray) machines = ptyxis_nsl_parse_machines (documents[i], &local_error);

      g_assert_null (machines);
      g_assert_nonnull (local_error);
    }

  /* Rows with invalid names are skipped rather than trusted */
  empty = ptyxis_nsl_parse_machines ("{\"machines\": [{\"name\": \"../x\"}, 7, {}]}", &error);
  g_assert_no_error (error);
  g_assert_cmpuint (empty->len, ==, 0);
}

static void
test_nsl_images (void)
{
  g_autofree char *json = load ("images.json");
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) images = ptyxis_nsl_parse_images (json, &error);
  const PtyxisNslImage *image;

  g_assert_no_error (error);

  /* The VM image is skipped */
  g_assert_cmpuint (images->len, ==, 7);

  image = g_ptr_array_index (images, 0);
  g_assert_cmpuint (g_strv_length (image->selectors), ==, 2);
  g_assert_cmpstr (image->selectors[0], ==, "debian:trixie");
  g_assert_cmpstr (image->selectors[1], ==, "debian:13");
  g_assert_cmpstr (image->build, ==, "nsl-machine-debian-trixie-x86-64-r4");
  g_assert_true (g_str_has_prefix (image->manifest, "sha256:"));

  image = g_ptr_array_index (images, 1);
  g_assert_cmpuint (g_strv_length (image->selectors), ==, 1);
  g_assert_cmpstr (image->selectors[0], ==, "fedora:44");

  image = g_ptr_array_index (images, 3);
  g_assert_cmpstr (image->selectors[0], ==, "opensuse:tumbleweed");
  g_assert_cmpstr (image->selectors[1], ==, "opensuse-tumbleweed:rolling");
}

static void
test_nsl_config (void)
{
  g_autofree char *json = load ("config.json");
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) settings = ptyxis_nsl_parse_config (json, &path, &error);
  const PtyxisNslSetting *setting;

  g_assert_no_error (error);
  g_assert_true (g_str_has_suffix (path, "/.config/nsl/nsl.conf"));
  g_assert_cmpuint (settings->len, ==, 6);

  setting = g_ptr_array_index (settings, 0);
  g_assert_cmpstr (setting->key, ==, "vm.memory");
  g_assert_cmpstr (setting->source, ==, "default");
  g_assert_cmpstr (setting->unit, ==, "GiB");
  g_assert_true (setting->has_range);
  g_assert_cmpint (setting->min, ==, 1);
  g_assert_cmpint (setting->max, ==, 128);

  setting = g_ptr_array_index (settings, 2);
  g_assert_cmpstr (setting->key, ==, "machines.autostart");
  g_assert_cmpstr (setting->value, ==, "true");
  g_assert_false (setting->has_range);
  g_assert_null (setting->unit);
}

static void
test_nsl_config_file (void)
{
  g_autofree char *json = load ("config-file.json");
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) settings = ptyxis_nsl_parse_config (json, &path, &error);
  const PtyxisNslSetting *setting;

  g_assert_no_error (error);
  g_assert_cmpstr (path, ==, "/home/u/.config/nsl/nsl.conf");
  g_assert_cmpuint (settings->len, ==, 6);

  setting = g_ptr_array_index (settings, 2);
  g_assert_cmpstr (setting->key, ==, "machines.autostart");
  g_assert_cmpstr (setting->value, ==, "false");
  g_assert_cmpstr (setting->source, ==, "file");

  /* A minimum of zero is a range like any other */
  setting = g_ptr_array_index (settings, 3);
  g_assert_cmpstr (setting->key, ==, "machines.idle_timeout");
  g_assert_cmpstr (setting->value, ==, "0");
  g_assert_true (setting->has_range);
  g_assert_cmpint (setting->min, ==, 0);
  g_assert_cmpint (setting->max, ==, 1440);
}

static void
test_nsl_error_message (void)
{
  struct {
    const char *output;
    const char *expected;
  } cases[] = {
    { "nsl: no machine named ghost; see nsl list\n", "no machine named ghost; see nsl list" },
    { "", "fallback" },
    { NULL, "fallback" },
  };
  const char *old[] = {
    "nsl: usage: list\n",
    "nsl: usage: config\n",
    "flag provided but not defined: -json\nUsage of images:\n  -offline\n\nnsl: flag provided but not defined: -json\n",
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *message = ptyxis_nsl_error_message (cases[i].output, "fallback");
      g_assert_cmpstr (message, ==, cases[i].expected);
    }

  /* nsl releases without --json are told apart from other failures */
  for (guint i = 0; i < G_N_ELEMENTS (old); i++)
    {
      g_autofree char *message = ptyxis_nsl_error_message (old[i], "fallback");
      g_assert_nonnull (strstr (message, "Update nsl"));
    }
}

static void
test_nsl_names (void)
{
  g_assert_true (ptyxis_nsl_is_valid_name ("trixie"));
  g_assert_true (ptyxis_nsl_is_valid_name ("a"));
  g_assert_true (ptyxis_nsl_is_valid_name ("dev-box-2"));
  g_assert_true (ptyxis_nsl_is_valid_name ("abcdefghijklmnopqrstuvwx"));
  g_assert_false (ptyxis_nsl_is_valid_name ("abcdefghijklmnopqrstuvwxy"));
  g_assert_false (ptyxis_nsl_is_valid_name (""));
  g_assert_false (ptyxis_nsl_is_valid_name ("2box"));
  g_assert_false (ptyxis_nsl_is_valid_name ("-box"));
  g_assert_false (ptyxis_nsl_is_valid_name ("box-"));
  g_assert_false (ptyxis_nsl_is_valid_name ("Box"));
  g_assert_false (ptyxis_nsl_is_valid_name ("my_box"));
  g_assert_false (ptyxis_nsl_is_valid_name (NULL));
}

static void
test_nsl_translate_uri (void)
{
  struct {
    const char *uri;
    const char *expected;
  } cases[] = {
    { "file://trixie/mnt/host/var/home/u/src", "file:///var/home/u/src" },
    { "file:///mnt/host/mnt/data", "file:///mnt/data" },
    { "file:///mnt/host", "file:///" },
    { "file:///home/u/notes.txt", "file:///home/u/notes.txt" },
    { "file:///mnt/hostile", "file:///mnt/hostile" },
    { "https://example.com/mnt/host/x", "https://example.com/mnt/host/x" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *translated = ptyxis_nsl_translate_uri (cases[i].uri);
      g_assert_cmpstr (translated, ==, cases[i].expected);
    }
}

static void
test_nsl_translate_directory (void)
{
  const char *home = g_get_home_dir ();
  g_autofree char *project = g_build_filename (home, "src", "project", NULL);
  g_autofree char *expected_home = NULL;
  g_autofree char *expected_project = NULL;
  g_autofree char *translated = NULL;
  char resolved[4096];

  g_assert_cmpint (g_mkdir_with_parents (project, 0700), ==, 0);
  g_assert_nonnull (realpath (home, resolved));
  expected_home = g_strconcat ("/mnt/host", resolved, NULL);
  expected_project = g_build_filename (expected_home, "src", "project", NULL);

  translated = ptyxis_nsl_translate_directory (home);
  g_assert_cmpstr (translated, ==, expected_home);
  g_clear_pointer (&translated, g_free);

  translated = ptyxis_nsl_translate_directory (project);
  g_assert_cmpstr (translated, ==, expected_project);
  g_clear_pointer (&translated, g_free);

  /* Not shared with machines */
  translated = ptyxis_nsl_translate_directory ("/usr/share");
  g_assert_null (translated);

  translated = ptyxis_nsl_translate_directory ("relative/path");
  g_assert_null (translated);
}

static void
test_nsl_guest_directory (void)
{
  g_autoptr(PtyxisNslContainer) shared = ptyxis_nsl_container_new ("/usr/bin/nsl", "trixie", FALSE);
  g_autoptr(PtyxisNslContainer) isolated = ptyxis_nsl_container_new ("/usr/bin/nsl", "sandbox", TRUE);
  g_autofree char *dir = NULL;

  g_assert_cmpstr (ptyxis_ipc_container_get_id (PTYXIS_IPC_CONTAINER (shared)), ==, "nsl:trixie");
  g_assert_cmpstr (ptyxis_ipc_container_get_provider (PTYXIS_IPC_CONTAINER (shared)), ==, "nsl");

  /* An empty directory starts in the guest home */
  g_assert_null (ptyxis_nsl_container_dup_guest_directory (shared, ""));

  /* Machine paths into the shared trees are kept, unless isolated */
  dir = ptyxis_nsl_container_dup_guest_directory (shared, "/mnt/host/var/home/u/src");
  g_assert_cmpstr (dir, ==, "/mnt/host/var/home/u/src");
  g_clear_pointer (&dir, g_free);
  g_assert_null (ptyxis_nsl_container_dup_guest_directory (isolated, "/mnt/host/var/home/u/src"));

  /* Host directories that are not shared are tried as machine paths */
  dir = ptyxis_nsl_container_dup_guest_directory (shared, "/usr/share");
  g_assert_cmpstr (dir, ==, "/usr/share");
  g_clear_pointer (&dir, g_free);

  /* Paths missing on the host are machine paths, such as the guest home */
  dir = ptyxis_nsl_container_dup_guest_directory (shared, "/home/nobody-here/src");
  g_assert_cmpstr (dir, ==, "/home/nobody-here/src");
}

static void
test_nsl_apply_settings (void)
{
  static const char existing[] =
    "# nsl settings\n"
    "[vm]\n"
    "memory = 16\n"
    "cpus = 8\n"
    "\n"
    "[machines]\n"
    "# keep machines running\n"
    "idle_timeout = 0\n";
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) changes = NULL;
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_autofree char *result = NULL;
  g_autofree char *memory = NULL;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&builder, "{ss}", "vm.memory", "24");
  g_variant_builder_add (&builder, "{ss}", "machines.idle_timeout", "");
  g_variant_builder_add (&builder, "{ss}", "isolated.cpus", "4");
  changes = g_variant_ref_sink (g_variant_builder_end (&builder));

  result = ptyxis_machines_impl_apply_settings (existing, changes, &error);
  g_assert_no_error (error);
  g_assert_nonnull (result);

  /* Comments survive; the emptied [machines] section is dropped */
  g_assert_nonnull (strstr (result, "# nsl settings"));
  g_assert_null (strstr (result, "[machines]"));

  g_key_file_load_from_data (key_file, result, -1, 0, &error);
  g_assert_no_error (error);

  memory = g_key_file_get_value (key_file, "vm", "memory", NULL);
  g_assert_cmpstr (memory, ==, "24");
  g_assert_cmpint (g_key_file_get_integer (key_file, "vm", "cpus", NULL), ==, 8);
  g_assert_cmpint (g_key_file_get_integer (key_file, "isolated", "cpus", NULL), ==, 4);
  g_assert_false (g_key_file_has_group (key_file, "machines"));
}

static void
test_nsl_apply_settings_invalid (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) changes = NULL;
  g_autofree char *result = NULL;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&builder, "{ss}", "memory", "24");
  changes = g_variant_ref_sink (g_variant_builder_end (&builder));

  result = ptyxis_machines_impl_apply_settings (NULL, changes, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_null (result);
}

int
main (int argc,
      char *argv[])
{
  g_autofree char *home = NULL;
  int ret;

  if (g_getenv ("G_TEST_SRCDIR") == NULL)
    g_error ("G_TEST_SRCDIR must be set!");

  /* Directory translation shares the home directory; use one of our own */
  home = g_dir_make_tmp ("test-nsl-XXXXXX", NULL);
  g_assert_nonnull (home);
  g_setenv ("HOME", home, TRUE);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Ptyxis/Nsl/list", test_nsl_list);
  g_test_add_func ("/Ptyxis/Nsl/list-mixed", test_nsl_list_mixed);
  g_test_add_func ("/Ptyxis/Nsl/list-invalid", test_nsl_list_invalid);
  g_test_add_func ("/Ptyxis/Nsl/images", test_nsl_images);
  g_test_add_func ("/Ptyxis/Nsl/config", test_nsl_config);
  g_test_add_func ("/Ptyxis/Nsl/config-file", test_nsl_config_file);
  g_test_add_func ("/Ptyxis/Nsl/error-message", test_nsl_error_message);
  g_test_add_func ("/Ptyxis/Nsl/names", test_nsl_names);
  g_test_add_func ("/Ptyxis/Nsl/translate-uri", test_nsl_translate_uri);
  g_test_add_func ("/Ptyxis/Nsl/translate-directory", test_nsl_translate_directory);
  g_test_add_func ("/Ptyxis/Nsl/guest-directory", test_nsl_guest_directory);
  g_test_add_func ("/Ptyxis/Nsl/apply-settings", test_nsl_apply_settings);
  g_test_add_func ("/Ptyxis/Nsl/apply-settings-invalid", test_nsl_apply_settings_invalid);
  ret = g_test_run ();

  {
    g_autofree char *project = g_build_filename (home, "src", "project", NULL);
    g_autofree char *src = g_build_filename (home, "src", NULL);

    g_rmdir (project);
    g_rmdir (src);
    g_rmdir (home);
  }

  return ret;
}
