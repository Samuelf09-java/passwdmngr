#pragma once

#include <gtk/gtk.h>

#define ENTRY_VIEW_BOX_TYPE (entry_view_box_get_type())
G_DECLARE_FINAL_TYPE(EntryViewBox, entry_view_box, ENTRY, VIEW_BOX, GtkBox)

struct _EntryViewBox {
    GtkBox parent_instance;

    GtkWidget *service_label;
    GtkWidget *username_label;
    GtkWidget *password_label;
    GtkWidget *notes_label;

    int entry_id;
};