#pragma once
#include <gtk/gtk.h>
#include "ui/entry_view_box.h"
#include "ui/entry_edit_box.h"
#include "ui/entry_none_box.h"

#define MAIN_WINDOW_TYPE (main_window_get_type())
G_DECLARE_FINAL_TYPE(MainWindow, main_window, MAIN, WINDOW, GtkBox)

struct _MainWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *menubar_box;
    GtkWidget *entries_listbox;
    GtkWidget *content_area;
    GtkWidget *add_entry_button;

    bool sidebar_ready;
};