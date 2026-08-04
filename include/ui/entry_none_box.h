#pragma once
#include <gtk/gtk.h>

#define ENTRY_NONE_BOX_TYPE (entry_none_box_get_type())
G_DECLARE_FINAL_TYPE(EntryNoneBox, entry_none_box, ENTRY, NONE_BOX, GtkBox)

struct _EntryNoneBox {
    GtkBox parent_instance;
};