#include "backend.h"
#include "crypto.h"
#include "storage.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <sodium.h>

bool backend_create_account(const char *username,
                            const char *password)
{
    if (!username || !password) return false;
    if (strlen(username) == 0 || strlen(password) == 0) return false;

    char *user_dir = storage_get_user_dir(username);
    if (!user_dir) return false;

    // Create user directory
    if (!storage_create_user_dir(user_dir)) {
        free(user_dir);
        return false;
    }

    // 1. Hash password for validation
    UserMetadata meta;
    if (!hash_password(password, meta.password_hash)) {
        free(user_dir);
        return false;
    }

    // 2. Generate salt for key derivation
    randombytes_buf(meta.salt, sizeof(meta.salt));

    // 3. Derive key
    uint8_t key[32];
    if (!derive_key(password, key, meta.salt)) {
        free(user_dir);
        return false;
    }

    // 4. Create empty vault (e.g., "{}")
    const char *empty_vault = "{}";
    if (!storage_save_vault(user_dir, key,
                            (const uint8_t *)empty_vault,
                            strlen(empty_vault))) {
        free(user_dir);
        return false;
    }

    // 5. Save metadata.json
    if (!storage_save_metadata(user_dir, &meta)) {
        free(user_dir);
        return false;
    }

    free(user_dir);
    return true;
}

bool backend_attempt_login(AppState *app,
                           const char *username,
                           const char *password)
{
    if (!app || !username || !password) return false;

    char *user_dir = storage_get_user_dir(username);
    if (!user_dir) return false;

    // 1. Load metadata.json
    UserMetadata meta;
    if (!storage_load_metadata(user_dir, &meta)) {
        free(user_dir);
        return false;
    }

    // 2. Verify password
    if (!verify_password(password, meta.password_hash)) {
        free(user_dir);
        return false;
    }

    // 3. Derive key
    uint8_t key[32];
    if (!derive_key(password, key, meta.salt)) {
        free(user_dir);
        return false;
    }

    // 4. Load and decrypt vault
    uint8_t *plaintext = NULL;
    size_t plaintext_len = 0;
    if (!storage_load_vault(user_dir, key, &plaintext, &plaintext_len)) {
        free(user_dir);
        return false;
    }

    // 5. Store state in AppState
    strncpy(app->username, username, sizeof(app->username) - 1);
    memcpy(app->key, key, sizeof(app->key));

    // TODO: parse plaintext JSON into your in-memory vault structure
    free(plaintext);
    free(user_dir);
    return true;
}