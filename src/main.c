#include <sys/stat.h>
#include <sodium.h>
#include "login_window.h"
#include "storage.h"
#include "util.h"
#include "main.h"

GtkApplication *passwdmngr = NULL;
GtkWindow *current_window = NULL;

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

    // Load accounts.json
    if (!load_accounts()) {
        util_fatal("Failed to load account data from accounts.json");
    }

    // Verify app files are present
    char *root = util_get_app_dir();
    if (!root) {
        util_fatal("Could not determine user data directory.");
        return;
    }

    if (!dir_exists(root)) mkdir(root, 0755);

    char *users_dir = malloc(strlen(root) + strlen("users") + 2);
    sprintf(users_dir, "%s/users", root);
    if (!dir_exists(users_dir)) mkdir(users_dir, 0755);

    char *accounts_path = malloc(strlen(root) + strlen("accounts.json") + 2);
    sprintf(accounts_path, "%s/accounts.json", root);

    FILE *fp = fopen(accounts_path, "a");
    if (!fp) {
        util_fatal("Could not create accounts.json");
        return;
    }
    fclose(fp);

    free(users_dir);
    free(accounts_path);
    free(root);
    
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.samuelf09.passwdmngr", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
