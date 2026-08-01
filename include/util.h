#pragma once
#include <gtk/gtk.h>

extern const char PATH_SEPARATOR;

// Used with `GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);`
#define REGISTER_CALLBACK(cb) gtk_widget_class_bind_template_callback(widget_class, cb)
#define REGISTER_CHILD(type, name) gtk_widget_class_bind_template_child(widget_class, type, name)

#define X(x) (void)(x) // suppress 'unused parameter' compiler warnings with void cast

#define STORAGE_SCHEMA_VERSION 1

enum ErrorType {
    FATAL,
    NONFATAL
};

char *util_get_app_dir();
int dir_exists(const char *path);

void util_info(const char *msg);

void util_error(const char *msg);
void util_error_dialog(GtkWindow *parent, const char *msg, enum ErrorType error_type, GtkApplication *app);
void util_fatal(const char *msg);
void util_nonfatal(const char *msg);
bool util_check_ptr(void *ptr, const char *msg);