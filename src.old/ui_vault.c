#include <gtk/gtk.h>
#include "ui.h"
#include "app.h"

typedef struct {
    AppState *app;
    GtkListView *list;
    GtkWidget *details_box;
    GtkSelectionModel *selection;
} VaultWidgets;

/* -----------------------------
   Update details panel
----------------------------- */
static void update_details_panel(VaultWidgets *vw, const char *service_name) {
    gtk_widget_set_visible(vw->details_box, TRUE);

    GtkWidget *child = gtk_widget_get_first_child(vw->details_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
                         g_strdup_printf("<big><b>%s</b></big>", service_name));
    gtk_box_append(GTK_BOX(vw->details_box), title);

    gtk_box_append(GTK_BOX(vw->details_box),
                   gtk_label_new("Username: example_user"));
    gtk_box_append(GTK_BOX(vw->details_box),
                   gtk_label_new("Password: ********"));

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(vw->details_box), row);

    gtk_box_append(GTK_BOX(row), gtk_button_new_with_label("Copy Username"));
    gtk_box_append(GTK_BOX(row), gtk_button_new_with_label("Copy Password"));
    gtk_box_append(GTK_BOX(row), gtk_button_new_with_label("Delete Entry"));
}

/* -----------------------------
   Selection changed
----------------------------- */
static void on_selection_changed(GtkSelectionModel *model,
                                 guint pos,
                                 guint n_items,
                                 gpointer user_data)
{
    VaultWidgets *vw = user_data;

    guint selected = gtk_single_selection_get_selected(
        GTK_SINGLE_SELECTION(model)
    );

    if (selected == GTK_INVALID_LIST_POSITION)
        return;

    GtkStringList *sl = GTK_STRING_LIST(
        gtk_single_selection_get_model(GTK_SINGLE_SELECTION(model))
    );

    const char *service = gtk_string_list_get_string(sl, selected);
    update_details_panel(vw, service);
}

/* -----------------------------
   Factory setup
----------------------------- */
static void factory_setup(GtkListItemFactory *factory,
                          GtkListItem *item,
                          gpointer user_data)
{
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_list_item_set_child(item, lbl);
}

/* -----------------------------
   Factory bind
----------------------------- */
static void factory_bind(GtkListItemFactory *factory,
                         GtkListItem *item,
                         gpointer user_data)
{
    GtkWidget *lbl = gtk_list_item_get_child(item);

    // The item is a GObject containing the string
    GObject *obj = gtk_list_item_get_item(item);
    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(obj));

    gtk_label_set_text(GTK_LABEL(lbl), text);
}


/* -----------------------------
   Build vault screen
----------------------------- */
GtkWidget *ui_vault_screen(AppState *app) {
    VaultWidgets *vw = g_new0(VaultWidgets, 1);
    vw->app = app;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Paned layout */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(root), paned);

    /* Left list */
    const char *items[] = { "GitHub", "Discord", "ProtonMail", "Steam", NULL };
    GtkStringList *model = gtk_string_list_new(items);

    vw->selection = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(model))
    );

    /* Use GtkBuilderListItemFactory (works on GTK 4.0+) */
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), NULL);
    g_signal_connect(factory, "bind",  G_CALLBACK(factory_bind),  NULL);

    vw->list = GTK_LIST_VIEW(gtk_list_view_new(vw->selection, factory));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
    GTK_WIDGET(vw->list));
    gtk_paned_set_start_child(GTK_PANED(paned), scroll);
    gtk_widget_set_size_request(scroll, 200, 300);

    /* Right details panel */
    vw->details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vw->details_box, 20);
    gtk_widget_set_margin_start(vw->details_box, 20);

    gtk_widget_set_size_request(vw->details_box, 300, -1);

    // Placeholder message shown before selection
    GtkWidget *placeholder = gtk_label_new("Select an entry to view details");
    gtk_box_append(GTK_BOX(vw->details_box), placeholder);

    gtk_paned_set_end_child(GTK_PANED(paned), vw->details_box);

    /* Connect selection */
    g_signal_connect(vw->selection, "selection-changed",
                     G_CALLBACK(on_selection_changed), vw);

    return root;
}
