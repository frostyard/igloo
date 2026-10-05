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

#include <json-glib/json-glib.h>

#include "ptyxis-nsl.h"

/* nsl reports its state with `--json` (nsl ADR-0021). Consumers must ignore
 * fields they do not know, so only the members used here are read.
 */

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
  g_clear_pointer (&image->manifest, g_free);
  g_free (image);
}

void
ptyxis_nsl_setting_free (PtyxisNslSetting *setting)
{
  g_clear_pointer (&setting->key, g_free);
  g_clear_pointer (&setting->value, g_free);
  g_clear_pointer (&setting->source, g_free);
  g_clear_pointer (&setting->unit, g_free);
  g_free (setting);
}

static JsonNode *
member (JsonObject *object,
        const char *name,
        GType       value_type)
{
  JsonNode *node;

  if (object == NULL ||
      !json_object_has_member (object, name) ||
      !(node = json_object_get_member (object, name)) ||
      !JSON_NODE_HOLDS_VALUE (node) ||
      json_node_get_value_type (node) != value_type)
    return NULL;

  return node;
}

static const char *
member_string (JsonObject *object,
               const char *name)
{
  JsonNode *node = member (object, name, G_TYPE_STRING);

  return node ? json_node_get_string (node) : NULL;
}

static gboolean
member_boolean (JsonObject *object,
                const char *name)
{
  JsonNode *node = member (object, name, G_TYPE_BOOLEAN);

  return node ? json_node_get_boolean (node) : FALSE;
}

static gboolean
member_int (JsonObject *object,
            const char *name,
            gint64     *value)
{
  JsonNode *node = member (object, name, G_TYPE_INT64);

  if (node == NULL)
    return FALSE;

  *value = json_node_get_int (node);

  return TRUE;
}

static JsonArray *
member_array (JsonObject *object,
              const char *name)
{
  JsonNode *node;

  if (object == NULL ||
      !json_object_has_member (object, name) ||
      !(node = json_object_get_member (object, name)) ||
      !JSON_NODE_HOLDS_ARRAY (node))
    return NULL;

  return json_node_get_array (node);
}

static JsonObject *
array_object (JsonArray *array,
              guint      index)
{
  JsonNode *node = json_array_get_element (array, index);

  return node && JSON_NODE_HOLDS_OBJECT (node) ? json_node_get_object (node) : NULL;
}

/* Parses @json and returns its root object and the array @name in it. */
static JsonObject *
parse_document (JsonParser  *parser,
                const char  *json,
                const char  *name,
                JsonArray  **array,
                GError     **error)
{
  JsonNode *root;
  JsonObject *object;

  if (json == NULL)
    json = "";

  if (!json_parser_load_from_data (parser, json, -1, error))
    return NULL;

  if (!(root = json_parser_get_root (parser)) ||
      !JSON_NODE_HOLDS_OBJECT (root) ||
      !(object = json_node_get_object (root)) ||
      !(*array = member_array (object, name)))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "nsl reported no \"%s\"", name);
      return NULL;
    }

  return object;
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

/**
 * ptyxis_nsl_parse_machines:
 * @json: the output of `nsl list --json`
 *
 * Returns: (transfer full) (nullable): the machines, or %NULL on error
 */
GPtrArray *
ptyxis_nsl_parse_machines (const char  *json,
                           GError     **error)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(GPtrArray) machines = NULL;
  JsonArray *array = NULL;

  if (!parse_document (parser, json, "machines", &array, error))
    return NULL;

  machines = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_machine_free);

  for (guint i = 0; i < json_array_get_length (array); i++)
    {
      JsonObject *object = array_object (array, i);
      const char *name = member_string (object, "name");
      PtyxisNslMachine *machine;

      if (!ptyxis_nsl_is_valid_name (name))
        continue;

      machine = g_new0 (PtyxisNslMachine, 1);
      machine->name = g_strdup (name);
      machine->state = g_strdup (member_string (object, "state"));
      machine->image = g_strdup (member_string (object, "image"));
      machine->tier = g_strdup (member_string (object, "tier"));
      machine->is_default = member_boolean (object, "default");

      g_ptr_array_add (machines, machine);
    }

  return g_steal_pointer (&machines);
}

/**
 * ptyxis_nsl_parse_images:
 * @json: the output of `nsl images --json`
 *
 * Returns: (transfer full) (nullable): the machine images, or %NULL on error
 */
GPtrArray *
ptyxis_nsl_parse_images (const char  *json,
                         GError     **error)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(GPtrArray) images = NULL;
  JsonArray *array = NULL;

  if (!parse_document (parser, json, "images", &array, error))
    return NULL;

  images = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_image_free);

  for (guint i = 0; i < json_array_get_length (array); i++)
    {
      JsonObject *object = array_object (array, i);
      JsonArray *selectors = member_array (object, "selectors");
      g_autoptr(GPtrArray) strv = g_ptr_array_new_with_free_func (g_free);
      PtyxisNslImage *image;

      /* Only machine images can be created */
      if (g_strcmp0 (member_string (object, "kind"), "machine") != 0 || selectors == NULL)
        continue;

      for (guint j = 0; j < json_array_get_length (selectors); j++)
        {
          JsonNode *node = json_array_get_element (selectors, j);

          if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
            g_ptr_array_add (strv, g_strdup (json_node_get_string (node)));
        }

      if (strv->len == 0)
        continue;

      g_ptr_array_add (strv, NULL);

      image = g_new0 (PtyxisNslImage, 1);
      image->selectors = (char **)g_ptr_array_free (g_steal_pointer (&strv), FALSE);
      image->build = g_strdup (member_string (object, "build"));
      image->manifest = g_strdup (member_string (object, "manifest"));
      image->cached = member_boolean (object, "cached");

      g_ptr_array_add (images, image);
    }

  return g_steal_pointer (&images);
}

/**
 * ptyxis_nsl_parse_config:
 * @json: the output of `nsl config --json`
 * @path: (out) (optional): the configuration file
 *
 * Returns: (transfer full) (nullable): the settings, or %NULL on error
 */
GPtrArray *
ptyxis_nsl_parse_config (const char  *json,
                         char       **path,
                         GError     **error)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(GPtrArray) settings = NULL;
  JsonObject *root;
  JsonArray *array = NULL;

  if (path != NULL)
    *path = NULL;

  if (!(root = parse_document (parser, json, "settings", &array, error)))
    return NULL;

  settings = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_setting_free);

  for (guint i = 0; i < json_array_get_length (array); i++)
    {
      JsonObject *object = array_object (array, i);
      const char *key = member_string (object, "key");
      PtyxisNslSetting *setting;
      JsonNode *value;
      gint64 n;

      if (key == NULL || object == NULL || !json_object_has_member (object, "value"))
        continue;

      setting = g_new0 (PtyxisNslSetting, 1);
      setting->key = g_strdup (key);
      setting->source = g_strdup (member_string (object, "source"));
      setting->unit = g_strdup (member_string (object, "unit"));
      setting->has_range = member_int (object, "min", &setting->min) &&
                           member_int (object, "max", &setting->max);

      value = json_object_get_member (object, "value");

      if (member_int (object, "value", &n))
        setting->value = g_strdup_printf ("%" G_GINT64_FORMAT, n);
      else if (JSON_NODE_HOLDS_VALUE (value) && json_node_get_value_type (value) == G_TYPE_BOOLEAN)
        setting->value = g_strdup (json_node_get_boolean (value) ? "true" : "false");
      else
        setting->value = g_strdup (member_string (object, "value"));

      g_ptr_array_add (settings, setting);
    }

  if (path != NULL)
    *path = g_strdup (member_string (root, "path"));

  return g_steal_pointer (&settings);
}

/**
 * ptyxis_nsl_error_message:
 * @output: (nullable): what a failed nsl command printed
 * @fallback: a message for when @output says nothing
 *
 * Returns: (transfer full): a message for the user
 */
char *
ptyxis_nsl_error_message (const char *output,
                          const char *fallback)
{
  g_auto(GStrv) lines = NULL;
  g_autofree char *copy = NULL;
  const char *message;

  if (output == NULL)
    return g_strdup (fallback);

  /* Releases before 0.8.0 reject --json with a bare usage line or as an
   * unknown flag.
   */
  lines = g_strsplit (output, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    {
      const char *line = g_strstrip (lines[i]);

      if (g_strcmp0 (line, "nsl: usage: list") == 0 ||
          g_strcmp0 (line, "nsl: usage: config") == 0 ||
          strstr (line, "flag provided but not defined: -json") != NULL)
        return g_strdup ("This version of nsl is too old to report its machines. Update nsl to 0.8.0 or later.");
    }

  copy = g_strstrip (g_strdup (output));
  message = copy;

  if (g_str_has_prefix (message, "nsl: "))
    message += strlen ("nsl: ");

  if (message[0] == 0)
    return g_strdup (fallback);

  return g_strdup (message);
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
