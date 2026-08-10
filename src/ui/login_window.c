#include "ui/login_window.h"
#include "ui/main_window.h"
#include "util.h"
#include "crypto.h"
#include "main.h"
#include "ui/account_creation_window.h"
#include "storage.h"

G_DEFINE_FINAL_TYPE(LoginWindow, login_window, GTK_TYPE_BOX)

static void on_login_clicked(GtkButton *button, LoginWindow *self) {
    X(button);
    const char *uname = gtk_editable_get_text(GTK_EDITABLE(self->uname_entry));
    const char *passwd = gtk_editable_get_text(GTK_EDITABLE(self->passwd_entry));

    if (verify_account(uname, passwd)) {
        username = strdup(uname);
        tmp_passwd = strdup(passwd);

        MainWindow *mainwin = g_object_new(MAIN_WINDOW_TYPE, NULL);
        gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(mainwin));

        util_log(INFO, "Login successful");
    } else {
        util_nonfatal_d("Invalid username or password");
    }
}

static void on_account_button_clicked(GtkButton *button, LoginWindow *self) {
    X(button);
    X(self);
    AccountCreationWindow *create_account_win = g_object_new(ACCOUNT_CREATION_WINDOW_TYPE, NULL);
    gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(create_account_win));
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

    gtk_window_set_title(GTK_WINDOW(root_window), "Password Manager - Login");
}
