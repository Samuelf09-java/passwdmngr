#pragma once

#include <gtk/gtk.h>

#define ENTRY_EDIT_BOX_TYPE (entry_edit_box_get_type())
G_DECLARE_FINAL_TYPE(EntryEditBox, entry_edit_box, ENTRY, EDIT_BOX, GtkBox)

enum ContentMode {
    ENTRY_ADD,
    ENTRY_EDIT
};

struct _EntryEditBox {
    GtkBox parent_instance;

    GtkWidget *service_entry;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *notes_text;

    GtkWidget *save_button;
    GtkWidget *cancel_button;

    enum ContentMode edit_mode;
    int entry_id;
};