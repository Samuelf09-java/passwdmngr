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
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

void util_error(const char *msg) {
    g_printerr("Error: %s\n", msg);
}

void util_error_dialog(GtkWindow *parent, const char *msg, enum ErrorType error_type, GtkApplication *app) {

    char *prefix = NULL;

    if (error_type == NONFATAL) prefix = "Error: ";
    else                        prefix = "FATAL ERROR: ";

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
    util_error_dialog(current_window, msg, FATAL, passwdmngr);
}

void util_nonfatal(const char *msg) {
    util_error_dialog(current_window, msg, NONFATAL, passwdmngr);
}

bool util_check_ptr(void *ptr, const char *msg) {
    if (!ptr) {
        util_error(msg);
        return false;
    }
    return true;
}