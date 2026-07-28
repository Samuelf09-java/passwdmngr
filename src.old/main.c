#include <gtk/gtk.h>
#include "app.h"
#include "ui.h"

static void on_activate(GtkApplication *gtk_app, gpointer user_data) {
    AppState *app = user_data;
    app_init(app, gtk_app);

    GtkWidget *login = ui_login_screen(app);

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "Password Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

    gtk_window_set_child(GTK_WINDOW(window), login);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),
                                    gtk_label_new("Login"));
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    AppState app_state;

    GtkApplication *app = gtk_application_new("com.sjfield09.passwdmngr",
                                              G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &app_state);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
