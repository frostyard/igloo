/* ptyxis-machine-util.c
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

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "ptyxis-machine-util.h"

/**
 * ptyxis_machine_logs_argv:
 * @nsl: the path to nsl
 * @name: (nullable): a machine name, or NULL for all logs
 *
 * Returns: (transfer full) (array zero-terminated=1): arguments for nsl logs
 */
char **
ptyxis_machine_logs_argv (const char *nsl,
                          const char *name)
{
  const char * const argv[] = { nsl, "logs", name, NULL };

  g_return_val_if_fail (nsl != NULL, NULL);

  /* A NULL name ends the array after the command */
  return g_strdupv ((char **)argv);
}

/**
 * ptyxis_machine_recover_argv:
 * @nsl: the path to nsl
 * @name: (nullable): an isolated machine name, or NULL for the shared VM
 *
 * Returns: (transfer full) (array zero-terminated=1): arguments for nsl recover
 */
char **
ptyxis_machine_recover_argv (const char *nsl,
                             const char *name)
{
  const char * const argv[] = { nsl, "recover", name, NULL };

  g_return_val_if_fail (nsl != NULL, NULL);

  /* A NULL name ends the array after the command */
  return g_strdupv ((char **)argv);
}

/**
 * ptyxis_machine_can_recover:
 * @tier: (nullable): a machine tier
 * @state: (nullable): a machine state
 *
 * Returns: whether the machine has its own VM and is usable for recovery
 */
gboolean
ptyxis_machine_can_recover (const char *tier,
                            const char *state)
{
  return g_strcmp0 (tier, "isolated") == 0 &&
         g_strcmp0 (state, "incomplete") != 0 &&
         g_strcmp0 (state, "removing") != 0;
}

#define DOCUMENTS_BUS_NAME "org.freedesktop.portal.Documents"
#define DOCUMENTS_OBJECT_PATH "/org/freedesktop/portal/documents"
#define DOCUMENTS_INTERFACE "org.freedesktop.portal.Documents"

static const struct {
  const char *id;
  const char *name;
} distributions[] = {
  { "arch", "Arch Linux" },
  { "centos", "CentOS Stream" },
  { "centos-stream", "CentOS Stream" },
  { "debian", "Debian" },
  { "fedora", "Fedora" },
  { "opensuse", "openSUSE" },
  { "opensuse-leap", "openSUSE Leap" },
  { "opensuse-tumbleweed", "openSUSE Tumbleweed" },
  { "ubuntu", "Ubuntu" },
};

static char *
pretty_distribution (const char *id)
{
  for (guint i = 0; i < G_N_ELEMENTS (distributions); i++)
    {
      if (g_strcmp0 (id, distributions[i].id) == 0)
        return g_strdup (distributions[i].name);
    }

  if (id == NULL || id[0] == 0)
    return g_strdup ("");

  return g_strdup_printf ("%c%s", g_ascii_toupper (id[0]), id + 1);
}

/**
 * ptyxis_machine_is_valid_name:
 *
 * Checks an nsl machine name: a lowercase ASCII letter, then lowercase
 * letters, digits or interior hyphens, at most 24 characters.
 */
gboolean
ptyxis_machine_is_valid_name (const char *name)
{
  gsize len;

  if (name == NULL)
    return FALSE;

  len = strlen (name);

  if (len == 0 || len > 24 || !g_ascii_islower (name[0]) || name[len-1] == '-')
    return FALSE;

  for (gsize i = 1; i < len; i++)
    {
      if (!g_ascii_islower (name[i]) && !g_ascii_isdigit (name[i]) && name[i] != '-')
        return FALSE;
    }

  return TRUE;
}

/**
 * ptyxis_machine_is_valid_user:
 *
 * Checks a POSIX account name as nsl accepts for `--user`: a lowercase
 * letter or underscore, then lowercase letters, digits, underscores or
 * hyphens, at most 32 characters.
 */
gboolean
ptyxis_machine_is_valid_user (const char *user)
{
  gsize len;

  if (user == NULL)
    return FALSE;

  len = strlen (user);

  if (len == 0 || len > 32 || !(g_ascii_islower (user[0]) || user[0] == '_'))
    return FALSE;

  for (gsize i = 1; i < len; i++)
    {
      if (!g_ascii_islower (user[i]) && !g_ascii_isdigit (user[i]) && user[i] != '_' && user[i] != '-')
        return FALSE;
    }

  return TRUE;
}

/**
 * ptyxis_machine_describe_image:
 * @build: (nullable): an image build, such as "nsl-machine-debian-trixie-x86-64-r4"
 *
 * Returns: (transfer full): a short description such as "Debian trixie"
 */
char *
ptyxis_machine_describe_image (const char *build)
{
  g_autofree char *rest = NULL;
  g_auto(GStrv) parts = NULL;
  g_autofree char *distro = NULL;
  g_autofree char *release = NULL;
  char *arch;
  guint n_parts;

  if (build == NULL || build[0] == 0)
    return g_strdup (_("Imported"));

  if (!g_str_has_prefix (build, "nsl-machine-"))
    return g_strdup (build);

  rest = g_strdup (build + strlen ("nsl-machine-"));

  /* Drop "-x86-64-rN" */
  if ((arch = strstr (rest, "-x86-64")))
    *arch = 0;

  parts = g_strsplit (rest, "-", 2);
  n_parts = g_strv_length (parts);

  /* openSUSE builds name the variant first, such as "opensuse-tumbleweed" */
  distro = pretty_distribution (parts[0]);

  if (n_parts < 2)
    return g_steal_pointer (&distro);

  release = g_strdup (parts[1]);

  return g_strdup_printf ("%s %s", distro, release);
}

/**
 * ptyxis_machine_describe_selectors:
 * @selectors: selectors such as ["debian:trixie", "debian:13"]
 *
 * Returns: (transfer full): a label such as "Debian trixie (debian:13)"
 */
char *
ptyxis_machine_describe_selectors (const char * const *selectors)
{
  g_autoptr(GString) str = g_string_new (NULL);
  g_auto(GStrv) first = NULL;
  g_autofree char *distro = NULL;

  if (selectors == NULL || selectors[0] == NULL)
    return g_strdup ("");

  first = g_strsplit (selectors[0], ":", 2);
  distro = pretty_distribution (first[0]);

  g_string_append (str, distro);

  if (first[1] != NULL)
    g_string_append_printf (str, " %s", first[1]);

  if (selectors[1] != NULL)
    {
      g_string_append (str, " (");

      for (guint i = 1; selectors[i]; i++)
        {
          if (i > 1)
            g_string_append (str, ", ");
          g_string_append (str, selectors[i]);
        }

      g_string_append_c (str, ')');
    }

  return g_string_free (g_steal_pointer (&str), FALSE);
}

static char *
sanitize (const char *str)
{
  GString *out = g_string_new (NULL);

  for (const char *c = str; *c; c++)
    {
      char ch = g_ascii_tolower (*c);

      if (g_ascii_islower (ch) || g_ascii_isdigit (ch))
        g_string_append_c (out, ch);
      else if (out->len > 0 && out->str[out->len-1] != '-')
        g_string_append_c (out, '-');
    }

  while (out->len > 0 && out->str[out->len-1] == '-')
    g_string_truncate (out, out->len - 1);

  if (out->len > 24)
    g_string_truncate (out, 24);

  while (out->len > 0 && out->str[out->len-1] == '-')
    g_string_truncate (out, out->len - 1);

  return g_string_free (out, FALSE);
}

static gboolean
is_available (const char         *name,
              const char * const *existing)
{
  return ptyxis_machine_is_valid_name (name) &&
         (existing == NULL || !g_strv_contains (existing, name));
}

/**
 * ptyxis_machine_suggest_name:
 * @selector: a selector such as "debian:trixie"
 * @existing: (nullable): names already in use
 *
 * Suggests an unused machine name for an image, such as "trixie" or
 * "fedora-44".
 *
 * Returns: (transfer full): a valid machine name
 */
char *
ptyxis_machine_suggest_name (const char         *selector,
                             const char * const *existing)
{
  g_auto(GStrv) parts = NULL;
  g_autofree char *release = NULL;
  g_autofree char *distro = NULL;
  g_autofree char *both = NULL;
  g_autofree char *base = NULL;
  const char *candidates[3];

  parts = g_strsplit (selector ? selector : "machine", ":", 2);
  distro = sanitize (parts[0]);
  release = sanitize (parts[1] ? parts[1] : "");
  both = g_strdup_printf ("%s-%s", distro, release);

  /* Release code names make good names; numbers and "rolling" do not */
  candidates[0] = g_strcmp0 (release, "rolling") == 0 ? NULL : release;
  candidates[1] = both;
  candidates[2] = distro;

  for (guint i = 0; i < G_N_ELEMENTS (candidates); i++)
    {
      if (candidates[i] != NULL && is_available (candidates[i], existing))
        return g_strdup (candidates[i]);
    }

  base = ptyxis_machine_is_valid_name (distro) ? g_strdup (distro) : g_strdup ("machine");

  for (guint i = 2; i < 1000; i++)
    {
      g_autofree char *name = g_strdup_printf ("%.20s-%u", base, i);

      if (is_available (name, existing))
        return g_steal_pointer (&name);
    }

  return g_strdup ("machine");
}

/**
 * ptyxis_machine_export_file_name:
 * @name: a machine name
 * @now: (nullable): the time to name the file after
 *
 * Returns: (transfer full): a file name such as "dev-2026-11-02.tar"
 */
char *
ptyxis_machine_export_file_name (const char *name,
                                 GDateTime  *now)
{
  g_autoptr(GDateTime) local = NULL;
  g_autofree char *date = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  if (now == NULL)
    now = local = g_date_time_new_now_local ();

  if (now == NULL)
    return g_strdup_printf ("%s.tar", name);

  date = g_date_time_format (now, "%Y-%m-%d");

  return g_strdup_printf ("%s-%s.tar", name, date);
}

/* Matches the "YYYY-MM-DD" of an exported file name */
static gboolean
is_date (const char *str)
{
  if (str == NULL || strlen (str) != strlen ("YYYY-MM-DD"))
    return FALSE;

  for (guint i = 0; str[i]; i++)
    {
      if (i == 4 || i == 7)
        {
          if (str[i] != '-')
            return FALSE;
        }
      else if (!g_ascii_isdigit (str[i]))
        return FALSE;
    }

  return TRUE;
}

/**
 * ptyxis_machine_suggest_import_name:
 * @file_name: the archive that is imported, such as "dev-2026-09-28.tar"
 * @existing: (nullable): names already in use
 *
 * Suggests an unused machine name for an archive.
 *
 * Returns: (transfer full): a valid machine name
 */
char *
ptyxis_machine_suggest_import_name (const char         *file_name,
                                    const char * const *existing)
{
  g_autofree char *base_name = NULL;
  g_autofree char *lower = NULL;
  g_autofree char *base = NULL;
  g_autofree char *numbered = NULL;
  gsize len;

  g_return_val_if_fail (file_name != NULL, NULL);

  base_name = g_path_get_basename (file_name);
  lower = g_ascii_strdown (base_name, -1);

  if (strlen (base_name) > strlen (".tar") && g_str_has_suffix (lower, ".tar"))
    base_name[strlen (base_name) - strlen (".tar")] = 0;

  /* Exported files end with the date they were made */
  len = strlen (base_name);
  if (len > strlen ("-YYYY-MM-DD") &&
      base_name[len - strlen ("-YYYY-MM-DD")] == '-' &&
      is_date (base_name + len - strlen ("YYYY-MM-DD")))
    base_name[len - strlen ("-YYYY-MM-DD")] = 0;

  base = sanitize (base_name);

  if (is_available (base, existing))
    return g_steal_pointer (&base);

  numbered = ptyxis_machine_is_valid_name (base) ? g_strdup (base) : g_strdup ("machine");

  for (guint i = 2; i < 1000; i++)
    {
      g_autofree char *name = g_strdup_printf ("%.20s-%u", numbered, i);

      if (is_available (name, existing))
        return g_steal_pointer (&name);
    }

  return g_strdup (numbered);
}

/**
 * ptyxis_machine_split_document_path:
 * @path: a path that may name a document portal file
 * @runtime_dir: the user's runtime directory, such as "/run/user/1000"
 * @remainder: (out) (optional) (nullable) (transfer full): the path below
 *   the document
 *
 * Splits "/run/user/1000/doc/abc/dir/x.tar" into the document ID "abc" and
 * the remainder "dir/x.tar".
 *
 * Returns: (transfer full) (nullable): the document ID, or %NULL
 */
char *
ptyxis_machine_split_document_path (const char  *path,
                                    const char  *runtime_dir,
                                    char       **remainder)
{
  g_autofree char *prefix = NULL;
  const char *rest;
  const char *slash;

  if (remainder != NULL)
    *remainder = NULL;

  if (path == NULL || runtime_dir == NULL || runtime_dir[0] == 0)
    return NULL;

  prefix = g_build_filename (runtime_dir, "doc", NULL);

  if (!g_str_has_prefix (path, prefix))
    return NULL;

  rest = path + strlen (prefix);

  if (rest[0] != '/')
    return NULL;

  rest++;
  slash = strchr (rest, '/');

  if (slash == NULL || slash == rest || slash[1] == 0)
    return NULL;

  if (remainder != NULL)
    *remainder = g_strdup (slash + 1);

  return g_strndup (rest, slash - rest);
}

/**
 * ptyxis_machine_compose_host_path:
 * @host_document_path: the host path of the document, such as "/srv/dir"
 * @remainder: (nullable): the path below the document, such as "dir/sub/f"
 *
 * Returns: (transfer full): the host path of the file, such as
 *   "/srv/dir/sub/f"
 */
char *
ptyxis_machine_compose_host_path (const char *host_document_path,
                                  const char *remainder)
{
  g_autofree char *dirname = NULL;

  g_return_val_if_fail (host_document_path != NULL, NULL);

  if (remainder == NULL || remainder[0] == 0)
    return g_strdup (host_document_path);

  dirname = g_path_get_dirname (host_document_path);

  return g_build_filename (dirname, remainder, NULL);
}

typedef struct
{
  char *docid;
  char *remainder;
} HostPath;

static void
host_path_free (HostPath *data)
{
  g_clear_pointer (&data->docid, g_free);
  g_clear_pointer (&data->remainder, g_free);
  g_free (data);
}

static void
host_path_return_error (GTask *task)
{
  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "%s",
                           _("Igloo could not find where this file is on the computer. Choose a file in your home folder."));
}

static void
host_path_call_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  HostPath *data = g_task_get_task_data (task);
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) paths = NULL;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *host_path = NULL;
  const char *host_document_path;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);

  if (reply == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_task_return_error (task, g_steal_pointer (&error));
      else
        host_path_return_error (task);

      return;
    }

  g_variant_get (reply, "(@a{say})", &paths);
  value = g_variant_lookup_value (paths, data->docid, G_VARIANT_TYPE_BYTESTRING);

  if (value == NULL)
    {
      host_path_return_error (task);
      return;
    }

  host_document_path = g_variant_get_bytestring (value);

  if (host_document_path == NULL || host_document_path[0] == 0)
    {
      host_path_return_error (task);
      return;
    }

  host_path = ptyxis_machine_compose_host_path (host_document_path, data->remainder);

  g_task_return_pointer (task, g_steal_pointer (&host_path), g_free);
}

static void
host_path_bus_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  HostPath *data = g_task_get_task_data (task);
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;
  const char *documents[] = { NULL, NULL };

  connection = g_bus_get_finish (result, &error);

  if (connection == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_task_return_error (task, g_steal_pointer (&error));
      else
        host_path_return_error (task);

      return;
    }

  documents[0] = data->docid;

  g_dbus_connection_call (connection,
                          DOCUMENTS_BUS_NAME,
                          DOCUMENTS_OBJECT_PATH,
                          DOCUMENTS_INTERFACE,
                          "GetHostPaths",
                          g_variant_new ("(^as)", documents),
                          G_VARIANT_TYPE ("(a{say})"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          g_task_get_cancellable (task),
                          host_path_call_cb,
                          g_steal_pointer (&task));
}

/**
 * ptyxis_machine_host_path_async:
 * @file: a file that the file chooser returned
 * @cancellable: (nullable): a #GCancellable
 * @callback: a callback to call when the path is known
 * @user_data: data for @callback
 *
 * Finds the path of @file on the computer. A file the Flatpak sees through
 * the document portal, such as "/run/user/1000/doc/abc/x.tar", becomes its
 * host path, such as "/var/tmp/x.tar".
 */
void
ptyxis_machine_host_path_async (GFile               *file,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *path = NULL;
  g_autofree char *docid = NULL;
  g_autofree char *remainder = NULL;
  HostPath *data;

  g_return_if_fail (G_IS_FILE (file));

  task = g_task_new (file, cancellable, callback, user_data);
  g_task_set_source_tag (task, ptyxis_machine_host_path_async);

  path = g_file_get_path (file);

  if (path == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "%s",
                               _("Choose a file on this computer."));
      return;
    }

  docid = ptyxis_machine_split_document_path (path, g_get_user_runtime_dir (), &remainder);

  /* A path the app can see is already the host path */
  if (docid == NULL)
    {
      g_task_return_pointer (task, g_strdup (path), g_free);
      return;
    }

  data = g_new0 (HostPath, 1);
  data->docid = g_steal_pointer (&docid);
  data->remainder = g_steal_pointer (&remainder);
  g_task_set_task_data (task, data, (GDestroyNotify)host_path_free);

  g_bus_get (G_BUS_TYPE_SESSION, cancellable, host_path_bus_cb, g_steal_pointer (&task));
}

/**
 * ptyxis_machine_host_path_finish:
 * @result: the result of ptyxis_machine_host_path_async()
 * @error: a location for an error
 *
 * Returns: (transfer full) (nullable): the path of the file on the computer
 */
char *
ptyxis_machine_host_path_finish (GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

char *
ptyxis_machine_dup_container_id (const char *name)
{
  return g_strconcat (PTYXIS_MACHINE_CONTAINER_PREFIX, name, NULL);
}
