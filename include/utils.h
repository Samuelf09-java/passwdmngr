#pragma once
#include <gtk/gtk.h>

// Use with `GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);` in
#define REGISTER_CALLBACK(cb) gtk_widget_class_bind_template_callback(widget_class, cb)
#define REGISTER_CHILD(type, name) gtk_widget_class_bind_template_child(widget_class, type, name)