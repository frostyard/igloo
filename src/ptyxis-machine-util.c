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

#include "ptyxis-machine-util.h"

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

char *
ptyxis_machine_dup_container_id (const char *name)
{
  return g_strconcat (PTYXIS_MACHINE_CONTAINER_PREFIX, name, NULL);
}
