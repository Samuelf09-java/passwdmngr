#include "crypto.h"
#include <sodium.h>
#include <stdlib.h>
#include <string.h>

static void crypto_init_once(void) {
    static int initialized = 0;
    if (!initialized) {
        if (sodium_init() < 0) {
            // panic: libsodium couldn't be initialized
            abort();
        }
        initialized = 1;
    }
}

bool hash_username_sha512(const char *username, uint8_t out[64]) {
    crypto_init_once();
    if (!username) return false;

    crypto_hash_sha512(out,
                       (const unsigned char *)username,
                       strlen(username));
    return true;
}

bool hash_password(const char *password, char out_hash[128]) {
    crypto_init_once();
    if (!password) return false;

    if (crypto_pwhash_str(out_hash,
                          password,
                          strlen(password),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        return false;
    }
    return true;
}

bool verify_password(const char *password, const char *stored_hash) {
    crypto_init_once();
    if (!password || !stored_hash) return false;

    return crypto_pwhash_str_verify(stored_hash,
                                    password,
                                    strlen(password)) == 0;
}

bool derive_key(const char *password,
                uint8_t key_out[32],
                const uint8_t salt[16]) {
    crypto_init_once();
    if (!password || !salt) return false;

    if (crypto_pwhash(key_out,
                      32,
                      password,
                      strlen(password),
                      salt,
                      crypto_pwhash_OPSLIMIT_MODERATE,
                      crypto_pwhash_MEMLIMIT_MODERATE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return false;
    }
    return true;
}

bool encrypt_data(const uint8_t key[32],
                  const uint8_t *plaintext, size_t len,
                  uint8_t **ciphertext_out, size_t *cipher_len_out) {
    crypto_init_once();
    if (!key || !plaintext || !ciphertext_out || !cipher_len_out) return false;

    uint8_t nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    size_t cipher_len = crypto_secretbox_MACBYTES + len + sizeof(nonce);
    uint8_t *buf = malloc(cipher_len);
    if (!buf) return false;

    memcpy(buf, nonce, sizeof(nonce));

    if (crypto_secretbox_easy(buf + sizeof(nonce),
                              plaintext,
                              len,
                              nonce,
                              key) != 0) {
        free(buf);
        return false;
    }

    *ciphertext_out = buf;
    *cipher_len_out = cipher_len;
    return true;
}

bool decrypt_data(const uint8_t key[32],
                  const uint8_t *ciphertext, size_t cipher_len,
                  uint8_t **plaintext_out, size_t *plain_len_out) {
    crypto_init_once();
    if (!key || !ciphertext || !plaintext_out || !plain_len_out) return false;

    if (cipher_len < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        return false;

    const uint8_t *nonce = ciphertext;
    const uint8_t *ct = ciphertext + crypto_secretbox_NONCEBYTES;
    size_t ct_len = cipher_len - crypto_secretbox_NONCEBYTES;

    size_t pt_len = ct_len - crypto_secretbox_MACBYTES;
    uint8_t *pt = malloc(pt_len);
    if (!pt) return false;

    if (crypto_secretbox_open_easy(pt,
                                   ct,
                                   ct_len,
                                   nonce,
                                   key) != 0) {
        free(pt);
        return false;
    }

    *plaintext_out = pt;
    *plain_len_out = pt_len;
    return true;
}

char *generate_password(int length, bool symbols, bool numbers) {
    crypto_init_once();
    if (length <= 0) return NULL;

    const char *lower = "abcdefghijklmnopqrstuvwxyz";
    const char *upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *digits = "0123456789";
    const char *sym = "!@#$%^&*()-_=+[]{};:,.<>/?";

    char charset[256];
    size_t charset_len = 0;

    memcpy(charset + charset_len, lower, strlen(lower));
    charset_len += strlen(lower);
    memcpy(charset + charset_len, upper, strlen(upper));
    charset_len += strlen(upper);

    if (numbers) {
        memcpy(charset + charset_len, digits, strlen(digits));
        charset_len += strlen(digits);
    }
    if (symbols) {
        memcpy(charset + charset_len, sym, strlen(sym));
        charset_len += strlen(sym);
    }

    if (charset_len == 0) return NULL;

    char *pw = malloc(length + 1);
    if (!pw) return NULL;

    for (int i = 0; i < length; i++) {
        uint32_t idx = randombytes_uniform(charset_len);
        pw[i] = charset[idx];
    }
    pw[length] = '\0';

    return pw;
}
