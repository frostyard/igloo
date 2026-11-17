/* ptyxis-machines-page.c
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
#include "ptyxis-machine-command-dialog.h"
#include "ptyxis-machine-create-dialog.h"
#include "ptyxis-machine-import-dialog.h"
#include "ptyxis-machine-util.h"
#include "ptyxis-machines-page.h"
#include "ptyxis-preferences-window.h"

#define NSL_INSTALL_URI "https://frostyard.github.io/nsl/getting-started/install/"
#define SAVE_DELAY_MSEC 600

/* nsl powers an isolated VM off about a minute after its last machine
 * stopped; this allows it longer, because a busy machine can hold it.
 */
#define RESIZE_WAIT_SECONDS 150

struct _PtyxisMachinesPage
{
  AdwPreferencesPage   parent_instance;

  PtyxisIpcMachines   *proxy;
  GCancellable        *cancellable;
  GVariant            *machines;
  GVariant            *vms;
  GHashTable          *pending_settings;
  guint                save_source;
  guint                busy;
  guint                resize_timeout;
  char                *resize_name;
  gint64               resize_gib;

  AdwPreferencesGroup *missing_group;
  AdwPreferencesGroup *machines_group;
  AdwPreferencesGroup *settings_group;
  AdwPreferencesGroup *host_group;
  GtkListBox          *machines_list_box;
  GtkLabel            *placeholder_label;
  AdwActionRow        *shared_disk_row;
  AdwSpinner          *spinner;
  AdwActionRow        *version_row;
  AdwSpinRow          *vm_memory;
  AdwSpinRow          *vm_cpus;
  AdwSwitchRow        *autostart;
  AdwSpinRow          *idle_timeout;
  AdwSpinRow          *isolated_memory;
  AdwSpinRow          *isolated_cpus;

  guint                loading_settings : 1;
  guint                resize_pending : 1;
};

static const struct {
  const char *key;
  gsize       offset;
} settings_map[] = {
  { "vm.memory", G_STRUCT_OFFSET (PtyxisMachinesPage, vm_memory) },
  { "vm.cpus", G_STRUCT_OFFSET (PtyxisMachinesPage, vm_cpus) },
  { "machines.autostart", G_STRUCT_OFFSET (PtyxisMachinesPage, autostart) },
  { "machines.idle_timeout", G_STRUCT_OFFSET (PtyxisMachinesPage, idle_timeout) },
  { "isolated.memory", G_STRUCT_OFFSET (PtyxisMachinesPage, isolated_memory) },
  { "isolated.cpus", G_STRUCT_OFFSET (PtyxisMachinesPage, isolated_cpus) },
};

G_DEFINE_FINAL_TYPE (PtyxisMachinesPage, ptyxis_machines_page, ADW_TYPE_PREFERENCES_PAGE)

static void ptyxis_machines_page_reload (PtyxisMachinesPage *self);
static void ptyxis_machines_page_check_pending_resize (PtyxisMachinesPage *self);

static GtkWidget *
setting_widget (PtyxisMachinesPage *self,
                guint               i)
{
  return *(GtkWidget **)G_STRUCT_MEMBER_P (self, settings_map[i].offset);
}

static void
ptyxis_machines_page_toast (PtyxisMachinesPage *self,
                            const char         *message)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (ADW_IS_PREFERENCES_WINDOW (root))
    adw_preferences_window_add_toast (ADW_PREFERENCES_WINDOW (root), adw_toast_new (message));
G_GNUC_END_IGNORE_DEPRECATIONS
}

static void
ptyxis_machines_page_show_error (PtyxisMachinesPage *self,
                                 const char         *heading,
                                 const char         *body)
{
  AdwDialog *alert;

  alert = adw_alert_dialog_new (heading, NULL);
  adw_alert_dialog_set_body (ADW_ALERT_DIALOG (alert), body ? body : "");
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert), "close", _("_Close"));
  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
ptyxis_machines_page_set_busy (PtyxisMachinesPage *self,
                               gboolean            busy)
{
  if (busy)
    self->busy++;
  else if (self->busy > 0)
    self->busy--;

  gtk_widget_set_visible (GTK_WIDGET (self->spinner), self->busy > 0);
}

static const char *
nsl_path (PtyxisMachinesPage *self)
{
  const char *path;

  if (self->proxy == NULL ||
      !(path = ptyxis_ipc_machines_get_nsl_path (self->proxy)) ||
      path[0] == 0)
    return NULL;

  return path;
}

static char **
dup_machine_names (PtyxisMachinesPage *self)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

  if (self->machines != NULL)
    {
      GVariantIter iter;
      GVariant *dict;

      g_variant_iter_init (&iter, self->machines);

      while ((dict = g_variant_iter_next_value (&iter)))
        {
          const char *name = NULL;

          if (g_variant_lookup (dict, "name", "&s", &name))
            g_strv_builder_add (builder, name);

          g_variant_unref (dict);
        }
    }

  return g_strv_builder_end (builder);
}

static char *
dup_machine_state (PtyxisMachinesPage *self,
                   const char         *name)
{
  GVariantIter iter;
  GVariant *dict;

  if (self->machines == NULL)
    return NULL;

  g_variant_iter_init (&iter, self->machines);

  while ((dict = g_variant_iter_next_value (&iter)))
    {
      const char *value = NULL;
      char *state = NULL;

      if (g_variant_lookup (dict, "name", "&s", &value) && g_strcmp0 (value, name) == 0)
        {
          if (g_variant_lookup (dict, "state", "&s", &value))
            state = g_strdup (value);

          g_variant_unref (dict);

          return state;
        }

      g_variant_unref (dict);
    }

  return NULL;
}

/* Finds a VM of the last list: the shared VM when @name is %NULL, else the
 * VM of the isolated machine @name. Matching the role keeps an isolated
 * machine that is called "shared" apart from the shared VM. Sizes of VMs
 * nsl does not list, or does not size, are 0.
 */
static gboolean
lookup_vm (PtyxisMachinesPage *self,
           const char         *name,
           char              **state,
           gint64             *data_disk_gib)
{
  const char *role = name ? "isolated" : "shared";
  GVariantIter iter;
  GVariant *dict;

  if (state != NULL)
    *state = NULL;

  if (data_disk_gib != NULL)
    *data_disk_gib = 0;

  if (self->vms == NULL)
    return FALSE;

  g_variant_iter_init (&iter, self->vms);

  while ((dict = g_variant_iter_next_value (&iter)))
    {
      const char *value = NULL;
      const char *vm_role = NULL;

      if (g_variant_lookup (dict, "role", "&s", &vm_role) &&
          g_strcmp0 (vm_role, role) == 0 &&
          (name == NULL ||
           (g_variant_lookup (dict, "name", "&s", &value) && g_strcmp0 (value, name) == 0)))
        {
          if (state != NULL && g_variant_lookup (dict, "state", "&s", &value))
            *state = g_strdup (value);

          if (data_disk_gib != NULL)
            g_variant_lookup (dict, "data_disk_gib", "x", data_disk_gib);

          g_variant_unref (dict);

          return TRUE;
        }

      g_variant_unref (dict);
    }

  return FALSE;
}

static const char *
state_label (const char *state)
{
  if (g_strcmp0 (state, "running") == 0)
    return _("Running");
  else if (g_strcmp0 (state, "stopped") == 0)
    return _("Stopped");
  else if (g_strcmp0 (state, "incomplete") == 0)
    return _("Incomplete — remove it and create it again");
  else if (g_strcmp0 (state, "removing") == 0)
    return _("Removing — remove it again to finish");

  return state;
}

static GtkWidget *
create_machine_row (PtyxisMachinesPage *self,
                    GVariant           *dict)
{
  g_autoptr(GMenu) menu = g_menu_new ();
  g_autoptr(GMenu) open_section = g_menu_new ();
  g_autoptr(GMenu) config_section = g_menu_new ();
  g_autoptr(GMenu) diagnose_section = g_menu_new ();
  g_autoptr(GMenu) remove_section = g_menu_new ();
  g_autoptr(GString) subtitle = g_string_new (NULL);
  g_autofree char *description = NULL;
  g_autofree char *detailed = NULL;
  const char *name = "";
  const char *state = "";
  const char *image = "";
  const char *tier = "";
  gint64 data_disk_gib = 0;
  gboolean is_default = FALSE;
  gboolean usable;
  gboolean running;
  GtkWidget *row;
  GtkWidget *icon;
  GtkWidget *button;

  g_variant_lookup (dict, "name", "&s", &name);
  g_variant_lookup (dict, "state", "&s", &state);
  g_variant_lookup (dict, "image", "&s", &image);
  g_variant_lookup (dict, "tier", "&s", &tier);
  g_variant_lookup (dict, "default", "b", &is_default);

  usable = g_strcmp0 (state, "incomplete") != 0 && g_strcmp0 (state, "removing") != 0;
  running = g_strcmp0 (state, "running") == 0;

  /* An isolated machine runs in a VM of its own, whose data disk nsl sizes */
  if (g_strcmp0 (tier, "isolated") == 0)
    lookup_vm (self, name, NULL, &data_disk_gib);

  description = ptyxis_machine_describe_image (image);
  g_string_append (subtitle, description);
  if (g_strcmp0 (tier, "isolated") == 0)
    g_string_append_printf (subtitle, " · %s", _("Isolated"));
  g_string_append_printf (subtitle, " · %s", state_label (state));

  if (data_disk_gib > 0)
    {
      g_string_append (subtitle, " · ");
      /* translators: %" G_GINT64_FORMAT " is a data disk size in GiB, such
       * as 128
       */
      g_string_append_printf (subtitle, _("%" G_GINT64_FORMAT " GiB disk"), data_disk_gib);
    }

  row = adw_action_row_new ();
  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle->str);

  icon = gtk_image_new_from_icon_name ("container-nsl-symbolic");
  gtk_accessible_update_property (GTK_ACCESSIBLE (icon),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, state_label (state),
                                  -1);
  if (running)
    gtk_widget_add_css_class (icon, "success");
  else if (!usable)
    gtk_widget_add_css_class (icon, "warning");
  else
    gtk_widget_add_css_class (icon, "dimmed");
  adw_action_row_add_prefix (ADW_ACTION_ROW (row), icon);

  if (is_default)
    {
      GtkWidget *label = gtk_label_new (_("Default"));

      gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
      gtk_widget_add_css_class (label, "caption");
      gtk_widget_add_css_class (label, "dimmed");
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), label);
    }

  if (usable)
    {
      button = gtk_button_new_from_icon_name ("utilities-terminal-symbolic");
      gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
      gtk_widget_set_tooltip_text (button, _("Open Terminal"));
      gtk_widget_add_css_class (button, "flat");
      gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "machines.open");
      gtk_actionable_set_action_target (GTK_ACTIONABLE (button), "s", name);
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), button);

      detailed = g_strdup_printf ("machines.open('%s')", name);
      g_menu_append (open_section, _("Open _Terminal"), detailed);
      g_clear_pointer (&detailed, g_free);

      detailed = g_strdup_printf (running ? "machines.stop('%s')" : "machines.start('%s')", name);
      g_menu_append (open_section, running ? _("_Stop") : _("St_art"), detailed);
      g_clear_pointer (&detailed, g_free);

      if (!is_default)
        {
          detailed = g_strdup_printf ("machines.default('%s')", name);
          g_menu_append (config_section, _("Make _Default"), detailed);
          g_clear_pointer (&detailed, g_free);
        }

      detailed = g_strdup_printf ("machines.profile('%s')", name);
      g_menu_append (config_section, _("Edit _Profile…"), detailed);
      g_clear_pointer (&detailed, g_free);

      detailed = g_strdup_printf ("machines.export('%s')", name);
      g_menu_append (config_section, _("_Export…"), detailed);
      g_clear_pointer (&detailed, g_free);

      if (ptyxis_machine_can_resize (tier, state, data_disk_gib))
        {
          detailed = g_strdup_printf ("machines.resize('%s')", name);
          g_menu_append (config_section, _("Grow _Disk…"), detailed);
          g_clear_pointer (&detailed, g_free);
        }
    }

  if (usable)
    {
      detailed = g_strdup_printf ("machines.logs('%s')", name);
      g_menu_append (diagnose_section, _("Show _Logs"), detailed);
      g_clear_pointer (&detailed, g_free);
    }

  if (ptyxis_machine_can_recover (tier, state))
    {
      detailed = g_strdup_printf ("machines.recover('%s')", name);
      g_menu_append (diagnose_section, _("Re_cover…"), detailed);
      g_clear_pointer (&detailed, g_free);
    }

  detailed = g_strdup_printf ("machines.remove('%s')", name);
  g_menu_append (remove_section, _("_Remove…"), detailed);

  g_menu_append_section (menu, NULL, G_MENU_MODEL (open_section));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (config_section));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (diagnose_section));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (remove_section));

  button = gtk_menu_button_new ();
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "view-more-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), G_MENU_MODEL (menu));
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (button, _("Machine Actions"));
  gtk_widget_add_css_class (button, "flat");
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), button);

  return row;
}

/* The Host group shows the size of the shared VM's data disk, and offers to
 * grow it while nsl can.
 */
static void
ptyxis_machines_page_update_shared_disk (PtyxisMachinesPage *self)
{
  g_autofree char *subtitle = NULL;
  gint64 data_disk_gib = 0;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  lookup_vm (self, NULL, NULL, &data_disk_gib);

  if (data_disk_gib >= PTYXIS_MACHINE_MAX_DISK_GIB)
    subtitle = g_strdup (_("The data disk has the largest size nsl supports"));
  else if (data_disk_gib > 0)
    /* translators: %" G_GINT64_FORMAT " is a data disk size in GiB, such as
     * 128
     */
    subtitle = g_strdup_printf (_("The shared VM’s data disk has %" G_GINT64_FORMAT " GiB"), data_disk_gib);
  else
    subtitle = g_strdup (_("nsl did not report the size of the data disk"));

  adw_action_row_set_subtitle (self->shared_disk_row, subtitle);
  gtk_widget_set_sensitive (GTK_WIDGET (self->shared_disk_row),
                            ptyxis_machine_disk_can_grow (data_disk_gib));
}

static void
ptyxis_machines_page_set_machines (PtyxisMachinesPage *self,
                                   GVariant           *machines,
                                   GVariant           *vms)
{
  GVariantIter iter;
  GVariant *dict;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  /* Keep rows, and any open menu, when nothing changed */
  if (self->machines != NULL && machines != NULL &&
      self->vms != NULL && vms != NULL &&
      g_variant_equal (self->machines, machines) &&
      g_variant_equal (self->vms, vms))
    return;

  g_clear_pointer (&self->machines, g_variant_unref);
  g_clear_pointer (&self->vms, g_variant_unref);

  if (machines != NULL)
    self->machines = g_variant_ref_sink (machines);

  if (vms != NULL)
    self->vms = g_variant_ref_sink (vms);

  /* Remove only rows: the placeholder is a child of the list too, and
   * gtk_list_box_remove_all() would remove it as well.
   */
  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->machines_list_box)); child != NULL;)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);

      if (GTK_IS_LIST_BOX_ROW (child))
        gtk_list_box_remove (self->machines_list_box, child);

      child = next;
    }

  ptyxis_machines_page_update_shared_disk (self);

  if (self->machines == NULL)
    return;

  g_variant_iter_init (&iter, self->machines);

  while ((dict = g_variant_iter_next_value (&iter)))
    {
      gtk_list_box_append (self->machines_list_box, create_machine_row (self, dict));
      g_variant_unref (dict);
    }
}

static void
ptyxis_machines_page_list_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  PtyxisIpcMachines *proxy = (PtyxisIpcMachines *)object;
  g_autoptr(PtyxisMachinesPage) self = user_data;
  g_autoptr(GVariant) machines = NULL;
  g_autoptr(GVariant) vms = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IPC_IS_MACHINES (proxy));
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!ptyxis_ipc_machines_call_list_finish (proxy, &machines, &vms, result, &error))
    {
      g_autofree char *message = NULL;

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      ptyxis_machines_page_set_busy (self, FALSE);
      g_dbus_error_strip_remote_error (error);
      message = g_strdup_printf (_("Could not list machines: %s"), error->message);
      gtk_label_set_label (self->placeholder_label, message);

      /* Rows would show stale states; the placeholder shows the error */
      ptyxis_machines_page_set_machines (self, NULL, NULL);

      return;
    }

  ptyxis_machines_page_set_busy (self, FALSE);

  gtk_label_set_label (self->placeholder_label, _("No Machines"));
  ptyxis_machines_page_set_machines (self, machines, vms);

  /* A growth that waits for the VM to stop finishes with this list */
  ptyxis_machines_page_check_pending_resize (self);
}

static void
ptyxis_machines_page_reload_machines (PtyxisMachinesPage *self)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->proxy == NULL || nsl_path (self) == NULL)
    return;

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_ipc_machines_call_list (self->proxy,
                                 self->cancellable,
                                 ptyxis_machines_page_list_cb,
                                 g_object_ref (self));
}

static void
ptyxis_machines_page_set_settings_sensitive (PtyxisMachinesPage *self,
                                             gboolean            sensitive)
{
  for (guint i = 0; i < G_N_ELEMENTS (settings_map); i++)
    gtk_widget_set_sensitive (setting_widget (self, i), sensitive);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "machines.reset-settings", sensitive);
}

static void
ptyxis_machines_page_get_config_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  PtyxisIpcMachines *proxy = (PtyxisIpcMachines *)object;
  g_autoptr(PtyxisMachinesPage) self = user_data;
  g_autoptr(GVariant) settings = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  g_autofree char *description = NULL;
  GVariantIter iter;
  GVariant *dict;

  g_assert (PTYXIS_IPC_IS_MACHINES (proxy));
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!ptyxis_ipc_machines_call_get_config_finish (proxy, &path, &settings, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_dbus_error_strip_remote_error (error);
          description = g_strdup_printf (_("nsl could not read its configuration: %s"), error->message);
          adw_preferences_group_set_description (self->settings_group, description);

          /* The rows would show placeholders as if they were settings */
          ptyxis_machines_page_set_settings_sensitive (self, FALSE);
        }

      return;
    }

  ptyxis_machines_page_set_settings_sensitive (self, TRUE);
  self->loading_settings = TRUE;

  g_variant_iter_init (&iter, settings);

  while ((dict = g_variant_iter_next_value (&iter)))
    {
      const char *key = NULL;
      const char *value = NULL;

      if (g_variant_lookup (dict, "key", "&s", &key) &&
          g_variant_lookup (dict, "value", "&s", &value))
        {
          for (guint i = 0; i < G_N_ELEMENTS (settings_map); i++)
            {
              GtkWidget *widget;

              if (g_strcmp0 (settings_map[i].key, key) != 0 ||
                  g_hash_table_contains (self->pending_settings, key))
                continue;

              widget = setting_widget (self, i);

              if (ADW_IS_SPIN_ROW (widget))
                {
                  GtkAdjustment *adjustment = adw_spin_row_get_adjustment (ADW_SPIN_ROW (widget));
                  gint64 lower, upper;

                  /* nsl reports the range it accepts for each number */
                  if (g_variant_lookup (dict, "min", "x", &lower) &&
                      g_variant_lookup (dict, "max", "x", &upper) &&
                      lower <= upper)
                    {
                      gtk_adjustment_set_lower (adjustment, lower);
                      gtk_adjustment_set_upper (adjustment, upper);
                    }

                  adw_spin_row_set_value (ADW_SPIN_ROW (widget), g_ascii_strtod (value, NULL));
                }
              else if (ADW_IS_SWITCH_ROW (widget))
                adw_switch_row_set_active (ADW_SWITCH_ROW (widget), g_strcmp0 (value, "true") == 0);
            }
        }

      g_variant_unref (dict);
    }

  self->loading_settings = FALSE;

  /* translators: %s is the path to nsl.conf */
  description = g_strdup_printf (_("Saved in %s. Memory and processor changes apply the next time the VM starts."), path);
  adw_preferences_group_set_description (self->settings_group, description);
}

static void
ptyxis_machines_page_reload_config (PtyxisMachinesPage *self)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->proxy == NULL || nsl_path (self) == NULL)
    return;

  ptyxis_ipc_machines_call_get_config (self->proxy,
                                       self->cancellable,
                                       ptyxis_machines_page_get_config_cb,
                                       g_object_ref (self));
}

static void
ptyxis_machines_page_update_availability (PtyxisMachinesPage *self)
{
  const char *path = nsl_path (self);
  gboolean available = path != NULL;
  g_autofree char *version = NULL;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  gtk_widget_set_visible (GTK_WIDGET (self->missing_group), !available);
  gtk_widget_set_visible (GTK_WIDGET (self->machines_group), available);
  gtk_widget_set_visible (GTK_WIDGET (self->settings_group), available);
  gtk_widget_set_visible (GTK_WIDGET (self->host_group), available);

  if (available)
    {
      const char *nsl_version = ptyxis_ipc_machines_get_version (self->proxy);

      version = g_strdup_printf ("%s — %s", nsl_version && nsl_version[0] ? nsl_version : _("unknown version"), path);
      adw_action_row_set_subtitle (self->version_row, version);
    }
}

static void
ptyxis_machines_page_reload (PtyxisMachinesPage *self)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  ptyxis_machines_page_update_availability (self);
  ptyxis_machines_page_reload_machines (self);
  ptyxis_machines_page_reload_config (self);
}

static void
ptyxis_machines_page_changed_cb (PtyxisMachinesPage *self,
                                 PtyxisIpcMachines  *proxy)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  ptyxis_machines_page_update_availability (self);
  ptyxis_machines_page_reload_machines (self);
}

static void
ptyxis_machines_page_connect (PtyxisMachinesPage *self)
{
  PtyxisIpcMachines *proxy;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  proxy = ptyxis_application_get_machines (PTYXIS_APPLICATION_DEFAULT);

  if (proxy == self->proxy)
    return;

  if (self->proxy != NULL)
    g_signal_handlers_disconnect_by_data (self->proxy, self);

  g_set_object (&self->proxy, proxy);

  if (self->proxy != NULL)
    {
      g_signal_connect_object (self->proxy,
                               "changed",
                               G_CALLBACK (ptyxis_machines_page_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->proxy,
                               "notify::nsl-path",
                               G_CALLBACK (ptyxis_machines_page_reload),
                               self,
                               G_CONNECT_SWAPPED);
    }
}

static void
ptyxis_machines_page_map (GtkWidget *widget)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;

  GTK_WIDGET_CLASS (ptyxis_machines_page_parent_class)->map (widget);

  ptyxis_machines_page_connect (self);
  ptyxis_machines_page_reload (self);
}

typedef struct
{
  PtyxisMachinesPage *self;
  char *success;
  char *failure;
} RunState;

static void
run_state_free (RunState *state)
{
  g_clear_object (&state->self);
  g_clear_pointer (&state->success, g_free);
  g_clear_pointer (&state->failure, g_free);
  g_free (state);
}

static void
ptyxis_machines_page_run_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  PtyxisIpcMachines *proxy = (PtyxisIpcMachines *)object;
  RunState *state = user_data;
  PtyxisMachinesPage *self = state->self;
  g_autoptr(GError) error = NULL;
  g_autofree char *output = NULL;
  int exit_code = 0;

  g_assert (PTYXIS_IPC_IS_MACHINES (proxy));
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!ptyxis_ipc_machines_call_run_finish (proxy, &exit_code, &output, result, &error) &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      run_state_free (state);
      return;
    }

  ptyxis_machines_page_set_busy (self, FALSE);

  if (error != NULL)
    {
      g_dbus_error_strip_remote_error (error);
      ptyxis_machines_page_show_error (self, state->failure, error->message);
    }
  else if (exit_code != 0)
    {
      ptyxis_machines_page_show_error (self, state->failure, g_strstrip (output));
    }
  else if (state->success != NULL)
    {
      ptyxis_machines_page_toast (self, state->success);
    }

  /* The agent reloads machines after every command and emits Changed */
  run_state_free (state);
}

static void
ptyxis_machines_page_run (PtyxisMachinesPage *self,
                          const char * const *args,
                          const char         *success,
                          const char         *failure)
{
  RunState *state;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->proxy == NULL)
    return;

  state = g_new0 (RunState, 1);
  state->self = g_object_ref (self);
  state->success = g_strdup (success);
  state->failure = g_strdup (failure);

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_ipc_machines_call_run (self->proxy,
                                args,
                                self->cancellable,
                                ptyxis_machines_page_run_cb,
                                state);
}

static void
ptyxis_machines_page_command_finished_cb (PtyxisMachinesPage         *self,
                                          int                         exit_code,
                                          PtyxisMachineCommandDialog *dialog)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  ptyxis_machines_page_reload (self);
}

static PtyxisMachineCommandDialog *
ptyxis_machines_page_present_command (PtyxisMachinesPage *self,
                                      const char         *title,
                                      const char * const *argv)
{
  PtyxisMachineCommandDialog *dialog;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  dialog = ptyxis_machine_command_dialog_new (title, argv);
  g_signal_connect_object (dialog,
                           "finished",
                           G_CALLBACK (ptyxis_machines_page_command_finished_cb),
                           self,
                           G_CONNECT_SWAPPED);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));

  return dialog;
}

static void
ptyxis_machines_page_create_cb (PtyxisMachinesPage        *self,
                                const char * const        *argv,
                                const char                *name,
                                PtyxisMachineCreateDialog *create)
{
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  g_autofree char *container_id = NULL;
  PtyxisMachineCommandDialog *dialog;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  /* translators: %s is a machine name */
  title = g_strdup_printf (_("Creating %s"), name);
  running = g_strdup_printf (_("Creating %s. Images are verified and downloaded when they are not cached, which can take a few minutes."), name);
  succeeded = g_strdup_printf (_("%s is ready"), name);
  failed = g_strdup_printf (_("Could not create %s"), name);
  container_id = ptyxis_machine_dup_container_id (name);

  dialog = ptyxis_machines_page_present_command (self, title, argv);
  ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
  ptyxis_machine_command_dialog_set_open_container (dialog, container_id);
}

static void
ptyxis_machines_page_create_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  g_auto(GStrv) names = NULL;
  PtyxisMachineCreateDialog *dialog;
  const char *path;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!(path = nsl_path (self)))
    return;

  names = dup_machine_names (self);
  dialog = ptyxis_machine_create_dialog_new (path, (const char * const *)names);
  g_signal_connect_object (dialog,
                           "create",
                           G_CALLBACK (ptyxis_machines_page_create_cb),
                           self,
                           G_CONNECT_SWAPPED);
  adw_dialog_present (ADW_DIALOG (dialog), widget);
}

static void
ptyxis_machines_page_open_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  g_autofree char *container_id = ptyxis_machine_dup_container_id (g_variant_get_string (param, NULL));

  ptyxis_application_open_container (PTYXIS_APPLICATION_DEFAULT, container_id);
}

typedef struct
{
  PtyxisMachinesPage *self;
  char               *name;
  guint               stop : 1;
} ExportState;

static void
export_state_free (ExportState *state)
{
  g_clear_object (&state->self);
  g_clear_pointer (&state->name, g_free);
  g_free (state);
}

static void
ptyxis_machines_page_export_path_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  ExportState *state = user_data;
  PtyxisMachinesPage *self = state->self;
  g_autoptr(GError) error = NULL;
  g_autofree char *host_path = NULL;
  g_autofree char *heading = NULL;
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  PtyxisMachineCommandDialog *dialog;
  const char *path;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  host_path = ptyxis_machine_host_path_finish (result, &error);

  if (host_path == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          /* translators: %s is a machine name */
          heading = g_strdup_printf (_("Could not export %s"), state->name);
          ptyxis_machines_page_show_error (self, heading, error->message);
        }

      export_state_free (state);
      return;
    }

  if ((path = nsl_path (self)) == NULL)
    {
      export_state_free (state);
      return;
    }

  {
    /* Stop the machine only when the user agreed to; nsl refuses to export
     * a machine that runs, so a stale state never stops one unasked.
     */
    const char * const stop_argv[] = {
      "/bin/sh", "-c", "\"$0\" stop \"$1\" && exec \"$0\" export \"$1\" \"$2\"",
      path, state->name, host_path, NULL
    };
    const char * const export_argv[] = { path, "export", state->name, host_path, NULL };

    /* translators: %s is a machine name */
    title = g_strdup_printf (_("Exporting %s"), state->name);
    if (state->stop)
      running = g_strdup_printf (_("Stopping and exporting %s. A large machine can take several minutes."), state->name);
    else
      running = g_strdup_printf (_("Exporting %s. A large machine can take several minutes."), state->name);
    succeeded = g_strdup_printf (_("%s was exported"), state->name);
    failed = g_strdup_printf (_("Could not export %s"), state->name);

    dialog = ptyxis_machines_page_present_command (self, title, state->stop ? stop_argv : export_argv);
    ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
  }

  export_state_free (state);
}

static void
ptyxis_machines_page_export_save_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  ExportState *state = user_data;
  PtyxisMachinesPage *self = state->self;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *heading = NULL;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (object), result, &error);

  if (file == NULL)
    {
      if (g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) ||
          g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) ||
          g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          export_state_free (state);
          return;
        }

      /* translators: %s is a machine name */
      heading = g_strdup_printf (_("Could not export %s"), state->name);
      ptyxis_machines_page_show_error (self, heading, error->message);

      export_state_free (state);
      return;
    }

  /* nsl stages the archive beside the file and publishes it with a hard link */
  if (g_file_query_exists (file, NULL))
    {
      ptyxis_machines_page_show_error (self,
                                       _("Choose a New File"),
                                       _("nsl never replaces a file. Export to a name that does not exist yet."));

      export_state_free (state);
      return;
    }

  ptyxis_machine_host_path_async (file,
                                  self->cancellable,
                                  ptyxis_machines_page_export_path_cb,
                                  state);
}

static void
ptyxis_machines_page_export_choose_file (PtyxisMachinesPage *self,
                                         const char         *name,
                                         gboolean            stop)
{
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GDateTime) now = NULL;
  g_autofree char *title = NULL;
  g_autofree char *file_name = NULL;
  ExportState *state;
  GtkRoot *root;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  state = g_new0 (ExportState, 1);
  state->self = g_object_ref (self);
  state->name = g_strdup (name);
  state->stop = !!stop;

  now = g_date_time_new_now_local ();
  file_name = ptyxis_machine_export_file_name (name, now);

  /* translators: %s is a machine name */
  title = g_strdup_printf (_("Export %s"), name);

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, title);
  gtk_file_dialog_set_accept_label (dialog, _("_Export"));
  gtk_file_dialog_set_initial_name (dialog, file_name);
  gtk_file_dialog_set_modal (dialog, TRUE);

  root = gtk_widget_get_root (GTK_WIDGET (self));

  gtk_file_dialog_save (dialog,
                        GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        self->cancellable,
                        ptyxis_machines_page_export_save_cb,
                        state);
}

static void
ptyxis_machines_page_export_response_cb (AdwAlertDialog *alert,
                                         const char     *response,
                                         gpointer        user_data)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (alert), "MACHINE_NAME");

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (g_strcmp0 (response, "export") != 0 || name == NULL)
    return;

  ptyxis_machines_page_export_choose_file (self, name, TRUE);
}

static void
ptyxis_machines_page_export_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = g_variant_get_string (param, NULL);
  g_autofree char *state = dup_machine_state (self, name);
  g_autofree char *heading = NULL;
  AdwDialog *alert;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  /* nsl exports only a stopped machine */
  if (g_strcmp0 (state, "running") != 0)
    {
      ptyxis_machines_page_export_choose_file (self, name, FALSE);
      return;
    }

  /* translators: %s is a machine name */
  heading = g_strdup_printf (_("Export %s?"), name);

  alert = adw_alert_dialog_new (heading, NULL);
  /* translators: %s is a machine name */
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (alert),
                                _("%s must stop before it can be exported. Programs running in it will close."),
                                name);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert),
                                  "cancel", _("_Cancel"),
                                  "export", _("_Stop and Export"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert), "export", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");
  g_object_set_data_full (G_OBJECT (alert), "MACHINE_NAME", g_strdup (name), g_free);

  g_signal_connect_object (alert,
                           "response",
                           G_CALLBACK (ptyxis_machines_page_export_response_cb),
                           self,
                           0);

  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
ptyxis_machines_page_import_cb (PtyxisMachinesPage        *self,
                                const char * const        *argv,
                                const char                *name,
                                PtyxisMachineImportDialog *import)
{
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  g_autofree char *container_id = NULL;
  PtyxisMachineCommandDialog *dialog;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  /* translators: %s is a machine name */
  title = g_strdup_printf (_("Importing %s"), name);
  running = g_strdup_printf (_("Verifying and importing %s. A large archive can take several minutes."), name);
  succeeded = g_strdup_printf (_("%s is ready"), name);
  failed = g_strdup_printf (_("Could not import %s"), name);
  container_id = ptyxis_machine_dup_container_id (name);

  dialog = ptyxis_machines_page_present_command (self, title, argv);
  ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
  ptyxis_machine_command_dialog_set_open_container (dialog, container_id);
}

static void
ptyxis_machines_page_import_path_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  g_autoptr(PtyxisMachinesPage) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *host_path = NULL;
  g_auto(GStrv) names = NULL;
  PtyxisMachineImportDialog *dialog;
  const char *path;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  host_path = ptyxis_machine_host_path_finish (result, &error);

  if (host_path == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        ptyxis_machines_page_show_error (self, _("Could not import a machine"), error->message);

      return;
    }

  if ((path = nsl_path (self)) == NULL)
    return;

  names = dup_machine_names (self);
  dialog = ptyxis_machine_import_dialog_new (path, (const char * const *)names, host_path);
  g_signal_connect_object (dialog,
                           "import",
                           G_CALLBACK (ptyxis_machines_page_import_cb),
                           self,
                           G_CONNECT_SWAPPED);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
ptyxis_machines_page_import_open_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  g_autoptr(PtyxisMachinesPage) self = user_data;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (object), result, &error);

  if (file == NULL)
    {
      if (g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) ||
          g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) ||
          g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      ptyxis_machines_page_show_error (self, _("Could not import a machine"), error->message);
      return;
    }

  ptyxis_machine_host_path_async (file,
                                  self->cancellable,
                                  ptyxis_machines_page_import_path_cb,
                                  g_object_ref (self));
}

static void
ptyxis_machines_page_import_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_autoptr(GtkFileFilter) archives = gtk_file_filter_new ();
  g_autoptr(GtkFileFilter) all_files = gtk_file_filter_new ();
  GtkRoot *root;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  gtk_file_filter_set_name (archives, _("Machine Archives"));
  gtk_file_filter_add_suffix (archives, "tar");
  g_list_store_append (filters, archives);

  gtk_file_filter_set_name (all_files, _("All Files"));
  gtk_file_filter_add_pattern (all_files, "*");
  g_list_store_append (filters, all_files);

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Import Machine"));
  gtk_file_dialog_set_accept_label (dialog, _("_Choose"));
  gtk_file_dialog_set_modal (dialog, TRUE);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_set_default_filter (dialog, archives);

  root = gtk_widget_get_root (GTK_WIDGET (self));

  gtk_file_dialog_open (dialog,
                        GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        self->cancellable,
                        ptyxis_machines_page_import_open_cb,
                        g_object_ref (self));
}

static void
ptyxis_machines_page_start_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = g_variant_get_string (param, NULL);
  const char *args[] = { "start", name, NULL };
  g_autofree char *success = g_strdup_printf (_("%s is running"), name);
  g_autofree char *failure = g_strdup_printf (_("Could not start %s"), name);

  ptyxis_machines_page_run (self, args, success, failure);
}

static void
ptyxis_machines_page_stop_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = g_variant_get_string (param, NULL);
  const char *args[] = { "stop", name, NULL };
  g_autofree char *success = g_strdup_printf (_("%s stopped"), name);
  g_autofree char *failure = g_strdup_printf (_("Could not stop %s"), name);

  ptyxis_machines_page_run (self, args, success, failure);
}

static void
ptyxis_machines_page_default_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = g_variant_get_string (param, NULL);
  const char *args[] = { "default", name, NULL };
  g_autofree char *success = g_strdup_printf (_("%s is the default machine"), name);
  g_autofree char *failure = g_strdup_printf (_("Could not make %s the default"), name);

  ptyxis_machines_page_run (self, args, success, failure);
}

static void
ptyxis_machines_page_profile_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  const char *name = g_variant_get_string (param, NULL);
  g_autofree char *container_id = ptyxis_machine_dup_container_id (name);
  g_autoptr(PtyxisProfile) profile = NULL;
  g_autoptr(GListModel) profiles = NULL;
  GtkRoot *root;
  guint n_items;

  /* Reuse the profile that already opens this machine */
  profiles = ptyxis_application_list_profiles (app);
  n_items = g_list_model_get_n_items (profiles);

  for (guint i = 0; i < n_items && profile == NULL; i++)
    {
      g_autoptr(PtyxisProfile) item = g_list_model_get_item (profiles, i);
      g_autofree char *default_container = ptyxis_profile_dup_default_container (item);

      if (g_strcmp0 (default_container, container_id) == 0)
        profile = g_steal_pointer (&item);
    }

  if (profile == NULL)
    {
      profile = ptyxis_profile_new (NULL);
      ptyxis_profile_set_label (profile, name);
      ptyxis_profile_set_default_container (profile, container_id);
      ptyxis_application_add_profile (app, profile);
    }

  root = gtk_widget_get_root (widget);

  if (PTYXIS_IS_PREFERENCES_WINDOW (root))
    ptyxis_preferences_window_edit_profile (PTYXIS_PREFERENCES_WINDOW (root), profile);
}

static void
ptyxis_machines_page_remove_response_cb (AdwAlertDialog *alert,
                                         const char     *response,
                                         gpointer        user_data)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (alert), "MACHINE_NAME");
  g_autofree char *state = NULL;
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  g_auto(GStrv) argv = NULL;
  PtyxisMachineCommandDialog *dialog;
  const char *path;

  if (g_strcmp0 (response, "remove") != 0 || !(path = nsl_path (self)))
    return;

  state = dup_machine_state (self, name);
  argv = ptyxis_machine_remove_argv (path, name, state);

  /* translators: %s is a machine name */
  title = g_strdup_printf (_("Removing %s"), name);

  if (ptyxis_machine_remove_needs_stop (state))
    /* translators: %s is a machine name */
    running = g_strdup_printf (_("Stopping and removing %s…"), name);
  else
    /* translators: %s is a machine name */
    running = g_strdup_printf (_("Removing %s…"), name);

  succeeded = g_strdup_printf (_("%s was removed"), name);
  failed = g_strdup_printf (_("Could not remove %s"), name);

  dialog = ptyxis_machines_page_present_command (self, title, (const char * const *)argv);
  ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
}

static void
ptyxis_machines_page_remove_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = g_variant_get_string (param, NULL);
  g_autofree char *heading = NULL;
  AdwDialog *alert;

  /* translators: %s is a machine name */
  heading = g_strdup_printf (_("Remove %s?"), name);

  alert = adw_alert_dialog_new (heading, NULL);
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (alert),
                                _("%s and everything stored only inside it, including its home directory and installed packages, will be permanently deleted. Files on this computer under /mnt/host are not affected.\n\nTo keep a backup, cancel and choose Export… in its menu first."),
                                name);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert),
                                  "cancel", _("_Cancel"),
                                  "remove", _("_Remove"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert), "remove", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");
  g_object_set_data_full (G_OBJECT (alert), "MACHINE_NAME", g_strdup (name), g_free);

  g_signal_connect_object (alert,
                           "response",
                           G_CALLBACK (ptyxis_machines_page_remove_response_cb),
                           self,
                           0);

  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
ptyxis_machines_page_refresh_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  ptyxis_machines_page_reload (PTYXIS_MACHINES_PAGE (widget));
}

static void
ptyxis_machines_page_rescan_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(PtyxisMachinesPage) self = user_data;

  g_autoptr(GError) error = NULL;

  if (!ptyxis_ipc_machines_call_refresh_finish (PTYXIS_IPC_MACHINES (object), result, &error) &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  ptyxis_machines_page_set_busy (self, FALSE);
  ptyxis_machines_page_reload (self);
}

static void
ptyxis_machines_page_rescan_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);

  ptyxis_machines_page_connect (self);

  if (self->proxy == NULL)
    return;

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_ipc_machines_call_refresh (self->proxy,
                                    self->cancellable,
                                    ptyxis_machines_page_rescan_cb,
                                    g_object_ref (self));
}

static void
ptyxis_machines_page_doctor_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);
  PtyxisMachineCommandDialog *dialog;
  const char *path;

  if (!(path = nsl_path (self)))
    return;

  {
    const char * const argv[] = { path, "doctor", NULL };

    dialog = ptyxis_machines_page_present_command (self, _("Check This Computer"), argv);
    ptyxis_machine_command_dialog_set_messages (dialog,
                                                _("Checking what nsl needs on this computer…"),
                                                _("This computer has everything nsl needs"),
                                                _("Some checks failed"));
  }
}

static void
ptyxis_machines_page_logs_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);
  const char *name = param ? g_variant_get_string (param, NULL) : NULL;
  const char *path;
  g_auto(GStrv) argv = NULL;
  g_autofree char *title = NULL;
  PtyxisMachineCommandDialog *dialog;

  if (!(path = nsl_path (self)))
    return;

  argv = ptyxis_machine_logs_argv (path, name);
  if (name != NULL)
    /* translators: %s is a machine name */
    title = g_strdup_printf (_("Logs of %s"), name);
  else
    title = g_strdup (_("Logs"));
  dialog = ptyxis_machines_page_present_command (self, title, (const char * const *)argv);
  ptyxis_machine_command_dialog_set_messages (dialog,
                                              _("Reading recent logs…"),
                                              _("These are the most recent logs"),
                                              _("Could not read the logs"));
}

static void
ptyxis_machines_page_recover_response_cb (AdwAlertDialog *alert,
                                          const char     *response,
                                          gpointer        user_data)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (alert), "MACHINE_NAME");
  const char *path;
  g_auto(GStrv) argv = NULL;
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  PtyxisMachineCommandDialog *dialog;

  if (g_strcmp0 (response, "recover") != 0 || !(path = nsl_path (self)))
    return;

  argv = ptyxis_machine_recover_argv (path, name);
  if (name != NULL)
    {
      /* translators: %s is a machine name */
      title = g_strdup_printf (_("Recovering %s"), name);
      /* translators: %s is a machine name */
      running = g_strdup_printf (_("Restarting the VM of %s from a fresh root. This can take a few minutes."), name);
      /* translators: %s is a machine name */
      succeeded = g_strdup_printf (_("%s was recovered. Its files are unchanged."), name);
      /* translators: %s is a machine name */
      failed = g_strdup_printf (_("Could not recover %s"), name);
    }
  else
    {
      title = g_strdup (_("Recovering the Shared VM"));
      running = g_strdup (_("Restarting the shared VM from a fresh root. This can take a few minutes."));
      succeeded = g_strdup (_("The shared VM was recovered. Machines are unchanged."));
      failed = g_strdup (_("Could not recover the shared VM"));
    }

  dialog = ptyxis_machines_page_present_command (self, title, (const char * const *)argv);
  ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
}

static void
ptyxis_machines_page_recover_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);
  const char *name = param ? g_variant_get_string (param, NULL) : NULL;
  g_autofree char *heading = NULL;
  AdwDialog *alert;

  if (nsl_path (self) == NULL)
    return;

  if (name != NULL)
    /* translators: %s is a machine name */
    heading = g_strdup_printf (_("Recover %s?"), name);
  else
    heading = g_strdup (_("Recover the Shared VM?"));
  alert = adw_alert_dialog_new (heading, NULL);
  if (name != NULL)
    {
      /* translators: %s is a machine name */
      adw_alert_dialog_format_body (ADW_ALERT_DIALOG (alert),
                                    _("nsl stops %s and its own VM, then starts the VM again from a fresh root. The VM runs afterwards, even if it was stopped. Programs running in %s will close. The machine, its files and keys are kept."),
                                    name, name);
    }
  else
    adw_alert_dialog_set_body (ADW_ALERT_DIALOG (alert),
                               _("nsl stops the shared VM and every machine in it, then starts the VM again from a fresh root. The VM runs afterwards, even if it was stopped. Programs running in these machines will close. Machines, their files and keys are kept."));

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert),
                                  "cancel", _("_Cancel"),
                                  "recover", _("_Recover"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert), "recover", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");
  g_object_set_data_full (G_OBJECT (alert), "MACHINE_NAME", g_strdup (name), g_free);

  g_signal_connect_object (alert,
                           "response",
                           G_CALLBACK (ptyxis_machines_page_recover_response_cb),
                           self,
                           0);

  adw_dialog_present (alert, GTK_WIDGET (self));
}

/* Ends a wait for a VM to stop, if one is running, and clears the spinner of
 * the command that started it.
 */
static void
ptyxis_machines_page_end_resize_wait (PtyxisMachinesPage *self)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!self->resize_pending)
    return;

  self->resize_pending = FALSE;
  g_clear_handle_id (&self->resize_timeout, g_source_remove);
  g_clear_pointer (&self->resize_name, g_free);
  self->resize_gib = 0;

  ptyxis_machines_page_set_busy (self, FALSE);
}

/* Runs the growth in a terminal. nsl refuses a size for a VM that runs, and
 * says so there.
 */
static void
ptyxis_machines_page_present_resize (PtyxisMachinesPage *self,
                                     const char         *name,
                                     gint64              gib)
{
  g_autofree char *title = NULL;
  g_autofree char *running = NULL;
  g_autofree char *succeeded = NULL;
  g_autofree char *failed = NULL;
  g_auto(GStrv) argv = NULL;
  PtyxisMachineCommandDialog *dialog;
  const char *path;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!(path = nsl_path (self)) ||
      !(argv = ptyxis_machine_resize_argv (path, name, gib)))
    return;

  if (name != NULL)
    /* translators: %s is a machine name */
    title = g_strdup_printf (_("Growing the Disk of %s"), name);
  else
    title = g_strdup (_("Growing the Shared VM’s Disk"));

  /* translators: %" G_GINT64_FORMAT " is a data disk size in GiB, such as
   * 128
   */
  running = g_strdup_printf (_("Growing the data disk to %" G_GINT64_FORMAT " GiB…"), gib);
  /* translators: %" G_GINT64_FORMAT " is a data disk size in GiB, such as
   * 128
   */
  succeeded = g_strdup_printf (_("The data disk has %" G_GINT64_FORMAT " GiB. The VM grows its file system the next time it starts."), gib);
  failed = g_strdup (_("Could not grow the data disk"));

  dialog = ptyxis_machines_page_present_command (self, title, (const char * const *)argv);
  ptyxis_machine_command_dialog_set_messages (dialog, running, succeeded, failed);
}

static gboolean
ptyxis_machines_page_resize_timeout_cb (gpointer user_data)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (user_data);
  g_autofree char *name = g_steal_pointer (&self->resize_name);
  g_autofree char *body = NULL;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  self->resize_timeout = 0;

  if (name != NULL)
    /* translators: %s is a machine name */
    body = g_strdup_printf (_("The VM of %s is still running. nsl stops an isolated VM about a minute after its last machine stops, unless something still uses it. Choose Grow Disk… again when the VM has stopped."), name);
  else
    body = g_strdup (_("The shared VM is still running. Choose Shut Down All Machines, then Grow the Shared VM’s Disk… again."));

  ptyxis_machines_page_end_resize_wait (self);
  ptyxis_machines_page_show_error (self, _("Could not grow the disk"), body);

  return G_SOURCE_REMOVE;
}

/* Waits for the list to show the VM stopped. The agent emits Changed when a
 * VM's runtime directory goes away, so this needs no timer of its own.
 */
static void
ptyxis_machines_page_start_resize_wait (PtyxisMachinesPage *self,
                                        const char         *name,
                                        gint64              gib)
{
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  /* 1 wait at a time: a second request replaces the first */
  ptyxis_machines_page_end_resize_wait (self);

  self->resize_pending = TRUE;
  self->resize_name = g_strdup (name);
  self->resize_gib = gib;
  self->resize_timeout = g_timeout_add_seconds (RESIZE_WAIT_SECONDS,
                                               ptyxis_machines_page_resize_timeout_cb,
                                               self);

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_machines_page_toast (self, _("Waiting for the VM to stop…"));
  ptyxis_machines_page_reload_machines (self);
}

static void
ptyxis_machines_page_check_pending_resize (PtyxisMachinesPage *self)
{
  g_autofree char *name = NULL;
  g_autofree char *state = NULL;
  gint64 gib;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!self->resize_pending)
    return;

  /* NULL is the shared VM, for the list and for nsl resize alike */
  name = g_strdup (self->resize_name);
  gib = self->resize_gib;

  if (!lookup_vm (self, name, &state, NULL))
    {
      ptyxis_machines_page_end_resize_wait (self);
      ptyxis_machines_page_show_error (self,
                                       _("Could not grow the disk"),
                                       _("nsl does not list the VM of the machine. Check the machines and grow the disk again."));
      return;
    }

  if (ptyxis_machine_vm_needs_stop (state))
    return;

  ptyxis_machines_page_end_resize_wait (self);
  ptyxis_machines_page_present_resize (self, name, gib);
}

typedef struct
{
  PtyxisMachinesPage *self;
  char               *name;
  gint64              gib;
} ResizeState;

static void
resize_state_free (ResizeState *state)
{
  g_clear_object (&state->self);
  g_clear_pointer (&state->name, g_free);
  g_free (state);
}

static void
ptyxis_machines_page_resize_stop_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  PtyxisIpcMachines *proxy = (PtyxisIpcMachines *)object;
  ResizeState *state = user_data;
  PtyxisMachinesPage *self = state->self;
  g_autoptr(GError) error = NULL;
  g_autofree char *output = NULL;
  int exit_code = 0;

  g_assert (PTYXIS_IPC_IS_MACHINES (proxy));
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!ptyxis_ipc_machines_call_run_finish (proxy, &exit_code, &output, result, &error) &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      resize_state_free (state);
      return;
    }

  ptyxis_machines_page_set_busy (self, FALSE);

  if (error != NULL)
    {
      g_dbus_error_strip_remote_error (error);
      ptyxis_machines_page_show_error (self, _("Could not stop the VM"), error->message);
    }
  else if (exit_code != 0)
    {
      ptyxis_machines_page_show_error (self, _("Could not stop the VM"),
                                       output ? g_strstrip (output) : "");
    }
  else
    {
      ptyxis_machines_page_start_resize_wait (self, state->name, state->gib);
    }

  resize_state_free (state);
}

/* Stops the VM of @name, or every machine and VM for the shared VM, and then
 * waits for the list to show it stopped.
 */
static void
ptyxis_machines_page_stop_for_resize (PtyxisMachinesPage *self,
                                      const char         *name,
                                      gint64              gib)
{
  g_auto(GStrv) args = NULL;
  ResizeState *state;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->proxy == NULL || !(args = ptyxis_machine_vm_stop_args (name)))
    return;

  state = g_new0 (ResizeState, 1);
  state->self = g_object_ref (self);
  state->name = g_strdup (name);
  state->gib = gib;

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_ipc_machines_call_run (self->proxy,
                                (const char * const *)args,
                                self->cancellable,
                                ptyxis_machines_page_resize_stop_cb,
                                state);
}

static void
ptyxis_machines_page_resize_response_cb (AdwAlertDialog *alert,
                                         const char     *response,
                                         gpointer        user_data)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (alert), "MACHINE_NAME");
  GtkWidget *spin = g_object_get_data (G_OBJECT (alert), "RESIZE_SPIN");
  gint64 current = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (alert), "RESIZE_GIB"));
  gboolean needs_stop = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (alert), "RESIZE_NEEDS_STOP"));
  gint64 target;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (g_strcmp0 (response, "grow") != 0 || !ADW_IS_SPIN_ROW (spin))
    return;

  target = (gint64)adw_spin_row_get_value (ADW_SPIN_ROW (spin));

  /* nsl grows a disk but never shrinks it */
  if (!ptyxis_machine_is_valid_disk_size (current, target))
    return;

  if (!needs_stop)
    ptyxis_machines_page_present_resize (self, name, target);
  else
    ptyxis_machines_page_stop_for_resize (self, name, target);
}

static void
ptyxis_machines_page_resize_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)widget;
  const char *name = param ? g_variant_get_string (param, NULL) : NULL;
  g_autofree char *state = NULL;
  g_autofree char *heading = NULL;
  g_autofree char *body = NULL;
  g_autofree char *paragraph = NULL;
  g_autofree char *text = NULL;
  g_autoptr(GtkAdjustment) adjustment = NULL;
  GtkWidget *list;
  GtkWidget *spin;
  AdwDialog *alert;
  gint64 data_disk_gib = 0;
  gboolean needs_stop;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  lookup_vm (self, name, &state, &data_disk_gib);

  if (!ptyxis_machine_disk_can_grow (data_disk_gib))
    {
      ptyxis_machines_page_show_error (self,
                                       _("Could not grow the disk"),
                                       _("nsl did not report the size of the data disk, or it has the largest size nsl supports."));
      return;
    }

  needs_stop = ptyxis_machine_vm_needs_stop (state);

  if (name != NULL)
    /* translators: %s is a machine name */
    heading = g_strdup_printf (_("Grow the Disk of %s?"), name);
  else
    heading = g_strdup (_("Grow the Shared VM’s Disk?"));

  /* translators: %" G_GINT64_FORMAT " is a data disk size in GiB, such as
   * 128
   */
  body = g_strdup_printf (_("The data disk has %" G_GINT64_FORMAT " GiB. nsl can grow a data disk but never shrinks it, so choose the size with care. Files in the machines are kept."), data_disk_gib);

  if (!needs_stop)
    paragraph = g_strdup (_("The VM is stopped."));
  else if (name != NULL)
    /* translators: %s is a machine name */
    paragraph = g_strdup_printf (_("The VM of %s is running. Igloo first stops %s; nsl powers its VM off about a minute later, and then the disk grows. Programs running in %s will close."), name, name, name);
  else
    paragraph = g_strdup (_("The shared VM is running. Igloo first runs nsl shutdown, which stops every machine and every nsl VM, isolated ones included. Programs running in them will close."));

  text = g_strdup_printf ("%s\n\n%s", body, paragraph);

  adjustment = gtk_adjustment_new ((double)ptyxis_machine_suggest_disk_size (data_disk_gib),
                                   data_disk_gib + 1,
                                   PTYXIS_MACHINE_MAX_DISK_GIB,
                                   1, 16, 0);

  spin = adw_spin_row_new (adjustment, 0, 0);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (spin), _("New Size"));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (spin), _("GiB"));

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (list, "boxed-list");
  gtk_list_box_append (GTK_LIST_BOX (list), spin);

  alert = adw_alert_dialog_new (heading, NULL);
  adw_alert_dialog_set_body (ADW_ALERT_DIALOG (alert), text);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (alert), list);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert),
                                  "cancel", _("_Cancel"),
                                  "grow", needs_stop ? _("_Stop and Grow") : _("_Grow Disk"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert), "grow",
                                            needs_stop ? ADW_RESPONSE_DESTRUCTIVE
                                                       : ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");
  g_object_set_data_full (G_OBJECT (alert), "MACHINE_NAME", g_strdup (name), g_free);
  g_object_set_data (G_OBJECT (alert), "RESIZE_GIB", GINT_TO_POINTER ((int)data_disk_gib));
  g_object_set_data (G_OBJECT (alert), "RESIZE_NEEDS_STOP", GINT_TO_POINTER (needs_stop ? 1 : 0));
  g_object_set_data_full (G_OBJECT (alert), "RESIZE_SPIN", g_object_ref (spin), g_object_unref);

  g_signal_connect_object (alert,
                           "response",
                           G_CALLBACK (ptyxis_machines_page_resize_response_cb),
                           self,
                           0);

  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
ptyxis_machines_page_update_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);
  PtyxisMachineCommandDialog *dialog;
  const char *path;

  if (!(path = nsl_path (self)))
    return;

  {
    const char * const argv[] = { path, "update", NULL };

    dialog = ptyxis_machines_page_present_command (self, _("Update the VM Image"), argv);
    ptyxis_machine_command_dialog_set_messages (dialog,
                                                _("Verifying and selecting the newest VM image…"),
                                                _("The selected VM image is used the next time each VM starts"),
                                                _("Could not update the VM image"));
  }
}

static void
ptyxis_machines_page_shutdown_action (GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *param)
{
  static const char * const args[] = { "shutdown", NULL };

  ptyxis_machines_page_run (PTYXIS_MACHINES_PAGE (widget),
                            args,
                            _("All machines are stopped"),
                            _("Could not shut down the machines"));
}

static void
ptyxis_machines_page_install_guide_action (GtkWidget  *widget,
                                           const char *action_name,
                                           GVariant   *param)
{
  g_autoptr(GtkUriLauncher) launcher = gtk_uri_launcher_new (NSL_INSTALL_URI);
  GtkRoot *root = gtk_widget_get_root (widget);

  gtk_uri_launcher_launch (launcher, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, NULL, NULL, NULL);
}

static void
ptyxis_machines_page_set_config_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  PtyxisIpcMachines *proxy = (PtyxisIpcMachines *)object;
  g_autoptr(PtyxisMachinesPage) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IPC_IS_MACHINES (proxy));
  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (!ptyxis_ipc_machines_call_set_config_finish (proxy, result, &error) &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  ptyxis_machines_page_set_busy (self, FALSE);

  if (error != NULL)
    {
      g_dbus_error_strip_remote_error (error);
      ptyxis_machines_page_show_error (self, _("Could not save the settings"), error->message);
    }

  ptyxis_machines_page_reload_config (self);
}

static void
ptyxis_machines_page_save_settings (PtyxisMachinesPage *self,
                                    GHashTable         *settings)
{
  GVariantBuilder builder;
  GHashTableIter iter;
  gpointer key, value;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->proxy == NULL || g_hash_table_size (settings) == 0)
    return;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_hash_table_iter_init (&iter, settings);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_variant_builder_add (&builder, "{ss}", key, value);

  ptyxis_machines_page_set_busy (self, TRUE);
  ptyxis_ipc_machines_call_set_config (self->proxy,
                                       g_variant_builder_end (&builder),
                                       self->cancellable,
                                       ptyxis_machines_page_set_config_cb,
                                       g_object_ref (self));
}

static gboolean
ptyxis_machines_page_save_source_func (gpointer data)
{
  PtyxisMachinesPage *self = data;
  g_autoptr(GHashTable) settings = NULL;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  self->save_source = 0;

  settings = g_steal_pointer (&self->pending_settings);
  self->pending_settings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  ptyxis_machines_page_save_settings (self, settings);

  return G_SOURCE_REMOVE;
}

static void
ptyxis_machines_page_setting_changed_cb (PtyxisMachinesPage *self,
                                         GParamSpec         *pspec,
                                         GtkWidget          *widget)
{
  const char *key = g_object_get_data (G_OBJECT (widget), "NSL_SETTING");
  char *value;

  g_assert (PTYXIS_IS_MACHINES_PAGE (self));

  if (self->loading_settings || key == NULL)
    return;

  if (ADW_IS_SPIN_ROW (widget))
    value = g_strdup_printf ("%u", (guint)adw_spin_row_get_value (ADW_SPIN_ROW (widget)));
  else
    value = g_strdup (adw_switch_row_get_active (ADW_SWITCH_ROW (widget)) ? "true" : "false");

  g_hash_table_insert (self->pending_settings, (gpointer)key, value);

  g_clear_handle_id (&self->save_source, g_source_remove);
  self->save_source = g_timeout_add (SAVE_DELAY_MSEC, ptyxis_machines_page_save_source_func, self);
}

static void
ptyxis_machines_page_reset_settings_action (GtkWidget  *widget,
                                            const char *action_name,
                                            GVariant   *param)
{
  PtyxisMachinesPage *self = PTYXIS_MACHINES_PAGE (widget);
  g_autoptr(GHashTable) settings = g_hash_table_new (g_str_hash, g_str_equal);

  g_clear_handle_id (&self->save_source, g_source_remove);
  g_hash_table_remove_all (self->pending_settings);

  /* An empty value removes the key, so nsl uses its default */
  for (guint i = 0; i < G_N_ELEMENTS (settings_map); i++)
    g_hash_table_insert (settings, (gpointer)settings_map[i].key, (gpointer)"");

  ptyxis_machines_page_save_settings (self, settings);
}

static void
ptyxis_machines_page_dispose (GObject *object)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)object;

  g_cancellable_cancel (self->cancellable);
  g_clear_handle_id (&self->save_source, g_source_remove);
  g_clear_handle_id (&self->resize_timeout, g_source_remove);
  g_clear_pointer (&self->resize_name, g_free);

  if (self->proxy != NULL)
    g_signal_handlers_disconnect_by_data (self->proxy, self);

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_MACHINES_PAGE);

  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (ptyxis_machines_page_parent_class)->dispose (object);
}

static void
ptyxis_machines_page_finalize (GObject *object)
{
  PtyxisMachinesPage *self = (PtyxisMachinesPage *)object;

  g_clear_object (&self->cancellable);
  g_clear_pointer (&self->machines, g_variant_unref);
  g_clear_pointer (&self->vms, g_variant_unref);
  g_clear_pointer (&self->pending_settings, g_hash_table_unref);

  G_OBJECT_CLASS (ptyxis_machines_page_parent_class)->finalize (object);
}

static void
ptyxis_machines_page_class_init (PtyxisMachinesPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_machines_page_dispose;
  object_class->finalize = ptyxis_machines_page_finalize;

  widget_class->map = ptyxis_machines_page_map;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-machines-page.ui");
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, autostart);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, host_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, idle_timeout);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, isolated_cpus);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, isolated_memory);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, machines_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, machines_list_box);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, missing_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, placeholder_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, settings_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, shared_disk_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, spinner);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, version_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, vm_cpus);
  gtk_widget_class_bind_template_child (widget_class, PtyxisMachinesPage, vm_memory);

  gtk_widget_class_install_action (widget_class, "machines.create", NULL, ptyxis_machines_page_create_action);
  gtk_widget_class_install_action (widget_class, "machines.import", NULL, ptyxis_machines_page_import_action);
  gtk_widget_class_install_action (widget_class, "machines.refresh", NULL, ptyxis_machines_page_refresh_action);
  gtk_widget_class_install_action (widget_class, "machines.rescan", NULL, ptyxis_machines_page_rescan_action);
  gtk_widget_class_install_action (widget_class, "machines.doctor", NULL, ptyxis_machines_page_doctor_action);
  gtk_widget_class_install_action (widget_class, "machines.host-logs", NULL, ptyxis_machines_page_logs_action);
  gtk_widget_class_install_action (widget_class, "machines.host-recover", NULL, ptyxis_machines_page_recover_action);
  gtk_widget_class_install_action (widget_class, "machines.host-resize", NULL, ptyxis_machines_page_resize_action);
  gtk_widget_class_install_action (widget_class, "machines.logs", "s", ptyxis_machines_page_logs_action);
  gtk_widget_class_install_action (widget_class, "machines.recover", "s", ptyxis_machines_page_recover_action);
  gtk_widget_class_install_action (widget_class, "machines.resize", "s", ptyxis_machines_page_resize_action);
  gtk_widget_class_install_action (widget_class, "machines.update", NULL, ptyxis_machines_page_update_action);
  gtk_widget_class_install_action (widget_class, "machines.shutdown", NULL, ptyxis_machines_page_shutdown_action);
  gtk_widget_class_install_action (widget_class, "machines.install-guide", NULL, ptyxis_machines_page_install_guide_action);
  gtk_widget_class_install_action (widget_class, "machines.reset-settings", NULL, ptyxis_machines_page_reset_settings_action);
  gtk_widget_class_install_action (widget_class, "machines.open", "s", ptyxis_machines_page_open_action);
  gtk_widget_class_install_action (widget_class, "machines.start", "s", ptyxis_machines_page_start_action);
  gtk_widget_class_install_action (widget_class, "machines.stop", "s", ptyxis_machines_page_stop_action);
  gtk_widget_class_install_action (widget_class, "machines.default", "s", ptyxis_machines_page_default_action);
  gtk_widget_class_install_action (widget_class, "machines.profile", "s", ptyxis_machines_page_profile_action);
  gtk_widget_class_install_action (widget_class, "machines.remove", "s", ptyxis_machines_page_remove_action);
  gtk_widget_class_install_action (widget_class, "machines.export", "s", ptyxis_machines_page_export_action);
}

static void
ptyxis_machines_page_init (PtyxisMachinesPage *self)
{
  self->cancellable = g_cancellable_new ();
  self->pending_settings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Enabled once nsl's configuration has been read */
  ptyxis_machines_page_set_settings_sensitive (self, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS (settings_map); i++)
    {
      GtkWidget *widget = setting_widget (self, i);

      g_object_set_data (G_OBJECT (widget), "NSL_SETTING", (gpointer)settings_map[i].key);
      g_signal_connect_object (widget,
                               ADW_IS_SPIN_ROW (widget) ? "notify::value" : "notify::active",
                               G_CALLBACK (ptyxis_machines_page_setting_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
    }
}
