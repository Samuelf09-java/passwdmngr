#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

bool verify_account(const char *uname, const char *passwd);

int hash_pw(const char *passwd, char *out, size_t out_len);
char *hash_uname(const char *uname, size_t len);

bool derive_vault_key(
    const char *passwd,
    const uint8_t *salt,
    uint8_t *key_out,
    size_t key_len);

int aes_gcm_encrypt(
    uint8_t *plaintext,
    int plaintext_len,
    uint8_t *key,
    uint8_t *iv,
    int iv_len,
    uint8_t *ciphertext,
    uint8_t *tag);