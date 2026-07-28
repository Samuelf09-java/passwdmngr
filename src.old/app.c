#include "app.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *app_get_data_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return NULL;

    char *path = malloc(512);
    snprintf(path, 512, "%s/.local/share/passwdmngr", home);
    return path;
}

bool app_ensure_data_dirs(void) {
    char *root = app_get_data_dir();
    if (!root) return false;

    // Create ~/.local/share/passwdmngr
    if (!util_path_exists(root)) {
        if (!util_mkdir_p(root)) {
            fprintf(stderr, "Failed to create data directory: %s\n", root);
            free(root);
            return false;
        }
    }

    // Create ~/.local/share/passwdmngr/users
    char users_dir[600];
    snprintf(users_dir, sizeof(users_dir), "%s/users", root);

    if (!util_path_exists(users_dir)) {
        if (!util_mkdir_p(users_dir)) {
            fprintf(stderr, "Failed to create users directory: %s\n", users_dir);
            free(root);
            return false;
        }
    }

    free(root);
    return true;
}

void app_init(AppState *app, GtkApplication *gtk_app) {
    memset(app, 0, sizeof(AppState));
    app->gtk_app = gtk_app;

    // FIRST-RUN INITIALIZATION
    if (!app_ensure_data_dirs()) {
        fprintf(stderr, "Fatal: Could not initialize data directories.\n");
        exit(1);
    }
}
