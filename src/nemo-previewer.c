/*
 * nemo-previewer: nemo previewer DBus wrapper
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include "nemo-previewer.h"

#include "nemo-view.h"
#include "nemo-window.h"
#include "nemo-window-slot.h"

#define DEBUG_FLAG NEMO_DEBUG_PREVIEWER
#include <libnemo-private/nemo-debug.h>

#include <gio/gio.h>

G_DEFINE_TYPE (NemoPreviewer, nemo_previewer, G_TYPE_OBJECT);

#define PREVIEWER_DBUS_NAME "org.gnome.NautilusPreviewer"
#define PREVIEWER_DBUS_IFACE "org.gnome.NautilusPreviewer"
#define PREVIEWER_DBUS_EVENT "org.gnome.NautilusPreviewer2"
#define PREVIEWER_DBUS_PATH "/org/gnome/NautilusPreviewer"

static NemoPreviewer *singleton = NULL;

struct _NemoPreviewerPriv {
  GDBusConnection *connection;
  guint previewer_selection_id;
};

static void
nemo_previewer_dispose (GObject *object)
{
  NemoPreviewer *self = NEMO_PREVIEWER (object);

  DEBUG ("%p", self);

  g_clear_object (&self->priv->connection);

  G_OBJECT_CLASS (nemo_previewer_parent_class)->dispose (object);
}

static GObject *
nemo_previewer_constructor (GType type,
                                guint n_construct_params,
                                GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (singleton != NULL)
    return G_OBJECT (singleton);

  retval = G_OBJECT_CLASS (nemo_previewer_parent_class)->constructor
    (type, n_construct_params, construct_params);

  singleton = NEMO_PREVIEWER (retval);
  g_object_add_weak_pointer (retval, (gpointer) &singleton);

  return retval;
}

static void
nemo_previewer_init (NemoPreviewer *self)
{
  GError *error = NULL;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NEMO_TYPE_PREVIEWER,
                                            NemoPreviewerPriv);

  self->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                           NULL, &error);

  if (error != NULL) {
    g_printerr ("Unable to initialize DBus connection: %s", error->message);
    g_error_free (error);
    return;
  }
}

static void
nemo_previewer_class_init (NemoPreviewerClass *klass)
{
  GObjectClass *oclass;

  oclass = G_OBJECT_CLASS (klass);
  oclass->constructor = nemo_previewer_constructor;
  oclass->dispose = nemo_previewer_dispose;

  g_type_class_add_private (klass, sizeof (NemoPreviewerPriv));
}

static void
previewer_show_file_ready_cb (GObject *source,
                              GAsyncResult *res,
                              gpointer user_data)
{
  NemoPreviewer *self = user_data;
  GError *error = NULL;

  g_dbus_connection_call_finish (self->priv->connection,
                                 res, &error);

  if (error != NULL) {
    DEBUG ("Unable to call ShowFile on NemoPreviewer: %s",
           error->message);
    g_error_free (error);
  }

  g_object_unref (self);
}

static void
previewer_close_ready_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
  NemoPreviewer *self = user_data;
  GError *error = NULL;

  g_dbus_connection_call_finish (self->priv->connection,
                                 res, &error);

  if (error != NULL) {
    DEBUG ("Unable to call Close on NemoPreviewer: %s",
           error->message);
    g_error_free (error);
  }

  g_object_unref (self);
}

NemoPreviewer *
nemo_previewer_get_singleton (void)
{
  return g_object_new (NEMO_TYPE_PREVIEWER, NULL);
}

void
nemo_previewer_call_show_file (NemoPreviewer *self,
                                   const gchar *uri,
                                   guint xid,
				   gboolean close_if_already_visible)
{
  if (self->priv->connection == NULL) {
    g_printerr ("No DBus connection available");
    return;
  }

  g_dbus_connection_call (self->priv->connection,
                          PREVIEWER_DBUS_NAME,
                          PREVIEWER_DBUS_PATH,
                          PREVIEWER_DBUS_IFACE,
                          "ShowFile",
                          g_variant_new ("(sib)",
                            uri, xid, close_if_already_visible),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          previewer_show_file_ready_cb,
                          g_object_ref (self));

  /* Disconnect any existing Nemo Preview */
  if (self->priv->previewer_selection_id != 0)
  {
    nemo_previewer_disconnect_selection_event (self->priv->connection,
                                               self->priv->previewer_selection_id);
    self->priv->previewer_selection_id = 0;
  }

  /* Connect to new Nemo Preview */
  self->priv->previewer_selection_id = nemo_previewer_connect_selection_event (self->priv->connection);
}

void
nemo_previewer_call_close (NemoPreviewer *self)
{
  if (self->priv->connection == NULL) {
    g_printerr ("No DBus connection available");
    return;
  }

  /* don't autostart the previewer if it's not running */
  g_dbus_connection_call (self->priv->connection,
                          PREVIEWER_DBUS_NAME,
                          PREVIEWER_DBUS_PATH,
                          PREVIEWER_DBUS_IFACE,
                          "Close",
                          NULL,
                          NULL,
                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                          -1,
                          NULL,
                          previewer_close_ready_cb,
                          g_object_ref (self));

  /* Disconnect Nemo Preview */
  if (self->priv->previewer_selection_id != 0)
  {
    nemo_previewer_disconnect_selection_event (self->priv->connection,
                                               self->priv->previewer_selection_id);
    self->priv->previewer_selection_id = 0;
  }
}

static void
previewer_selection_event (GDBusConnection *connection,
                           const gchar     *sender_name,
                           const gchar     *object_path,
                           const gchar     *interface_name,
                           const gchar     *signal_name,
                           GVariant        *parameters,
                           gpointer         user_data)
{
    GApplication *application = g_application_get_default ();
    GList *l, *windows = gtk_application_get_windows (GTK_APPLICATION (application));
    NemoWindow *window = NULL;
    NemoWindowSlot *slot;
    NemoView *view;
    GtkDirectionType direction;

    for (l = windows; l != NULL; l = l->next)
    {
        if (NEMO_IS_WINDOW (l->data))
        {
            window = l->data;
            break;
        }
    }

    if (window == NULL)
    {
        return;
    }

    slot = nemo_window_get_active_slot (window);
    view = nemo_window_slot_get_current_view (slot);

    if (!NEMO_IS_VIEW (view))
    {
        return;
    }

    g_variant_get (parameters, "(u)", &direction);
    nemo_view_preview_selection_event (NEMO_VIEW (view), direction);
}

guint
nemo_previewer_connect_selection_event (GDBusConnection *connection)
{
    return g_dbus_connection_signal_subscribe (connection,
                                               PREVIEWER_DBUS_NAME,
                                               PREVIEWER_DBUS_EVENT,
                                               "SelectionEvent",
                                               PREVIEWER_DBUS_PATH,
                                               NULL,
                                               G_DBUS_SIGNAL_FLAGS_NONE,
                                               previewer_selection_event,
                                               NULL,
                                               NULL);
}

void
nemo_previewer_disconnect_selection_event (GDBusConnection *connection,
                                               guint            event_id)
{
    g_dbus_connection_signal_unsubscribe (connection, event_id);
}
