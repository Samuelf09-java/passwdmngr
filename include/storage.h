#pragma once

#include <stdbool.h>
#include <time.h>
#include <stdint.h>

extern int num_accounts;
extern struct Account *accounts;
extern struct PasswdEntry *entries;

extern char *tmp_passwd;
extern uint8_t aes_key[32];
extern bool key_set; // whether key has been loaded yet; used to verify key is valid before attempting to use it

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
    int id;
    char *service;
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

typedef struct Account Account;
typedef struct PasswdEntry PasswdEntry;
typedef struct Metadata Metadata;

char *storage_get_user_dir(char *uname);

Metadata *storage_read_user_metadata();
bool storage_read_user_vault(Metadata *md);
bool storage_write_metadata(Metadata *data);
bool storage_write_user_vault(Metadata *md);

int storage_assign_new_id();
PasswdEntry *storage_get_entry(int id);

bool add_entry(PasswdEntry *entry);
bool delete_entry(int id);
bool update_entry(int id, PasswdEntry *new_entry);

void wipe_passwd_entries();