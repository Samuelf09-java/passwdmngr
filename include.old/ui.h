#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>
#include "app.h"

// Login screen
GtkWidget *ui_login_screen(AppState *app);

// Account creation screen
GtkWidget *ui_create_account_screen(AppState *app);

// Vault screen (main app UI)
GtkWidget *ui_vault_screen(AppState *app);

#endif
