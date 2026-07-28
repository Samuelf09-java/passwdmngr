#ifndef APP_H
#define APP_H

#include <gtk/gtk.h>
#include <stdint.h>

typedef struct {
    GtkApplication *gtk_app;

    char username[64];     // currently logged-in user
    uint8_t key[32];       // derived encryption key for vault
} AppState;

// Initialize the application state
void app_init(AppState *app, GtkApplication *gtk_app);

// Path helpers (platform-aware)
char *app_get_user_dir(const char *hashed_username);
char *app_get_data_dir(void);
// Ensure ~/.local/share/passwdmngr exists
bool app_ensure_data_dirs(void);

#endif
