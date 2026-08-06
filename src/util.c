#include <sys/stat.h>
#include "util.h"
#include "main.h"

#ifdef _WIN32
    const char PATH_SEPARATOR = '\\';
#else
    const char PATH_SEPARATOR = '/';
#endif

char *util_get_app_dir() {
    const char *home = getenv("HOME");
    char *root;

#if defined(__linux__)
    if (!home) return NULL;
    root = malloc(strlen(home) + strlen("/.local/share/passwdmngr/") + 1);
    if (!root) {
        util_error("Failed to allocate memory for root path");
        return NULL;
    }
    sprintf(root, "%s/.local/share/passwdmngr/", home);
    return root;

#elif defined(__APPLE__)
    if (!home) return NULL;
    root = malloc(strlen(home) + strlen("/Library/Application Support/passwdmngr/") + 1);
    if (!root) {
        util_error("Failed to allocate memory for root path");
        return NULL;
    }
    sprintf(root, "%s/Library/Application Support/passwdmngr/", home);
    return root;

#elif defined(_WIN32)
    const char *local = getenv("LOCALAPPDATA");
    if (!local) return NULL;
    root = malloc(strlen(local) + strlen("\\passwdmngr\\") + 1);
    if (!root) {
        util_error("Failed to allocate memory for root path");
        return NULL;
    }
    sprintf(root, "%s\\passwdmngr\\", local);
    return root;

#else
    #error "Unrecognized platform!"

#endif
}

int dir_exists(const char *path) {
    return g_file_test(path, G_FILE_TEST_IS_DIR);
}

void util_info(const char *msg) {
    g_print("[passwdmngr/INFO]: %s\n", msg);
}

void util_error(const char *msg) {
    g_printerr("[passwdmngr/ERROR]: %s\n", msg);
}

void util_error_dialog(GtkWindow *parent, const char *msg, enum ErrorType error_type, GtkApplication *app) {

    char *prefix = NULL;

    if (error_type == WARN)          prefix = "[passwdmngr/WARNING]: ";
    else if (error_type == NONFATAL) prefix = "[passwdmngr/ERROR]: ";
    else                             prefix = "[passwdmngr/FATAL ERROR]: ";

    size_t msg_len = strlen(msg) + strlen(prefix) + 1;
    char *error_msg = malloc(msg_len);
    sprintf(error_msg, "%s%s", prefix, msg);

    g_printerr("%s\n", error_msg);
    if (!parent) {
        if (error_type == FATAL) g_application_quit(G_APPLICATION(app));
        return;
    }

    GtkAlertDialog *dialog = gtk_alert_dialog_new(error_msg);

    const char *buttons[] = { "Close", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_show(dialog, parent);

    if (error_type == FATAL) g_application_quit(G_APPLICATION(app));
}

void util_fatal(const char *msg) {
    util_error_dialog(root_window, msg, FATAL, passwdmngr);
}

void util_nonfatal(const char *msg) {
    util_error_dialog(root_window, msg, NONFATAL, passwdmngr);
}

void util_warn(const char *msg) {
    util_error_dialog(root_window, msg, WARN, passwdmngr);
}

bool util_check_ptr(void *ptr, const char *msg) {
    if (!ptr) {
        util_error(msg);
        return false;
    }
    return true;
}