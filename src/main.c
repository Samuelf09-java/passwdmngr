#include <sys/stat.h>
#include <sodium.h>
#include "ui/login_window.h"
#include "storage.h"
#include "util.h"
#include "main.h"

GtkApplication *passwdmngr = NULL;
GtkWindow *root_window = NULL;
char *accounts_path = NULL;

static void on_activate(GtkApplication *app) {

    util_log(INFO, "Started passwdmngr app");
    util_log(INFO, "Runtime gtk v%d.%d.%d", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());

    passwdmngr = app;

    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/samuelf09/passwdmngr/style.css");

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    GtkApplicationWindow *win = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    root_window = GTK_WINDOW(win);
    gtk_window_set_default_size(root_window, 1200, 800);

    LoginWindow *login_win = g_object_new(LOGIN_WINDOW_TYPE, NULL);
    gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(login_win));

    if (sodium_init() < 0) {
        util_fatal_d("Failed to initialize libsodium");
        return;
    }

    // Verify app files are present
    char *root = util_get_app_dir();
    if (!root) {
        util_fatal_d("Could not determine user data directory.");
        return;
    }

    if (!dir_exists(root)) g_mkdir_with_parents(root, 0755);

    char *users_dir = malloc(strlen(root) + strlen("users") + 1);
    sprintf(users_dir, "%susers", root);
    if (!dir_exists(users_dir)) g_mkdir_with_parents(users_dir, 0755);

    accounts_path = malloc(strlen(root) + strlen("accounts.json") + 1);
    sprintf(accounts_path, "%saccounts.json", root);

    gchar *contents = NULL;
    gsize length = 0;

    bool accounts_exists = g_file_get_contents(accounts_path, &contents, &length, NULL);

    g_free(contents);

    // If accounts.json does not exist, write empty array
    if (!accounts_exists || length == 0) init_accounts_json();

    free(users_dir);
    free(root);

    // Load accounts.json
    if (!load_accounts()) {
        free(accounts_path);
        util_fatal_d("Failed to load account data from accounts.json; see stderr for more information");
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
