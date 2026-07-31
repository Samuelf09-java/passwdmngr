#include <openssl/evp.h>
#include <sodium.h>
#include <string.h>
#include "crypto.h"
#include "storage.h"
#include "util.h"

bool verify_account(const char *uname, const char *passwd) {

    char *uname_hash = hash_uname(uname, strlen(uname));

    if (!uname_hash) {
        util_fatal("Failed to hash username");
        return false;
    }

    char *passwd_hash = NULL;

    for (int i = 0; i < num_accounts; i++) {
        if (strcmp((accounts + i)->uname_hash, uname_hash) == 0) {
            passwd_hash = (accounts + i) -> passwd_hash;
            break;
        }
    }

    free(uname_hash);

    if (passwd_hash == NULL) return false; // invalid uname

    if (crypto_pwhash_str_verify(passwd_hash, passwd, strlen(passwd)) == 0) {
        return true; // valid uname + passwd
    } else {
        return false; // invalid passwd
    }
}

int hash_pw(const char *passwd, char *out, size_t out_len) {

    return crypto_pwhash_str(
        out,
        passwd,
        strlen(passwd),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    );
}

char *hash_uname(const char *uname, size_t len) {
    uint8_t uname_hash_bin[32];
    uint32_t out_len;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();

    if (!ctx) return NULL;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, uname, len) != 1 ||
        EVP_DigestFinal_ex(ctx, uname_hash_bin, &out_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return NULL;
    }

    EVP_MD_CTX_free(ctx);
    
    char *uname_hash = malloc(65);
    if (!uname_hash) return NULL;
    for (int i = 0; i < 32; i++)
        sprintf(&uname_hash[i*2], "%02x", uname_hash_bin[i]);

    uname_hash[64] = '\0';
    return uname_hash;
}

int aes_gcm_encrypt(
    uint8_t *plaintext, int plaintext_len,
    uint8_t *key,
    uint8_t *iv, int iv_len,
    uint8_t *ciphertext,
    uint8_t *tag)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len, ciphertext_len;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);

    EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}