#include "ui/account_creation_window.h"
#include "main.h"
#include "storage.h"
#include "ui/login_window.h"
#include "ui/main_window.h"
#include "util.h"

G_DEFINE_FINAL_TYPE(AccountCreationWindow, account_creation_window, GTK_TYPE_BOX)

void on_remote_account_created(const int ret_val, const char *msg, void *args) {

    switch (ret_val) {
    case 0:
        // show 'ok' notification
        break;
    case -1:
        // show 'queue full' notification and retry later
        break;

    default:
        // show generic error
        break;
    }
}

static void on_create_account_clicked(GtkButton *button, AccountCreationWindow *self) {
    X(button);
    char *email          = strdup(gtk_editable_get_text(GTK_EDITABLE(self->email_entry)));
    char *uname          = strdup(gtk_editable_get_text(GTK_EDITABLE(self->uname_entry)));
    char *passwd         = strdup(gtk_editable_get_text(GTK_EDITABLE(self->passwd_entry)));
    char *confirm_passwd = strdup(gtk_editable_get_text(GTK_EDITABLE(self->confirm_passwd_entry)));
    bool  cloud_sync     = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->cloud_enabled_btn));

    if (strlen(uname) == 0 || strlen(passwd) == 0 || strlen(confirm_passwd) == 0) {
        util_nonfatal_d("Could not create account: fields cannot be blank");
        free(uname);
        free(passwd);
        free(confirm_passwd);
        return;
    }

    if (!strcmp(passwd, confirm_passwd)) {

        bool res = create_new_account(email, uname, passwd, cloud_sync, on_remote_account_created);
        if (!res) {
            util_nonfatal_d("Could not create account; check log for more information");
            free(uname);
            free(passwd);
            free(confirm_passwd);
        } else {
            MainWindow *mainwin = g_object_new(MAIN_WINDOW_TYPE, NULL);
            register_actions(mainwin);
            gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(mainwin));

            util_log(LOG_INFO, "New user %s: login successful", uname);

            free(uname);
            free(passwd);
            free(confirm_passwd);
        }
    } else {
        util_nonfatal_d("Could not create account: passwords do not match");
        free(uname);
        free(passwd);
        free(confirm_passwd);
    }
}

static void on_cancel_clicked(GtkButton *button, AccountCreationWindow *self) {
    X(button);
    X(self);
    LoginWindow *loginwin = g_object_new(LOGIN_WINDOW_TYPE, NULL);
    gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(loginwin));
}

static void on_cloud_enabled_toggled(GtkCheckButton *btn, AccountCreationWindow *self) {

    bool enabled = gtk_check_button_get_active(btn);
    if (enabled)
        gtk_widget_set_visible(self->email_entry, true);
    else
        gtk_widget_set_visible(self->email_entry, false);
}

static void account_creation_window_class_init(AccountCreationWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_class, "/com/samuelf09/passwdmngr/account_creation_window.ui");

    REGISTER_CHILD(AccountCreationWindow, email_entry);
    REGISTER_CHILD(AccountCreationWindow, uname_entry);
    REGISTER_CHILD(AccountCreationWindow, passwd_entry);
    REGISTER_CHILD(AccountCreationWindow, confirm_passwd_entry);
    REGISTER_CHILD(AccountCreationWindow, cloud_enabled_btn);

    REGISTER_CALLBACK(on_create_account_clicked);
    REGISTER_CALLBACK(on_cancel_clicked);
    REGISTER_CALLBACK(on_cloud_enabled_toggled);
}

static void account_creation_window_init(AccountCreationWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    gtk_window_set_title(GTK_WINDOW(root_window), "Password Manager - Create Account");
}
