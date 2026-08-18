#include <sys/stat.h>
#include <sodium.h>
#include "ui/login_window.h"
#include "ui/main_window.h"
#include "storage.h"
#include "util.h"
#include "main.h"

AppMode mode;

GtkApplication *passwdmngr = NULL;
GtkWindow *root_window = NULL;
char *accounts_path = NULL;

static bool app_init() {

    util_log(INFO, "Started passwdmngr app");
    util_log(DEBUG, "Debug messages are enabled");

    if (sodium_init() < 0) {
        util_log(FATAL, "Failed to initialize libsodium");
        return false;
    }

    // Verify app files are present
    char *root = util_get_app_dir();
    if (!root) {
        util_log(FATAL, "Could not determine user data directory.");
        return false;
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
        util_log(FATAL, "Failed to load account data from accounts.json; see stderr for more information");
        return false;
    }

    return true;
}

static void on_activate(GtkApplication *app) {

    util_log(DEBUG, "Runtime gtk v%d.%d.%d", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());

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

    if (!app_init()) {
        util_log(FATAL, "app_init failed");
        g_application_quit(G_APPLICATION(passwdmngr));
    }
    
    gtk_window_present(GTK_WINDOW(win));
}

static void on_shutdown(GApplication *app, gpointer user_data) {

    X(app);
    X(user_data);

    if (entries) wipe_passwd_entries();
    if (tmp_passwd) {
        wipe_mem(tmp_passwd, strlen(tmp_passwd));
        free(tmp_passwd);
        tmp_passwd = NULL;
    }
    wipe_mem(aes_key, sizeof(aes_key));
    key_set = false;
    if (username) {
        free(username);
        username = NULL;
    }

    free_accounts();

    util_log(INFO, "App shut down (cleanup successful)");
}

static int run_cli(int argc, char **argv) {

    X(argc);
    X(argv);

    mode = CLI;

    if (!app_init()) {
        util_log(FATAL, "app_init failed");
        exit(1);
    }

    printf("passwdmngr running in cli mode!\n");

    on_shutdown(NULL, NULL);
    exit(0);
}

int main(int argc, char **argv) {

    if (argc > 1) {
        if (!strcmp(argv[1], "--cli")) {
            for (int i = 2; i < argc; i++)
                util_log(WARN, "Ignoring unrecognized argument '%s'", argv[i]);
            return run_cli(argc, argv); // Run in cli mode
        } else {
            for (int i = 1; i < argc; i++)
                util_log(WARN, "Ignoring unrecognized argument '%s'", argv[i]);
        }
    }

    mode = GUI;
    GtkApplication *app = gtk_application_new("com.samuelf09.passwdmngr", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
