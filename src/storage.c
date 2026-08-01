#include <json-glib/json-glib.h>
#include "storage.h"
#include "crypto.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sodium.h>
#include <sys/stat.h>
#include "util.h"
#include "main.h"

int num_accounts;
struct Account *accounts;

static char *storage_get_user_dir_with_hash(const char *uname_hash);

bool load_accounts() {
    // num_accounts = 1;

    // accounts = malloc(sizeof(struct Account));
    // accounts->passwd_hash = malloc(crypto_pwhash_STRBYTES);

    // accounts->uname_hash = hash_uname("samuel", strlen("samuel"));
    // if (!accounts->uname_hash) {
    //     util_error("Failed to hash username");
    //     return false;
    // }
    // hash_pw("test", accounts->passwd_hash, crypto_pwhash_STRBYTES);

    // return true;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, accounts_path, NULL)) {
        util_error("Failed to load accounts.json");
        g_object_unref(parser);
        return false;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    JsonArray *accounts_array = json_object_get_array_member(obj, "accounts");
    if (!accounts_array) {
        util_error("accounts.json missing 'accounts' array");
        g_object_unref(parser);
        return false;
    }

    num_accounts = json_array_get_length(accounts_array);
    accounts = malloc(sizeof(struct Account) * num_accounts);

    for (int i = 0; i < num_accounts; i++) {
        JsonObject *entry = json_array_get_object_element(accounts_array, i);

        const char *uname_hash = json_object_get_string_member(entry, "uname_hash");
        const char *passwd_hash = json_object_get_string_member(entry, "passwd_hash");

        accounts[i].uname_hash = strdup(uname_hash);
        accounts[i].passwd_hash = strdup(passwd_hash);
    }
    
    g_object_unref(parser);
    return true;
}

void free_accounts() {
    for (int i = 0; i < num_accounts; i++) {
        free(accounts[i].uname_hash);
        free(accounts[i].passwd_hash);
    }
    free(accounts);
}

void init_accounts_json() {

    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "accounts");
    json_builder_begin_array(builder);

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);

    json_generator_set_root(gen, root);
    json_generator_to_file(gen, accounts_path, NULL);

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

void save_accounts() {
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "accounts");
    json_builder_begin_array(builder);

    for (int i = 0; i < num_accounts; i++) {
        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "uname_hash");
        json_builder_add_string_value(builder, accounts[i].uname_hash);

        json_builder_set_member_name(builder, "passwd_hash");
        json_builder_add_string_value(builder, accounts[i].passwd_hash);

        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);

    json_generator_set_root(gen, root);
    json_generator_to_file(gen, accounts_path, NULL);

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

bool create_new_account(char *uname, char *passwd) {

    char *new_uname_hash = hash_uname(uname, strlen(uname));
    if (!util_check_ptr(new_uname_hash, "Failed to hash username")) return false;

    for (int i = 0; i < num_accounts; i++) {
        if (strcmp(accounts[i].uname_hash, new_uname_hash) == 0) {
            free(new_uname_hash);
            util_error("Duplicate username");
            return false;
        }
    }

    char *new_passwd_hash = malloc(crypto_pwhash_STRBYTES);

    hash_pw(passwd, new_passwd_hash, crypto_pwhash_STRBYTES);

    num_accounts++;

    accounts = realloc(accounts, sizeof(struct Account) * num_accounts);
    if (!accounts) {
        free(new_uname_hash);
        free(new_passwd_hash);
        util_fatal("Failed to expand accounts array");
        return false;
    }

    accounts[num_accounts - 1].uname_hash  = new_uname_hash;
    accounts[num_accounts - 1].passwd_hash = new_passwd_hash;

    save_accounts();

    char *user_dir = storage_get_user_dir_with_hash(new_uname_hash);
    if (!user_dir) {
        util_error("Failed to build user dir structure");
        return false;
    }
    mkdir(user_dir, 0755);
    
    char *meta_path = malloc(strlen(user_dir) + strlen("metadata.json") + 1);
    sprintf(meta_path, "%smetadata.json", user_dir);
    char *vault_path = malloc(strlen(user_dir) + strlen("vault.bin") + 1);
    sprintf(vault_path, "%svault.bin", user_dir);
    free(user_dir);

    FILE *fp = fopen(meta_path, "a");
    if (!fp) {
        util_error("Failed to create metadata.json");
        return false;
    }
    fclose(fp);

    fp = fopen(vault_path, "a");
    if (!fp) {
        util_error("Failed to create vault.bin");
        return false;
    }
    fclose(fp);

    util_info("Created new account with user directory; saved to accounts.json");

    return true;
}

static char *storage_get_user_dir_with_hash(const char *uname_hash) {
    char *app_dir = util_get_app_dir();
    if (!util_check_ptr(app_dir, "Failed to get app dir")) {
        free(uname_hash);
        return NULL;
    }

    int path_len = strlen(app_dir) + strlen("users") + strlen(uname_hash) + 4;
    char *path = malloc(path_len);
    snprintf(path, path_len, "%s%cusers%c%s%c", app_dir, PATH_SEPARATOR, PATH_SEPARATOR, uname_hash, PATH_SEPARATOR);
    free(app_dir);
    return path;
}

char *storage_get_user_dir(char *uname) {

    char *uname_hash = hash_uname(uname, strlen(uname));
    if (!util_check_ptr(uname_hash, "Failed to hash username")) return NULL;

    char *user_dir = storage_get_user_dir_with_hash(uname_hash);
    free(uname_hash);
    return user_dir;
}

struct Metadata *storage_read_user_metadata(char *dir) {
    return NULL;
}

struct PasswdEntry *storage_read_user_vault(char *dir, int *count) {
    return NULL;
}

bool storage_write_metadata(char *dir, struct Metadata *data) {
    return false;
}

bool storage_write_vault(char *dir, struct PasswdEntry *entries, int count) {
    return false;
}