#pragma once

#include <gtk/gtk.h>

#define ENTRY_VIEW_BOX_TYPE (entry_view_box_get_type())
G_DECLARE_FINAL_TYPE(EntryViewBox, entry_view_box, ENTRY, VIEW_BOX, GtkBox)

struct _EntryViewBox {
    GtkBox parent_instance;

    GtkWidget *service_box;
    GtkWidget *service_label;
    GtkWidget *service_copy_button;
    GtkWidget *username_box;
    GtkWidget *username_label;
    GtkWidget *username_copy_button;
    GtkWidget *password_box;
    GtkWidget *password_label;
    GtkWidget *password_copy_button;
    GtkWidget *notes_label;
    GtkWidget *notes_title;
    GtkWidget *notes_container;
    GtkWidget *notes_copy_button;

    int entry_id;
};