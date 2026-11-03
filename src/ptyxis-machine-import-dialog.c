/* ptyxis-machine-import-dialog.c
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

#include "ptyxis-machine-import-dialog.h"
#include "ptyxis-machine-util.h"

struct _PtyxisMachineImportDialog
{
  AdwDialog       parent_instance;

  char           *nsl_path;
  char           *archive_path;
  char          **existing;

  AdwActionRow        *archive_row;
  AdwEntryRow         *name_row;
  AdwPreferencesGroup *message_group;
  GtkLabel            *message_label;
  AdwSwitchRow        *isolated_row;
  GtkButton           *import_button;
};

enum {
  IMPORT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (PtyxisMachineImportDialog, ptyxis_machine_import_dialog, ADW_TYPE_DIALOG)

static void
ptyxis_machine_import_dialog_validate (PtyxisMachineImportDialog *self)
{
  const char *name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));
  g_autofree char *problem = NULL;
  gboolean name_ok;

  g_assert (PTYXIS_IS_MACHINE_IMPORT_DIALOG (self));

  name_ok = ptyxis_machine_is_valid_name (name) &&
            !(self->existing && g_strv_contains ((const char * const *)self->existing, name));

  if (name[0] != 0 && !ptyxis_machine_is_valid_name (name))
    problem = g_strdup (_("Machine names start with a lowercase letter and use lowercase letters, digits and hyphens, up to 24 characters."));
  else if (name[0] != 0 && !name_ok)
    problem = g_strdup_printf (_("A machine named “%s” already exists."), name);

  if (name[0] != 0 && !name_ok)
    gtk_widget_add_css_class (GTK_WIDGET (self->name_row), "error");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self->name_row), "error");

  gtk_widget_set_visible (GTK_WIDGET (self->message_group), problem != NULL);
  gtk_label_set_label (self->message_label, problem ? problem : "");
  gtk_widget_set_sensitive (GTK_WIDGET (self->import_button), name_ok);
}

static void
ptyxis_machine_import_dialog_name_changed_cb (PtyxisMachineImportDialog *self,
                                              GtkEditable               *editable)
{
  g_assert (PTYXIS_IS_MACHINE_IMPORT_DIALOG (self));

  ptyxis_machine_import_dialog_validate (self);
}

static void
ptyxis_machine_import_dialog_cancel_cb (PtyxisMachineImportDialog *self,
                                        GtkButton                 *button)
{
  g_assert (PTYXIS_IS_MACHINE_IMPORT_DIALOG (self));

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_import_dialog_import_cb (PtyxisMachineImportDialog *self,
                                        GtkButton                 *button)
{
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;
  const char *name;

  g_assert (PTYXIS_IS_MACHINE_IMPORT_DIALOG (self));

  if (!gtk_widget_get_sensitive (GTK_WIDGET (self->import_button)))
    return;

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder, self->nsl_path, "import", name, self->archive_path, NULL);

  if (adw_switch_row_get_active (self->isolated_row))
    g_strv_builder_add (builder, "--isolated");

  argv = g_strv_builder_end (builder);

  g_signal_emit (self, signals[IMPORT], 0, argv, name);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ptyxis_machine_import_dialog_dispose (GObject *object)
{
  PtyxisMachineImportDialog *self = (PtyxisMachineImportDialog *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_MACHINE_IMPORT_DIALOG);

  G_OBJECT_CLASS (ptyxis_machine_import_dialog_parent_class)->dispose (object);
}

static void
ptyxis_machine_import_dialog_finalize (GObject *object)
{
  PtyxisMachineImportDialog *self = (PtyxisMachineImportDialog *)object;

  g_clear_pointer (&self->nsl_path, g_free);
  g_clear_pointer (&self->archive_path, g_free);
  g_clear_pointer (&self->existing, g_strfreev);

  G_OBJECT_CLASS (ptyxis_machine_import_dialog_parent_class)->finalize (object);
}

static void
ptyxis_machine_import_dialog_class_init (PtyxisMachineImportDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_machine_import_dialog_dispose;
  object_class->finalize = ptyxis_machine_import_dialog_finalize;

  /**
   * PtyxisMachineImportDialog::import:
   * @argv: the nsl command that imports the machine
   * @name: the name of the imported machine
   */
  signals[IMPORT] =
    g_signal_new ("import",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 2, G_TYPE_STRV, G_TYPE_STRING);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-machine-import-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, archive_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, import_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, isolated_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, message_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, message_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachineImportDialog, name_row);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_import_dialog_cancel_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_import_dialog_import_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_machine_import_dialog_name_changed_cb);
}

static void
ptyxis_machine_import_dialog_init (PtyxisMachineImportDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

/**
 * ptyxis_machine_import_dialog_new:
 * @nsl_path: nsl on the host
 * @existing_names: (nullable): machines that already exist
 * @archive_path: the archive to import, as a path on the host
 *
 * Creates a dialog that asks for the name of an imported machine and its
 * isolation, then emits #PtyxisMachineImportDialog::import with the nsl
 * command to run.
 */
PtyxisMachineImportDialog *
ptyxis_machine_import_dialog_new (const char         *nsl_path,
                                  const char * const *existing_names,
                                  const char         *archive_path)
{
  PtyxisMachineImportDialog *self;
  g_autofree char *name = NULL;

  g_return_val_if_fail (nsl_path != NULL, NULL);
  g_return_val_if_fail (archive_path != NULL, NULL);

  self = g_object_new (PTYXIS_TYPE_MACHINE_IMPORT_DIALOG, NULL);
  self->nsl_path = g_strdup (nsl_path);
  self->archive_path = g_strdup (archive_path);
  self->existing = g_strdupv ((char **)existing_names);

  adw_action_row_set_subtitle (self->archive_row, archive_path);

  /* Name the machine after the archive until the user picks a name */
  name = ptyxis_machine_suggest_import_name (archive_path, existing_names);
  gtk_editable_set_text (GTK_EDITABLE (self->name_row), name);

  ptyxis_machine_import_dialog_validate (self);

  return self;
}
