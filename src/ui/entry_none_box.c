#include "ui/entry_none_box.h"

G_DEFINE_FINAL_TYPE(EntryNoneBox, entry_none_box, GTK_TYPE_BOX)

static void entry_none_box_class_init(EntryNoneBoxClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/entry_none_box.ui"
    );
}

static void entry_none_box_init(EntryNoneBox *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}