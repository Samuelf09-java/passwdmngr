#include "login_window.h"
#include "main_window.h"
#include "util.h"
#include "crypto.h"
#include "main.h"
#include "account_creation_window.h"
#include "storage.h"

struct _LoginWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *logo;
    GtkWidget *uname_entry;
    GtkWidget *passwd_entry;
    GtkWidget *login_button;
};

G_DEFINE_FINAL_TYPE(LoginWindow, login_window, GTK_TYPE_APPLICATION_WINDOW)

LoginWindow *login_window_new(GtkApplication *app) {
    return g_object_new(LOGIN_WINDOW_TYPE, "application", app, NULL);
}

static void on_login_clicked(GtkButton *button, LoginWindow *self) {
    X(button);
    const char *uname = gtk_editable_get_text(GTK_EDITABLE(self->uname_entry));
    const char *passwd = gtk_editable_get_text(GTK_EDITABLE(self->passwd_entry));

    if (verify_account(uname, passwd)) {
        username = strdup(uname);

        MainWindow *mainwin = main_window_new(passwdmngr);
        current_window = GTK_WINDOW(mainwin);
        gtk_window_present(GTK_WINDOW(mainwin));
        gtk_window_destroy(GTK_WINDOW(self));

        util_info("Login successful");
    } else {
        util_nonfatal("Invalid username or password");
    }
}

static void on_account_button_clicked(GtkButton *button, LoginWindow *self) {
    X(button);
    AccountCreationWindow *create_account_win = account_creation_window_new(passwdmngr);
    current_window = GTK_WINDOW(create_account_win);
    gtk_window_present(GTK_WINDOW(create_account_win));
    gtk_window_destroy(GTK_WINDOW(self));
}

static void login_window_class_init(LoginWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/login_window.ui"
    );

    REGISTER_CHILD(LoginWindow, logo);
    REGISTER_CHILD(LoginWindow, uname_entry);
    REGISTER_CHILD(LoginWindow, passwd_entry);
    REGISTER_CHILD(LoginWindow, login_button);

    REGISTER_CALLBACK(on_login_clicked);
    REGISTER_CALLBACK(on_account_button_clicked);
}

static void login_window_init(LoginWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}
