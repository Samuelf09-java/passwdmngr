#include "login_window.h"

struct _LoginWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *logo;
    GtkWidget *uname_entry;
    GtkWidget *passwd_entry;
    GtkWidget *login_button;
};

G_DEFINE_FINAL_TYPE(LoginWindow, login_window, GTK_TYPE_APPLICATION_WINDOW)

static void on_login_clicked(GtkButton *button, LoginWindow *self) {
    const char *uname = gtk_editable_get_text(GTK_EDITABLE(self->uname_entry));
    const char *passwd = gtk_editable_get_text(GTK_EDITABLE(self->passwd_entry));

    g_print("uname: %s, passwd: %s\n", uname, passwd);
}

static void on_account_button_clicked(GtkButton *button, LoginWindow *self) {
    g_print("*create account*\n");
}

static void login_window_class_init(LoginWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/login_window.ui"
    );

    gtk_widget_class_bind_template_child(widget_class, LoginWindow, logo);
    gtk_widget_class_bind_template_child(widget_class, LoginWindow, uname_entry);
    gtk_widget_class_bind_template_child(widget_class, LoginWindow, passwd_entry);
    gtk_widget_class_bind_template_child(widget_class, LoginWindow, login_button);

    gtk_widget_class_bind_template_callback(widget_class, on_login_clicked);
    gtk_widget_class_bind_template_callback(widget_class, on_account_button_clicked);
}

static void login_window_init(LoginWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}
