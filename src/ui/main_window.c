#include <string.h>
#include "ui/main_window.h"
#include "ui/login_window.h"
#include "util.h"
#include "storage.h"
#include "main.h"

G_DEFINE_FINAL_TYPE(MainWindow, main_window, GTK_TYPE_BOX)

static void on_edit_save(EntryEditBox *edit_box, MainWindow *self);
static void on_edit_cancel(EntryEditBox *edit_box, MainWindow *self);

static void reload_sidebar(MainWindow *self) {
    gtk_list_box_remove_all(GTK_LIST_BOX(self->entries_listbox));

    for (int i = 0; i < num_entries; i++) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_list_box_row_new());
        GtkWidget *label = gtk_label_new(entries[i].service);
        gtk_list_box_row_set_child(row, label);
        gtk_list_box_append(GTK_LIST_BOX(self->entries_listbox), GTK_WIDGET(row));

        int *id_ptr = g_new(int, 1);
        *id_ptr = entries[i].id;

        g_object_set_data_full(G_OBJECT(row), "entry-id", id_ptr, g_free);
    }
}

static void box_remove_children(GtkBox *box) {
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(box));
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(box, child);
        child = next;
    }
}

static void on_delete_response(GObject *source, GAsyncResult *result, gpointer user_data) {

    X(source);

    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(user_data);
    int response = gtk_alert_dialog_choose_finish(dialog, result, NULL);

    if (response != 1) { // cancel
        g_object_unref(dialog);
        return;
    }

    int entry_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "entry-id"));
    MainWindow *self = g_object_get_data(G_OBJECT(dialog), "main-window");

    g_object_unref(dialog);

    if (!delete_entry(entry_id)) {
        util_nonfatal_d("Failed to delete entry; check stderr for more information");
        return;
    }

    reload_sidebar(self);

    box_remove_children(GTK_BOX(self->content_area));
    GtkWidget *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
    gtk_box_append(GTK_BOX(self->content_area), none_box);
}

static void on_view_edit(EntryViewBox *view_box, MainWindow *self) {

    gtk_widget_set_sensitive(GTK_WIDGET(self->add_entry_button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(self->entries_listbox), false);
    
    box_remove_children(GTK_BOX(self->content_area));

    EntryEditBox *edit_box = g_object_new(ENTRY_EDIT_BOX_TYPE, NULL);
    g_signal_connect(edit_box, "save",   G_CALLBACK(on_edit_save),   self);
    g_signal_connect(edit_box, "cancel", G_CALLBACK(on_edit_cancel), self);
    edit_box->edit_mode = ENTRY_EDIT;
    edit_box->entry_id = view_box->entry_id;
    PasswdEntry *entry = storage_get_entry(view_box->entry_id);
    if (!entry) {
        util_nonfatal_d("Failed to open edit menu: entry lookup failed");
        return;
    }
    
    gtk_editable_set_text(GTK_EDITABLE(edit_box->service_entry), entry->service);
    gtk_editable_set_text(GTK_EDITABLE(edit_box->username_entry), entry->username);
    gtk_editable_set_text(GTK_EDITABLE(edit_box->password_entry), entry->password);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(edit_box->notes_text));
    gtk_text_buffer_set_text(buffer, entry->notes, -1);

    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(edit_box));
}

static void on_view_delete(EntryViewBox *view_box, MainWindow *self) {

    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Are you sure you want to delete this entry?\n"
        "This operation is permanent and cannot be undone."
    );

    const char *buttons[] = { "Cancel", "Delete", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);

    g_object_set_data(G_OBJECT(dialog), "entry-id", GINT_TO_POINTER(view_box->entry_id));
    g_object_set_data(G_OBJECT(dialog), "main-window", self);

    gtk_alert_dialog_choose(dialog, root_window, NULL, on_delete_response, dialog);
}

static void on_edit_save(EntryEditBox *edit_box, MainWindow *self) {

    char *service = strdup(gtk_editable_get_text(GTK_EDITABLE(edit_box->service_entry)));
    if (!strlen(service)) {
        util_nonfatal_d("Missing required field 'service'");
        free(service);
        return;
    }
    char *new_username = strdup(gtk_editable_get_text(GTK_EDITABLE(edit_box->username_entry)));
    if (!strlen(new_username)) util_warn_d("Field 'new_username' is missing, continuing");
    char *password = strdup(gtk_editable_get_text(GTK_EDITABLE(edit_box->password_entry)));
    if (!strlen(password)) util_warn_d("Field 'password' is missing, continuing");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(edit_box->notes_text));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *notes = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    PasswdEntry *new_entry = malloc(sizeof(PasswdEntry));
    if (!new_entry) {
        util_nonfatal_d("Failed to allocate memory for temporary new PasswdEntry");
        free(service);
        free(new_username);
        free(password);
        return;
    }

    new_entry->id = edit_box->entry_id;
    new_entry->service = service;
    new_entry->username = new_username;
    new_entry->password = password;
    new_entry->notes = notes;

    if (edit_box->edit_mode == ENTRY_ADD) {
        if (!add_entry(new_entry)) {
            util_nonfatal_d("Failed to add entry; check stderr for more information");
            free(new_entry);
            free(service);
            free(new_username);
            free(password);
            return;
        }

        free(new_entry);
        free(service);
        free(new_username);
        free(password);
    } else if (edit_box->edit_mode == ENTRY_EDIT) {
        if (!update_entry(edit_box->entry_id, new_entry)) {
            util_nonfatal_d("Failed to update entry; check stderr for more information");
            free(new_entry);
            free(service);
            free(new_username);
            free(password);
            return;
        }

        free(new_entry);
        free(service);
        free(new_username);
        free(password);
    } else {
        util_fatal_d("Invalid edit mode!");
        free(new_entry);
        free(service);
        free(new_username);
        free(password);
        return;
    }

    reload_sidebar(self);

    box_remove_children(GTK_BOX(self->content_area));
    if (edit_box->edit_mode == ENTRY_ADD) {

        GtkWidget *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
        gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(none_box));

    } else if (edit_box->edit_mode == ENTRY_EDIT) {

        GtkListBox *listbox = GTK_LIST_BOX(self->entries_listbox);
        int target_id = edit_box->entry_id;

        GtkListBoxRow *row = NULL;

        for (int i = 0; i < num_entries; i++) {
            GtkListBoxRow *candidate = gtk_list_box_get_row_at_index(listbox, i);
            if (!candidate) continue;

            int *id_ptr = g_object_get_data(G_OBJECT(candidate), "entry-id");
            if (id_ptr && *id_ptr == target_id) {
                row = candidate;
                break;
            }
        }

        if (row) {
            gtk_list_box_select_row(listbox, row);
        } else {
            util_nonfatal_d("Failed to select edited entry");
        }
        
    } else {
        util_fatal_d("Invalid edit mode!");
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(self->add_entry_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(self->entries_listbox), true);
}

static void on_edit_cancel(EntryEditBox *edit_box, MainWindow *self) {

    X(edit_box);

    box_remove_children(GTK_BOX(self->content_area));
    GtkWidget *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(none_box));
    gtk_widget_set_sensitive(GTK_WIDGET(self->add_entry_button), true);
    gtk_widget_set_sensitive(GTK_WIDGET(self->entries_listbox), true);
}

static void on_entry_selected(GtkListBox *box, GtkListBoxRow *row, MainWindow *self) {

    X(box);
    
    box_remove_children(GTK_BOX(self->content_area));

    if (!row) {
        EntryNoneBox *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
        gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(none_box));
        return;
    }

    int *id_ptr = g_object_get_data(G_OBJECT(row), "entry-id");
    int id = *id_ptr;
    PasswdEntry *e = storage_get_entry(id);
    if (!e) {
        util_nonfatal_d("Failed to load entry data; see stderr");
        return;
    }

    EntryViewBox *view_box = g_object_new(ENTRY_VIEW_BOX_TYPE, NULL);
    view_box->entry_id = id;
    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(view_box));

    g_signal_connect(view_box, "edit",   G_CALLBACK(on_view_edit),   self);
    g_signal_connect(view_box, "delete", G_CALLBACK(on_view_delete), self);

    char *service = e->service;
    char *username = malloc(strlen("Username: ") + strlen(e->username) + 1);
    sprintf(username, "Username: %s", e->username);
    char *password = malloc(strlen("Password: ") + strlen(e->password) + 1);
    sprintf(password, "Password: %s", e->password);
    char *notes = e->notes;

    gtk_label_set_text(GTK_LABEL(view_box->service_label), service);
    gtk_label_set_text(GTK_LABEL(view_box->username_label), username);
    gtk_label_set_text(GTK_LABEL(view_box->password_label), password);
    gtk_label_set_text(GTK_LABEL(view_box->notes_label), notes);
    if (strlen(notes) == 0) gtk_label_set_text(GTK_LABEL(view_box->notes_title), "");

    free(username);
    free(password);
}

static void on_add_entry_clicked(GtkButton *button, MainWindow *self) {
    gtk_list_box_unselect_all(GTK_LIST_BOX(self->entries_listbox));
    gtk_widget_set_sensitive(GTK_WIDGET(button), false);
    gtk_widget_set_sensitive(GTK_WIDGET(self->entries_listbox), false);
    
    box_remove_children(GTK_BOX(self->content_area));

    EntryEditBox *edit_box = g_object_new(ENTRY_EDIT_BOX_TYPE, NULL);
    g_signal_connect(edit_box, "save",   G_CALLBACK(on_edit_save),   self);
    g_signal_connect(edit_box, "cancel", G_CALLBACK(on_edit_cancel), self);
    edit_box->edit_mode = ENTRY_ADD;
    edit_box->entry_id = storage_assign_new_id();

    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(edit_box));
}

static void on_clearlog_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    X(source);

    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(user_data);
    int response = gtk_alert_dialog_choose_finish(dialog, result, NULL);

    if (response != 1) { // cancel
        g_object_unref(dialog);
        return;
    }

    FILE *fp = fopen(util_get_logfile(), "w");
    if (fp) {
        fclose(fp);
        util_log(INFO, "Logfile cleared");
        return;
    }

    util_nonfatal_d("Failed to clear logfile: util returned null file pointer");
}

static void export_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Export triggered");
}

static void import_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Import triggered");
}

static void openlog_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Open log triggered");
}

static void clearlog_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Clear log triggered");
    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Are you sure you want to clear the log file?\n"
        "This operation is permanent and cannot be undone."
    );

    const char *buttons[] = { "Cancel", "Clear", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);

    gtk_alert_dialog_choose(dialog, root_window, NULL, on_clearlog_response, dialog);
}

static void logout_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    util_log(DEBUG, "Logout triggered");

    util_log(INFO, "Logging out user %s", username);

    MainWindow *self = MAIN_WINDOW(user_data);

    GtkWidget *menubar = gtk_widget_get_first_child(GTK_WIDGET(self->menubar_box));
    if (menubar)
        gtk_box_remove(GTK_BOX(self->menubar_box), menubar);

    // Wipe entries array (all the decrypted data from vault.bin)
    wipe_passwd_entries();

    // shouldn't be necessary but wipe password just in case wasn't already
    if (tmp_passwd) {
        wipe_mem(tmp_passwd, strlen(tmp_passwd));
        free(tmp_passwd);
        tmp_passwd = NULL;
    }

    wipe_mem(aes_key, sizeof(aes_key));
    key_set = false;

    free(username);
    username = NULL;

    util_log(DEBUG, "Cleared all user-specific globals from storage.c");
    
    // Unregister actions
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "logout");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "export");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "import");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "openlog");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "clearlog");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "changepasswd");
    g_action_map_remove_action(G_ACTION_MAP(passwdmngr), "deleteacc");

    // Disconnect signal handlers
    g_signal_handlers_disconnect_by_func(self->entries_listbox, on_entry_selected, self);
    g_signal_handlers_disconnect_by_func(self->add_entry_button, on_add_entry_clicked, self);
    g_signal_handlers_disconnect_by_func(self->content_area, on_view_edit, self);
    g_signal_handlers_disconnect_by_func(self->content_area, on_view_delete, self);
    g_signal_handlers_disconnect_by_func(self->content_area, on_edit_save, self);
    g_signal_handlers_disconnect_by_func(self->content_area, on_edit_cancel, self);

    util_log(DEBUG, "Disconnected signal handlers");

    LoginWindow *loginwin = g_object_new(LOGIN_WINDOW_TYPE, NULL);
    gtk_window_set_child(GTK_WINDOW(root_window), GTK_WIDGET(loginwin));

    util_log(DEBUG, "Successfully reset to login_window view");
}

static void changepasswd_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Change password triggered");
}

static void deleteacc_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Delete account triggered");
}

void register_actions(MainWindow *self) {
    const GActionEntry entries[] = {
        { "export",        export_cb,        NULL, NULL, NULL, {0} },
        { "import",        import_cb,        NULL, NULL, NULL, {0} },
        { "openlog",       openlog_cb,       NULL, NULL, NULL, {0} },
        { "clearlog",      clearlog_cb,      NULL, NULL, NULL, {0} },
        { "logout",        logout_cb,        NULL, NULL, NULL, {0} },
        { "changepasswd",  changepasswd_cb,  NULL, NULL, NULL, {0} },
        { "deleteacc",     deleteacc_cb,     NULL, NULL, NULL, {0} },
    };

    g_action_map_add_action_entries(G_ACTION_MAP(passwdmngr), entries, G_N_ELEMENTS(entries), self);
}

// wrapper for `g_idle_add((GSourceFunc)unselect_all_idle, self->entries_listbox);` to call
static gboolean unselect_all_idle(gpointer data) {
    gtk_list_box_unselect_all(GTK_LIST_BOX(data));
    return G_SOURCE_REMOVE;
}

static void main_window_class_init(MainWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/com/samuelf09/passwdmngr/main_window.ui"
    );

    REGISTER_CHILD(MainWindow, menubar_box);
    REGISTER_CHILD(MainWindow, entries_listbox);
    REGISTER_CHILD(MainWindow, content_area);
    REGISTER_CHILD(MainWindow, add_entry_button);

    REGISTER_CALLBACK(on_entry_selected);
    REGISTER_CALLBACK(on_add_entry_clicked);
    REGISTER_CALLBACK(on_edit_save);
    REGISTER_CALLBACK(on_edit_cancel);
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
    if (!title) util_log(ERROR, "Window title malloc failed");
    sprintf(title, "Password Manager - %s", username);
    gtk_window_set_title(GTK_WINDOW(root_window), title);
    free(title);

    Metadata *md = storage_read_user_metadata();
    if (!md) util_fatal_d("Could not read user's metadata");
    
    if (md->version != STORAGE_SCHEMA_VERSION) util_fatal_d("Invalid storage schema version; update with porting tool if applicable");

    if (!storage_read_user_vault(md)) util_fatal_d("Failed to read user vault; check stderr for more information");

    reload_sidebar(self);

    box_remove_children(GTK_BOX(self->content_area));
    GtkWidget *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(none_box));

    g_idle_add((GSourceFunc)unselect_all_idle, self->entries_listbox);
}