#include <string.h>
#include "ui/main_window.h"
#include "ui/login_window.h"
#include "crypto.h"
#include "util.h"
#include "storage.h"
#include "main.h"

typedef struct _DeleteAccDialogData {
    GtkWidget *dialog;
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
} DeleteAccDialogData;

typedef struct _ChangePasswdDialogData {
    GtkWidget *dialog;
    GtkWidget *curr_pass_entry;
    GtkWidget *new_pass_entry;
    GtkWidget *conf_new_pass_entry;
} ChangePasswdDialogData;

typedef struct _ChooseEntriesDialogData {
    GtkWidget *dialog;
    GtkWidget *entries_box;
    PasswdEntry *import_entries;
    int num_import_entries;
} ChooseEntriesDialogData;

typedef struct _ImportPasswdDialogData {
    GtkWidget *dialog;
    GtkWidget *pass_entry;
    char *vault_path;
    VaultHeader *header;
    PasswdEntry *import_entries;
    int num_import_entries;
} ImportPasswdDialogData;

typedef struct _ImportModeDialogData {
    GtkWidget *dialog;
    GtkWidget *import_mode_dropdown;
    PasswdEntry *import_entries;
    int num_import_entries;
} ImportModeDialogData;

typedef struct _ImportContext {
    ImportModeDialogData *mode_data;
    int current_index;
    int dupe_id;
} ImportContext;

typedef struct _RenameEntryDialogData {
    GtkWidget *dialog;
    GtkWidget *name_entry;
    ImportContext *context;
} RenameEntryDialogData;

typedef struct _ExportData {
    int *ids;
    int num_ids;
} ExportData;

typedef enum _ImportMode {
    REPLACE,
    OVERWRITE,
    PROMPT
} ImportMode;

G_DEFINE_FINAL_TYPE(MainWindow, main_window, GTK_TYPE_BOX)

static void on_edit_save(EntryEditBox *edit_box, MainWindow *self);
static void on_edit_cancel(EntryEditBox *edit_box, MainWindow *self);
static void logout_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_dupe_import_choice(GObject *source, GAsyncResult *result, gpointer user_data);

static void reload_sidebar() {
    MainWindow *self = MAIN_WINDOW(gtk_window_get_child(root_window));
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
        util_nonfatal_d("Failed to delete entry; check log for more information");
        return;
    }

    reload_sidebar();

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
    if (!strlen(new_username)) util_warn_d("Field 'username' is missing, continuing");
    char *password = strdup(gtk_editable_get_text(GTK_EDITABLE(edit_box->password_entry)));
    if (!strlen(password)) util_warn_d("Field 'password' is missing, continuing");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(edit_box->notes_text));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *notes = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    trim(notes);

    PasswdEntry *new_entry = ec_malloc(sizeof(PasswdEntry));
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
            util_nonfatal_d("Failed to add entry; check log for more information");
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
            util_nonfatal_d("Failed to update entry; check log for more information");
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

    reload_sidebar();

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

    // id 'defrag' may have run, changing ids
    reload_sidebar();
}

static void on_entry_selected(GtkListBox *box, GtkListBoxRow *row, MainWindow *self) {

    X(box);

    if (!self->content_area)
        return;
    
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
    char *username = ec_malloc(strlen("Username: ") + strlen(e->username) + 1);
    sprintf(username, "Username: %s", e->username);
    char *password = ec_malloc(strlen("Password: ") + strlen(e->password) + 1);
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
    edit_box->entry_id = storage_get_next_id();

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

static void on_select_all_choose_entry_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ChooseEntriesDialogData *data = (ChooseEntriesDialogData *)user_data;

    GtkListBox *entries_box = GTK_LIST_BOX(data->entries_box);
    
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_widget_get_first_child(GTK_WIDGET(entries_box)));
    while (row) {
        GtkWidget *child = gtk_list_box_row_get_child(row);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(child), true);
        row = GTK_LIST_BOX_ROW(gtk_widget_get_next_sibling(GTK_WIDGET(row)));
    }
}

static void on_deselect_all_choose_entry_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ChooseEntriesDialogData *data = (ChooseEntriesDialogData *)user_data;

    GtkListBox *entries_box = GTK_LIST_BOX(data->entries_box);
    
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_widget_get_first_child(GTK_WIDGET(entries_box)));
    while (row) {
        GtkWidget *child = gtk_list_box_row_get_child(row);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(child), false);
        row = GTK_LIST_BOX_ROW(gtk_widget_get_next_sibling(GTK_WIDGET(row)));
    }
}

static void on_cancel_choose_entry_exp_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    util_log(DEBUG, "Export canceled");
    gtk_window_destroy(GTK_WINDOW(((ChooseEntriesDialogData *) user_data)->dialog));
    g_free(user_data);
}

static void on_export_file_response(GObject *dialog, GAsyncResult *result, gpointer user_data) {

    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(dialog), result, NULL);
    if (!file) { // canceled
        util_log(DEBUG, "Export canceled");
        g_free(user_data);
        return;
    }

    if (g_file_query_exists(file, NULL)) {
        util_nonfatal_d("Cannot overwrite existing file!");
        g_free(user_data);
        return;
    }
    
    char *path = g_file_get_path(file);
    g_object_unref(file);

    // export encrypted passwords
    ExportData *data = (ExportData *)user_data;
    PasswdEntry *export_entries = ec_malloc(sizeof(PasswdEntry) * data->num_ids);
    for (int i = 0; i < data->num_ids; i++) {
        PasswdEntry *e1 = storage_get_entry(data->ids[i]);
        PasswdEntry *e2 = &export_entries[i];
        e2->id       =        e1->id;
        e2->service  = strdup(e1->service);
        e2->username = strdup(e1->username);
        e2->password = strdup(e1->password);
        e2->notes    = strdup(e1->notes);
    }

    uint8_t *salt = get_user_salt();
    if (!util_check_ptr(salt, "Failed to get user salt for export")) {
        wipe_passwd_entries(export_entries, data->num_ids);
        g_free(path);
        g_free(data);
        return;
    }

    storage_write_vault(path, export_entries, data->num_ids, salt);
    
    util_log(INFO, "Exported %d entries to %s", data->num_ids, path);
    
    wipe_passwd_entries(export_entries, data->num_ids);
    free(salt);
    g_free(path);
    g_free(data);
}


static void on_confirm_choose_entry_exp_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ChooseEntriesDialogData *data = (ChooseEntriesDialogData *)user_data;
    
    GtkListBox *entries_box = GTK_LIST_BOX(data->entries_box);
    
    int *ids = ec_malloc(num_entries * sizeof(int));
    int index = 0;
    
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_widget_get_first_child(GTK_WIDGET(entries_box)));
    while (row) {
        GtkWidget *child = gtk_list_box_row_get_child(row);
        GtkCheckButton *check = GTK_CHECK_BUTTON(child);
        
        if (gtk_check_button_get_active(check)) {
            int *id_ptr = g_object_get_data(G_OBJECT(check), "entry-id");
            util_log(DEBUG, "Adding entry with id '%d' to export data", *id_ptr);
            if (id_ptr)
                ids[index++] = *id_ptr;
            else
                util_log(ERROR, "Failed to fetch id from checkbutton");
        }
        
        row = GTK_LIST_BOX_ROW(gtk_widget_get_next_sibling(GTK_WIDGET(row)));
    }

    gtk_window_destroy(GTK_WINDOW(data->dialog));
    g_free(data);

    if (index == 0) {
        util_warn_d("No entries selected; stopping export");
        free(ids);
        return;
    }

    ids = ec_realloc(ids, index * sizeof(int));

    GtkFileDialog *dialog = gtk_file_dialog_new();
    char *default_path = util_get_app_dir();
    GFile *folder = g_file_new_for_path(default_path);
    gtk_file_dialog_set_initial_folder(dialog, folder);

    time_t now = time(NULL);
    struct tm *curr_time = localtime(&now);
    char default_file[34];
    strftime(default_file, sizeof(default_file), "export-%m-%d-%Y_%H-%M-%S.pwmngr", curr_time);
    gtk_file_dialog_set_initial_name(dialog, default_file);
    gtk_file_dialog_set_title(dialog, "Select export file");

    ExportData *edata = g_new0(ExportData, 1);
    edata->ids     = ids;
    edata->num_ids = index;

    gtk_file_dialog_save(dialog, root_window, NULL, on_export_file_response, edata);
}

static void on_cancel_import_mode_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ImportModeDialogData *data = (ImportModeDialogData *)user_data;

    util_log(DEBUG, "Import canceled");
    gtk_window_destroy(GTK_WINDOW(data->dialog));
    if (data->import_entries)
        wipe_passwd_entries(data->import_entries, data->num_import_entries);
    g_free(data);
}

static void on_cancel_rename_entry_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    RenameEntryDialogData *data = (RenameEntryDialogData *)user_data;

    util_log(DEBUG, "Rename dupe entry canceled");
    gtk_window_destroy(GTK_WINDOW(data->dialog));

    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Duplicate entry '%s'\n"
        "Choose one option:",
        data->context->mode_data->import_entries[data->context->current_index].service
    );

    const char *buttons[] = { "Keep old entry", "Overwrite", "Rename new entry", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(root_window), NULL, on_dupe_import_choice, data->context);
    return;
}

static void on_confirm_rename_entry_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    RenameEntryDialogData *data = (RenameEntryDialogData *)user_data;
    ImportContext *context = data->context;

    char *new_service = (char *)gtk_editable_get_text(GTK_EDITABLE(data->name_entry));

    if (!strlen(new_service)) {
        util_nonfatal_d("Field 'name_entry' cannot be empty!");
        return;
    }

    bool found_dupe = false;
    for (int i = 0; i < num_entries; i++)
        if (!strcmp(entries[i].service, new_service)) {
            found_dupe = true;
            break;
        }
    
    if (found_dupe) {
        util_nonfatal_d("Name must not be a duplicate of a current entry's service name");
        return;
    }
    
    PasswdEntry *entry = &(context->mode_data->import_entries[context->current_index]);
    free(entry->service);
    entry->service = strdup(new_service);
    entry->id = storage_get_next_id();
    add_entry(entry);
    reload_sidebar();

    context->current_index++;
    gtk_window_destroy(GTK_WINDOW(data->dialog));
    free(data);
    on_dupe_import_choice(NULL, NULL, context);
}

static void on_dupe_import_choice(GObject *source, GAsyncResult *result, gpointer user_data) {
    ImportContext *context = (ImportContext *)user_data;

    if (source) { // rename callback passes NULL, NULL, context, so this should only be run if it is called as an actual callback
        GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
        int response = gtk_alert_dialog_choose_finish(dialog, result, NULL);
        g_object_unref(dialog);

        PasswdEntry *entry = &(context->mode_data->import_entries[context->current_index]);

        switch (response) {
            case 0: // Keep
                util_log(DEBUG, "Keeping existing entry for '%s'", entry->service);
                context->current_index++;
                break;

            case 1: // Overwrite
                entry->id = context->dupe_id;
                update_entry(context->dupe_id, entry);
                util_log(DEBUG, "Overwrote entry '%s'", entry->service);
                context->current_index++;
                break;

            case 2: // Rename

                GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/rename_entry_dialog.ui");

                GtkWidget *dialog      = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
                GtkWidget *name_entry  = GTK_WIDGET(gtk_builder_get_object(builder, "name_entry"));
                GtkWidget *cancel_btn  = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
                GtkWidget *confirm_btn = GTK_WIDGET(gtk_builder_get_object(builder, "conf_btn"));

                gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

                RenameEntryDialogData *rename_data = g_new0(RenameEntryDialogData, 1);
                rename_data->dialog     = dialog;
                rename_data->name_entry = name_entry;
                rename_data->context    = context;

                g_signal_connect(cancel_btn,  "clicked", G_CALLBACK(on_cancel_rename_entry_clicked),  rename_data);
                g_signal_connect(confirm_btn, "clicked", G_CALLBACK(on_confirm_rename_entry_clicked), rename_data);

                gtk_window_present(GTK_WINDOW(dialog));
                return;

            default: // quit
                util_warn_d("Import canceled: entries may contained partially updated data");
                wipe_passwd_entries(context->mode_data->import_entries, context->mode_data->num_import_entries);
                free(context->mode_data);
                free(context);
                return;
        }
    }

    for (int i = context->current_index; i < context->mode_data->num_import_entries; i++) {

        bool dupe_service = false;
        int dupe_id = 0;

        for (int j = 0; j < num_entries; j++) {
            if (!strcmp(entries[j].service, context->mode_data->import_entries[i].service)) { // dupe
                dupe_service = true;
                dupe_id = entries[j].id;
                break;
            }
        }

        if (dupe_service) {
            GtkAlertDialog *dialog = gtk_alert_dialog_new(
                "Duplicate entry '%s'\n"
                "Choose one option:",
                context->mode_data->import_entries[i].service
            );

            const char *buttons[] = { "Keep old entry", "Overwrite", "Rename new entry", NULL };
            gtk_alert_dialog_set_buttons(dialog, buttons);

            context->current_index = i;
            context->dupe_id = dupe_id;

            gtk_alert_dialog_choose(dialog, GTK_WINDOW(root_window), NULL, on_dupe_import_choice, context);
            return;
        } else {
            add_entry(&(context->mode_data->import_entries[i]));
            util_log(DEBUG, "Added entry '%s' from imported file", context->mode_data->import_entries[i].service);
        }
    }

    // only runs once iteration of import_entries is finished
    if (context->mode_data->import_entries)
        wipe_passwd_entries(context->mode_data->import_entries, context->mode_data->num_import_entries);
    free(context->mode_data);
    free(context);

    reload_sidebar();
    util_log(INFO, "Import complete");
}

static void on_confirm_import_mode_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ImportModeDialogData *data = (ImportModeDialogData *)user_data;

    GtkDropDown *dd = GTK_DROP_DOWN(data->import_mode_dropdown);
    guint mode = gtk_drop_down_get_selected(dd);

    switch (mode) {
        case REPLACE:

            // export current data
            char *appdir = util_get_app_dir();
            if (!util_check_ptr(appdir, "Failed to load app dir")) {
                if (data->import_entries)
                    wipe_passwd_entries(data->import_entries, data->num_import_entries);
                return;
            }

            time_t now = time(NULL);
            struct tm *curr_time = localtime(&now);
            char backup_file[34];
            strftime(backup_file, sizeof(backup_file), "backup_%m-%d-%Y_%H-%M-%S.pwmngr", curr_time);

            char *backup_path = ec_malloc(strlen(appdir) + 34 + 1);
            sprintf(backup_path, "%s%s", appdir, backup_file);
            free(appdir);

            GFile *bck_file = g_file_new_for_path(backup_path);
            if (g_file_query_exists(bck_file, NULL)) {
                util_nonfatal_d("Backup file already exists! (should not be possible)");
                free(backup_path);
                if (data->import_entries)
                    wipe_passwd_entries(data->import_entries, data->num_import_entries);
                break;
            }

            uint8_t *salt = get_user_salt();
            if (!util_check_ptr(salt, "Failed to get user salt for backup data")) {
                gtk_window_destroy(GTK_WINDOW(data->dialog));
                if (data->import_entries)
                    wipe_passwd_entries(data->import_entries, data->num_import_entries);
                g_free(data);
                free(backup_path);
                return;
            }

            storage_write_vault(backup_path, entries, num_entries, salt);

            util_log(INFO, "Exported %d entries to backup file %s", num_entries, backup_path);

            free(backup_path);
            wipe_passwd_entries(entries, num_entries);
            
            // replace data
            entries = data->import_entries;
            num_entries = data->num_import_entries;
            storage_write_user_vault();

            util_log(INFO, "Import complete: %d entries imported", data->num_import_entries);
            gtk_window_destroy(GTK_WINDOW(data->dialog));
            g_free(data);
            reload_sidebar();

            return;

        case OVERWRITE:
        case PROMPT:

            for (int i = 0; i < data->num_import_entries; i++) {

                bool dupe_service = false;
                int dupe_id = 0;

                for (int j = 0; j < num_entries; j++) {
                    if (!strcmp(entries[j].service, data->import_entries[i].service)) { // dupe
                        dupe_service = true;
                        dupe_id = entries[j].id;
                        break;
                    }
                }

                if (dupe_service) {
                    if (mode == OVERWRITE) {
                        data->import_entries[i].id = dupe_id;
                        update_entry(dupe_id, &(data->import_entries[i]));
                        util_log(DEBUG, "Overwrote entry '%s' with version from imported file", data->import_entries[i].service);
                    } else {
                        /*
                        prompt:
                        overwrite = update like for mode == OVERWRITE
                        keep = do nothing
                        rename = prompt for new name; scan for dupe, reprompt if dupe
                        */

                        gtk_window_destroy(GTK_WINDOW(data->dialog));

                        GtkAlertDialog *dialog = gtk_alert_dialog_new(
                            "Duplicate entry '%s'\n"
                            "Choose one option:",
                            data->import_entries[i].service
                        );

                        const char *buttons[] = { "Keep old entry", "Overwrite", "Rename new entry", NULL };
                        gtk_alert_dialog_set_buttons(dialog, buttons);

                        ImportContext *context = g_new0(ImportContext, 1);
                        context->mode_data = data;
                        context->current_index = i;
                        context->dupe_id = dupe_id;

                        gtk_alert_dialog_choose(dialog, GTK_WINDOW(root_window), NULL, on_dupe_import_choice, context);
                        return;
                    }
                } else {
                    data->import_entries[i].id = storage_get_next_id();
                    add_entry(&(data->import_entries[i]));
                    reload_sidebar();
                    util_log(DEBUG, "Added entry '%s' from imported file", data->import_entries[i].service);
                }
            }

            break;

        default:
            util_nonfatal_d("Invalid import mode!");
            if (data->import_entries)
                wipe_passwd_entries(data->import_entries, data->num_import_entries);
            gtk_window_destroy(GTK_WINDOW(data->dialog));
            g_free(data);
            return;
    }

    reload_sidebar();
    gtk_window_destroy(GTK_WINDOW(data->dialog));
    util_log(INFO, "Import complete: %d entries imported", data->num_import_entries);
    if (data->import_entries)
        wipe_passwd_entries(data->import_entries, data->num_import_entries);
    g_free(data);
}

static void on_cancel_choose_entry_imp_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ChooseEntriesDialogData *data = (ChooseEntriesDialogData *)user_data;

    util_log(DEBUG, "Import canceled");
    gtk_window_destroy(GTK_WINDOW(data->dialog));
    if (data->import_entries)
        wipe_passwd_entries(data->import_entries, data->num_import_entries);
    g_free(data);
}

static void on_confirm_choose_entry_imp_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ChooseEntriesDialogData *data = (ChooseEntriesDialogData *)user_data;
    
    GtkListBox *entries_box = GTK_LIST_BOX(data->entries_box);
    
    int *ids = ec_malloc(data->num_import_entries * sizeof(int));
    int index = 0;
    
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_widget_get_first_child(GTK_WIDGET(entries_box)));
    while (row) {
        GtkWidget *child = gtk_list_box_row_get_child(row);
        GtkCheckButton *check = GTK_CHECK_BUTTON(child);
        
        if (gtk_check_button_get_active(check)) {
            int *id_ptr = g_object_get_data(G_OBJECT(check), "entry-id");
            util_log(DEBUG, "Adding entry with id '%d' to import data", *id_ptr);
            if (id_ptr)
                ids[index++] = *id_ptr;
            else
                util_log(ERROR, "Failed to fetch id from checkbutton");
        }
        
        row = GTK_LIST_BOX_ROW(gtk_widget_get_next_sibling(GTK_WIDGET(row)));
    }

    gtk_window_destroy(GTK_WINDOW(data->dialog));
    
    if (index == 0) {
        util_warn_d("No entries selected; stopping import");
        g_free(data);
        free(ids);
        return;
    }

    if (index < data->num_import_entries) { // import_entries needs to be shrunk
        for (int i = 0; i < data->num_import_entries; i++) {
            bool found_id = false;
            for (int j = 0; j < index; j++) {
                if (ids[j] == data->import_entries[i].id) {
                    found_id = true;
                    break;
                }
            }
            if (!found_id) { // delete entry not in list and shrink array
                wipe_mem(data->import_entries[i].service,  strlen(data->import_entries[i].service));
                free(data->import_entries[i].service);
                wipe_mem(data->import_entries[i].username, strlen(data->import_entries[i].username));
                free(data->import_entries[i].username);
                wipe_mem(data->import_entries[i].password, strlen(data->import_entries[i].password));
                free(data->import_entries[i].password);
                wipe_mem(data->import_entries[i].notes,    strlen(data->import_entries[i].notes));
                free(data->import_entries[i].notes);

                for (int j = i; j < data->num_import_entries - 1; j++)
                    data->import_entries[j] = data->import_entries[j + 1];

                --i;
                --(data->num_import_entries);
                data->import_entries = ec_realloc(data->import_entries, data->num_import_entries * sizeof(PasswdEntry));
            }
        }
    }
    
    free(ids);

    // select import mode (replace current data (DANGEROUS, will make backup), append to current data (w/overwrite dupes), or append (prompt to overwrite))

    GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/importmode_dialog.ui");

    GtkWidget *dialog               = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    GtkWidget *import_mode_dropdown = GTK_WIDGET(gtk_builder_get_object(builder, "import_mode_dropdown"));
    GtkWidget *cancel_btn           = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
    GtkWidget *confirm_btn          = GTK_WIDGET(gtk_builder_get_object(builder, "confirm_btn"));

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

    ImportModeDialogData *mode_data = g_new0(ImportModeDialogData, 1);
    mode_data->dialog               = dialog;
    mode_data->import_mode_dropdown = import_mode_dropdown;
    mode_data->import_entries       = data->import_entries;
    mode_data->num_import_entries   = data->num_import_entries;

    g_free(data);

    g_signal_connect(cancel_btn,       "clicked", G_CALLBACK(on_cancel_import_mode_clicked),  mode_data);
    g_signal_connect(confirm_btn,      "clicked", G_CALLBACK(on_confirm_import_mode_clicked), mode_data);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void do_import_with_decrypted_data(PasswdEntry *import_entries, int num_import_entries) {
    GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/choose_entries_dialog.ui");

    GtkWidget *dialog           = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    GtkWidget *select_all_btn   = GTK_WIDGET(gtk_builder_get_object(builder, "select_all_btn"));
    GtkWidget *deselect_all_btn = GTK_WIDGET(gtk_builder_get_object(builder, "deselect_all_btn"));
    GtkWidget *entries_box      = GTK_WIDGET(gtk_builder_get_object(builder, "entries_box"));
    GtkWidget *cancel_btn       = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
    GtkWidget *confirm_btn      = GTK_WIDGET(gtk_builder_get_object(builder, "confirm_btn"));

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));
    gtk_window_set_title(GTK_WINDOW(dialog), "Select entries to import");

    for (int i = 0; i < num_import_entries; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *checkbtn = gtk_check_button_new_with_label(import_entries[i].service);

        int *id_ptr = g_new(int, 1);
        *id_ptr = import_entries[i].id;
        g_object_set_data_full(G_OBJECT(checkbtn), "entry-id", id_ptr, g_free);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), checkbtn);
        gtk_list_box_append(GTK_LIST_BOX(entries_box), row);
    }

    ChooseEntriesDialogData *data = g_new0(ChooseEntriesDialogData, 1);
    data->dialog             = dialog;
    data->entries_box        = entries_box;
    data->import_entries     = import_entries;
    data->num_import_entries = num_import_entries;

    g_signal_connect(cancel_btn,       "clicked", G_CALLBACK(on_cancel_choose_entry_imp_clicked),       data);
    g_signal_connect(confirm_btn,      "clicked", G_CALLBACK(on_confirm_choose_entry_imp_clicked),      data);
    g_signal_connect(select_all_btn,   "clicked", G_CALLBACK(on_select_all_choose_entry_clicked),   data);
    g_signal_connect(deselect_all_btn, "clicked", G_CALLBACK(on_deselect_all_choose_entry_clicked), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_cancel_importpasswd_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    util_log(DEBUG, "Import canceled (no password entered)");
    ImportPasswdDialogData *data = (ImportPasswdDialogData *)user_data;
    gtk_window_destroy(GTK_WINDOW(data->dialog));
    free(data->header);
    if (data->import_entries)
        wipe_passwd_entries(data->import_entries, data->num_import_entries);
    free(data);
}

static void on_confirm_importpasswd_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    ImportPasswdDialogData *data = (ImportPasswdDialogData *)user_data;

    char *passwd = (char *)gtk_editable_get_text(GTK_EDITABLE(data->pass_entry));
    uint8_t key[32];

    if (!derive_vault_key(passwd, data->header->salt, key, sizeof(key))) {
        util_nonfatal_d("Failed to derive vault key from user-provided password");
        free(data->header);
        g_free(data->vault_path);
        if (data->import_entries)
            wipe_passwd_entries(data->import_entries, data->num_import_entries);
        free(data);
        return;
    }

    data->num_import_entries = storage_read_vault_with_key(data->vault_path, key, &(data->import_entries), &(data->header));

    if (data->num_import_entries <= 0) {
        util_nonfatal_d("Failed to decrypt imported entries");
        free(data->header);
        if (data->import_entries)
            wipe_passwd_entries(data->import_entries, data->num_import_entries);
        g_free(data->vault_path);
        free(data);
        return;
    }

    free(data->header);
    g_free(data->vault_path);
    do_import_with_decrypted_data(data->import_entries, data->num_import_entries);

    free(data);
}

static void on_file_import_response(GObject *dialog, GAsyncResult *result, gpointer user_data)  {

    X(user_data);

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, NULL);
    if (!file) {
        util_log(INFO, "Import canceled: no file selected");
        return;
    }

    char *path = g_file_get_path(file);

    
    PasswdEntry *import_entries = NULL;
    VaultHeader *hdr = NULL;
    int num_import_entries = storage_read_vault(path, &import_entries, &hdr);

    if (num_import_entries <= 0) {
        util_log(WARN, "Failed to decrypt imported entries with current user's information; trying again with user-defined key info");
        
        // retrieve password for second key attempt
        GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/importpasswd_dialog.ui");

        GtkWidget *dialog              = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
        GtkWidget *pass_entry          = GTK_WIDGET(gtk_builder_get_object(builder, "pass_entry"));
        GtkWidget *cancel_btn          = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
        GtkWidget *conf_btn            = GTK_WIDGET(gtk_builder_get_object(builder, "conf_btn"));

        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

        ImportPasswdDialogData *data = g_new0(ImportPasswdDialogData, 1);

        data->dialog             = dialog;
        data->pass_entry         = pass_entry;
        data->vault_path         = path;
        data->header             = hdr;
        data->import_entries     = import_entries;
        data->num_import_entries = num_import_entries;
        
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_importpasswd_clicked), data);
        g_signal_connect(conf_btn, "clicked", G_CALLBACK(on_confirm_importpasswd_clicked), data);

        gtk_window_present(GTK_WINDOW(dialog));
    } else {
        free(hdr);
        g_free(path);
        do_import_with_decrypted_data(import_entries, num_import_entries);
    }
}

static void on_cancel_changepasswd_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    util_log(DEBUG, "Password change canceled");
    gtk_window_destroy(GTK_WINDOW(((ChangePasswdDialogData *) user_data)->dialog));
    g_free(user_data);
}

static void on_confirm_changepasswd_clicked (GtkButton *button, gpointer user_data) {
    X(button);

    ChangePasswdDialogData *data = (ChangePasswdDialogData *)user_data;

    const char *curr_pass = gtk_editable_get_text(GTK_EDITABLE(data->curr_pass_entry));
    const char *new_pass = gtk_editable_get_text(GTK_EDITABLE(data->new_pass_entry));
    const char *conf_new_pass = gtk_editable_get_text(GTK_EDITABLE(data->conf_new_pass_entry));

    gtk_window_destroy(GTK_WINDOW(data->dialog));
    g_free(data);

    if (strlen(curr_pass) == 0 || strlen(new_pass) == 0 || strlen(conf_new_pass) == 0) {
        util_nonfatal_d("Fields cannot be empty");
        return;
    }

    if (!verify_account(username, curr_pass)) {
        util_nonfatal_d("Wrong password - doing nothing");
        return;
    }

    if (!strcmp(curr_pass, new_pass)) {
        util_warn_d("New password is the same as old password - doing nothing");
        return;
    }

    if (strcmp(new_pass, conf_new_pass)) {
        util_nonfatal_d("Passwords do not match - doing nothing");
        return;
    }

    if (!storage_change_passwd(username, (char *)new_pass)) {
        util_fatal_d("Failed to change password; check log for more information");
        return;
    }

    util_log(INFO, "Successfully changed password");
}

static void on_cancel_deleteacc_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    util_log(DEBUG, "Account deletion canceled (stage two)");
    gtk_window_destroy(GTK_WINDOW(((DeleteAccDialogData *) user_data)->dialog));
    g_free(user_data);
}

static void on_confirm_deleteacc_clicked(GtkButton *button, gpointer user_data) {
    X(button);

    DeleteAccDialogData *data = (DeleteAccDialogData *)user_data;
    
    const char *conf_username = gtk_editable_get_text(GTK_EDITABLE(data->user_entry));
    const char *conf_password = gtk_editable_get_text(GTK_EDITABLE(data->pass_entry));

    gtk_window_destroy(GTK_WINDOW(data->dialog));
    g_free(data);

    if (strcmp(conf_username, username)) { // user is attempting to delete another user's account
        util_nonfatal_d("Wrong username (you can only delete your own account through this menu - nice try!)");
        return;
    }

    if (verify_account(conf_username, conf_password)) {

        util_log(DEBUG, "Manually triggering user logout");
        logout_cb(NULL, NULL, MAIN_WINDOW(gtk_window_get_child(root_window)));

        if (!storage_delete_account((char *)conf_username)) {
            util_fatal_d("Failed to delete account; check log for more information");
            return;
        }

        util_log(INFO, "Deleted account with username '%s'", conf_username);
        
    } else util_nonfatal_d("Failed to delete account: invalid login information");
}

static void on_deleteacc_confirm_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    X(source);

    GtkAlertDialog *dialog1 = GTK_ALERT_DIALOG(user_data);
    int response = gtk_alert_dialog_choose_finish(dialog1, result, NULL);
    g_object_unref(dialog1);

    if (response != 1) { // cancel
        util_log(DEBUG, "Delete account canceled");
        return;
    }

    GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/deleteacc_dialog.ui");

    GtkWidget *dialog     = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    GtkWidget *user_entry = GTK_WIDGET(gtk_builder_get_object(builder, "user_entry"));
    GtkWidget *pass_entry = GTK_WIDGET(gtk_builder_get_object(builder, "pass_entry"));
    GtkWidget *cancel_btn = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
    GtkWidget *delete_btn = GTK_WIDGET(gtk_builder_get_object(builder, "delete_btn"));

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

    DeleteAccDialogData *data = g_new0(DeleteAccDialogData, 1);
    data->dialog = dialog;
    data->user_entry = user_entry;
    data->pass_entry = pass_entry;

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_deleteacc_clicked), data);
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_confirm_deleteacc_clicked), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void export_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Export triggered");

    GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/choose_entries_dialog.ui");

    GtkWidget *dialog           = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    GtkWidget *select_all_btn   = GTK_WIDGET(gtk_builder_get_object(builder, "select_all_btn"));
    GtkWidget *deselect_all_btn = GTK_WIDGET(gtk_builder_get_object(builder, "deselect_all_btn"));
    GtkWidget *entries_box      = GTK_WIDGET(gtk_builder_get_object(builder, "entries_box"));
    GtkWidget *cancel_btn       = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
    GtkWidget *confirm_btn      = GTK_WIDGET(gtk_builder_get_object(builder, "confirm_btn"));

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

    for (int i = 0; i < num_entries; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *checkbtn = gtk_check_button_new_with_label(entries[i].service);

        int *id_ptr = g_new(int, 1);
        *id_ptr = entries[i].id;
        g_object_set_data_full(G_OBJECT(checkbtn), "entry-id", id_ptr, g_free);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), checkbtn);
        gtk_list_box_append(GTK_LIST_BOX(entries_box), row);
    }

    ChooseEntriesDialogData *data = g_new0(ChooseEntriesDialogData, 1);
    data->dialog             = dialog;
    data->entries_box        = entries_box;
    data->import_entries     = NULL;
    data->num_import_entries = 0;

    g_signal_connect(cancel_btn,       "clicked", G_CALLBACK(on_cancel_choose_entry_exp_clicked),       data);
    g_signal_connect(confirm_btn,      "clicked", G_CALLBACK(on_confirm_choose_entry_exp_clicked),      data);
    g_signal_connect(select_all_btn,   "clicked", G_CALLBACK(on_select_all_choose_entry_clicked),   data);
    g_signal_connect(deselect_all_btn, "clicked", G_CALLBACK(on_deselect_all_choose_entry_clicked), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void import_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Import triggered");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Password Manager User Data (*.pwmngr)");
    gtk_file_filter_add_pattern(filter, "*.pwmngr");

    GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(store, filter);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(store));

    gtk_file_dialog_open(dialog, root_window, NULL, on_file_import_response, user_data);
}

static void openlog_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Open log triggered");

    GError *err = NULL;
    char *uri = g_filename_to_uri(util_get_logfile(), NULL, &err);
    if (!uri) {
        char *msg = ec_malloc(strlen("Failed to convert filename to URI: ") + strlen(err->message) + 1);
        sprintf(msg, "Failed to convert filename to URI: %s", err->message);
        util_nonfatal_d(msg);
        g_error_free(err);
        return;
    }

    if (!g_app_info_launch_default_for_uri(uri, NULL, &err)) {
        char *msg = ec_malloc(strlen("Failed to open file: ") + strlen(err->message) + 1);
        sprintf(msg, "Failed to open file: %s", err->message);
        util_nonfatal_d(msg);
        g_error_free(err);
        g_free(uri);
        return;
    }

    util_log(INFO, "Opened passwdmngr.log in default text editor");

    g_free(uri);
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
    wipe_passwd_entries(entries, num_entries);
    entries = NULL;
    num_entries = -1;

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
    
    if (curr_prefs) {
        free(curr_prefs);
        curr_prefs = NULL;
    }

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

    GtkBuilder *builder = gtk_builder_new_from_resource("/com/samuelf09/passwdmngr/changepasswd_dialog.ui");

    GtkWidget *dialog              = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
    GtkWidget *curr_pass_entry     = GTK_WIDGET(gtk_builder_get_object(builder, "curr_pass_entry"));
    GtkWidget *new_pass_entry      = GTK_WIDGET(gtk_builder_get_object(builder, "new_pass_entry"));
    GtkWidget *conf_new_pass_entry = GTK_WIDGET(gtk_builder_get_object(builder, "conf_new_pass_entry"));
    GtkWidget *cancel_btn          = GTK_WIDGET(gtk_builder_get_object(builder, "cancel_btn"));
    GtkWidget *conf_btn            = GTK_WIDGET(gtk_builder_get_object(builder, "conf_btn"));

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root_window));

    ChangePasswdDialogData *data = g_new0(ChangePasswdDialogData, 1);
    data->dialog = dialog;
    data->curr_pass_entry = curr_pass_entry;
    data->new_pass_entry = new_pass_entry;
    data->conf_new_pass_entry = conf_new_pass_entry;

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_changepasswd_clicked), data);
    g_signal_connect(conf_btn, "clicked", G_CALLBACK(on_confirm_changepasswd_clicked), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void deleteacc_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    X(action);
    X(parameter);
    X(user_data);
    util_log(DEBUG, "Delete account triggered");

    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Are you sure you want to delete your account?\n"
        "This operation is permanent and cannot be undone."
    );

    const char *buttons[] = { "Cancel", "Delete", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);

    gtk_alert_dialog_choose(dialog, root_window, NULL, on_deleteacc_confirm_response, dialog);
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

// wrapper for idle call; must be called after root_window has main_window set as child
static gboolean reload_sidebar_idle(gpointer data) {
    X(data);
    reload_sidebar();
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

    char *title = ec_malloc(strlen("Password Manager - ") + strlen(username) + 1);
    if (!title) util_log(ERROR, "Window title malloc failed");
    sprintf(title, "Password Manager - %s", username);
    gtk_window_set_title(GTK_WINDOW(root_window), title);
    free(title);

    if (!storage_read_user_vault())
        util_fatal_d("Failed to read user vault; check log for more information");

    box_remove_children(GTK_BOX(self->content_area));
    GtkWidget *none_box = g_object_new(ENTRY_NONE_BOX_TYPE, NULL);
    gtk_box_append(GTK_BOX(self->content_area), GTK_WIDGET(none_box));

    g_idle_add((GSourceFunc)reload_sidebar_idle, NULL);
}