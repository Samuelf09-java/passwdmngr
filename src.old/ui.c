#include <gtk/gtk.h>
#include "ui.h"
#include "app.h"
#include "backend.h"

typedef struct {
    AppState *app;
    GtkEntry *username_entry;
    GtkEntry *password_entry;
} LoginWidgets;

static void on_error_response(GtkDialog *dialog, int response_id, gpointer user_data) {
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void show_error(GtkWindow *parent, const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s",
        msg
    );

    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_error_response), NULL);

    gtk_window_present(GTK_WINDOW(dialog));
}


static void on_show_password_toggled(GtkCheckButton *check, gpointer user_data) {
    LoginWidgets *w = user_data;
    gboolean active = gtk_check_button_get_active(check);

    // UNIVERSAL GTK4 fallback: works on all versions
    gtk_entry_set_visibility(GTK_ENTRY(w->password_entry), active);
}

static void on_login_clicked(GtkButton *btn, gpointer user_data) {
    LoginWidgets *w = user_data;

    const char *username = gtk_editable_get_text(GTK_EDITABLE(w->username_entry));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(w->password_entry));

    GtkWindow *root = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));

    if (username[0] == '\0' || password[0] == '\0') {
        show_error(root, "Please enter both username and password.");
        return;
    }

    if (!backend_attempt_login(w->app, username, password)) {
        show_error(root, "Invalid username or password.");
        return;
    }

    GtkWidget *vault = ui_vault_screen(w->app);
    gtk_window_set_child(root, vault);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
                                    gtk_label_new("Vault"));
    gtk_window_set_titlebar(root, header);
}

static void on_create_account_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = user_data;
    GtkWindow *root = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));

    GtkWidget *create = ui_create_account_screen(app);
    gtk_window_set_child(root, create);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
                                    gtk_label_new("Create Account"));
    gtk_window_set_titlebar(root, header);
}

GtkWidget *ui_login_screen(AppState *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 40);
    gtk_widget_set_margin_bottom(box, 40);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);

    GtkWidget *title = gtk_label_new("<big><b>Password Manager</b></big>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *username = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username), "Username");
    gtk_box_append(GTK_BOX(box), username);

    GtkWidget *password = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(password), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);  // start hidden
    gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_append(GTK_BOX(box), password);

    GtkWidget *show_pw = gtk_check_button_new_with_label("Show password");
    gtk_box_append(GTK_BOX(box), show_pw);

    GtkWidget *login_btn = gtk_button_new_with_label("Login");
    gtk_box_append(GTK_BOX(box), login_btn);

    GtkWidget *create_btn = gtk_button_new_with_label("Create Account");
    gtk_box_append(GTK_BOX(box), create_btn);

    LoginWidgets *w = g_new0(LoginWidgets, 1);
    w->app = app;
    w->username_entry = GTK_ENTRY(username);
    w->password_entry = GTK_ENTRY(password);

    g_signal_connect_data(show_pw, "toggled", G_CALLBACK(on_show_password_toggled), w, NULL, 0);
    g_signal_connect_data(login_btn, "clicked", G_CALLBACK(on_login_clicked), w, NULL, 0);
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_account_clicked), app);

    return box;
}
