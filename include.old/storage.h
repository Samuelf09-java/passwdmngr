#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

// Metadata structure (stored in metadata.json)
typedef struct {
    char password_hash[128];
    uint8_t salt[16];
} UserMetadata;

// Load metadata.json for a user
bool storage_load_metadata(const char *user_dir, UserMetadata *meta_out);

// Save metadata.json
bool storage_save_metadata(const char *user_dir, const UserMetadata *meta);

// Load encrypted vault.bin
bool storage_load_vault(const char *user_dir,
                        const uint8_t key[32],
                        uint8_t **plaintext_out,
                        size_t *plaintext_len_out);

// Save encrypted vault.bin
bool storage_save_vault(const char *user_dir,
                        const uint8_t key[32],
                        const uint8_t *plaintext,
                        size_t plaintext_len);

// Create user directory structure
bool storage_create_user_dir(const char *hashed_username);

char *storage_get_user_dir(const char *username);

#endif
