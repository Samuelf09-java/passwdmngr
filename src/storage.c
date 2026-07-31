// #include <json-glib/json-glib.h>
#include "storage.h"
#include "crypto.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sodium.h>
#include "util.h"

int num_accounts;
struct Account *accounts;

bool load_accounts() { // TODO: json implementation
    num_accounts = 1;

    accounts = malloc(sizeof(struct Account));
    accounts->passwd_hash = malloc(crypto_pwhash_STRBYTES);

    accounts->uname_hash = hash_uname("samuel", strlen("samuel"));
    if (!accounts->uname_hash) {
        util_error("Failed to hash username");
        return false;
    }
    hash_pw("test", accounts->passwd_hash, crypto_pwhash_STRBYTES);

    return true;
}

void free_accounts() {
    for (int i = 0; i < num_accounts; i++) {
        free(accounts[i].uname_hash);
        free(accounts[i].passwd_hash);
    }
    free(accounts);
}

bool create_new_account(char *uname, char *passwd) {
    return false;
}

char *storage_get_user_dir(char *uname) {

    char *uname_hash = hash_uname(uname, strlen(uname));
    if (!util_check_ptr(uname_hash, "Failed to hash username")) return NULL;

    char *app_dir = util_get_app_dir();
    if (!util_check_ptr(app_dir, "Failed to get app dir")) {
        free(uname_hash);
        return NULL;
    }

    int path_len = strlen(app_dir) + strlen("users") + strlen(uname_hash) + 4;
    char *path = malloc(path_len);
    snprintf(path, path_len, "%s%cusers%c%s%c", app_dir, PATH_SEPARATOR, PATH_SEPARATOR, uname_hash, PATH_SEPARATOR);
    free(app_dir);
    free(uname_hash);
    return path;
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