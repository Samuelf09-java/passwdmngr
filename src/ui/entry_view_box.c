#include "ui/entry_view_box.h"
#include "util.h"

G_DEFINE_FINAL_TYPE(EntryViewBox, entry_view_box, GTK_TYPE_BOX)

enum {
    EDIT,
    DELETE,
    N_SIGNALS
};

static guint entry_view_box_signals[N_SIGNALS];

static void on_edit_clicked(GtkButton *button, EntryViewBox *self) {
    g_signal_emit(self, entry_view_box_signals[EDIT], 0);
}

static void on_delete_clicked(GtkButton *button, EntryViewBox *self) {
    g_signal_emit(self, entry_view_box_signals[DELETE], 0);
}

static void entry_view_box_class_init(EntryViewBoxClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/entry_view_box.ui"
    );

    REGISTER_CHILD(EntryViewBox, service_label);
    REGISTER_CHILD(EntryViewBox, username_label);
    REGISTER_CHILD(EntryViewBox, password_label);
    REGISTER_CHILD(EntryViewBox, notes_label);
    REGISTER_CHILD(EntryViewBox, notes_title);

    REGISTER_CALLBACK(on_edit_clicked);
    REGISTER_CALLBACK(on_delete_clicked);

    entry_view_box_signals[EDIT] =
        g_signal_new("edit",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);

    entry_view_box_signals[DELETE] =
        g_signal_new("delete",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);
}

static void entry_view_box_init(EntryViewBox *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}