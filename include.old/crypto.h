#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Hash username using SHA-512
bool hash_username_sha512(const char *username, uint8_t out[64]);

// Hash password using libsodium's Argon2id (crypto_pwhash_str)
bool hash_password(const char *password, char out_hash[128]);

// Verify password hash
bool verify_password(const char *password, const char *stored_hash);

// Derive a 256-bit key from password + salt (Argon2id)
bool derive_key(const char *password,
                uint8_t key_out[32],
                const uint8_t salt[16]);

// Encrypt data using crypto_secretbox_easy (XSalsa20-Poly1305)
bool encrypt_data(const uint8_t key[32],
                  const uint8_t *plaintext, size_t len,
                  uint8_t **ciphertext_out, size_t *cipher_len_out);

// Decrypt data
bool decrypt_data(const uint8_t key[32],
                  const uint8_t *ciphertext, size_t cipher_len,
                  uint8_t **plaintext_out, size_t *plain_len_out);

// Secure random password generator
char *generate_password(int length, bool symbols, bool numbers);

#endif
