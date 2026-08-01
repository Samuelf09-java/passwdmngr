#include <sys/stat.h>
#include <sodium.h>
#include "login_window.h"
#include "storage.h"
#include "util.h"
#include "main.h"

GtkApplication *passwdmngr = NULL;
GtkWindow *current_window = NULL;
char *accounts_path = NULL;

static void on_activate(GtkApplication *app) {
    passwdmngr = app;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/samuelf09/passwdmngr/style.css");

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    LoginWindow *win = g_object_new(
        LOGIN_WINDOW_TYPE,
        "application", app,
        NULL
    );

    current_window = GTK_WINDOW(win);

    if (sodium_init() < 0) {
        util_fatal("Failed to initialize libsodium");
        return;
    }

    // Verify app files are present
    char *root = util_get_app_dir();
    if (!root) {
        util_fatal("Could not determine user data directory.");
        return;
    }

    if (!dir_exists(root)) mkdir(root, 0755);

    char *users_dir = malloc(strlen(root) + strlen("users") + 1);
    sprintf(users_dir, "%susers", root);
    if (!dir_exists(users_dir)) mkdir(users_dir, 0755);

    accounts_path = malloc(strlen(root) + strlen("accounts.json") + 1);
    sprintf(accounts_path, "%saccounts.json", root);

    struct stat st;
    bool file_missing = (stat(accounts_path, &st) != 0);
    bool file_empty   = (!file_missing && st.st_size == 0);

    // If accounts.json does not exist, write empty array
    if (file_missing || file_empty) init_accounts_json(accounts_path);

    free(users_dir);
    free(root);

    // Load accounts.json
    if (!load_accounts(accounts_path)) {
        free(accounts_path);
        util_fatal("Failed to load account data from accounts.json; see stderr for more information");
    }
    
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.samuelf09.passwdmngr", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
