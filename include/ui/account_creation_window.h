#pragma once
#include <gtk/gtk.h>

#define ACCOUNT_CREATION_WINDOW_TYPE (account_creation_window_get_type())

struct _AccountCreationWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *logo;
    GtkWidget *uname_entry;
    GtkWidget *passwd_entry;
    GtkWidget *confirm_passwd_entry;
};

G_DECLARE_FINAL_TYPE(AccountCreationWindow, account_creation_window, ACCOUNT_CREATION, WINDOW, GtkBox)