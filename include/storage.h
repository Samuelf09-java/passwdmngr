#pragma once

#include <stdbool.h>
#include <time.h>

extern int num_accounts;
extern struct Account *accounts;

bool load_accounts();
void free_accounts();

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
    time_t last_modified;
    int entries;
};


char *storage_get_user_dir(char *uname);
struct Metadata *storage_read_user_metadata(char *dir);
struct PasswdEntry *storage_read_user_vault(char *dir, int *count);

bool storage_write_metadata(char *dir, struct Metadata *data);
bool storage_write_vault(char *dir, struct PasswdEntry *entries, int count);