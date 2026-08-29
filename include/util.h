#pragma once
#include <gtk/gtk.h>

extern const char PATH_SEPARATOR;

// Used with `GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);`
#define REGISTER_CALLBACK(cb) gtk_widget_class_bind_template_callback(widget_class, cb)
#define REGISTER_CHILD(type, name) gtk_widget_class_bind_template_child(widget_class, type, name)

#define X(x) (void)(x) // suppress 'unused parameter' compiler warnings with void cast
#define UNIMPLEMENTED util_error("This function is currently unimplemented") // mark a function as unimplemented

enum ErrorType {
    WARN_D,
    NONFATAL_D,
    FATAL_D
};

enum LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

typedef enum ErrorType ErrorType;
typedef enum LogLevel LogLevel;

char *util_get_app_dir();
char *util_get_logfile();
char *util_get_prefs_file();
char *util_get_accounts_file();
int dir_exists(const char *path);
bool delete_recursive(const char *path, GError **error);

void util_assert(int cond, char *fail_msg);

void util_log(LogLevel level, const char *fmt, ...);

void util_error_dialog(GtkWindow *parent, const char *msg, ErrorType error_type, GtkApplication *app);
void util_fatal_d(const char *msg);
void util_nonfatal_d(const char *msg);
void util_warn_d(const char *msg);
bool util_check_ptr(void *ptr, const char *msg);

void wipe_mem(void *mem, size_t bytes);
void *ec_malloc(size_t size);
void *ec_realloc(void *ptr, size_t size);
char *trim(char *s);