/* ptyxis-machine-create-dialog.c
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

#include <glib/gi18n.h>

#include "ptyxis-application.h"
#include "ptyxis-machine-create-dialog.h"
#include "ptyxis-machine-util.h"

typedef struct
{
  char     **selectors;
  char      *build;
  gboolean   cached;
} Image;

struct _PtyxisMachineCreateDialog
{
  AdwDialog       parent_instance;

  char           *nsl_path;
  char           *load_error;
  char          **existing;
  GPtrArray      *images;
  GCancellable   *cancellable;

  AdwEntryRow    *name_row;
  AdwComboRow    *image_row;
  GtkStringList  *image_labels;
  AdwSpinner     *images_spinner;
  AdwPreferencesGroup *message_group;
  GtkLabel       *message_label;
  GtkButton      *retry_button;
  AdwSwitchRow   *default_row;
  AdwSwitchRow   *isolated_row;
  AdwEntryRow    *user_row;
  GtkButton      *create_button;

  guint           name_edited : 1;
  guint           setting_name : 1;
};

enum {
  CREATE,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (PtyxisMachineCreateDialog, ptyxis_machine_create_dialog, ADW_TYPE_DIALOG)

static void
image_free (Image *image)
{
  g_clear_pointer (&image->selectors, g_strfreev);
  g_clear_pointer (&image->build, g_free);
  g_free (image);
}

static const Image *
selected_image (PtyxisMachineCreateDialog *self)
{
  guint position = adw_combo_row_get_selected (self->image_row);

  if (self->images == NULL || position == GTK_INVALID_LIST_POSITION || position >= self->images->len)
    return NULL;

  return g_ptr_array_index (self->images, position);
}

static void
show_message (PtyxisMachineCreateDialog *self,
              const char                *message,
              gboolean                   can_retry)
{
  gtk_widget_set_visible (GTK_WIDGET (self->message_group), message != NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->retry_button), can_retry);
  gtk_label_set_label (self->message_label, message ? message : "");
}

static void
ptyxis_machine_create_dialog_validate (PtyxisMachineCreateDialog *self)
{
  const char *name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));
  const char *user = gtk_editable_get_text (GTK_EDITABLE (self->user_row));
  g_autofree char *problem = NULL;
  gboolean name_ok;
  gboolean user_ok;

  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  name_ok = ptyxis_machine_is_valid_name (name) &&
            !(self->existing && g_strv_contains ((const char * const *)self->existing, name));
  user_ok = user[0] == 0 || ptyxis_machine_is_valid_user (user);

  if (name[0] != 0 && !ptyxis_machine_is_valid_name (name))
    problem = g_strdup (_("Machine names start with a lowercase letter and use lowercase letters, digits and hyphens, up to 24 characters."));
  else if (name[0] != 0 && !name_ok)
    problem = g_strdup_printf (_("A machine named “%s” already exists."), name);
  else if (!user_ok)
    problem = g_strdup (_("Usernames start with a lowercase letter or underscore and use lowercase letters, digits, underscores and hyphens, up to 32 characters."));

  if (name[0] != 0 && !name_ok)
    gtk_widget_add_css_class (GTK_WIDGET (self->name_row), "error");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self->name_row), "error");

  if (!user_ok)
    gtk_widget_add_css_class (GTK_WIDGET (self->user_row), "error");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self->user_row), "error");

  /* A problem with the form comes first, then a failure to load images */
  if (problem != NULL)
    show_message (self, problem, FALSE);
  else
    show_message (self, self->load_error, self->load_error != NULL);

  gtk_widget_set_sensitive (GTK_WIDGET (self->create_button),
                            name_ok && user_ok && selected_image (self) != NULL);
}

static void
ptyxis_machine_create_dialog_name_changed_cb (PtyxisMachineCreateDialog *self,
                                              GtkEditable               *editable)
{
  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  if (GTK_WIDGET (editable) == GTK_WIDGET (self->name_row) && !self->setting_name)
    self->name_edited = gtk_editable_get_text (editable)[0] != 0;

  ptyxis_machine_create_dialog_validate (self);
}

static void
ptyxis_machine_create_dialog_image_changed_cb (PtyxisMachineCreateDialog *self)
{
  const Image *image;
  g_autofree char *subtitle = NULL;

  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  if (!(image = selected_image (self)))
    {
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->image_row), NULL);
      ptyxis_machine_create_dialog_validate (self);
      return;
    }

  if (image->cached)
    /* translators: %s is an nsl image selector such as debian:trixie */
    subtitle = g_strdup_printf (_("%s · Downloaded"), image->selectors[0]);
  else
    /* translators: %s is an nsl image selector such as debian:trixie */
    subtitle = g_strdup_printf (_("%s · Downloads when created"), image->selectors[0]);

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->image_row), subtitle);

  /* Name the machine after its release until the user picks a name */
  if (!self->name_edited)
    {
      g_autofree char *name = ptyxis_machine_suggest_name (image->selectors[0],
                                                           (const char * const *)self->existing);

      self->setting_name = TRUE;
      gtk_editable_set_text (GTK_EDITABLE (self->name_row), name);
      self->setting_name = FALSE;
    }

  ptyxis_machine_create_dialog_validate (self);
}

static void
ptyxis_machine_create_dialog_list_images_cb (GObject      *object,
                                             GAsyncResult *result,
                                             gpointer      user_data)
{
  PtyxisIpcMachines *machines = (PtyxisIpcMachines *)object;
  g_autoptr(PtyxisMachineCreateDialog) self = user_data;
  g_autoptr(GVariant) images = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) entries = NULL;
  GVariantIter iter;
  GVariant *dict;

  g_assert (PTYXIS_IPC_IS_MACHINES (machines));
  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  gtk_widget_set_visible (GTK_WIDGET (self->images_spinner), FALSE);

  if (!ptyxis_ipc_machines_call_list_images_finish (machines, &images, result, &error))
    {
      g_autofree char *message = NULL;

      g_dbus_error_strip_remote_error (error);
      g_free (self->load_error);
      self->load_error = g_strdup_printf (_("Could not list machine images: %s"), error->message);
      ptyxis_machine_create_dialog_validate (self);
      return;
    }

  entries = g_ptr_array_new_with_free_func ((GDestroyNotify)image_free);
  g_variant_iter_init (&iter, images);

  while ((dict = g_variant_iter_next_value (&iter)))
    {
      g_autoptr(GVariantDict) d = g_variant_dict_new (dict);
      g_autofree const char **selectors = NULL;
      const char *build = NULL;
      gboolean cached = FALSE;
      Image *image;

      g_variant_unref (dict);

      if (!g_variant_dict_lookup (d, "selectors", "^a&s", &selectors) ||
          selectors == NULL || selectors[0] == NULL)
        continue;

      g_variant_dict_lookup (d, "build", "&s", &build);
      g_variant_dict_lookup (d, "cached", "b", &cached);

      image = g_new0 (Image, 1);
      image->selectors = g_strdupv ((char **)selectors);
      image->build = g_strdup (build);
      image->cached = cached;
      g_ptr_array_add (entries, image);
    }

  if (entries->len == 0)
    {
      g_set_str (&self->load_error, _("The nsl catalogue has no machine images for this computer."));
      ptyxis_machine_create_dialog_validate (self);
      return;
    }

  self->images = g_steal_pointer (&entries);
  g_clear_pointer (&self->load_error, g_free);

  for (guint i = 0; i < self->images->len; i++)
    {
      const Image *image = g_ptr_array_index (self->images, i);
      g_autofree char *label = ptyxis_machine_describe_selectors ((const char * const *)image->selectors);

      gtk_string_list_append (self->image_labels, label);
    }

  show_message (self, NULL, FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->image_row), TRUE);
  adw_combo_row_set_selected (self->image_row, 0);
  ptyxis_machine_create_dialog_image_changed_cb (self);
}

static void
ptyxis_machine_create_dialog_load_images (PtyxisMachineCreateDialog *self)
{
  PtyxisIpcMachines *machines;

  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  g_clear_pointer (&self->load_error, g_free);
  ptyxis_machine_create_dialog_validate (self);
  gtk_widget_set_visible (GTK_WIDGET (self->images_spinner), TRUE);

  if (!(machines = ptyxis_application_get_machines (PTYXIS_APPLICATION_DEFAULT)))
    {
      gtk_widget_set_visible (GTK_WIDGET (self->images_spinner), FALSE);
      g_set_str (&self->load_error, _("The terminal agent is not available."));
      ptyxis_machine_create_dialog_validate (self);
      return;
    }

  /* The catalogue is cached for an hour; nsl checks the registry otherwise */
  ptyxis_ipc_machines_call_list_images (machines,
                                        FALSE,
                                        self->cancellable,
                                        ptyxis_machine_create_dialog_list_images_cb,
                                        g_object_ref (self));
}

static void
ptyxis_machine_create_dialog_retry_cb (PtyxisMachineCreateDialog *self,
                                       GtkButton                 *button)
{
  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  ptyxis_machine_create_dialog_load_images (self);
}

static void
ptyxis_machine_create_dialog_cancel_cb (PtyxisMachineCreateDialog *self,
                                        GtkButton                 *button)
{
  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_create_dialog_create_cb (PtyxisMachineCreateDialog *self,
                                        GtkButton                 *button)
{
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;
  const Image *image;
  const char *name;
  const char *user;

  g_assert (PTYXIS_IS_MACHINE_CREATE_DIALOG (self));

  if (!gtk_widget_get_sensitive (GTK_WIDGET (self->create_button)) ||
      !(image = selected_image (self)))
    return;

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));
  user = gtk_editable_get_text (GTK_EDITABLE (self->user_row));

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder, self->nsl_path, "create", name, "--distro", image->selectors[0], NULL);

  if (adw_switch_row_get_active (self->isolated_row))
    g_strv_builder_add (builder, "--isolated");

  if (adw_switch_row_get_active (self->default_row))
    g_strv_builder_add (builder, "--default");

  if (user[0] != 0)
    g_strv_builder_add_many (builder, "--user", user, NULL);

  argv = g_strv_builder_end (builder);

  g_signal_emit (self, signals[CREATE], 0, argv, name);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_create_dialog_dispose (GObject *object)
{
  PtyxisMachineCreateDialog *self = (PtyxisMachineCreateDialog *)object;

  g_cancellable_cancel (self->cancellable);

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_MACHINE_CREATE_DIALOG);

  G_OBJECT_CLASS (ptyxis_machine_create_dialog_parent_class)->dispose (object);
}

static void
ptyxis_machine_create_dialog_finalize (GObject *object)
{
  PtyxisMachineCreateDialog *self = (PtyxisMachineCreateDialog *)object;

  g_clear_pointer (&self->nsl_path, g_free);
  g_clear_pointer (&self->load_error, g_free);
  g_clear_pointer (&self->existing, g_strfreev);
  g_clear_pointer (&self->images, g_ptr_array_unref);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (ptyxis_machine_create_dialog_parent_class)->finalize (object);
}

static void
ptyxis_machine_create_dialog_class_init (PtyxisMachineCreateDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_machine_create_dialog_dispose;
  object_class->finalize = ptyxis_machine_create_dialog_finalize;

  /**
   * PtyxisMachineCreateDialog::create:
   * @argv: the nsl command that creates the machine
   * @name: the name of the new machine
   */
  signals[CREATE] =
    g_signal_new ("create",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 2, G_TYPE_STRV, G_TYPE_STRING);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-machine-create-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, name_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, image_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, image_labels);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, images_spinner);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, message_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, message_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, retry_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, default_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, isolated_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, user_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineCreateDialog, create_button);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_create_dialog_cancel_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_create_dialog_create_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_create_dialog_image_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_create_dialog_name_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_create_dialog_retry_cb);
}

static void
ptyxis_machine_create_dialog_init (PtyxisMachineCreateDialog *self)
{
  self->cancellable = g_cancellable_new ();

  gtk_widget_init_template (GTK_WIDGET (self));
}

/**
 * ptyxis_machine_create_dialog_new:
 * @nsl_path: nsl on the host
 * @existing_names: (nullable): machines that already exist
 *
 * Creates a dialog that asks for a new machine's name, distribution and
 * options, then emits #PtyxisMachineCreateDialog::create with the nsl
 * command to run.
 */
PtyxisMachineCreateDialog *
ptyxis_machine_create_dialog_new (const char         *nsl_path,
                                  const char * const *existing_names)
{
  PtyxisMachineCreateDialog *self;

  g_return_val_if_fail (nsl_path != NULL, NULL);

  self = g_object_new (PTYXIS_TYPE_MACHINE_CREATE_DIALOG, NULL);
  self->nsl_path = g_strdup (nsl_path);
  self->existing = g_strdupv ((char **)existing_names);

  /* nsl makes the first machine the default regardless */
  adw_switch_row_set_active (self->default_row,
                             existing_names == NULL || existing_names[0] == NULL);

  ptyxis_machine_create_dialog_load_images (self);

  return self;
}
