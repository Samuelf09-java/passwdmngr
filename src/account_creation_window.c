#include "account_creation_window.h"
#include "util.h"
#include "main.h"
#include "main_window.h"
#include "login_window.h"
#include "storage.h"

struct _AccountCreationWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *logo;
    GtkWidget *uname_entry;
    GtkWidget *passwd_entry;
    GtkWidget *confirm_passwd_entry;
};

G_DEFINE_FINAL_TYPE(AccountCreationWindow, account_creation_window, GTK_TYPE_APPLICATION_WINDOW)

AccountCreationWindow *account_creation_window_new(GtkApplication *app) {
    return g_object_new(ACCOUNT_CREATION_WINDOW_TYPE, "application", app, NULL);
}

static void on_create_account_clicked(GtkButton *button, AccountCreationWindow *self) {
    const char *uname = gtk_editable_get_text(GTK_EDITABLE(self->uname_entry));
    const char *passwd = gtk_editable_get_text(GTK_EDITABLE(self->passwd_entry));
    const char *confirm_passwd = gtk_editable_get_text(GTK_EDITABLE(self->confirm_passwd_entry));

    if (strlen(uname) == 0 || strlen(passwd) == 0 || strlen(confirm_passwd) == 0) {
        util_nonfatal("Could not create account: fields cannot be blank");
        return;
    }

    if (!strcmp(passwd, confirm_passwd)) {
        bool res = create_new_account(uname, passwd);
        if (!res) util_nonfatal("Could not create account; check stderr for more information");
        else {
            MainWindow *mainwin = main_window_new(passwdmngr);
            current_window = GTK_WINDOW(mainwin);
            gtk_window_present(GTK_WINDOW(mainwin));
            gtk_window_destroy(GTK_WINDOW(self));
        }
    } else {
        util_nonfatal("Could not create account: passwords do not match");
    }
}

static void on_cancel_clicked(GtkButton *button, AccountCreationWindow *self) {
    LoginWindow *loginwin = login_window_new(passwdmngr);
    current_window = GTK_WINDOW(loginwin);
    gtk_window_present(GTK_WINDOW(loginwin));
    gtk_window_destroy(GTK_WINDOW(self));
}

static void account_creation_window_class_init(AccountCreationWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/account_creation_window.ui"
    );

    REGISTER_CHILD(AccountCreationWindow, uname_entry);
    REGISTER_CHILD(AccountCreationWindow, passwd_entry);
    REGISTER_CHILD(AccountCreationWindow, confirm_passwd_entry);

    REGISTER_CALLBACK(on_create_account_clicked);
    REGISTER_CALLBACK(on_cancel_clicked);
}

static void account_creation_window_init(AccountCreationWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}
