#include "main_window.h"
#include "util.h"
#include "storage.h"

struct _MainWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *menubar_box;
};

G_DEFINE_FINAL_TYPE(MainWindow, main_window, GTK_TYPE_APPLICATION_WINDOW)

MainWindow *main_window_new(GtkApplication *app) {
    return g_object_new(MAIN_WINDOW_TYPE, "application", app, NULL);
}

static void main_window_class_init(MainWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/main_window.ui"
    );

    REGISTER_CHILD(MainWindow, menubar_box);
}

static void main_window_init(MainWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    GtkBuilder *builder = gtk_builder_new_from_resource(
        "/com/samuelf09/passwdmngr/main_menu.ui"
    );

    GMenuModel *menu = G_MENU_MODEL(gtk_builder_get_object(builder, "main_menu"));
    GtkWidget *menubar = gtk_popover_menu_bar_new_from_model(menu);
    gtk_box_append(GTK_BOX(self->menubar_box), menubar);

    g_object_unref(builder);

    char *title = malloc(strlen("Password Manager - ") + strlen(username) + 1);
    if (!title) util_error("Window title malloc failed");
    sprintf(title, "Password Manager - %s", username);
    gtk_window_set_title(GTK_WINDOW(self), title);
    free(title);

    Metadata *md = storage_read_user_metadata();
    if (!md) util_fatal("Could not read user's metadata");
    
    if (md->version != STORAGE_SCHEMA_VERSION) util_fatal("Invalid storage schema version; update with porting tool if applicable");

    if (!storage_read_user_vault(md)) util_fatal("Failed to read user vault; check stderr for more information");   
}