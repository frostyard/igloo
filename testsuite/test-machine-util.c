/* test-machine-util.c
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

#include "ptyxis-machine-util.h"

#define RUNTIME_DIR "/run/user/1000"

static void
test_split_document_path (void)
{
  struct {
    const char *path;
    const char *docid;
    const char *remainder;
  } cases[] = {
    { "/run/user/1000/doc/abc/x.tar", "abc", "x.tar" },
    { "/run/user/1000/doc/abc/dir/sub/f", "abc", "dir/sub/f" },
    { "/run/user/1000/doc/2d4e3d7/dev-export.tar", "2d4e3d7", "dev-export.tar" },
  };
  const char *no_match[] = {
    /* Another folder, a folder that only starts the same, and no document */
    "/var/tmp/x.tar",
    "/run/user/1000/docs/x.tar",
    "/run/user/1000/document/abc/x.tar",
    "/run/user/1000",
    "/run/user/1000/doc",
    /* An empty document ID and an empty rest */
    "/run/user/1000/doc/",
    "/run/user/1000/doc//x.tar",
    "/run/user/1000/doc/abc",
    "/run/user/1000/doc/abc/",
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *remainder = NULL;
      g_autofree char *docid = NULL;

      docid = ptyxis_machine_split_document_path (cases[i].path, RUNTIME_DIR, &remainder);
      g_assert_cmpstr (docid, ==, cases[i].docid);
      g_assert_cmpstr (remainder, ==, cases[i].remainder);
    }

  for (guint i = 0; i < G_N_ELEMENTS (no_match); i++)
    {
      g_autofree char *remainder = NULL;
      g_autofree char *docid = NULL;

      docid = ptyxis_machine_split_document_path (no_match[i], RUNTIME_DIR, &remainder);
      g_assert_null (docid);
      g_assert_null (remainder);
    }

  /* A different runtime directory is not the document portal */
  {
    g_autofree char *remainder = NULL;
    g_autofree char *docid = NULL;

    docid = ptyxis_machine_split_document_path ("/run/user/1000/doc/abc/x.tar",
                                                "/run/user/1001",
                                                &remainder);
    g_assert_null (docid);
    g_assert_null (remainder);
  }

  /* The remainder is optional */
  {
    g_autofree char *docid = NULL;

    docid = ptyxis_machine_split_document_path ("/run/user/1000/doc/abc/x.tar", RUNTIME_DIR, NULL);
    g_assert_cmpstr (docid, ==, "abc");
  }
}

static void
test_compose_host_path (void)
{
  struct {
    const char *host_document_path;
    const char *remainder;
    const char *expected;
  } cases[] = {
    { "/var/tmp/x.tar", "x.tar", "/var/tmp/x.tar" },
    { "/srv/dir", "dir/sub/f", "/srv/dir/sub/f" },
    { "/var/tmp/dev-export.tar", "dev-export.tar", "/var/tmp/dev-export.tar" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *path = NULL;

      path = ptyxis_machine_compose_host_path (cases[i].host_document_path, cases[i].remainder);
      g_assert_cmpstr (path, ==, cases[i].expected);
    }

  /* Without a remainder the document is the file */
  {
    g_autofree char *path = ptyxis_machine_compose_host_path ("/var/tmp/x.tar", NULL);

    g_assert_cmpstr (path, ==, "/var/tmp/x.tar");
  }
}

static void
test_export_file_name (void)
{
  g_autoptr(GDateTime) now = g_date_time_new_local (2026, 11, 2, 9, 30, 0);
  g_autofree char *file_name = NULL;

  g_assert_nonnull (now);

  file_name = ptyxis_machine_export_file_name ("dev", now);
  g_assert_cmpstr (file_name, ==, "dev-2026-11-02.tar");
}

static void
test_suggest_import_name (void)
{
  g_autofree char *name = NULL;

  /* An exported file names the machine */
  name = ptyxis_machine_suggest_import_name ("/b/dev-2026-09-28.tar", NULL);
  g_assert_cmpstr (name, ==, "dev");
  g_clear_pointer (&name, g_free);

  /* The suffix and the case do not matter */
  name = ptyxis_machine_suggest_import_name ("Dev Box.TAR", NULL);
  g_assert_cmpstr (name, ==, "dev-box");
  g_clear_pointer (&name, g_free);

  name = ptyxis_machine_suggest_import_name ("/var/tmp/x.tar", NULL);
  g_assert_cmpstr (name, ==, "x");
  g_clear_pointer (&name, g_free);

  /* A name in use gets a number */
  {
    const char *existing[] = { "dev", NULL };

    name = ptyxis_machine_suggest_import_name ("/b/dev-2026-09-28.tar", existing);
    g_assert_cmpstr (name, ==, "dev-2");
    g_clear_pointer (&name, g_free);
  }

  /* A name that cannot be a machine name still gives a valid one */
  name = ptyxis_machine_suggest_import_name ("123.tar", NULL);
  g_assert_nonnull (name);
  g_assert_true (ptyxis_machine_is_valid_name (name));
  g_assert_true (g_str_has_prefix (name, "machine"));
  g_clear_pointer (&name, g_free);

  /* The result is always a valid name */
  {
    const char *files[] = {
      "/b/dev.tar",
      "Trixie 13.tar",
      "/b/2026-09-28.tar",
      "/b/.tar",
      "/b/x",
      "/b/debian-trixie-x86-64-r4-2026-09-28.tar",
    };
    const char *taken[] = { "dev", "trixie-13", "debian-trixie-x86-64-r4", NULL };

    for (guint i = 0; i < G_N_ELEMENTS (files); i++)
      {
        g_autofree char *suggested = ptyxis_machine_suggest_import_name (files[i], taken);

        g_assert_nonnull (suggested);
        g_assert_true (ptyxis_machine_is_valid_name (suggested));
        g_assert_false (g_strv_contains (taken, suggested));
      }
  }
}

static void
test_logs_argv (void)
{
  const char *names[] = { NULL, "dev", "name with spaces", "-leading" };

  for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
      g_auto(GStrv) argv = ptyxis_machine_logs_argv ("/host/bin/nsl", names[i]);

      g_assert_cmpuint (g_strv_length (argv), ==, names[i] ? 3 : 2);
      g_assert_cmpstr (argv[0], ==, "/host/bin/nsl");
      g_assert_cmpstr (argv[1], ==, "logs");
      g_assert_cmpstr (argv[2], ==, names[i]);
    }
}

static void
test_recover_argv (void)
{
  const char *names[] = { NULL, "dev", "name with spaces", "-leading" };

  for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
      g_auto(GStrv) argv = ptyxis_machine_recover_argv ("/host/bin/nsl", names[i]);

      g_assert_cmpuint (g_strv_length (argv), ==, names[i] ? 3 : 2);
      g_assert_cmpstr (argv[0], ==, "/host/bin/nsl");
      g_assert_cmpstr (argv[1], ==, "recover");
      g_assert_cmpstr (argv[2], ==, names[i]);
    }
}

static void
test_can_recover (void)
{
  struct {
    const char *tier;
    const char *state;
    gboolean expected;
  } cases[] = {
    { "isolated", "running", TRUE },
    { "isolated", "stopped", TRUE },
    { "isolated", "starting", TRUE },
    { "isolated", NULL, TRUE },
    { "shared", "running", FALSE },
    { "shared", "stopped", FALSE },
    { "isolated", "incomplete", FALSE },
    { "isolated", "removing", FALSE },
    { NULL, "running", FALSE },
    { NULL, NULL, FALSE },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    g_assert_cmpint (ptyxis_machine_can_recover (cases[i].tier, cases[i].state), ==, cases[i].expected);
}

static void
test_remove_argv (void)
{
  const char *names[] = { "dev", "-leading" };
  const char *stop_states[] = { NULL, "running", "stopped", "starting", "stopping", "" };
  const char *no_stop_states[] = { "removing", "incomplete" };

  for (guint n = 0; n < G_N_ELEMENTS (names); n++)
    {
      for (guint i = 0; i < G_N_ELEMENTS (stop_states); i++)
        {
          g_auto(GStrv) argv = ptyxis_machine_remove_argv ("/host/bin/nsl", names[n], stop_states[i]);

          g_assert_cmpuint (g_strv_length (argv), ==, 5);
          g_assert_cmpstr (argv[0], ==, "/bin/sh");
          g_assert_cmpstr (argv[1], ==, "-c");
          g_assert_cmpstr (argv[2], ==, "\"$0\" stop \"$1\" && exec \"$0\" remove \"$1\" --yes");
          g_assert_cmpstr (argv[3], ==, "/host/bin/nsl");
          g_assert_cmpstr (argv[4], ==, names[n]);
        }

      for (guint i = 0; i < G_N_ELEMENTS (no_stop_states); i++)
        {
          g_auto(GStrv) argv = ptyxis_machine_remove_argv ("/host/bin/nsl", names[n], no_stop_states[i]);

          g_assert_cmpuint (g_strv_length (argv), ==, 4);
          g_assert_cmpstr (argv[0], ==, "/host/bin/nsl");
          g_assert_cmpstr (argv[1], ==, "remove");
          g_assert_cmpstr (argv[2], ==, names[n]);
          g_assert_cmpstr (argv[3], ==, "--yes");
        }
    }
}

static void
test_remove_needs_stop (void)
{
  struct {
    const char *state;
    gboolean expected;
  } cases[] = {
    { NULL, TRUE },
    { "running", TRUE },
    { "stopped", TRUE },
    { "starting", TRUE },
    { "stopping", TRUE },
    { "", TRUE },
    { "removing", FALSE },
    { "incomplete", FALSE },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    g_assert_cmpint (ptyxis_machine_remove_needs_stop (cases[i].state), ==, cases[i].expected);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/Ptyxis/MachineUtil/split-document-path", test_split_document_path);
  g_test_add_func ("/Ptyxis/MachineUtil/compose-host-path", test_compose_host_path);
  g_test_add_func ("/Ptyxis/MachineUtil/export-file-name", test_export_file_name);
  g_test_add_func ("/Ptyxis/MachineUtil/suggest-import-name", test_suggest_import_name);

  g_test_add_func ("/Ptyxis/MachineUtil/logs-argv", test_logs_argv);
  g_test_add_func ("/Ptyxis/MachineUtil/recover-argv", test_recover_argv);
  g_test_add_func ("/Ptyxis/MachineUtil/can-recover", test_can_recover);
  g_test_add_func ("/Ptyxis/MachineUtil/remove-argv", test_remove_argv);
  g_test_add_func ("/Ptyxis/MachineUtil/remove-needs-stop", test_remove_needs_stop);

  return g_test_run ();
}
