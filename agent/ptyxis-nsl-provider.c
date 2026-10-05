/* ptyxis-nsl-provider.c
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
#include <unistd.h>

#include "ptyxis-agent-compat.h"
#include "ptyxis-agent-util.h"
#include "ptyxis-nsl.h"
#include "ptyxis-nsl-container.h"
#include "ptyxis-nsl-provider.h"
#include "ptyxis-run-context.h"

/* Coalesce bursts of file changes, such as nsl writing a record and then
 * renaming it into place.
 */
#define NSL_RELOAD_DELAY_MSEC 750

struct _PtyxisNslProvider
{
  PtyxisContainerProvider parent_instance;
  char *nsl_path;
  char *nsl_home;
  GPtrArray *monitors;
  guint queued_update;
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (PtyxisNslProvider, ptyxis_nsl_provider, PTYXIS_TYPE_CONTAINER_PROVIDER)

static gboolean
machine_is_usable (const PtyxisNslMachine *machine)
{
  /* nsl cannot run commands in a machine whose creation did not finish or
   * that is being removed.
   */
  return g_strcmp0 (machine->state, "incomplete") != 0 &&
         g_strcmp0 (machine->state, "removing") != 0;
}

static gboolean
has_container (PtyxisNslProvider *self,
               const char        *name)
{
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(PtyxisNslContainer) container = g_list_model_get_item (G_LIST_MODEL (self), i);

      if (g_strcmp0 (ptyxis_nsl_container_get_name (container), name) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
ptyxis_nsl_provider_followup_cb (gpointer data)
{
  ptyxis_nsl_provider_queue_update (PTYXIS_NSL_PROVIDER (data));

  return G_SOURCE_REMOVE;
}

static void
ptyxis_nsl_provider_container_spawned_cb (PtyxisNslProvider  *self,
                                          PtyxisNslContainer *container)
{
  g_assert (PTYXIS_IS_NSL_PROVIDER (self));
  g_assert (PTYXIS_IS_NSL_CONTAINER (container));

  /* Starting a terminal may boot the VM and then the machine. Nothing on
   * disk changes when a machine starts, and polling would keep the VM from
   * idling, so look again while it comes up.
   */
  g_timeout_add_seconds_full (G_PRIORITY_LOW, 5,
                              ptyxis_nsl_provider_followup_cb,
                              g_object_ref (self), g_object_unref);
  g_timeout_add_seconds_full (G_PRIORITY_LOW, 20,
                              ptyxis_nsl_provider_followup_cb,
                              g_object_ref (self), g_object_unref);
}

/* Unlike ptyxis_container_provider_merge(), containers that remain keep
 * their object, which the agent has already exported on the bus.
 */
static void
ptyxis_nsl_provider_merge (PtyxisNslProvider *self,
                           GPtrArray         *machines)
{
  g_autoptr(GPtrArray) stale = g_ptr_array_new_with_free_func (g_object_unref);
  guint n_items;

  g_assert (PTYXIS_IS_NSL_PROVIDER (self));
  g_assert (machines != NULL);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(PtyxisNslContainer) container = g_list_model_get_item (G_LIST_MODEL (self), i);
      const char *name = ptyxis_nsl_container_get_name (container);
      gboolean found = FALSE;

      for (guint j = 0; j < machines->len; j++)
        {
          const PtyxisNslMachine *machine = g_ptr_array_index (machines, j);

          if (g_strcmp0 (machine->name, name) == 0 && machine_is_usable (machine))
            {
              ptyxis_nsl_container_set_isolated (container, g_strcmp0 (machine->tier, "isolated") == 0);
              ptyxis_nsl_container_set_nsl_path (container, self->nsl_path);
              found = TRUE;
              break;
            }
        }

      if (!found)
        g_ptr_array_add (stale, g_steal_pointer (&container));
    }

  for (guint i = 0; i < stale->len; i++)
    ptyxis_container_provider_emit_removed (PTYXIS_CONTAINER_PROVIDER (self),
                                            g_ptr_array_index (stale, i));

  for (guint i = 0; i < machines->len; i++)
    {
      const PtyxisNslMachine *machine = g_ptr_array_index (machines, i);
      g_autoptr(PtyxisNslContainer) container = NULL;

      if (!machine_is_usable (machine) || has_container (self, machine->name))
        continue;

      container = ptyxis_nsl_container_new (self->nsl_path,
                                            machine->name,
                                            g_strcmp0 (machine->tier, "isolated") == 0);
      g_signal_connect_object (container,
                               "spawned",
                               G_CALLBACK (ptyxis_nsl_provider_container_spawned_cb),
                               self,
                               G_CONNECT_SWAPPED);
      ptyxis_container_provider_emit_added (PTYXIS_CONTAINER_PROVIDER (self),
                                            PTYXIS_IPC_CONTAINER (container));
    }
}

static void
ptyxis_nsl_provider_list_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GPtrArray) machines = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *stdout_buf = NULL;
  g_autofree char *stderr_buf = NULL;
  PtyxisNslProvider *self;

  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!g_subprocess_communicate_utf8_finish (subprocess, result, &stdout_buf, &stderr_buf, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (!g_subprocess_get_successful (subprocess))
    {
      g_autofree char *message = ptyxis_nsl_error_message (stderr_buf, "nsl list failed");

      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", message);
      return;
    }

  if (!(machines = ptyxis_nsl_parse_machines (stdout_buf, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  ptyxis_nsl_provider_merge (self, machines);

  g_task_return_pointer (task, g_steal_pointer (&machines), (GDestroyNotify)g_ptr_array_unref);
}

void
ptyxis_nsl_provider_list_async (PtyxisNslProvider   *self,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autoptr(PtyxisRunContext) run_context = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (PTYXIS_IS_NSL_PROVIDER (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ptyxis_nsl_provider_list_async);

  if (self->nsl_path == NULL)
    {
      g_autoptr(GPtrArray) empty = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_machine_free);

      ptyxis_nsl_provider_merge (self, empty);
      g_task_return_pointer (task, g_steal_pointer (&empty), (GDestroyNotify)g_ptr_array_unref);
      return;
    }

  run_context = ptyxis_run_context_new ();
  ptyxis_run_context_push_host (run_context);
  ptyxis_run_context_set_cwd (run_context, g_get_home_dir ());
  ptyxis_run_context_append_argv (run_context, self->nsl_path);
  ptyxis_run_context_append_argv (run_context, "list");
  ptyxis_run_context_append_argv (run_context, "--json");

  if (!(subprocess = ptyxis_run_context_spawn_with_flags (run_context,
                                                          (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                           G_SUBPROCESS_FLAGS_STDERR_PIPE),
                                                          &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_subprocess_communicate_utf8_async (subprocess,
                                       NULL,
                                       cancellable,
                                       ptyxis_nsl_provider_list_cb,
                                       g_steal_pointer (&task));
}

GPtrArray *
ptyxis_nsl_provider_list_finish (PtyxisNslProvider  *self,
                                 GAsyncResult       *result,
                                 GError            **error)
{
  g_return_val_if_fail (PTYXIS_IS_NSL_PROVIDER (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
ptyxis_nsl_provider_update_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  PtyxisNslProvider *self = (PtyxisNslProvider *)object;
  g_autoptr(GPtrArray) machines = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IS_NSL_PROVIDER (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!(machines = ptyxis_nsl_provider_list_finish (self, result, &error)))
    g_debug ("Failed to list nsl machines: %s", error->message);

  g_signal_emit (self, signals[CHANGED], 0);
}

static gboolean
ptyxis_nsl_provider_update_source_func (gpointer user_data)
{
  PtyxisNslProvider *self = user_data;

  g_assert (PTYXIS_IS_NSL_PROVIDER (self));

  self->queued_update = 0;

  ptyxis_nsl_provider_list_async (self, NULL, ptyxis_nsl_provider_update_cb, NULL);

  return G_SOURCE_REMOVE;
}

/**
 * ptyxis_nsl_provider_queue_update:
 * @self: a #PtyxisNslProvider
 *
 * Reloads machines shortly and emits #PtyxisNslProvider::changed.
 */
void
ptyxis_nsl_provider_queue_update (PtyxisNslProvider *self)
{
  g_return_if_fail (PTYXIS_IS_NSL_PROVIDER (self));

  if (self->queued_update == 0)
    self->queued_update = g_timeout_add_full (G_PRIORITY_LOW,
                                              NSL_RELOAD_DELAY_MSEC,
                                              ptyxis_nsl_provider_update_source_func,
                                              self, NULL);
}

static gboolean
is_interesting (GFile      *file,
                const char *only_name)
{
  g_autofree char *name = NULL;

  if (file == NULL)
    return FALSE;

  name = g_file_get_basename (file);

  /* Ignore lock files and temporary files nsl writes before renaming */
  if (name == NULL || name[0] == '.')
    return FALSE;

  if (only_name != NULL)
    return g_strcmp0 (name, only_name) == 0;

  return g_str_has_suffix (name, ".json");
}

static void
ptyxis_nsl_provider_home_changed_cb (PtyxisNslProvider *self,
                                     GFile             *file,
                                     GFile             *other_file,
                                     GFileMonitorEvent  event,
                                     GFileMonitor      *monitor)
{
  g_autofree char *name = g_file_get_basename (file);

  g_assert (PTYXIS_IS_NSL_PROVIDER (self));

  if (is_interesting (file, "default") || is_interesting (other_file, "default"))
    ptyxis_nsl_provider_queue_update (self);
  else if ((event == G_FILE_MONITOR_EVENT_CREATED || event == G_FILE_MONITOR_EVENT_MOVED_IN) &&
           (g_strcmp0 (name, "machines") == 0 || g_strcmp0 (name, "removing") == 0))
    /* nsl was used for the first time; watch the new state directories */
    ptyxis_nsl_provider_rescan (self);
}

static void
ptyxis_nsl_provider_records_changed_cb (PtyxisNslProvider *self,
                                        GFile             *file,
                                        GFile             *other_file,
                                        GFileMonitorEvent  event,
                                        GFileMonitor      *monitor)
{
  g_assert (PTYXIS_IS_NSL_PROVIDER (self));

  if (event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED)
    return;

  if (is_interesting (file, NULL) || is_interesting (other_file, NULL))
    ptyxis_nsl_provider_queue_update (self);
}

static void
ptyxis_nsl_provider_runtime_changed_cb (PtyxisNslProvider *self,
                                        GFile             *file,
                                        GFile             *other_file,
                                        GFileMonitorEvent  event,
                                        GFileMonitor      *monitor)
{
  g_assert (PTYXIS_IS_NSL_PROVIDER (self));

  /* A VM's runtime directory appears and disappears with the VM */
  if (event == G_FILE_MONITOR_EVENT_CREATED ||
      event == G_FILE_MONITOR_EVENT_DELETED ||
      event == G_FILE_MONITOR_EVENT_MOVED_IN ||
      event == G_FILE_MONITOR_EVENT_MOVED_OUT)
    ptyxis_nsl_provider_queue_update (self);
}

static void
ptyxis_nsl_provider_add_monitor (PtyxisNslProvider *self,
                                 const char        *path,
                                 GCallback          callback)
{
  g_autoptr(GFile) file = g_file_new_for_path (path);
  g_autoptr(GFileMonitor) monitor = NULL;

  if (!(monitor = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL)))
    return;

  g_signal_connect_object (monitor,
                           "changed",
                           callback,
                           self,
                           G_CONNECT_SWAPPED);

  g_ptr_array_add (self->monitors, g_steal_pointer (&monitor));
}

static void
ptyxis_nsl_provider_setup_monitors (PtyxisNslProvider *self)
{
  g_autofree char *machines = NULL;
  g_autofree char *removing = NULL;
  g_autofree char *runtime = NULL;

  g_assert (PTYXIS_IS_NSL_PROVIDER (self));

  if (self->monitors->len > 0)
    g_ptr_array_remove_range (self->monitors, 0, self->monitors->len);

  machines = g_build_filename (self->nsl_home, "machines", NULL);
  removing = g_build_filename (self->nsl_home, "removing", NULL);
  runtime = g_strdup_printf ("/run/user/%u/nsl", (guint)getuid ());

  ptyxis_nsl_provider_add_monitor (self, self->nsl_home, G_CALLBACK (ptyxis_nsl_provider_home_changed_cb));

  if (g_file_test (machines, G_FILE_TEST_IS_DIR))
    ptyxis_nsl_provider_add_monitor (self, machines, G_CALLBACK (ptyxis_nsl_provider_records_changed_cb));

  if (g_file_test (removing, G_FILE_TEST_IS_DIR))
    ptyxis_nsl_provider_add_monitor (self, removing, G_CALLBACK (ptyxis_nsl_provider_records_changed_cb));

  /* nsl creates this on first use after boot; file monitors pick up
   * directories that appear later.
   */
  ptyxis_nsl_provider_add_monitor (self, runtime, G_CALLBACK (ptyxis_nsl_provider_runtime_changed_cb));
}

static char *
find_program (void)
{
  g_autoptr(PtyxisRunContext) run_context = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autofree char *stdout_buf = NULL;

  if (!ptyxis_agent_is_sandboxed ())
    return ptyxis_nsl_find_program ();

  /* The agent could not run on the host, so look there for it */
  run_context = ptyxis_run_context_new ();
  ptyxis_run_context_push_host (run_context);
  ptyxis_run_context_append_argv (run_context, "sh");
  ptyxis_run_context_append_argv (run_context, "-c");
  ptyxis_run_context_append_argv (run_context,
                                  "for p in \"$(command -v nsl)\" \"$HOME/.local/bin/nsl\" "
                                  "/home/linuxbrew/.linuxbrew/bin/nsl \"$HOME/.linuxbrew/bin/nsl\" "
                                  "/usr/local/bin/nsl; do "
                                  "if [ -n \"$p\" ] && [ -x \"$p\" ]; then echo \"$p\"; exit 0; fi; "
                                  "done; exit 1");

  if (!(subprocess = ptyxis_run_context_spawn_with_flags (run_context, G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL)) ||
      !g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &stdout_buf, NULL, NULL) ||
      !g_subprocess_get_successful (subprocess) ||
      stdout_buf == NULL)
    return NULL;

  g_strstrip (stdout_buf);

  if (stdout_buf[0] == 0)
    return NULL;

  return g_steal_pointer (&stdout_buf);
}

/**
 * ptyxis_nsl_provider_load_names:
 * @self: a #PtyxisNslProvider
 *
 * Adds a container for each machine record without running nsl, so that
 * machines are available immediately, such as to restore a session. A list
 * from nsl replaces the result shortly after.
 */
void
ptyxis_nsl_provider_load_names (PtyxisNslProvider *self)
{
  g_autoptr(GPtrArray) machines = NULL;
  g_autofree char *machines_dir = NULL;
  g_autoptr(GDir) dir = NULL;
  const char *name;

  g_return_if_fail (PTYXIS_IS_NSL_PROVIDER (self));

  if (self->nsl_path == NULL)
    return;

  machines_dir = g_build_filename (self->nsl_home, "machines", NULL);

  if (!(dir = g_dir_open (machines_dir, 0, NULL)))
    return;

  machines = g_ptr_array_new_with_free_func ((GDestroyNotify)ptyxis_nsl_machine_free);

  while ((name = g_dir_read_name (dir)))
    {
      g_autofree char *machine_name = NULL;
      PtyxisNslMachine *machine;

      if (!g_str_has_suffix (name, ".json"))
        continue;

      machine_name = g_strndup (name, strlen (name) - strlen (".json"));

      if (!ptyxis_nsl_is_valid_name (machine_name))
        continue;

      machine = g_new0 (PtyxisNslMachine, 1);
      machine->name = g_steal_pointer (&machine_name);
      machine->state = g_strdup ("stopped");
      machine->tier = g_strdup ("shared");
      g_ptr_array_add (machines, machine);
    }

  ptyxis_nsl_provider_merge (self, machines);
}

/**
 * ptyxis_nsl_provider_rescan:
 * @self: a #PtyxisNslProvider
 *
 * Looks for nsl again, such as after it was installed, and reloads.
 */
void
ptyxis_nsl_provider_rescan (PtyxisNslProvider *self)
{
  g_autofree char *nsl_path = NULL;

  g_return_if_fail (PTYXIS_IS_NSL_PROVIDER (self));

  nsl_path = find_program ();
  _g_set_str (&self->nsl_path, nsl_path);

  ptyxis_nsl_provider_setup_monitors (self);
  ptyxis_nsl_provider_queue_update (self);
}

const char *
ptyxis_nsl_provider_get_nsl_path (PtyxisNslProvider *self)
{
  g_return_val_if_fail (PTYXIS_IS_NSL_PROVIDER (self), NULL);

  return self->nsl_path;
}

static void
ptyxis_nsl_provider_constructed (GObject *object)
{
  PtyxisNslProvider *self = (PtyxisNslProvider *)object;

  G_OBJECT_CLASS (ptyxis_nsl_provider_parent_class)->constructed (object);

  self->nsl_home = ptyxis_nsl_dup_home ();
  self->nsl_path = find_program ();

  g_debug ("nsl: %s, state in %s",
           self->nsl_path ? self->nsl_path : "not found",
           self->nsl_home);

  ptyxis_nsl_provider_setup_monitors (self);
}

static void
ptyxis_nsl_provider_dispose (GObject *object)
{
  PtyxisNslProvider *self = (PtyxisNslProvider *)object;

  if (self->monitors->len > 0)
    g_ptr_array_remove_range (self->monitors, 0, self->monitors->len);

  g_clear_handle_id (&self->queued_update, g_source_remove);

  G_OBJECT_CLASS (ptyxis_nsl_provider_parent_class)->dispose (object);
}

static void
ptyxis_nsl_provider_finalize (GObject *object)
{
  PtyxisNslProvider *self = (PtyxisNslProvider *)object;

  g_clear_pointer (&self->monitors, g_ptr_array_unref);
  g_clear_pointer (&self->nsl_path, g_free);
  g_clear_pointer (&self->nsl_home, g_free);

  G_OBJECT_CLASS (ptyxis_nsl_provider_parent_class)->finalize (object);
}

static void
ptyxis_nsl_provider_class_init (PtyxisNslProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ptyxis_nsl_provider_constructed;
  object_class->dispose = ptyxis_nsl_provider_dispose;
  object_class->finalize = ptyxis_nsl_provider_finalize;

  /**
   * PtyxisNslProvider::changed:
   *
   * Emitted after machines were reloaded because their state on disk
   * changed or a rescan was requested.
   */
  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
ptyxis_nsl_provider_init (PtyxisNslProvider *self)
{
  self->monitors = g_ptr_array_new_with_free_func (g_object_unref);
}

PtyxisContainerProvider *
ptyxis_nsl_provider_new (void)
{
  return g_object_new (PTYXIS_TYPE_NSL_PROVIDER, NULL);
}
