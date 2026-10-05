/* ptyxis-nsl-container.c
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

#include "ptyxis-agent-compat.h"
#include "ptyxis-agent-util.h"
#include "ptyxis-nsl.h"
#include "ptyxis-nsl-container.h"
#include "ptyxis-process-impl.h"
#include "ptyxis-run-context.h"

struct _PtyxisNslContainer
{
  PtyxisIpcContainerSkeleton parent_instance;
  char *nsl_path;
  char *name;
  guint isolated : 1;
};

typedef struct
{
  PtyxisNslContainer *container;
  char *directory;
} NslSpawn;

enum {
  SPAWNED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void container_iface_init (PtyxisIpcContainerIface *iface);

G_DEFINE_TYPE_WITH_CODE (PtyxisNslContainer, ptyxis_nsl_container, PTYXIS_IPC_TYPE_CONTAINER_SKELETON,
                         G_IMPLEMENT_INTERFACE (PTYXIS_IPC_TYPE_CONTAINER, container_iface_init))

/* Runs in the machine as `sh -c SCRIPT ptyxis-nsl DIRECTORY ARGV...`.
 *
 * nsl starts the command in "/" so that it never refuses a directory it
 * cannot translate. The script then changes to DIRECTORY, or to the guest
 * home when DIRECTORY is empty or missing in this machine. When ARGV[0] is
 * not installed in the machine, such as a host shell the distribution does
 * not ship, it starts the account's login shell like `nsl` does.
 */
static const char guest_script[] =
  "if [ -z \"$1\" ] || ! cd \"$1\" 2>/dev/null; then cd 2>/dev/null || cd /; fi\n"
  "shift\n"
  "command -v \"$1\" >/dev/null 2>&1 || set -- \"${SHELL:-/bin/sh}\" -l\n"
  "exec \"$@\"\n";

/* Environment the terminal sets for the guest that should not be passed
 * into the machine.
 */
static const char * const skip_guest_env[] = {
  "PWD",
  "FLATPAK_TTY_PROGRESS",
};

/* Environment nsl forwards to the machine by itself; keep it on the host
 * process too so nsl sees the terminal it runs in.
 */
static const char * const host_terminal_env[] = {
  "TERM",
  "COLORTERM",
  "LANG",
  "LANGUAGE",
};

static void
nsl_spawn_free (gpointer data)
{
  NslSpawn *spawn = data;

  g_clear_object (&spawn->container);
  g_clear_pointer (&spawn->directory, g_free);
  g_free (spawn);
}

static gboolean
env_is_skipped (const char *pair)
{
  for (guint i = 0; i < G_N_ELEMENTS (skip_guest_env); i++)
    {
      gsize len = strlen (skip_guest_env[i]);

      if (strncmp (pair, skip_guest_env[i], len) == 0 && pair[len] == '=')
        return TRUE;
    }

  return FALSE;
}

static gboolean
ptyxis_nsl_container_run_context_cb (PtyxisRunContext    *run_context,
                                     const char * const  *argv,
                                     const char * const  *env,
                                     const char          *cwd,
                                     PtyxisUnixFDMap     *unix_fd_map,
                                     gpointer             user_data,
                                     GError             **error)
{
  NslSpawn *spawn = user_data;
  PtyxisNslContainer *self;

  g_assert (PTYXIS_IS_RUN_CONTEXT (run_context));
  g_assert (argv != NULL);
  g_assert (env != NULL);
  g_assert (PTYXIS_IS_UNIX_FD_MAP (unix_fd_map));
  g_assert (spawn != NULL);
  g_assert (PTYXIS_IS_NSL_CONTAINER (spawn->container));

  self = spawn->container;

  if (self->nsl_path == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "nsl is not installed");
      return FALSE;
    }

  /* Pass the PTY through as stdin/stdout/stderr */
  if (!ptyxis_run_context_merge_unix_fd_map (run_context, unix_fd_map, error))
    return FALSE;

  /* nsl itself runs on the host. The directory in the machine is chosen by
   * the guest script instead.
   */
  ptyxis_run_context_set_cwd (run_context, g_get_home_dir ());

  for (guint i = 0; i < G_N_ELEMENTS (host_terminal_env); i++)
    {
      const char *value = g_environ_getenv ((char **)env, host_terminal_env[i]);

      if (value != NULL)
        ptyxis_run_context_setenv (run_context, host_terminal_env[i], value);
    }

  ptyxis_run_context_append_argv (run_context, self->nsl_path);
  ptyxis_run_context_append_argv (run_context, "run");
  ptyxis_run_context_append_argv (run_context, "-m");
  ptyxis_run_context_append_argv (run_context, self->name);
  ptyxis_run_context_append_argv (run_context, "--cd");
  ptyxis_run_context_append_argv (run_context, "/");
  ptyxis_run_context_append_argv (run_context, "--");

  /* nsl only forwards terminal and locale variables, so set the rest
   * (VTE_VERSION, PTYXIS_PROFILE, proxies) with env(1) in the machine.
   */
  ptyxis_run_context_append_argv (run_context, "env");
  for (guint i = 0; env[i]; i++)
    {
      if (strchr (env[i], '=') != NULL && !env_is_skipped (env[i]))
        ptyxis_run_context_append_argv (run_context, env[i]);
    }

  ptyxis_run_context_append_argv (run_context, "/bin/sh");
  ptyxis_run_context_append_argv (run_context, "-c");
  ptyxis_run_context_append_argv (run_context, guest_script);
  ptyxis_run_context_append_argv (run_context, "ptyxis-nsl");
  ptyxis_run_context_append_argv (run_context, spawn->directory ? spawn->directory : "");
  ptyxis_run_context_append_args (run_context, argv);

  return TRUE;
}

static gboolean
ptyxis_nsl_container_handle_spawn (PtyxisIpcContainer    *container,
                                   GDBusMethodInvocation *invocation,
                                   GUnixFDList           *in_fd_list,
                                   const char            *cwd,
                                   const char * const    *argv,
                                   GVariant              *in_fds,
                                   GVariant              *in_env)
{
  PtyxisNslContainer *self = (PtyxisNslContainer *)container;
  g_autoptr(PtyxisRunContext) run_context = NULL;
  g_autoptr(PtyxisIpcProcess) process = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GUnixFDList) out_fd_list = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *object_path = NULL;
  g_autofree char *guid = NULL;
  GDBusConnection *connection;
  NslSpawn *spawn;

  g_assert (PTYXIS_IS_NSL_CONTAINER (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));
  g_assert (G_IS_UNIX_FD_LIST (in_fd_list));
  g_assert (cwd != NULL);
  g_assert (argv != NULL);

  spawn = g_new0 (NslSpawn, 1);
  spawn->container = g_object_ref (self);
  spawn->directory = ptyxis_nsl_container_dup_guest_directory (self, cwd);

  run_context = ptyxis_run_context_new ();

  /* In case the agent itself had to be sandboxed */
  ptyxis_run_context_push_host (run_context);

  /* The machine has its own display, session bus and runtime directory, so
   * unlike toolbox no host session environment is copied into it.
   */
  ptyxis_run_context_push (run_context,
                           ptyxis_nsl_container_run_context_cb,
                           spawn,
                           nsl_spawn_free);

  ptyxis_agent_push_spawn (run_context, in_fd_list, cwd, argv, in_fds, in_env);

  guid = g_dbus_generate_guid ();
  object_path = g_strdup_printf ("/org/gnome/Ptyxis/Process/%s", guid);
  out_fd_list = g_unix_fd_list_new ();
  connection = g_dbus_method_invocation_get_connection (invocation);

  if (!(subprocess = ptyxis_run_context_spawn (run_context, &error)) ||
      !(process = ptyxis_process_impl_new (connection, subprocess, object_path, &error)))
    g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
  else
    {
      ptyxis_ipc_container_complete_spawn (container,
                                           g_steal_pointer (&invocation),
                                           out_fd_list,
                                           object_path);
      g_signal_emit (self, signals[SPAWNED], 0);
    }

  return TRUE;
}

static gboolean
ptyxis_nsl_container_handle_find_program_in_path (PtyxisIpcContainer    *container,
                                                  GDBusMethodInvocation *invocation,
                                                  const char            *program)
{
  g_assert (PTYXIS_IS_NSL_CONTAINER (container));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  /* Looking the program up would start the machine, and its VM, only to
   * answer a question. The guest script falls back to the account's login
   * shell when the program is missing, so report it as found.
   */
  ptyxis_ipc_container_complete_find_program_in_path (container,
                                                      g_steal_pointer (&invocation),
                                                      program);

  return TRUE;
}

static gboolean
ptyxis_nsl_container_handle_translate_uri (PtyxisIpcContainer    *container,
                                           GDBusMethodInvocation *invocation,
                                           const char            *uri)
{
  g_autofree char *translated = NULL;

  g_assert (PTYXIS_IS_NSL_CONTAINER (container));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  translated = ptyxis_nsl_translate_uri (uri);

  ptyxis_ipc_container_complete_translate_uri (container,
                                               g_steal_pointer (&invocation),
                                               translated);

  return TRUE;
}

static void
container_iface_init (PtyxisIpcContainerIface *iface)
{
  iface->handle_spawn = ptyxis_nsl_container_handle_spawn;
  iface->handle_find_program_in_path = ptyxis_nsl_container_handle_find_program_in_path;
  iface->handle_translate_uri = ptyxis_nsl_container_handle_translate_uri;
}

static void
ptyxis_nsl_container_finalize (GObject *object)
{
  PtyxisNslContainer *self = (PtyxisNslContainer *)object;

  g_clear_pointer (&self->nsl_path, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (ptyxis_nsl_container_parent_class)->finalize (object);
}

static void
ptyxis_nsl_container_class_init (PtyxisNslContainerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ptyxis_nsl_container_finalize;

  /**
   * PtyxisNslContainer::spawned:
   *
   * Emitted after a command was started in the machine, which may have
   * started the machine and its VM.
   */
  signals[SPAWNED] =
    g_signal_new ("spawned",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
ptyxis_nsl_container_init (PtyxisNslContainer *self)
{
  ptyxis_ipc_container_set_provider (PTYXIS_IPC_CONTAINER (self), "nsl");
  ptyxis_ipc_container_set_icon_name (PTYXIS_IPC_CONTAINER (self), "container-nsl-symbolic");
}

PtyxisNslContainer *
ptyxis_nsl_container_new (const char *nsl_path,
                          const char *name,
                          gboolean    isolated)
{
  PtyxisNslContainer *self;
  g_autofree char *id = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  self = g_object_new (PTYXIS_TYPE_NSL_CONTAINER, NULL);
  self->nsl_path = g_strdup (nsl_path);
  self->name = g_strdup (name);
  self->isolated = !!isolated;

  id = g_strconcat (PTYXIS_NSL_CONTAINER_ID_PREFIX, name, NULL);
  ptyxis_ipc_container_set_id (PTYXIS_IPC_CONTAINER (self), id);
  ptyxis_ipc_container_set_display_name (PTYXIS_IPC_CONTAINER (self), name);

  return self;
}

const char *
ptyxis_nsl_container_get_name (PtyxisNslContainer *self)
{
  g_return_val_if_fail (PTYXIS_IS_NSL_CONTAINER (self), NULL);

  return self->name;
}

void
ptyxis_nsl_container_set_nsl_path (PtyxisNslContainer *self,
                                   const char         *nsl_path)
{
  g_return_if_fail (PTYXIS_IS_NSL_CONTAINER (self));

  _g_set_str (&self->nsl_path, nsl_path);
}

void
ptyxis_nsl_container_set_isolated (PtyxisNslContainer *self,
                                   gboolean            isolated)
{
  g_return_if_fail (PTYXIS_IS_NSL_CONTAINER (self));

  self->isolated = !!isolated;
}

/**
 * ptyxis_nsl_container_dup_guest_directory:
 * @self: a #PtyxisNslContainer
 * @cwd: the directory requested for a new terminal, or ""
 *
 * Chooses the directory a new terminal starts in. @cwd may be a host
 * directory (from a host terminal) or a machine directory (reported by a
 * terminal already in a machine, such as /mnt/host/... or the guest home).
 *
 * Returns: (transfer full) (nullable): a machine directory, or %NULL for
 *   the guest home
 */
char *
ptyxis_nsl_container_dup_guest_directory (PtyxisNslContainer *self,
                                          const char         *cwd)
{
  static const gsize prefix_len = sizeof PTYXIS_NSL_HOST_PREFIX - 1;
  g_autofree char *translated = NULL;

  g_return_val_if_fail (PTYXIS_IS_NSL_CONTAINER (self), NULL);

  if (cwd == NULL || cwd[0] != '/')
    return NULL;

  /* Already a machine path into the shared host trees */
  if (strncmp (cwd, PTYXIS_NSL_HOST_PREFIX, prefix_len) == 0 &&
      (cwd[prefix_len] == '/' || cwd[prefix_len] == 0))
    return self->isolated ? NULL : g_strdup (cwd);

  /* A host directory in a shared tree */
  if (!self->isolated &&
      g_file_test (cwd, G_FILE_TEST_IS_DIR) &&
      (translated = ptyxis_nsl_translate_directory (cwd)))
    return g_steal_pointer (&translated);

  /* Probably a machine directory such as its home, /etc or /tmp. The guest
   * script falls back to the home directory when it does not exist there.
   */
  return g_strdup (cwd);
}
