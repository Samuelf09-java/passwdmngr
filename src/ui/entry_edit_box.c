#include "ui/entry_edit_box.h"
#include "ui/main_window.h"
#include "util.h"

G_DEFINE_FINAL_TYPE(EntryEditBox, entry_edit_box, GTK_TYPE_BOX)

enum {
    SIGNAL_SAVE,
    SIGNAL_CANCEL,
    N_SIGNALS
};

static guint entry_edit_box_signals[N_SIGNALS];

static void on_save_clicked(GtkButton *button, EntryEditBox *self) {
    X(button);
    g_signal_emit(self, entry_edit_box_signals[SIGNAL_SAVE], 0);
}

static void on_cancel_clicked(GtkButton *button, EntryEditBox *self) {
    X(button);
    g_signal_emit(self, entry_edit_box_signals[SIGNAL_CANCEL], 0);
}

static void entry_edit_box_class_init(EntryEditBoxClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/entry_edit_box.ui"
    );

    REGISTER_CHILD(EntryEditBox, service_entry);
    REGISTER_CHILD(EntryEditBox, username_entry);
    REGISTER_CHILD(EntryEditBox, password_entry);
    REGISTER_CHILD(EntryEditBox, notes_text);
    REGISTER_CHILD(EntryEditBox, save_button);
    REGISTER_CHILD(EntryEditBox, cancel_button);

    REGISTER_CALLBACK(on_save_clicked);
    REGISTER_CALLBACK(on_cancel_clicked);

    entry_edit_box_signals[SIGNAL_SAVE] =
        g_signal_new("save",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);

    entry_edit_box_signals[SIGNAL_CANCEL] =
        g_signal_new("cancel",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);
}

static void entry_edit_box_init(EntryEditBox *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}