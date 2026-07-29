#pragma once
#include <gtk/gtk.h>

#define MAIN_WINDOW_TYPE (main_window_get_type())
G_DECLARE_FINAL_TYPE(MainWindow, main_window, MAIN, WINDOW, GtkApplicationWindow)

MainWindow *main_window_new(GtkApplication *app);