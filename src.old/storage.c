#include "storage.h"
#include "crypto.h"
#include "util.h"
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sodium.h>

// Convert bytes → hex string
void bytes_to_hex(const uint8_t *bytes, size_t len, char *out_hex) {
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[i*2]     = hex[(bytes[i] >> 4) & 0xF];
        out_hex[i*2 + 1] = hex[bytes[i] & 0xF];
    }
    out_hex[len*2] = '\0';
}

// Convert hex → bytes
bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) return false;

    for (size_t i = 0; i < out_len; i++) {
        char byte_hex[3] = { hex[i*2], hex[i*2+1], 0 };
        out[i] = strtol(byte_hex, NULL, 16);
    }
    return true;
}

// ~/.local/share/passwdmngr/users/<hash>
char *storage_get_user_dir(const char *username) {
    uint8_t hash[64];
    if (!hash_username_sha512(username, hash))
        return NULL;

    char hex[129];
    bytes_to_hex(hash, 64, hex);

    char *root = app_get_data_dir();
    if (!root) return NULL;

    char *path = malloc(512);
    snprintf(path, 512, "%s/users/%s", root, hex);

    free(root);
    return path;
}

bool storage_create_user_dir(const char *path) {
    return util_mkdir_p(path);
}

// metadata.json path
static char *metadata_path(const char *user_dir) {
    char *p = malloc(600);
    snprintf(p, 600, "%s/metadata.json", user_dir);
    return p;
}

// vault.bin path
static char *vault_path(const char *user_dir) {
    char *p = malloc(600);
    snprintf(p, 600, "%s/vault.bin", user_dir);
    return p;
}

bool storage_save_metadata(const char *user_dir, const UserMetadata *meta) {
    char *path = metadata_path(user_dir);
    FILE *f = fopen(path, "w");
    if (!f) { free(path); return false; }

    char salt_hex[33];
    bytes_to_hex(meta->salt, 16, salt_hex);

    fprintf(f,
        "{\n"
        "  \"password_hash\": \"%s\",\n"
        "  \"salt\": \"%s\"\n"
        "}\n",
        meta->password_hash,
        salt_hex
    );

    fclose(f);
    free(path);
    return true;
}

bool storage_load_metadata(const char *user_dir, UserMetadata *meta) {
    char *path = metadata_path(user_dir);
    FILE *f = fopen(path, "r");
    if (!f) { free(path); return false; }

    char buffer[512];
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    free(path);

    buffer[n] = '\0';  // null-terminate at actual length

    char *hash = strstr(buffer, "password_hash");
    char *salt = strstr(buffer, "salt");
    if (!hash || !salt) return false;

    if (sscanf(hash, "password_hash\": \"%127[^\"]", meta->password_hash) != 1)
        return false;

    char salt_hex[33] = {0};
    if (sscanf(salt, "salt\": \"%32[^\"]", salt_hex) != 1)
        return false;

    return hex_to_bytes(salt_hex, meta->salt, 16);
}


bool storage_save_vault(const char *user_dir,
                        const uint8_t key[32],
                        const uint8_t *plaintext,
                        size_t len)
{
    uint8_t *cipher = NULL;
    size_t cipher_len = 0;

    if (!encrypt_data(key, plaintext, len, &cipher, &cipher_len))
        return false;

    char *path = vault_path(user_dir);
    FILE *f = fopen(path, "wb");
    if (!f) { free(cipher); free(path); return false; }

    fwrite(cipher, 1, cipher_len, f);
    fclose(f);

    free(cipher);
    free(path);
    return true;
}

bool storage_load_vault(const char *user_dir,
                        const uint8_t key[32],
                        uint8_t **plaintext_out,
                        size_t *plain_len_out)
{
    char *path = vault_path(user_dir);
    FILE *f = fopen(path, "rb");
    if (!f) { free(path); return false; }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *cipher = malloc(size);
    fread(cipher, 1, size, f);
    fclose(f);
    free(path);

    bool ok = decrypt_data(key, cipher, size, plaintext_out, plain_len_out);
    free(cipher);
    return ok;
}
