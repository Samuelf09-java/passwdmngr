#pragma once
#include <gtk/gtk.h>

extern const char PATH_SEPARATOR;

// Use with `GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);` in
#define REGISTER_CALLBACK(cb) gtk_widget_class_bind_template_callback(widget_class, cb)
#define REGISTER_CHILD(type, name) gtk_widget_class_bind_template_child(widget_class, type, name)

char *util_get_app_dir();
int dir_exists(const char *path);

void util_error(const char *msg);
void util_fatal_custom(GtkWindow *parent, const char *msg, GtkApplication *app);
void util_fatal(const char *msg);
bool util_check_ptr(void *ptr, const char *msg);