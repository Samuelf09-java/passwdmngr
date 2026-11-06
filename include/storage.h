#pragma once

#include "remote.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * New storage structure:
 * appdir
 * |-vaults
 * | |-<uname_hash>.pwmngr (vault)
 * |-accounts.bin
 * |-preferences.json (contains array of preferences, default = no entry)
 * |-passwdmngr.log
 *
 * Vault structure (used for vault + export/backup files; <name> (<bytes>,<type>)):
 * [magic bytes (6,char) | version (4,uint32_t) | sha256 over remaining header + data (32)]
 * [last modified timestamp (8,uint64_t) | num_entries (4,uint32_t) | ciphertext_len (4,uint32_t)]
 * [salt (16) | nonce (12) | tag (16)]
 * [ciphertext (ciphertext_len)]
 */

#define VAULT_MAGIC "PWMNGR"
#define VAULT_SCHEMA_VERSION 2
#define ACCOUNTS_MAGIC "PWMACC"
#define ACCOUNTS_SCHEMA_VERSION 3
#ifndef HASH_LEN
#define HASH_LEN 32
#endif
#ifndef SALT_LEN
#define SALT_LEN 16
#endif
#ifndef NONCE_LEN
#define NONCE_LEN 12
#endif
#ifndef TAG_LEN
#define TAG_LEN 16
#endif

#define MAX_UNAME_LEN 24
#define MAX_PASSWD_LEN 36

#ifndef AES_KEY_LEN
#define AES_KEY_LEN 32
#endif
#ifndef AUTH_PK_LEN
#define AUTH_PK_LEN 32
#endif
#ifndef AUTH_SK_LEN
#define AUTH_SK_LEN 64
#endif

// encrypted token length
#define E_TOKEN_LEN (32 + NONCE_LEN + TAG_LEN)

#ifndef RK_LEN
#define RK_LEN AES_KEY_LEN
#endif

typedef struct __attribute__((packed)) KeyRing {
    uint8_t vault_key[AES_KEY_LEN];
    uint8_t auth_pk[AUTH_PK_LEN];
    uint8_t auth_sk[AUTH_SK_LEN];
    uint8_t set;
} KeyRing;

typedef struct __attribute__((packed)) AccountHeader {
    char     magic[6];
    uint32_t version;
    uint8_t  hash[HASH_LEN];
    uint32_t num_accounts;
} AccountHeader;

typedef struct __attribute__((packed)) Account {
    uint8_t  uname_hash[HASH_LEN];
    uint8_t  passwd_hash[HASH_LEN + SALT_LEN];
    uint8_t  recovery_key[RK_LEN];
    uint64_t auth_seed_id;
    uint8_t  salt[SALT_LEN];
    uint8_t  e_token[E_TOKEN_LEN];
    int64_t  server_last_modified;
    uint16_t email_len;
    char    *email;
} Account;

#define ACCOUNT_SIZE (sizeof(Account) - sizeof(uint16_t) - sizeof(char *))

typedef struct ServerEntry {
    char    *ip;
    uint16_t port;
    uint8_t  priority;
} ServerEntry;

// contains settings for preferences.json
typedef struct UserPref {
    char *uname_hash;
} UserPref;

typedef struct PasswdEntry {
    int   id;
    char *service;
    char *username;
    char *password;
    char *notes;
} PasswdEntry;

typedef struct __attribute__((packed)) VaultHeader {
    char     magic[6];
    uint32_t version;
    uint8_t  hash[HASH_LEN];
    uint64_t timestamp;
    uint32_t num_entries;
    uint32_t ciphertext_len;
    uint8_t  salt[SALT_LEN];
    uint8_t  nonce[NONCE_LEN];
    uint8_t  tag[TAG_LEN];
} VaultHeader;

#define HEADER_LEN sizeof(VaultHeader)

typedef enum _ImportMode { REPLACE, OVERWRITE, PROMPT } ImportMode;

extern int          num_accounts;
extern Account     *accounts;
extern PasswdEntry *entries;

extern char *username;
extern int   num_entries;

#ifndef NO_CLOUD
extern char *token_b64;
#endif

extern UserPref *curr_prefs;

int  load_servers();
bool init_servers();
bool save_servers();
void free_servers();

bool try_pwmngr_connect();

bool load_accounts();
bool init_accounts();
void init_pref_json(char *pref_path);
bool save_accounts();
bool save_token();

int       storage_read_prefs(UserPref **prefs);
bool      storage_save_prefs(UserPref *prefs, int num_prefs);
UserPref *get_user_prefs(char *uname);

bool create_new_account(char *email, char *uname, char *passwd, bool cloud_sync,
                        sync_job_completion_cb *on_remote_completion);
bool storage_delete_account(char *uname);
bool storage_change_passwd(char *uname, char *new_pass);

bool storage_load_keys(const char *uname, char *passwd);
bool storage_login(char *uname, char *passwd);
void wipe_keys();

uint8_t *get_user_salt();
char    *storage_get_user_vault_path(char *uname);

int  storage_dump_json(char *vault, char **out, uint8_t *key, bool pretty);
bool encrypt_entries(PasswdEntry *entries, int num_entries, uint8_t **ciphertext, int *ciphertext_len, uint8_t **nonce,
                     uint8_t **tag);
bool decrypt_entries_with_key(uint8_t *key, uint8_t *ciphertext, int ciphertext_len, uint8_t *nonce, uint8_t *tag,
                              PasswdEntry **entries, int *num_entries);
bool decrypt_entries(uint8_t *ciphertext, int ciphertext_len, uint8_t *nonce, uint8_t *tag, PasswdEntry **entries,
                     int *num_entries);
int  storage_read_vault_with_key(char *vault_path, uint8_t *key, PasswdEntry **entries, VaultHeader **hdr);
int  storage_read_vault(char *vault_path, PasswdEntry **entries, VaultHeader **hdr);
bool storage_read_user_vault();
bool storage_write_vault(char *vault_path, PasswdEntry *entries, int num_entries, uint8_t *salt);
bool storage_write_user_vault();

int          storage_get_next_id();
PasswdEntry *storage_get_entry(int id);

bool add_entry(PasswdEntry *entry);
bool delete_entry(int id);
bool update_entry(int id, PasswdEntry *new_entry);

void wipe_passwd_entries(PasswdEntry *entries, int num_entries);