#pragma once
#include <gtk/gtk.h>

#define LOGIN_WINDOW_TYPE (login_window_get_type())
G_DECLARE_FINAL_TYPE(LoginWindow, login_window, LOGIN, WINDOW, GtkBox)

struct _LoginWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *logo;
    GtkWidget *uname_entry;
    GtkWidget *passwd_entry;
    GtkWidget *login_button;
};