#include "ui/entry_view_box.h"
#include "util.h"

G_DEFINE_FINAL_TYPE(EntryViewBox, entry_view_box, GTK_TYPE_BOX)

typedef struct HoverData {
    GtkWidget *button;
    GtkWidget *notes_label;
} HoverData;

enum { EDIT, DELETE, N_SIGNALS };

static guint entry_view_box_signals[N_SIGNALS];

static void on_edit_clicked(GtkButton *button, EntryViewBox *self) {
    X(button);
    g_signal_emit(self, entry_view_box_signals[EDIT], 0);
}

static void on_delete_clicked(GtkButton *button, EntryViewBox *self) {
    X(button);
    g_signal_emit(self, entry_view_box_signals[DELETE], 0);
}

static void on_service_copy_clicked(GtkButton *button, gpointer user_data) {
    GtkLabel   *label = GTK_LABEL(user_data);
    const char *text  = gtk_label_get_text(label);

    GdkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(button));
    gdk_clipboard_set_text(clip, text);
}

static void on_username_copy_clicked(GtkButton *button, gpointer user_data) {
    GtkLabel   *label = GTK_LABEL(user_data);
    const char *text  = gtk_label_get_text(label);

    GdkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(button));
    gdk_clipboard_set_text(clip, text + strlen("Username: "));
}

static void on_password_copy_clicked(GtkButton *button, gpointer user_data) {
    GtkLabel   *label = GTK_LABEL(user_data);
    const char *text  = gtk_label_get_text(label);

    GdkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(button));
    gdk_clipboard_set_text(clip, text + strlen("Password: "));
}

static void on_notes_copy_clicked(GtkButton *button, gpointer user_data) {
    GtkLabel   *label = GTK_LABEL(user_data);
    const char *text  = gtk_label_get_text(label);

    GdkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(button));
    gdk_clipboard_set_text(clip, text);
}

static void on_container_enter(GtkEventControllerMotion *motion, double x, double y, gpointer user_data) {
    X(motion);
    X(x);
    X(y);
    HoverData *data = (HoverData *)user_data;
    if (data->notes_label) {
        const char *notes = gtk_label_get_text(GTK_LABEL(data->notes_label));
        if (strlen(notes))
            gtk_widget_set_visible(data->button, true);
    } else
        gtk_widget_set_visible(data->button, true);
}

static void on_container_leave(GtkEventControllerMotion *motion, gpointer user_data) {
    X(motion);
    gtk_widget_set_visible(GTK_WIDGET(((HoverData *)user_data)->button), false);
}

static void entry_view_box_class_init(EntryViewBoxClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_class, "/com/samuelf09/passwdmngr/entry_view_box.ui");

    REGISTER_CHILD(EntryViewBox, service_box);
    REGISTER_CHILD(EntryViewBox, service_label);
    REGISTER_CHILD(EntryViewBox, service_copy_button);
    REGISTER_CHILD(EntryViewBox, username_box);
    REGISTER_CHILD(EntryViewBox, username_label);
    REGISTER_CHILD(EntryViewBox, username_copy_button);
    REGISTER_CHILD(EntryViewBox, password_box);
    REGISTER_CHILD(EntryViewBox, password_label);
    REGISTER_CHILD(EntryViewBox, password_copy_button);
    REGISTER_CHILD(EntryViewBox, notes_title);
    REGISTER_CHILD(EntryViewBox, notes_container);
    REGISTER_CHILD(EntryViewBox, notes_label);
    REGISTER_CHILD(EntryViewBox, notes_copy_button);

    REGISTER_CALLBACK(on_edit_clicked);
    REGISTER_CALLBACK(on_delete_clicked);

    entry_view_box_signals[EDIT] =
        g_signal_new("edit", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

    entry_view_box_signals[DELETE] =
        g_signal_new("delete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void entry_view_box_init(EntryViewBox *self) {
    gtk_widget_init_template(GTK_WIDGET(self));

    GtkEventController *service_motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(self->service_box, service_motion);
    HoverData *service_data = g_new0(HoverData, 1);
    service_data->button    = self->service_copy_button;
    g_signal_connect(service_motion, "enter", G_CALLBACK(on_container_enter), service_data);
    g_signal_connect(service_motion, "leave", G_CALLBACK(on_container_leave), service_data);

    GtkEventController *username_motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(self->username_box, username_motion);
    HoverData *username_data = g_new0(HoverData, 1);
    username_data->button    = self->username_copy_button;
    g_signal_connect(username_motion, "enter", G_CALLBACK(on_container_enter), username_data);
    g_signal_connect(username_motion, "leave", G_CALLBACK(on_container_leave), username_data);

    GtkEventController *password_motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(self->password_box, password_motion);
    HoverData *password_data = g_new0(HoverData, 1);
    password_data->button    = self->password_copy_button;
    g_signal_connect(password_motion, "enter", G_CALLBACK(on_container_enter), password_data);
    g_signal_connect(password_motion, "leave", G_CALLBACK(on_container_leave), password_data);

    GtkEventController *notes_motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(self->notes_container, notes_motion);
    HoverData *notes_data   = g_new0(HoverData, 1);
    notes_data->button      = self->notes_copy_button;
    notes_data->notes_label = self->notes_label;
    g_signal_connect(notes_motion, "enter", G_CALLBACK(on_container_enter), notes_data);
    g_signal_connect(notes_motion, "leave", G_CALLBACK(on_container_leave), notes_data);

    g_signal_connect(self->service_copy_button, "clicked", G_CALLBACK(on_service_copy_clicked), self->service_label);
    g_signal_connect(self->username_copy_button, "clicked", G_CALLBACK(on_username_copy_clicked), self->username_label);
    g_signal_connect(self->password_copy_button, "clicked", G_CALLBACK(on_password_copy_clicked), self->password_label);
    g_signal_connect(self->notes_copy_button, "clicked", G_CALLBACK(on_notes_copy_clicked), self->notes_label);
}