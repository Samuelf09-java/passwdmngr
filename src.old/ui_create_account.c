#include <gtk/gtk.h>
#include "ui.h"
#include "backend.h"

typedef struct {
    AppState *app;
    GtkEntry *username;
    GtkEntry *password;
    GtkEntry *confirm;
} CreateWidgets;

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

static void on_create_clicked(GtkButton *btn, gpointer user_data) {
    CreateWidgets *w = user_data;

    const char *username = gtk_editable_get_text(GTK_EDITABLE(w->username));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(w->password));
    const char *confirm  = gtk_editable_get_text(GTK_EDITABLE(w->confirm));

    GtkWindow *root = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));

    if (strcmp(password, confirm) != 0) {
        show_error(root, "Passwords do not match.");
        return;
    }

    if (!backend_create_account(username, password)) {
        show_error(root, "Failed to create account.");
        return;
    }

    // Success → return to login screen
    GtkWidget *login = ui_login_screen(w->app);
    gtk_window_set_child(root, login);
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
                                    gtk_label_new("Login"));
    gtk_window_set_titlebar(root, header);

}

static void on_back_clicked(GtkButton *btn, gpointer user_data) {
    AppState *app = user_data;
    GtkWindow *root = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));

    GtkWidget *login = ui_login_screen(app);
    gtk_window_set_child(root, login);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
                                    gtk_label_new("Login"));
    gtk_window_set_titlebar(root, header);
}

GtkWidget *ui_create_account_screen(AppState *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 40);
    gtk_widget_set_margin_bottom(box, 40);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);

    GtkWidget *title = gtk_label_new("<big><b>Create Account</b></big>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget *username = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username), "Username");
    gtk_box_append(GTK_BOX(box), username);

    GtkWidget *password = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(password), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_append(GTK_BOX(box), password);

    GtkWidget *confirm = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirm), "Confirm Password");
    gtk_entry_set_visibility(GTK_ENTRY(confirm), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(confirm), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_append(GTK_BOX(box), confirm);

    GtkWidget *create_btn = gtk_button_new_with_label("Create Account");
    gtk_box_append(GTK_BOX(box), create_btn);

    GtkWidget *back_btn = gtk_button_new_with_label("Back to Login");
    gtk_box_append(GTK_BOX(box), back_btn);

    CreateWidgets *w = g_new0(CreateWidgets, 1);
    w->app = app;
    w->username = GTK_ENTRY(username);
    w->password = GTK_ENTRY(password);
    w->confirm  = GTK_ENTRY(confirm);

    g_signal_connect_data(create_btn, "clicked", G_CALLBACK(on_create_clicked), w, NULL, 0);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), app);

    return box;
}
