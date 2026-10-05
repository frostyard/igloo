/* ptyxis-machines-impl.c
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
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "ptyxis-machines-impl.h"
#include "ptyxis-nsl.h"
#include "ptyxis-run-context.h"

struct _PtyxisMachinesImpl
{
  PtyxisIpcMachinesSkeleton parent_instance;
  PtyxisNslProvider *provider;
};

static void machines_iface_init (PtyxisIpcMachinesIface *iface);

G_DEFINE_TYPE_WITH_CODE (PtyxisMachinesImpl, ptyxis_machines_impl, PTYXIS_IPC_TYPE_MACHINES_SKELETON,
                         G_IMPLEMENT_INTERFACE (PTYXIS_IPC_TYPE_MACHINES, machines_iface_init))

static PtyxisRunContext *
nsl_run_context (PtyxisMachinesImpl *self)
{
  PtyxisRunContext *run_context = ptyxis_run_context_new ();

  ptyxis_run_context_push_host (run_context);
  ptyxis_run_context_set_cwd (run_context, g_get_home_dir ());
  ptyxis_run_context_append_argv (run_context, ptyxis_nsl_provider_get_nsl_path (self->provider));

  return run_context;
}

static gboolean
return_if_missing (PtyxisMachinesImpl    *self,
                   GDBusMethodInvocation *invocation)
{
  if (ptyxis_nsl_provider_get_nsl_path (self->provider) != NULL)
    return FALSE;

  g_dbus_method_invocation_return_error_literal (invocation,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_NOT_FOUND,
                                                 "nsl is not installed");

  return TRUE;
}

static char *
error_from_output (const char *output,
                   const char *fallback)
{
  g_autofree char *copy = NULL;
  const char *message;

  if (output == NULL)
    return g_strdup (fallback);

  copy = g_strstrip (g_strdup (output));
  message = copy;

  /* nsl reports "nsl: message" */
  if (g_str_has_prefix (message, "nsl: "))
    message += strlen ("nsl: ");

  if (message[0] == 0)
    return g_strdup (fallback);

  return g_strdup (message);
}

static void
ptyxis_machines_impl_version_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  g_autoptr(PtyxisMachinesImpl) self = user_data;
  g_autofree char *version = NULL;

  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (PTYXIS_IS_MACHINES_IMPL (self));

  if (g_subprocess_communicate_utf8_finish (subprocess, result, &version, NULL, NULL) &&
      g_subprocess_get_successful (subprocess) &&
      version != NULL)
    ptyxis_ipc_machines_set_version (PTYXIS_IPC_MACHINES (self), g_strstrip (version));
}

static void
ptyxis_machines_impl_update_properties (PtyxisMachinesImpl *self)
{
  const char *nsl_path = ptyxis_nsl_provider_get_nsl_path (self->provider);
  g_autoptr(PtyxisRunContext) run_context = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));

  ptyxis_ipc_machines_set_nsl_path (PTYXIS_IPC_MACHINES (self), nsl_path ? nsl_path : "");
  ptyxis_ipc_machines_set_version (PTYXIS_IPC_MACHINES (self), "");

  if (nsl_path == NULL)
    return;

  run_context = nsl_run_context (self);
  ptyxis_run_context_append_argv (run_context, "version");

  if ((subprocess = ptyxis_run_context_spawn_with_flags (run_context,
                                                         (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE),
                                                         NULL)))
    g_subprocess_communicate_utf8_async (subprocess,
                                         NULL,
                                         NULL,
                                         ptyxis_machines_impl_version_cb,
                                         g_object_ref (self));
}

static void
ptyxis_machines_impl_provider_changed_cb (PtyxisMachinesImpl *self,
                                          PtyxisNslProvider  *provider)
{
  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (PTYXIS_IS_NSL_PROVIDER (provider));

  ptyxis_ipc_machines_emit_changed (PTYXIS_IPC_MACHINES (self));
}

static gboolean
ptyxis_machines_impl_handle_refresh (PtyxisIpcMachines     *machines,
                                     GDBusMethodInvocation *invocation)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  ptyxis_nsl_provider_rescan (self->provider);
  ptyxis_machines_impl_update_properties (self);

  ptyxis_ipc_machines_complete_refresh (machines, g_steal_pointer (&invocation));

  return TRUE;
}

static void
ptyxis_machines_impl_list_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  PtyxisNslProvider *provider = (PtyxisNslProvider *)object;
  g_autoptr(GDBusMethodInvocation) invocation = user_data;
  g_autoptr(GPtrArray) machines = NULL;
  g_autoptr(GError) error = NULL;
  GVariantBuilder builder;
  PtyxisIpcMachines *self;

  g_assert (PTYXIS_IS_NSL_PROVIDER (provider));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  self = g_object_get_data (G_OBJECT (invocation), "PTYXIS_IPC_MACHINES");

  if (!(machines = ptyxis_nsl_provider_list_finish (provider, result, &error)))
    {
      g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);
      return;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (guint i = 0; i < machines->len; i++)
    {
      const PtyxisNslMachine *machine = g_ptr_array_index (machines, i);

      g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_string (machine->name));
      g_variant_builder_add (&builder, "{sv}", "state", g_variant_new_string (machine->state ? machine->state : ""));
      g_variant_builder_add (&builder, "{sv}", "image", g_variant_new_string (machine->image ? machine->image : ""));
      g_variant_builder_add (&builder, "{sv}", "tier", g_variant_new_string (machine->tier ? machine->tier : ""));
      g_variant_builder_add (&builder, "{sv}", "default", g_variant_new_boolean (machine->is_default));
      g_variant_builder_close (&builder);
    }

  ptyxis_ipc_machines_complete_list (self,
                                     g_steal_pointer (&invocation),
                                     g_variant_builder_end (&builder));
}

static gboolean
ptyxis_machines_impl_handle_list (PtyxisIpcMachines     *machines,
                                  GDBusMethodInvocation *invocation)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  g_object_set_data_full (G_OBJECT (invocation),
                          "PTYXIS_IPC_MACHINES",
                          g_object_ref (machines),
                          g_object_unref);

  ptyxis_nsl_provider_list_async (self->provider,
                                  NULL,
                                  ptyxis_machines_impl_list_cb,
                                  g_steal_pointer (&invocation));

  return TRUE;
}

typedef void (*OutputHandler) (PtyxisIpcMachines     *self,
                               GDBusMethodInvocation *invocation,
                               GSubprocess           *subprocess,
                               const char            *output);

typedef struct
{
  PtyxisIpcMachines     *self;
  GDBusMethodInvocation *invocation;
  OutputHandler          handler;
} Communicate;

static void
communicate_free (Communicate *state)
{
  g_clear_object (&state->self);
  g_clear_object (&state->invocation);
  g_free (state);
}

static void
communicate_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  Communicate *state = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *output = NULL;

  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (state != NULL);

  if (!g_subprocess_communicate_utf8_finish (subprocess, result, &output, NULL, &error))
    g_dbus_method_invocation_return_gerror (g_steal_pointer (&state->invocation), error);
  else
    state->handler (state->self, g_steal_pointer (&state->invocation), subprocess, output);

  communicate_free (state);
}

/* Runs nsl with @args, merging stderr into the output given to @handler */
static void
run_nsl (PtyxisMachinesImpl    *self,
         GDBusMethodInvocation *invocation,
         const char * const    *args,
         OutputHandler          handler)
{
  g_autoptr(PtyxisRunContext) run_context = nsl_run_context (self);
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GError) error = NULL;
  Communicate *state;

  ptyxis_run_context_append_args (run_context, args);

  /* A closed stdin makes sure nsl never waits for input */
  if (!(subprocess = ptyxis_run_context_spawn_with_flags (run_context,
                                                          (G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                           G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                           G_SUBPROCESS_FLAGS_STDERR_MERGE),
                                                          &error)))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  state = g_new0 (Communicate, 1);
  state->self = g_object_ref (PTYXIS_IPC_MACHINES (self));
  state->invocation = invocation;
  state->handler = handler;

  g_subprocess_communicate_utf8_async (subprocess, NULL, NULL, communicate_cb, state);
}

static int
exit_code (GSubprocess *subprocess)
{
  if (g_subprocess_get_if_exited (subprocess))
    return g_subprocess_get_exit_status (subprocess);

  if (g_subprocess_get_if_signaled (subprocess))
    return 128 + g_subprocess_get_term_sig (subprocess);

  return -1;
}

static void
images_output (PtyxisIpcMachines     *self,
               GDBusMethodInvocation *invocation,
               GSubprocess           *subprocess,
               const char            *output)
{
  g_autoptr(GPtrArray) images = NULL;
  GVariantBuilder builder;

  if (!g_subprocess_get_successful (subprocess))
    {
      g_autofree char *message = error_from_output (output, "Failed to list machine images");
      g_dbus_method_invocation_return_error_literal (invocation, G_IO_ERROR, G_IO_ERROR_FAILED, message);
      return;
    }

  images = ptyxis_nsl_parse_images (output);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (guint i = 0; i < images->len; i++)
    {
      const PtyxisNslImage *image = g_ptr_array_index (images, i);

      g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&builder, "{sv}", "selectors", g_variant_new_strv ((const char * const *)image->selectors, -1));
      g_variant_builder_add (&builder, "{sv}", "build", g_variant_new_string (image->build ? image->build : ""));
      g_variant_builder_add (&builder, "{sv}", "cached", g_variant_new_boolean (image->cached));
      g_variant_builder_close (&builder);
    }

  ptyxis_ipc_machines_complete_list_images (self, invocation, g_variant_builder_end (&builder));
}

static gboolean
ptyxis_machines_impl_handle_list_images (PtyxisIpcMachines     *machines,
                                         GDBusMethodInvocation *invocation,
                                         gboolean               refresh)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;
  const char *args[] = { "images", refresh ? "--refresh" : NULL, NULL };

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (!return_if_missing (self, invocation))
    run_nsl (self, invocation, args, images_output);

  return TRUE;
}

static GVariant *
settings_to_variant (GPtrArray *settings)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (guint i = 0; i < settings->len; i++)
    {
      const PtyxisNslSetting *setting = g_ptr_array_index (settings, i);

      g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&builder, "{sv}", "key", g_variant_new_string (setting->key));
      g_variant_builder_add (&builder, "{sv}", "value", g_variant_new_string (setting->value ? setting->value : ""));
      g_variant_builder_add (&builder, "{sv}", "source", g_variant_new_string (setting->source ? setting->source : ""));
      g_variant_builder_close (&builder);
    }

  return g_variant_builder_end (&builder);
}

static void
config_output (PtyxisIpcMachines     *self,
               GDBusMethodInvocation *invocation,
               GSubprocess           *subprocess,
               const char            *output)
{
  g_autoptr(GPtrArray) settings = NULL;
  g_autofree char *path = NULL;

  if (!g_subprocess_get_successful (subprocess))
    {
      g_autofree char *message = error_from_output (output, "Failed to read the nsl configuration");
      g_dbus_method_invocation_return_error_literal (invocation, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
      return;
    }

  settings = ptyxis_nsl_parse_config (output, &path);

  ptyxis_ipc_machines_complete_get_config (self,
                                           invocation,
                                           path ? path : "",
                                           settings_to_variant (settings));
}

static gboolean
ptyxis_machines_impl_handle_get_config (PtyxisIpcMachines     *machines,
                                        GDBusMethodInvocation *invocation)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;
  static const char * const args[] = { "config", NULL };

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (!return_if_missing (self, invocation))
    run_nsl (self, invocation, args, config_output);

  return TRUE;
}

/**
 * ptyxis_machines_impl_dup_config_path:
 *
 * Gets the path of nsl.conf: `$XDG_CONFIG_HOME/nsl/nsl.conf`, or
 * `~/.config/nsl/nsl.conf` when XDG_CONFIG_HOME is unset, empty or
 * relative.
 *
 * Returns: (transfer full): the path to nsl.conf
 */
char *
ptyxis_machines_impl_dup_config_path (void)
{
  const char *config = g_getenv ("XDG_CONFIG_HOME");

  if (config != NULL && g_path_is_absolute (config))
    return g_build_filename (config, "nsl", "nsl.conf", NULL);

  return g_build_filename (g_get_home_dir (), ".config", "nsl", "nsl.conf", NULL);
}

/**
 * ptyxis_machines_impl_apply_settings:
 * @contents: (nullable): the current nsl.conf, or %NULL when absent
 * @settings: an "a{ss}" of "section.key" to value; "" removes the key
 *
 * Applies @settings to @contents, keeping comments and other keys.
 * Sections left empty are removed.
 *
 * Returns: (transfer full) (nullable): the new contents
 */
char *
ptyxis_machines_impl_apply_settings (const char  *contents,
                                     GVariant    *settings,
                                     GError     **error)
{
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_auto(GStrv) groups = NULL;
  GVariantIter iter;
  const char *key;
  const char *value;

  g_return_val_if_fail (settings != NULL, NULL);
  g_return_val_if_fail (g_variant_is_of_type (settings, G_VARIANT_TYPE ("a{ss}")), NULL);

  if (contents != NULL &&
      contents[0] != 0 &&
      !g_key_file_load_from_data (key_file, contents, -1, G_KEY_FILE_KEEP_COMMENTS, error))
    return NULL;

  g_variant_iter_init (&iter, settings);

  while (g_variant_iter_next (&iter, "{&s&s}", &key, &value))
    {
      g_autofree char *section = NULL;
      const char *dot = strchr (key, '.');

      if (dot == NULL || dot == key || dot[1] == 0 || strchr (dot + 1, '.') != NULL)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Invalid setting “%s”", key);
          return NULL;
        }

      for (const char *c = value; *c; c++)
        {
          if (*c == '\n' || *c == '\r')
            {
              g_set_error (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid value for “%s”", key);
              return NULL;
            }
        }

      section = g_strndup (key, dot - key);

      if (value[0] == 0)
        g_key_file_remove_key (key_file, section, dot + 1, NULL);
      else
        g_key_file_set_value (key_file, section, dot + 1, value);
    }

  groups = g_key_file_get_groups (key_file, NULL);

  for (guint i = 0; groups[i]; i++)
    {
      g_auto(GStrv) keys = g_key_file_get_keys (key_file, groups[i], NULL, NULL);

      if (keys == NULL || keys[0] == NULL)
        g_key_file_remove_group (key_file, groups[i], NULL);
    }

  return g_key_file_to_data (key_file, NULL, error);
}

typedef struct
{
  char     *target;
  char     *previous;
  gboolean  existed;
} Restore;

static void
restore_free (Restore *restore)
{
  g_clear_pointer (&restore->target, g_free);
  g_clear_pointer (&restore->previous, g_free);
  g_free (restore);
}

static void
check_config_output (PtyxisIpcMachines     *machines,
                     GDBusMethodInvocation *invocation,
                     GSubprocess           *subprocess,
                     const char            *output)
{
  Restore *restore = g_object_get_data (G_OBJECT (invocation), "NSL_RESTORE");
  g_autofree char *message = NULL;

  g_assert (restore != NULL);

  if (g_subprocess_get_successful (subprocess))
    {
      ptyxis_ipc_machines_complete_set_config (machines, invocation);
      return;
    }

  /* nsl must understand the whole file, or it refuses to start a VM */
  if (restore->existed)
    g_file_set_contents (restore->target, restore->previous, -1, NULL);
  else
    g_unlink (restore->target);

  message = error_from_output (output, "nsl rejected the configuration");
  g_dbus_method_invocation_return_error_literal (invocation, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
}

static gboolean
ptyxis_machines_impl_handle_set_config (PtyxisIpcMachines     *machines,
                                        GDBusMethodInvocation *invocation,
                                        GVariant              *settings)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;
  static const char * const check_args[] = { "config", NULL };
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  g_autofree char *target = NULL;
  g_autofree char *previous = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *dir = NULL;
  char resolved[PATH_MAX];
  Restore *restore;
  gboolean existed;

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (return_if_missing (self, invocation))
    return TRUE;

  path = ptyxis_machines_impl_dup_config_path ();

  /* Write through a symlink, such as one managed by a dotfiles tool */
  if (g_file_test (path, G_FILE_TEST_IS_SYMLINK) && realpath (path, resolved) != NULL)
    target = g_strdup (resolved);
  else
    target = g_strdup (path);

  existed = g_file_test (target, G_FILE_TEST_EXISTS);

  if (existed && !g_file_get_contents (target, &previous, NULL, &error))
    goto failure;

  if (!(contents = ptyxis_machines_impl_apply_settings (previous, settings, &error)))
    goto failure;

  /* Removing settings from a file that does not exist changes nothing */
  if (!existed && g_strstrip (contents)[0] == 0)
    {
      ptyxis_ipc_machines_complete_set_config (machines, g_steal_pointer (&invocation));
      return TRUE;
    }

  dir = g_path_get_dirname (target);

  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      int errsv = errno;
      g_set_error (&error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errsv),
                   "Failed to create %s: %s", dir, g_strerror (errsv));
      goto failure;
    }

  if (!g_file_set_contents (target, contents, -1, &error))
    goto failure;

  restore = g_new0 (Restore, 1);
  restore->target = g_steal_pointer (&target);
  restore->previous = g_steal_pointer (&previous);
  restore->existed = existed;
  g_object_set_data_full (G_OBJECT (invocation), "NSL_RESTORE", restore, (GDestroyNotify)restore_free);

  run_nsl (self, g_steal_pointer (&invocation), check_args, check_config_output);

  return TRUE;

failure:
  g_dbus_method_invocation_return_gerror (g_steal_pointer (&invocation), error);

  return TRUE;
}

static void
run_output (PtyxisIpcMachines     *machines,
            GDBusMethodInvocation *invocation,
            GSubprocess           *subprocess,
            const char            *output)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;

  /* The command may have changed machines; let clients know */
  ptyxis_nsl_provider_queue_update (self->provider);

  ptyxis_ipc_machines_complete_run (machines,
                                    invocation,
                                    exit_code (subprocess),
                                    output ? output : "");
}

static gboolean
ptyxis_machines_impl_handle_run (PtyxisIpcMachines     *machines,
                                 GDBusMethodInvocation *invocation,
                                 const char * const    *args)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)machines;

  g_assert (PTYXIS_IS_MACHINES_IMPL (self));
  g_assert (G_IS_DBUS_METHOD_INVOCATION (invocation));

  if (args == NULL || args[0] == NULL)
    {
      g_dbus_method_invocation_return_error_literal (g_steal_pointer (&invocation),
                                                     G_IO_ERROR,
                                                     G_IO_ERROR_INVALID_ARGUMENT,
                                                     "No nsl command given");
      return TRUE;
    }

  if (!return_if_missing (self, invocation))
    run_nsl (self, g_steal_pointer (&invocation), args, run_output);

  return TRUE;
}

static void
machines_iface_init (PtyxisIpcMachinesIface *iface)
{
  iface->handle_refresh = ptyxis_machines_impl_handle_refresh;
  iface->handle_list = ptyxis_machines_impl_handle_list;
  iface->handle_list_images = ptyxis_machines_impl_handle_list_images;
  iface->handle_get_config = ptyxis_machines_impl_handle_get_config;
  iface->handle_set_config = ptyxis_machines_impl_handle_set_config;
  iface->handle_run = ptyxis_machines_impl_handle_run;
}

static void
ptyxis_machines_impl_dispose (GObject *object)
{
  PtyxisMachinesImpl *self = (PtyxisMachinesImpl *)object;

  g_clear_object (&self->provider);

  G_OBJECT_CLASS (ptyxis_machines_impl_parent_class)->dispose (object);
}

static void
ptyxis_machines_impl_class_init (PtyxisMachinesImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ptyxis_machines_impl_dispose;
}

static void
ptyxis_machines_impl_init (PtyxisMachinesImpl *self)
{
}

PtyxisMachinesImpl *
ptyxis_machines_impl_new (PtyxisNslProvider *provider)
{
  PtyxisMachinesImpl *self;

  g_return_val_if_fail (PTYXIS_IS_NSL_PROVIDER (provider), NULL);

  self = g_object_new (PTYXIS_TYPE_MACHINES_IMPL, NULL);
  self->provider = g_object_ref (provider);

  g_signal_connect_object (provider,
                           "changed",
                           G_CALLBACK (ptyxis_machines_impl_provider_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  ptyxis_machines_impl_update_properties (self);

  return self;
}
