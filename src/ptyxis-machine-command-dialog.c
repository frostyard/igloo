/* ptyxis-machine-command-dialog.c
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

#include <sys/wait.h>

#include <glib/gi18n.h>
#include <vte/vte.h>

#include "ptyxis-application.h"
#include "ptyxis-machine-command-dialog.h"

/* How long to wait for a new machine to appear before opening it */
#define OPEN_CONTAINER_TIMEOUT_SECONDS 10

struct _PtyxisMachineCommandDialog
{
  AdwDialog         parent_instance;

  char            **argv;
  char             *running_message;
  char             *succeeded_message;
  char             *failed_message;
  char             *container_id;
  PtyxisIpcProcess *process;
  GCancellable     *cancellable;
  GListModel       *containers;
  guint             open_timeout;

  AdwSpinner       *spinner;
  GtkImage         *status_icon;
  GtkLabel         *status_label;
  VteTerminal      *terminal;
  GtkButton        *close_button;
  GtkButton        *open_button;

  guint             started : 1;
  guint             finished : 1;
  guint             close_when_finished : 1;
};

enum {
  FINISHED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (PtyxisMachineCommandDialog, ptyxis_machine_command_dialog, ADW_TYPE_DIALOG)

static void
ptyxis_machine_command_dialog_finish (PtyxisMachineCommandDialog *self,
                                      int                         exit_code,
                                      const char                 *error_message)
{
  gboolean success = exit_code == 0 && error_message == NULL;
  g_autofree char *message = NULL;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  if (self->finished)
    return;

  self->finished = TRUE;
  g_clear_object (&self->process);

  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->status_icon), TRUE);

  if (success)
    {
      gtk_image_set_from_icon_name (self->status_icon, "object-select-symbolic");
      gtk_widget_add_css_class (GTK_WIDGET (self->status_icon), "success");
      message = g_strdup (self->succeeded_message ? self->succeeded_message : _("Finished"));
    }
  else
    {
      gtk_image_set_from_icon_name (self->status_icon, "dialog-error-symbolic");
      gtk_widget_add_css_class (GTK_WIDGET (self->status_icon), "error");

      if (error_message != NULL)
        message = g_strdup (error_message);
      else
        /* translators: %s is a message such as "Failed to create machine"; %d an exit code */
        message = g_strdup_printf (_("%s (exit code %d)"),
                                   self->failed_message ? self->failed_message : _("Failed"),
                                   exit_code);
    }

  gtk_label_set_label (self->status_label, message);
  gtk_button_set_label (self->close_button, _("_Close"));
  gtk_widget_set_visible (GTK_WIDGET (self->open_button), success && self->container_id != NULL);
  adw_dialog_set_can_close (ADW_DIALOG (self), TRUE);

  if (success && self->container_id != NULL)
    gtk_widget_grab_focus (GTK_WIDGET (self->open_button));
  else
    gtk_widget_grab_focus (GTK_WIDGET (self->close_button));

  g_signal_emit (self, signals[FINISHED], 0, success ? 0 : (exit_code ? exit_code : 1));

  if (self->close_when_finished)
    adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_command_dialog_wait_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  PtyxisApplication *app = (PtyxisApplication *)object;
  g_autoptr(PtyxisMachineCommandDialog) self = user_data;
  g_autoptr(GError) error = NULL;
  int status;

  g_assert (PTYXIS_IS_APPLICATION (app));
  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  status = ptyxis_application_wait_finish (app, result, &error);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  if (error != NULL)
    ptyxis_machine_command_dialog_finish (self, 1, error->message);
  else if (WIFEXITED (status))
    ptyxis_machine_command_dialog_finish (self, WEXITSTATUS (status), NULL);
  else if (WIFSIGNALED (status))
    ptyxis_machine_command_dialog_finish (self, 128 + WTERMSIG (status), NULL);
  else
    ptyxis_machine_command_dialog_finish (self, 1, NULL);
}

static void
ptyxis_machine_command_dialog_spawn_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  PtyxisApplication *app = (PtyxisApplication *)object;
  g_autoptr(PtyxisMachineCommandDialog) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IS_APPLICATION (app));
  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  self->process = ptyxis_application_spawn_finish (app, result, &error);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  if (self->process == NULL)
    {
      ptyxis_machine_command_dialog_finish (self, 1, error->message);
      return;
    }

  ptyxis_application_wait_async (app,
                                 self->process,
                                 self->cancellable,
                                 ptyxis_machine_command_dialog_wait_cb,
                                 g_object_ref (self));
}

static void
ptyxis_machine_command_dialog_apply_style (PtyxisMachineCommandDialog *self)
{
  g_autoptr(PtyxisProfile) profile = NULL;
  g_autoptr(PtyxisPalette) palette = NULL;
  g_autoptr(PangoFontDescription) font = NULL;
  const PtyxisPaletteFace *face;
  const char *font_name;
  gboolean dark;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
  dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  if ((palette = ptyxis_profile_dup_palette (profile)) &&
      (face = ptyxis_palette_get_face (palette, dark)))
    vte_terminal_set_colors (self->terminal,
                             &face->foreground,
                             &face->background,
                             face->indexed,
                             G_N_ELEMENTS (face->indexed));

  if ((font_name = ptyxis_application_get_system_font_name (PTYXIS_APPLICATION_DEFAULT)) &&
      (font = pango_font_description_from_string (font_name)))
    vte_terminal_set_font (self->terminal, font);
}

static void
ptyxis_machine_command_dialog_start (PtyxisMachineCommandDialog *self)
{
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  g_autoptr(PtyxisIpcContainer) host = NULL;
  g_autoptr(PtyxisProfile) profile = NULL;
  g_autoptr(VtePty) pty = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  if (self->started)
    return;

  self->started = TRUE;

  ptyxis_machine_command_dialog_apply_style (self);

  gtk_label_set_label (self->status_label,
                       self->running_message ? self->running_message : _("Running…"));

  if (!(pty = ptyxis_application_create_pty (app, &error)))
    {
      ptyxis_machine_command_dialog_finish (self, 1, error->message);
      return;
    }

  vte_terminal_set_pty (self->terminal, pty);

  /* nsl runs on the host, through the agent */
  if (!(host = ptyxis_application_lookup_container (app, "session")))
    {
      ptyxis_machine_command_dialog_finish (self, 1, _("The terminal agent is not available"));
      return;
    }

  profile = ptyxis_application_dup_default_profile (app);

  ptyxis_application_spawn_async (app,
                                  host,
                                  profile,
                                  NULL,
                                  pty,
                                  (const char * const *)self->argv,
                                  self->cancellable,
                                  ptyxis_machine_command_dialog_spawn_cb,
                                  g_object_ref (self));
}

static void
ptyxis_machine_command_dialog_stop_response_cb (PtyxisMachineCommandDialog *self,
                                                const char                 *response,
                                                AdwAlertDialog             *alert)
{
  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  if (g_strcmp0 (response, "stop") != 0 || self->finished)
    return;

  self->close_when_finished = TRUE;

  /* Type Ctrl+C so the terminal interrupts its whole foreground process
   * group, including nsl's children and any wrapper shell. nsl keeps
   * whatever state it needs to resume or clean up later.
   */
  if (self->process != NULL)
    vte_terminal_feed_child (self->terminal, "\003", 1);
  else
    adw_dialog_force_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_command_dialog_close_attempt_cb (PtyxisMachineCommandDialog *self)
{
  AdwDialog *alert;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  if (self->finished)
    return;

  alert = adw_alert_dialog_new (_("Stop the Command?"),
                                _("nsl will be interrupted. Running the same command again resumes or cleans up what it started."));
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert),
                                  "continue", _("_Keep Running"),
                                  "stop", _("_Stop"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert), "stop", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "continue");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "continue");

  g_signal_connect_object (alert,
                           "response",
                           G_CALLBACK (ptyxis_machine_command_dialog_stop_response_cb),
                           self,
                           G_CONNECT_SWAPPED);

  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
ptyxis_machine_command_dialog_close_clicked_cb (PtyxisMachineCommandDialog *self,
                                                GtkButton                  *button)
{
  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_command_dialog_open_now (PtyxisMachineCommandDialog *self)
{
  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  g_clear_handle_id (&self->open_timeout, g_source_remove);

  if (self->containers != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->containers, self);
      g_clear_object (&self->containers);
    }

  ptyxis_application_open_container (PTYXIS_APPLICATION_DEFAULT, self->container_id);
  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_command_dialog_containers_changed_cb (PtyxisMachineCommandDialog *self)
{
  g_autoptr(PtyxisIpcContainer) container = NULL;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  if ((container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, self->container_id)))
    ptyxis_machine_command_dialog_open_now (self);
}

static gboolean
ptyxis_machine_command_dialog_open_timeout_cb (gpointer data)
{
  PtyxisMachineCommandDialog *self = data;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  self->open_timeout = 0;
  ptyxis_machine_command_dialog_open_now (self);

  return G_SOURCE_REMOVE;
}

static void
ptyxis_machine_command_dialog_open_clicked_cb (PtyxisMachineCommandDialog *self,
                                               GtkButton                  *button)
{
  g_autoptr(PtyxisIpcContainer) container = NULL;

  g_assert (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));
  g_assert (self->container_id != NULL);

  if ((container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, self->container_id)))
    {
      ptyxis_machine_command_dialog_open_now (self);
      return;
    }

  /* A machine that was just created shows up once the agent notices its
   * record, which may take a moment.
   */
  gtk_widget_set_sensitive (GTK_WIDGET (self->open_button), FALSE);

  self->containers = ptyxis_application_list_containers (PTYXIS_APPLICATION_DEFAULT);
  g_signal_connect_object (self->containers,
                           "items-changed",
                           G_CALLBACK (ptyxis_machine_command_dialog_containers_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  self->open_timeout = g_timeout_add_seconds (OPEN_CONTAINER_TIMEOUT_SECONDS,
                                              ptyxis_machine_command_dialog_open_timeout_cb,
                                              self);
}

static void
ptyxis_machine_command_dialog_map (GtkWidget *widget)
{
  PtyxisMachineCommandDialog *self = (PtyxisMachineCommandDialog *)widget;

  GTK_WIDGET_CLASS (ptyxis_machine_command_dialog_parent_class)->map (widget);

  ptyxis_machine_command_dialog_start (self);
}

static void
ptyxis_machine_command_dialog_dispose (GObject *object)
{
  PtyxisMachineCommandDialog *self = (PtyxisMachineCommandDialog *)object;

  g_cancellable_cancel (self->cancellable);
  g_clear_handle_id (&self->open_timeout, g_source_remove);

  if (self->containers != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->containers, self);
      g_clear_object (&self->containers);
    }

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_MACHINE_COMMAND_DIALOG);

  G_OBJECT_CLASS (ptyxis_machine_command_dialog_parent_class)->dispose (object);
}

static void
ptyxis_machine_command_dialog_finalize (GObject *object)
{
  PtyxisMachineCommandDialog *self = (PtyxisMachineCommandDialog *)object;

  g_clear_pointer (&self->argv, g_strfreev);
  g_clear_pointer (&self->running_message, g_free);
  g_clear_pointer (&self->succeeded_message, g_free);
  g_clear_pointer (&self->failed_message, g_free);
  g_clear_pointer (&self->container_id, g_free);
  g_clear_object (&self->process);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (ptyxis_machine_command_dialog_parent_class)->finalize (object);
}

static void
ptyxis_machine_command_dialog_class_init (PtyxisMachineCommandDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_machine_command_dialog_dispose;
  object_class->finalize = ptyxis_machine_command_dialog_finalize;

  widget_class->map = ptyxis_machine_command_dialog_map;

  /**
   * PtyxisMachineCommandDialog::finished:
   * @exit_code: 0 when the command succeeded
   */
  signals[FINISHED] =
    g_signal_new ("finished",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 1, G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-machine-command-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, spinner);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, status_icon);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, status_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, terminal);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, close_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCommandDialog, open_button);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_command_dialog_close_attempt_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_command_dialog_close_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_command_dialog_open_clicked_cb);

  g_type_ensure (VTE_TYPE_TERMINAL);
}

static void
ptyxis_machine_command_dialog_init (PtyxisMachineCommandDialog *self)
{
  self->cancellable = g_cancellable_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  adw_dialog_set_can_close (ADW_DIALOG (self), FALSE);
}

/**
 * ptyxis_machine_command_dialog_new:
 * @title: the dialog title
 * @argv: the command to run on the host, such as nsl and its arguments
 *
 * Creates a dialog that runs @argv in a terminal when it is shown.
 */
PtyxisMachineCommandDialog *
ptyxis_machine_command_dialog_new (const char         *title,
                                   const char * const *argv)
{
  PtyxisMachineCommandDialog *self;

  g_return_val_if_fail (argv != NULL && argv[0] != NULL, NULL);

  self = g_object_new (PTYXIS_TYPE_MACHINE_COMMAND_DIALOG,
                       "title", title,
                       NULL);
  self->argv = g_strdupv ((char **)argv);

  return self;
}

void
ptyxis_machine_command_dialog_set_messages (PtyxisMachineCommandDialog *self,
                                            const char                 *running,
                                            const char                 *succeeded,
                                            const char                 *failed)
{
  g_return_if_fail (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  g_set_str (&self->running_message, running);
  g_set_str (&self->succeeded_message, succeeded);
  g_set_str (&self->failed_message, failed);
}

/**
 * ptyxis_machine_command_dialog_set_open_container:
 * @self: a #PtyxisMachineCommandDialog
 * @container_id: (nullable): a container to offer opening on success
 */
void
ptyxis_machine_command_dialog_set_open_container (PtyxisMachineCommandDialog *self,
                                                  const char                 *container_id)
{
  g_return_if_fail (PTYXIS_IS_MACHINE_COMMAND_DIALOG (self));

  g_set_str (&self->container_id, container_id);
}
