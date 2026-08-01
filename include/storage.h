#pragma once

#include <stdbool.h>
#include <time.h>

extern int num_accounts;
extern struct Account *accounts;

extern char *username;
extern int num_entries;

bool load_accounts();
void free_accounts();
void init_accounts_json();
void save_accounts();

bool create_new_account(char *uname, char *passwd);

struct Account {
    char *uname_hash;
    char *passwd_hash;
};

struct PasswdEntry {
    char *username;
    char *password;
    char *notes;
};

struct Metadata {
    int version;
    time_t last_modified;
    int num_entries;
    char *vault_salt;
};


char *storage_get_user_dir(char *uname);
struct Metadata *storage_read_user_metadata();
struct PasswdEntry *storage_read_user_vault();

bool storage_write_metadata(struct Metadata *data);
bool storage_write_vault(struct PasswdEntry *entries, int count);