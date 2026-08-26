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
Account *accounts;
PasswdEntry *entries;

char *tmp_passwd;
uint8_t aes_key[32];
bool key_set = false;

char *username = NULL;
int num_entries = -1;

static char *storage_get_user_dir_with_hash(char *uname_hash);

static int compare_entries_by_service(const void *a, const void *b) { // Sort alphabetically by service
    const PasswdEntry *ea = a;
    const PasswdEntry *eb = b;
    return strcasecmp(ea->service, eb->service);
}

bool load_accounts() {

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, accounts_path, NULL)) {
        util_log(ERROR, "Failed to load accounts.json");
        g_object_unref(parser);
        return false;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    JsonArray *accounts_array = json_object_get_array_member(obj, "accounts");
    if (!accounts_array) {
        util_log(ERROR, "accounts.json missing 'accounts' array");
        g_object_unref(parser);
        return false;
    }

    num_accounts = json_array_get_length(accounts_array);
    accounts = ec_malloc(sizeof(Account) * num_accounts);

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
    json_generator_set_pretty(gen, true);
    if (!json_generator_to_file(gen, accounts_path, NULL)) util_log(ERROR, "Failed to write new metadata.json");

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
            util_log(ERROR, "Duplicate username");
            return false;
        }
    }

    char *new_passwd_hash = ec_malloc(crypto_pwhash_STRBYTES);

    if (hash_pw(passwd, new_passwd_hash, crypto_pwhash_STRBYTES) < 0) return false;

    num_accounts++;

    accounts = ec_realloc(accounts, sizeof(Account) * num_accounts);
    if (!accounts) {
        free(new_uname_hash);
        free(new_passwd_hash);
        util_log(ERROR, "Failed to expand accounts array");
        return false;
    }

    accounts[num_accounts - 1].uname_hash  = new_uname_hash;
    accounts[num_accounts - 1].passwd_hash = new_passwd_hash;

    save_accounts();

    username = strdup(uname);

    char *user_dir = storage_get_user_dir_with_hash(new_uname_hash);
    if (!user_dir) {
        util_log(ERROR, "Failed to build user dir structure");
        return false;
    }
    g_mkdir_with_parents(user_dir, 0755);
    
    char *meta_path = ec_malloc(strlen(user_dir) + strlen("metadata.json") + 1);
    sprintf(meta_path, "%smetadata.json", user_dir);
    char *vault_path = ec_malloc(strlen(user_dir) + strlen("vault.bin") + 1);
    sprintf(vault_path, "%svault.bin", user_dir);
    free(user_dir);

    FILE *fp = fopen(meta_path, "a");
    if (!fp) {
        util_log(ERROR, "Failed to create metadata.json");
        return false;
    }
    fclose(fp);

    Metadata *md = ec_malloc(sizeof(Metadata));
    if (!md) {
        util_log(ERROR, "Metadata malloc failed");
        return false;
    }

    md->version = STORAGE_SCHEMA_VERSION;
    md->last_modified = time(NULL);
    md->num_entries = 0;

    uint8_t salt[SALT_LEN];
    randombytes_buf(salt, sizeof(salt));
    md->vault_salt = g_base64_encode(salt, sizeof(salt));

    if (!storage_write_metadata(md)) {
        util_log(ERROR, "Failed to write defaults to metadata.json");
        return false;
    }

    fp = fopen(vault_path, "a");
    if (!fp) {
        util_log(ERROR, "Failed to create vault.bin");
        return false;
    }
    fclose(fp);

    storage_write_user_vault(md);

    util_log(INFO, "Created new account with user directory; saved to accounts.json");

    return true;
}

bool storage_delete_account(char *uname) {

    // delete data
    if (!delete_recursive(storage_get_user_dir(uname), NULL)) {
        util_log(ERROR, "Failed to delete user dir");
        return false;
    }

    // remove entry from accounts.json
    Account *acc = NULL;
    char *uname_hash = hash_uname(uname, strlen(uname));
    int index = -1;
    for (int i = 0; i < num_accounts; i++) {
        if (!strcmp(accounts[i].uname_hash, uname_hash)) {
            acc = accounts+i;
            index = i;
            break;
        }
    }
    
    if (!acc) {
        util_log(FATAL, "Could not find user account to delete");
        return false;
    }

    if (acc->uname_hash) {
        wipe_mem(acc->uname_hash, strlen(acc->uname_hash));
        free(acc->uname_hash);
    }
    
    if (acc->passwd_hash) {
        wipe_mem(acc->passwd_hash, strlen(acc->passwd_hash));
        free(acc->passwd_hash);
    }

    for (int j = index; j < num_accounts - 1; j++) {
        accounts[j] = accounts[j + 1];
    }

    num_accounts--;
    Account *new_accounts = ec_realloc(accounts, sizeof(Account) * num_accounts);
    if (!new_accounts)
        return false;
    accounts = new_accounts;

    save_accounts();

    return true;
}

// Expects account has already been verified before calling
bool storage_change_passwd(char *uname, char *new_pass) {

    Account *acc = NULL;
    char *uname_hash = hash_uname(uname, strlen(uname));
    for (int i = 0; i < num_accounts; i++) {
        if (!strcmp(accounts[i].uname_hash, uname_hash)) {
            acc = accounts+i;
            break;
        }
    }

    free(uname_hash);

    if (!acc) {
        util_log(FATAL, "Failed to find account with username %s in accounts array", uname);
        return false;
    }

    char *new_passwd_hash = ec_malloc(crypto_pwhash_STRBYTES);
    if (hash_pw(new_pass, new_passwd_hash, crypto_pwhash_STRBYTES) < 0) {
        free(new_passwd_hash);
        util_log(FATAL, "Failed to hash password");
        return false;
    }

    free(acc->passwd_hash);
    acc->passwd_hash = new_passwd_hash;

    save_accounts();

    key_set = false;
    tmp_passwd = strdup(new_pass);
    wipe_mem(aes_key, sizeof(aes_key));

    Metadata *md = storage_read_user_metadata();
    return storage_write_user_vault(md);
}

static char *storage_get_user_dir_with_hash(char *uname_hash) {
    char *app_dir = util_get_app_dir();
    if (!util_check_ptr(app_dir, "Failed to get app dir")) {
        free(uname_hash);
        return NULL;
    }

    int path_len = strlen(app_dir) + strlen("users") + strlen(uname_hash) + 4;
    char *path = ec_malloc(path_len);
    snprintf(path, path_len, "%s%cusers%c%s%c", app_dir, PATH_SEPARATOR, PATH_SEPARATOR, uname_hash, PATH_SEPARATOR);
    free(app_dir);
    return path;
}

char *storage_get_user_dir(char *uname) {

    if (!uname) {
        util_log(ERROR, "uname is null in storage_get_user_dir()");
        return NULL;
    }
    
    char *uname_hash = hash_uname(uname, strlen(uname));
    if (!util_check_ptr(uname_hash, "Failed to hash username")) return NULL;

    char *user_dir = storage_get_user_dir_with_hash(uname_hash);
    free(uname_hash);
    return user_dir;
}

bool encrypt_entries(PasswdEntry *entries, int num_entries, uint8_t *salt, uint8_t **ciphertext, int *ciphertext_len, uint8_t **nonce, uint8_t **tag) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "entries");
    json_builder_begin_array(builder);

    for (int i = 0; i < num_entries; i++) {
        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "id");
        json_builder_add_int_value(builder, entries[i].id);

        json_builder_set_member_name(builder, "service");
        json_builder_add_string_value(builder, entries[i].service);

        json_builder_set_member_name(builder, "username");
        json_builder_add_string_value(builder, entries[i].username);

        json_builder_set_member_name(builder, "password");
        json_builder_add_string_value(builder, entries[i].password);

        json_builder_set_member_name(builder, "notes");
        json_builder_add_string_value(builder, entries[i].notes);

        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);

    json_generator_set_root(gen, root);
    char *json_data = json_generator_to_data(gen, NULL);

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);

    if (!key_set) {
        if (!derive_vault_key(tmp_passwd, salt, aes_key, sizeof(aes_key))) {
            util_log(ERROR, "Failed to derive vault key");
            g_free(json_data);
            return false;
        }
        key_set = true;

        // get plaintext password out of memory
        wipe_mem(tmp_passwd, strlen(tmp_passwd));
        free(tmp_passwd);
        tmp_passwd = NULL;
    }

    *nonce = ec_malloc(12);
    randombytes_buf(*nonce, 12);

    int plaintext_len = strlen(json_data);
    *ciphertext = ec_malloc(plaintext_len + 16);
    *tag = ec_malloc(16);

    *ciphertext_len = aes_gcm_encrypt(
        (uint8_t *) json_data,
        plaintext_len,
        aes_key,
        *nonce,
        12,
        *ciphertext,
        *tag
    );

    g_free(json_data);

    if (*ciphertext_len <= 0) {
        util_log(ERROR, "Vault encryption failed");
        free(*ciphertext);
        free(*nonce);
        free(*tag);
        *ciphertext = NULL;
        *nonce = NULL;
        *tag = NULL;
        return false;
    }

    return true;
}

bool decrypt_entries_with_key(uint8_t *key, uint8_t *ciphertext, int ciphertext_len, uint8_t *nonce, uint8_t *tag, PasswdEntry **entries, int *num_entries) {
    uint8_t *plaintext = ec_malloc(ciphertext_len);

    int plaintext_len = aes_gcm_decrypt(
        ciphertext, ciphertext_len,
        key,
        nonce, 12,
        tag,
        plaintext
    );

    if (plaintext_len < 0) {
        util_log(ERROR, "Vault decryption failed (wrong password/corrupted vault)");
        free(plaintext);
        return false;
    }
    
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, (char *)plaintext, plaintext_len, NULL)) {
        util_log(ERROR, "Failed to load json from decrypted vault.bin");
        g_object_unref(parser);
        free(plaintext);
        return false;
    }
    
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);
    
    JsonArray *entries_array = json_object_get_array_member(obj, "entries");
    if (!entries_array) {
        util_log(ERROR, "decrypted data missing 'entries' array");
        g_object_unref(parser);
        return false;
    }

    *num_entries = json_array_get_length(entries_array);
    
    *entries = ec_malloc(sizeof(PasswdEntry) * *num_entries);
    if (!entries) {
        util_log(ERROR, "malloc failed for passwdentry array");
        return false;
    }

    for (int i = 0; i < *num_entries; i++) {
        JsonObject *entry = json_array_get_object_element(entries_array, i);

        int id               = json_object_get_int_member(   entry, "id");
        const char *service  = json_object_get_string_member(entry, "service");
        const char *username = json_object_get_string_member(entry, "username");
        const char *password = json_object_get_string_member(entry, "password");
        const char *notes    = json_object_get_string_member(entry, "notes");

        (*entries)[i].id       =        id;
        (*entries)[i].service  = strdup(service);
        (*entries)[i].username = strdup(username);
        (*entries)[i].password = strdup(password);
        (*entries)[i].notes    = strdup(notes);
    }
    
    g_object_unref(parser);
    free(plaintext);

    return true;
}

// Uses current user's key
bool decrypt_entries(uint8_t *salt, uint8_t *ciphertext, int ciphertext_len, uint8_t *nonce, uint8_t *tag, PasswdEntry **entries, int *num_entries) {
    if (!key_set) {
        if (!derive_vault_key(tmp_passwd, salt, aes_key, sizeof(aes_key))) {
            util_log(ERROR, "Failed to derive vault key");
            return false;
        }

        key_set = true;

        // get plaintext password out of memory
        wipe_mem(tmp_passwd, strlen(tmp_passwd));
        free(tmp_passwd);
        tmp_passwd = NULL;
    }

    return decrypt_entries_with_key(aes_key, ciphertext, ciphertext_len, nonce, tag, entries, num_entries);
}

Metadata *storage_read_user_metadata() {
    char *user_dir = storage_get_user_dir(username);
    if (!user_dir) {
        util_log(ERROR, "Failed to load user dir");
        return NULL;
    }
    
    char *meta_path = ec_malloc(strlen(user_dir) + strlen("metadata.json") + 1);
    sprintf(meta_path, "%smetadata.json", user_dir);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, meta_path, NULL)) {
        util_log(ERROR, "Failed to load metadata.json");
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    Metadata *metadata = ec_malloc(sizeof(Metadata));
    if (!metadata) {
        util_log(ERROR, "Failed to allocate metadata struct");
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }

    if (!json_object_has_member(obj, "version")) {
        util_log(ERROR, "metadata.json missing 'version'");
        free(metadata);
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }
    metadata->version = json_object_get_int_member(obj, "version");

    if (!json_object_has_member(obj, "last_modified")) {
        util_log(ERROR, "metadata.json missing 'last_modified'");
        free(metadata);
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }
    metadata->last_modified = (time_t)json_object_get_int_member(obj, "last_modified");

    if (!json_object_has_member(obj, "num_entries")) {
        util_log(ERROR, "metadata.json missing 'num_entries'");
        free(metadata);
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }
    metadata->num_entries = json_object_get_int_member(obj, "num_entries");

    if (!json_object_has_member(obj, "vault_salt")) {
        util_log(ERROR, "metadata.json missing 'vault_salt'");
        free(metadata);
        g_object_unref(parser);
        free(meta_path);
        return NULL;
    }
    const char *salt_str = json_object_get_string_member(obj, "vault_salt");
    metadata->vault_salt = strdup(salt_str);

    g_object_unref(parser);
    free(meta_path);

    num_entries = metadata->num_entries;

    return metadata;
}

bool storage_read_user_vault(Metadata *md) {

    char *user_dir = storage_get_user_dir(username);
    if (!user_dir) {
        util_log(ERROR, "Failed to get user directory");
        return false;
    }

    char *vault_path = ec_malloc(strlen(user_dir) + strlen("vault.bin") + 1);
    sprintf(vault_path, "%svault.bin", user_dir);
    free(user_dir);
    
    FILE *fp = fopen(vault_path, "rb");
    if (!fp) {
        util_log(ERROR, "Failed to open vault.bin");
        free(vault_path);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *vault_buf = ec_malloc(fsize);
    fread(vault_buf, 1, fsize, fp);
    fclose(fp);
    free(vault_path);

    if (fsize < 12 + 16) {
        util_log(ERROR, "vault.bin too small to contain nonce+tag");
        free(vault_buf);
        return false;
    }

    uint8_t nonce[12];
    uint8_t tag[16];

    memcpy(nonce, vault_buf, 12);
    memcpy(tag, vault_buf + 12, 16);

    uint8_t *ciphertext = vault_buf + 12 + 16;
    int ciphertext_len = fsize - (12 + 16);

    gsize salt_len;
    uint8_t *salt = g_base64_decode(md->vault_salt, &salt_len);

    if (!decrypt_entries(salt, ciphertext, ciphertext_len, nonce, tag, &entries, &num_entries)) {
        util_log(FATAL, "Failed to decrypt entries array");
        free(vault_buf);
        free(salt);
        return false;
    }

    free(vault_buf);
    free(salt);

    struct tm *last_modified = localtime(&(md->last_modified));
    char time_buf[23];
    strftime(time_buf, sizeof(time_buf), "%m-%d-%Y at %H:%M:%S", last_modified);

    util_log(DEBUG, "Loaded %d entries from user vault; last modified %s", md->num_entries, time_buf);

    return true;
}

bool storage_write_metadata(Metadata *data) {
    char *user_dir = storage_get_user_dir(username);
    if (!user_dir) {
        util_log(ERROR, "Failed to load user dir");
        return NULL;
    }
    
    char *meta_path = ec_malloc(strlen(user_dir) + strlen("metadata.json") + 1);
    sprintf(meta_path, "%smetadata.json", user_dir);

    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, data->version);

    json_builder_set_member_name(builder, "last_modified");
    json_builder_add_int_value(builder, (int)data->last_modified);

    json_builder_set_member_name(builder, "num_entries");
    json_builder_add_int_value(builder, data->num_entries);

    json_builder_set_member_name(builder, "vault_salt");
    json_builder_add_string_value(builder, data->vault_salt);

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);

    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, true);

    if (!json_generator_to_file(gen, meta_path, NULL)) {
        util_log(ERROR, "Failed to write metadata.json");
        json_node_free(root);
        g_object_unref(gen);
        g_object_unref(builder);
        free(meta_path);
        return false;
    }

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    free(meta_path);

    return true;
}

bool storage_write_user_vault(Metadata *md) {

    char *user_dir = storage_get_user_dir((char*)username);
    if (!user_dir) {
        util_log(ERROR, "Failed to get user directory");
        return false;
    }

    char *vault_path = ec_malloc(strlen(user_dir) + strlen("vault.bin") + 1);
    sprintf(vault_path, "%svault.bin", user_dir);
    free(user_dir);

    uint8_t *ciphertext = NULL;
    int ciphertext_len  = 0;
    uint8_t *nonce      = NULL;
    uint8_t *tag        = NULL;

    gsize salt_len;
    uint8_t *salt = g_base64_decode(md->vault_salt, &salt_len);

    if (!encrypt_entries(entries, num_entries, salt, &ciphertext, &ciphertext_len, &nonce, &tag)) {
        util_log(ERROR, "Failed to encrypt user vault");
        return false;
    };

    free(salt);

    FILE *fp = fopen(vault_path, "wb");
    if (!fp) {
        util_log(ERROR, "Failed to open vault.bin for writing");
        free(nonce);
        free(tag);
        free(ciphertext);
        free(vault_path);
        return false;
    }

    fwrite(nonce, 1, 12, fp);
    fwrite(tag,   1, 16, fp);
    fwrite(ciphertext, 1, ciphertext_len, fp);

    fclose(fp);
    free(ciphertext);
    free(nonce);
    free(tag);
    free(vault_path);

    return true;
}

int storage_assign_new_id() {
    int current_id = 0;
    for (int i = 0; i < num_entries; i++) current_id = MAX(current_id, i[entries].id); // fun c tricks with arrays :)
    return current_id + 1;
}

PasswdEntry *storage_get_entry(int id) {

    for (int i = 0; i < num_entries; i++)
        if (entries[i].id == id)
            return &entries[i];
    
    util_log(ERROR, "Failed to find password entry from id: invalid id");
    return NULL;
}

bool add_entry(PasswdEntry *entry) {

    for (int i = 0; i < num_entries; i++) {
        if (!strcmp(entries[i].service, entry->service)) {
            util_log(ERROR, "Duplicate service name");
            return false;
        }

        if (entries[i].id == entry->id) {
            util_log(ERROR, "Duplicate id");
            return false;
        }
    }
    
    Metadata *md = storage_read_user_metadata();

    num_entries++;

    PasswdEntry *new_entries = realloc(entries, sizeof(PasswdEntry) * num_entries);

    if (!new_entries) {
        util_log(ERROR, "Failed to expand passwd entries array");
        num_entries--;
        return false;
    }

    entries = new_entries;

    entries[num_entries - 1].id       =        entry->id;
    entries[num_entries - 1].service  = strdup(entry->service);
    entries[num_entries - 1].username = strdup(entry->username);
    entries[num_entries - 1].password = strdup(entry->password);
    entries[num_entries - 1].notes    = strdup(entry->notes);

    qsort(entries, num_entries, sizeof(PasswdEntry), compare_entries_by_service); // sort entries

    if (!storage_write_user_vault(md)) {
        util_log(ERROR, "Failed to write expanded entry list; this session will not be saved properly");
        return false;
    }

    md->last_modified = time(NULL);
    md->num_entries = num_entries;

    if (!storage_write_metadata(md)) {
        util_log(ERROR, "Failed to update metadata.json; entry count will be inaccurate");
        free(md);
        return false;
    }

    free(md);

    return true;
}

bool delete_entry(int id) {

    int index = -1;
    for (int i = 0; i < num_entries; i++) {
        if (entries[i].id == id) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        util_log(ERROR, "Failed to delete entry: id not found");
        return false;
    }

    wipe_mem(entries[index].service, strlen(entries[index].service));
    free(entries[index].service);
    wipe_mem(entries[index].username, strlen(entries[index].username));
    free(entries[index].username);
    wipe_mem(entries[index].password, strlen(entries[index].password));
    free(entries[index].password);
    wipe_mem(entries[index].notes, strlen(entries[index].notes));
    free(entries[index].notes);

    for (int j = index; j < num_entries - 1; j++) {
        entries[j] = entries[j + 1];
    }

    Metadata *md = storage_read_user_metadata();

    num_entries--;
    PasswdEntry *new_entries = ec_realloc(entries, sizeof(PasswdEntry) * num_entries);
    if (!new_entries)
        return false;
    entries = new_entries;

    md->num_entries = num_entries;
    md->last_modified = time(NULL);

    if (!storage_write_metadata(md)) {
        util_log(ERROR, "Failed to write new metadata.json");
        free(md);
        return false;
    }
    if (!storage_write_user_vault(md)) {
        util_log(ERROR, "Failed to write updated user vault");
        free(md);
        return false;
    }

    free(md);

    return true;
}

bool update_entry(int id, PasswdEntry *new_entry) {

    int index = -1;
    for (int i = 0; i < num_entries; i++) {
        if (entries[i].id == id) {
            index = i;
            continue;
        }

        if (!strcmp(entries[i].service, new_entry->service)) {
            util_log(ERROR, "Duplicate service name");
            return false;
        }
    }
    
    if (index < 0 ) {
        util_log(ERROR, "Failed to update entry: id not found");
        return false;
    }
    
    entries[index].service = strdup(new_entry->service);
    entries[index].username = strdup(new_entry->username);
    entries[index].password = strdup(new_entry->password);
    entries[index].notes = strdup(new_entry->notes);

    qsort(entries, num_entries, sizeof(PasswdEntry), compare_entries_by_service);

    Metadata *md = storage_read_user_metadata();
    md->last_modified = time(NULL);
    if (!storage_write_metadata(md)) {
        util_log(ERROR, "Failed to write updated metadata.json");
        return false;
    }
    
    if (!storage_write_user_vault(md)) {
        util_log(ERROR, "Failed to update user vault; edits will not be saved to disk");
        return false;
    }

    return true;
}

// Shred the sensitive entries array
void wipe_passwd_entries(PasswdEntry *entries, int num_entries) {
    for (int i = 0; i < num_entries; i++) {

        if (entries[i].service) {
            wipe_mem(entries[i].service, strlen(entries[i].service));
            free(entries[i].service);
            entries[i].service = NULL;
        }

        if (entries[i].username) {
            wipe_mem(entries[i].username, strlen(entries[i].username));
            free(entries[i].username);
            entries[i].username = NULL;
        }

        if (entries[i].password) {
            wipe_mem(entries[i].password, strlen(entries[i].password));
            free(entries[i].password);
            entries[i].password = NULL;
        }

        if (entries[i].notes) {
            wipe_mem(entries[i].notes, strlen(entries[i].notes));
            free(entries[i].notes);
            entries[i].notes = NULL;
        }
    }

    wipe_mem(entries, sizeof(PasswdEntry) * num_entries);
    free(entries);
}