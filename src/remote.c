#ifndef NO_CLOUD

#include "cJSON.h"
#include "crypto.h"
#include "server.h"
#include "storage.h"
#include "util.h"
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sodium.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "remote.h"

#define MAX_JOBS 20
SyncJob         job_queue[MAX_JOBS];
int             jq_head = 0, jq_tail = 0;
pthread_cond_t  jq_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t jq_mut  = PTHREAD_MUTEX_INITIALIZER;

static int server_fd = -1;

static SSL_CTX *ctx = NULL;
static SSL     *ssl = NULL;

void pwmngr_disconnect();
int  send_packet(uint8_t *data, int data_len, uint8_t *response_buf, int response_buf_size);

char *b64_encode(const uint8_t *input, int input_len) {
    if (!input || input_len <= 0)
        return NULL;

    int   out_len = 4 * ((input_len + 2) / 3);
    char *out     = ec_malloc(out_len + 1);

    int written  = EVP_EncodeBlock((uint8_t *)out, input, input_len);
    out[written] = '\0';

    return out;
}

uint8_t *b64_decode(const char *input, int *out_len) {
    if (!input)
        return NULL;

    int input_len = strlen(input);

    int      max_len = (input_len * 3) / 4;
    uint8_t *out     = ec_malloc(max_len);

    int written = EVP_DecodeBlock(out, (const unsigned char *)input, input_len);

    while (input_len > 0 && input[input_len - 1] == '=') {
        written--;
        input_len--;
    }

    if (out_len)
        *out_len = written;

    return out;
}

int check_response_code(cJSON *response) {
    cJSON *code = cJSON_GetObjectItem(response, "code");

    if (!code) {
        util_log(LOG_ERROR, "Invalid response from server! (no response code)");
        return -1;
    }

    int val = cJSON_GetNumberValue(code);

    // non-zero 'warning' or status codes should be handled by caller
    if (val != RESPONSE_OK && val != DATA_WAIT && val != E_VAULTUPTODATE && val != E_CLIENTUPTODATE) {
        util_log(LOG_ERROR, "Server returned code '%d'", val);

        cJSON *err;
        if ((err = cJSON_GetObjectItem(response, "err")) && cJSON_IsString(err))
            util_log(LOG_ERROR, "  %s", err->valuestring);
    }

    return val;
}

int remote_get_server_info(char **version, char **info, int *smtp);

int pwmngr_connect(const char *server_ip, uint16_t port) {

    struct sockaddr_in server_addr;

    socklen_t server_addr_len = sizeof(server_addr);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (server_fd >= 0) {
        util_log(LOG_WARN, "Already connected!");
        return 0;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        util_log(LOG_ERROR, "Failed to create socket!");
        return 1;
    }

    if (connect(server_fd, (struct sockaddr *)&server_addr, server_addr_len) < 0) {
        util_log(LOG_ERROR, "Failed to connect to server!");
        server_fd = -1;
        return 2;
    }

    if (!ctx) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!SSL_CTX_load_verify_locations(ctx, "/home/samuelf09/passwdmngr-server/ca.crt",
                                           NULL)) { // TODO: use user-provided crt
            util_log(LOG_ERROR, "Failed to load CA file for server!");
            SSL_CTX_free(ctx);
            ctx = NULL;
            close(server_fd);
            server_fd = -1;
            return 3;
        }
    }

    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, server_fd);

    if (SSL_connect(ssl) <= 0) {
        util_log(LOG_ERROR, "Failed to connect to server (handshake failed)!");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        ssl = NULL;
        close(server_fd);
        server_fd = -1;
        return 4;
    }

    X509 *cert   = SSL_get_peer_certificate(ssl);
    long  result = SSL_get_verify_result(ssl);

    if (result != X509_V_OK) {
        util_log(LOG_ERROR, "Certificate verification failed: %ld", result);
        X509_free(cert);
        return 5;
    }
    X509_free(cert);

    char *version      = NULL;
    char *info         = NULL;
    int   smtp_enabled = 0;

    int server_info_res = remote_get_server_info(&version, &info, &smtp_enabled);

    if (server_info_res) {
        util_log(LOG_ERROR, "Failed to get server info! (api call returned %d)", server_info_res);
        pwmngr_disconnect();
        return 6;
    }

    if (strcmp(version, SERVER_VERSION)) {
        free(info);
        util_log(LOG_ERROR, "Server is running unsupported version! (%s)", version);
        free(version);
        pwmngr_disconnect();
        return 7;
    }

    util_log(LOG_DEBUG, "Successfully connected to server! Server info string: '%s'; SMTP verification %senabled", info,
             smtp_enabled ? "" : "not ");

    free(version);
    free(info);
    return 0;
}

void pwmngr_disconnect() {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(server_fd);
    server_fd = -1;
    ssl       = NULL;
    util_log(LOG_DEBUG, "Successfully disconnected from server!");
}

int send_packet(uint8_t *data, int data_len, uint8_t *response_buf, int response_buf_size) {
    if (!ssl) {
        util_log(LOG_ERROR, "TLS not initialized (call pwmngr_connect() first)");
        return -1;
    }

    int sent = SSL_write(ssl, data, data_len);
    if (sent <= 0) {
        int err = SSL_get_error(ssl, sent);
        util_log(LOG_ERROR, "SSL_write failed: %d", err);
        return -2;
    }
    if (sent != data_len) {
        util_log(LOG_WARN, "Partial TLS write: sent %d of %d bytes", sent, data_len);
        return -3;
    }

    int received = SSL_read(ssl, response_buf, response_buf_size - 1);
    if (received <= 0) {
        int err = SSL_get_error(ssl, received);
        util_log(LOG_ERROR, "SSL_read failed: %d", err);
        return -4;
    }

    return received;
}

int remote_get_server_info(char **version, char **info, int *smtp) {
    char *get_info_req = "GETSERVERINFO";
    char  response_buf[1024];
    int   returned =
        send_packet((uint8_t *)get_info_req, strlen(get_info_req), (uint8_t *)response_buf, sizeof(response_buf));
    if (returned <= 0) {
        util_log(LOG_ERROR, "send_packet failed!");
        return -1;
    }

    cJSON *root = cJSON_Parse(response_buf);
    if (!root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -2;
    }

    cJSON *version_j = cJSON_GetObjectItem(root, "server_version");
    cJSON *info_j    = cJSON_GetObjectItem(root, "info");
    cJSON *smtp_j    = cJSON_GetObjectItem(root, "smtp_enabled");

    if (!version_j || !info_j || !smtp_j || !cJSON_IsString(version_j) || !cJSON_IsString(info_j) ||
        !cJSON_IsBool(smtp_j)) {
        cJSON_Delete(root);
        util_log(LOG_ERROR, "Invalid server response!");
        return -2;
    }

    int code_check_res = check_response_code(root);

    if (code_check_res) {
        cJSON_Delete(root);
        return -3;
    }

    *version = strdup(version_j->valuestring);
    *info    = strdup(info_j->valuestring);
    *smtp    = smtp_j->type == cJSON_True ? 1 : 0;

    cJSON_Delete(root);

    return 0;
}

int remote_create_account(char *email, uint8_t *username_hash, uint8_t *password_hash, uint8_t *auth_pk, uint8_t *rk,
                          uint8_t *salt, uint64_t auth_seed_id, time_t *timestamp, char **token_out) {

    if (!email || !username_hash || !password_hash || !auth_pk || !rk || !salt || auth_seed_id < 2 || !timestamp ||
        !token_out) {
        util_log(LOG_ERROR, "Invalid arguments passed to remote_create_account!");
        return -1;
    }

    char *username_hash_b64 = b64_encode(username_hash, UNAME_HASH_LEN);
    if (!username_hash_b64) {
        util_log(LOG_ERROR, "Failed to encode username_hash as base64 string!");
        return -2;
    }

    char *password_hash_b64 = b64_encode(password_hash, PASSWD_HASH_LEN);
    if (!password_hash_b64) {
        free(username_hash_b64);
        util_log(LOG_ERROR, "Failed to encode password_hash as base64 string!");
        return -2;
    }

    char *auth_pk_b64 = b64_encode(auth_pk, AUTH_PK_LEN);
    if (!auth_pk_b64) {
        free(username_hash_b64);
        free(password_hash_b64);
        util_log(LOG_ERROR, "Failed to encode auth_pk as base64 string!");
        return -2;
    }

    char *rk_b64 = b64_encode(rk, RK_LEN);
    if (!rk_b64) {
        free(username_hash_b64);
        free(password_hash_b64);
        free(auth_pk_b64);
        util_log(LOG_ERROR, "Failed to encode recovery key as base64 string!");
    }

    char *salt_b64 = b64_encode(salt, SALT_LEN);
    if (!salt_b64) {
        free(username_hash_b64);
        free(password_hash_b64);
        free(auth_pk_b64);
        free(rk_b64);
        util_log(LOG_ERROR, "Failed to encode salt as base64 string!");
        return -2;
    }

    char auth_seed_id_str[17];
    u64_to_hex(auth_seed_id, auth_seed_id_str, sizeof(auth_seed_id_str));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "request", "CREATEACCOUNT");
    cJSON_AddStringToObject(root, "email", email);
    cJSON_AddStringToObject(root, "username_hash", username_hash_b64);
    cJSON_AddStringToObject(root, "password_hash", password_hash_b64);
    cJSON_AddStringToObject(root, "auth_pk", auth_pk_b64);
    cJSON_AddStringToObject(root, "auth_seed_id", auth_seed_id_str);
    cJSON_AddStringToObject(root, "recovery_key", rk_b64);
    cJSON_AddStringToObject(root, "salt", salt_b64);

    free(username_hash_b64);
    free(password_hash_b64);
    free(auth_pk_b64);
    free(rk_b64);
    free(salt_b64);

    char    *request_j    = cJSON_PrintUnformatted(root);
    uint8_t *request_data = ec_malloc(strlen("REQUEST") + strlen(request_j) + 1);
    sprintf((char *)request_data, "REQUEST%s", request_j);
    char response[1024];
    int  sent_len = send_packet(request_data, strlen((char *)request_data), (uint8_t *)response, sizeof(response));
    free(request_data);
    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send CREATEACCOUNT request! (send_packet returned %d)", sent_len);
        return -1;
    }

    cJSON *response_root = cJSON_Parse(response);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -2;
    }

    int code_check_res = check_response_code(response_root);
    if (code_check_res < 0) {
        cJSON_Delete(response_root);
        return -3;
    }

    if (!code_check_res) {
        cJSON *token_obj = cJSON_GetObjectItem(response_root, "token");
        if (!token_obj || !cJSON_IsString(token_obj)) {
            util_log(LOG_ERROR, "Missing/invalid field 'token' in server response!");
            cJSON_Delete(response_root);
            return -4;
        }

        cJSON *timestamp_obj = cJSON_GetObjectItem(response_root, "last_modified");
        if (!timestamp_obj || !cJSON_IsString(timestamp_obj)) {
            util_log(LOG_ERROR, "Missing/invalid field 'timestamp' in server response!");
            cJSON_Delete(response_root);
            return -4;
        }

        *token_out = strdup(token_obj->valuestring);
        *timestamp = hex_to_u64(timestamp_obj->valuestring);
    }

    cJSON_Delete(response_root);
    return code_check_res;
}

int remote_update_account(char *current_email, char *token_b64, char *email, uint8_t *username_hash,
                          uint8_t *password_hash, uint8_t *auth_pk, uint8_t *recovery_key, uint8_t *salt,
                          uint64_t auth_seed_id) {

    if (!current_email || !token_b64 || !auth_pk || auth_seed_id < 2) {
        util_log(LOG_ERROR, "Invalid arguments passed to remote_update_account!");
        return -1;
    }

    char *username_hash_b64 = b64_encode(username_hash, UNAME_HASH_LEN);
    if (username_hash && !username_hash_b64) {
        util_log(LOG_ERROR, "Failed to encode username_hash as base64 string!");
        return -2;
    }

    char *password_hash_b64 = b64_encode(password_hash, PASSWD_HASH_LEN);
    if (password_hash && !password_hash_b64) {
        if (username_hash_b64)
            free(username_hash_b64);
        util_log(LOG_ERROR, "Failed to encode password_hash as base64 string!");
        return -2;
    }

    char *auth_pk_b64 = b64_encode(auth_pk, AUTH_PK_LEN);
    if (!auth_pk_b64) {
        if (username_hash_b64)
            free(username_hash_b64);
        if (password_hash_b64)
            free(password_hash_b64);
        util_log(LOG_ERROR, "Failed to encode auth_pk as base64 string!");
        return -2;
    }

    char *rk_b64 = b64_encode(recovery_key, RK_LEN);
    if (recovery_key && !rk_b64) {
        if (username_hash_b64)
            free(username_hash_b64);
        if (password_hash_b64)
            free(password_hash_b64);
        free(auth_pk_b64);
        util_log(LOG_ERROR, "Failed to encode recovery key as base64 string!");
        return -2;
    }

    char *salt_b64 = b64_encode(salt, SALT_LEN);
    if (salt && !salt_b64) {
        if (username_hash_b64)
            free(username_hash_b64);
        if (password_hash_b64)
            free(password_hash_b64);
        free(auth_pk_b64);
        if (recovery_key)
            free(rk_b64);
        util_log(LOG_ERROR, "Failed to encode salt as base64 string!");
        return -2;
    }

    char auth_seed_id_str[17];
    u64_to_hex(auth_seed_id, auth_seed_id_str, sizeof(auth_seed_id_str));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "request", "UPDATEACCOUNT");
    cJSON_AddStringToObject(root, "email", current_email);
    cJSON_AddStringToObject(root, "token", token_b64);
    if (email)
        cJSON_AddStringToObject(root, "new_email", email);
    if (username_hash_b64)
        cJSON_AddStringToObject(root, "new_username_hash", username_hash_b64);
    if (password_hash_b64)
        cJSON_AddStringToObject(root, "new_password_hash", password_hash_b64);
    cJSON_AddStringToObject(root, "new_auth_pk", auth_pk_b64);
    cJSON_AddStringToObject(root, "new_auth_seed_id", auth_seed_id_str);
    if (recovery_key)
        cJSON_AddStringToObject(root, "new_recovery_key", rk_b64);
    if (salt)
        cJSON_AddStringToObject(root, "new_salt", salt_b64);

    if (username_hash_b64)
        free(username_hash_b64);
    if (password_hash_b64)
        free(password_hash_b64);
    free(auth_pk_b64);
    if (rk_b64)
        free(rk_b64);
    if (salt)
        free(salt);

    char    *request_j    = cJSON_PrintUnformatted(root);
    uint8_t *request_data = ec_malloc(strlen("REQUEST") + strlen(request_j) + 1);
    sprintf((char *)request_data, "REQUEST%s", request_j);
    char response[1024];
    int  sent_len = send_packet(request_data, strlen((char *)request_data), (uint8_t *)response, sizeof(response));
    free(request_data);
    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send UPDATEACCOUNT request! (send_packet returned %d)", sent_len);
        return -1;
    }

    cJSON *response_root = cJSON_Parse(response);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -2;
    }

    int code_check_res = check_response_code(response_root);
    cJSON_Delete(response_root);

    if (code_check_res < 0)
        return -3;

    return code_check_res;
}

#define CREATEACC_RESEND 0
#define UPDATEACC_RESEND 1

int remote_request_code_resend(char *email, int mode, char *old_email, char *token_b64) {
    if (!email) {
        util_log(LOG_ERROR, "No email provided!");
        return -1;
    }

    if (mode && (!token_b64 || !old_email)) {
        util_log(LOG_ERROR, "Missing token/old email!");
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "request", !mode ? "CREATEACCOUNT" : "UPDATEACCOUNT");
    if (mode) {
        cJSON_AddStringToObject(root, "email", old_email);
        cJSON_AddStringToObject(root, "token", token_b64);
        cJSON_AddStringToObject(root, "new_email", email);
    } else
        cJSON_AddStringToObject(root, "email", email);
    cJSON_AddBoolToObject(root, "request_resend", cJSON_True);

    char    *request_j    = cJSON_PrintUnformatted(root);
    uint8_t *request_data = ec_malloc(strlen("REQUEST") + strlen(request_j) + 1);
    sprintf((char *)request_data, "REQUEST%s", request_j);
    char response[1024];
    int  sent_len = send_packet(request_data, strlen((char *)request_data), (uint8_t *)response, sizeof(response));
    free(request_data);
    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to resend %s request! (send_packet returned %d)",
                 !mode ? "CREATEACCOUNT" : "UPDATEACCOUNT", sent_len);
        return -1;
    }

    cJSON *response_root = cJSON_Parse(response);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -2;
    }

    int code_check_res = check_response_code(response_root);
    cJSON_Delete(response_root);

    if (code_check_res == DATA_WAIT)
        return 0;

    if (code_check_res)
        return -code_check_res - 4;

    return -2; // error because server didn't return DATA_WAIT
}

int remote_finish_create_account(int verification_code, char *email, time_t *timestamp, char **token_out) {
    uint8_t *request_data = ec_malloc(strlen("DATA") + sizeof(uint32_t) + strlen(email));
    memcpy(request_data, "DATA", 4);
    memcpy(request_data + strlen("DATA"), (uint32_t *)&verification_code, sizeof(uint32_t));
    memcpy(request_data + 8, email, strlen(email));
    char response[1024];
    int  sent_len = send_packet(request_data, strlen("DATA") + sizeof(uint32_t) + strlen(email), (uint8_t *)response,
                                sizeof(response));
    free(request_data);

    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send verification code for CREATEACCOUNT request! (send_packet returned %d)",
                 sent_len);
        return -1;
    }

    cJSON *response_root = cJSON_Parse(response);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -2;
    }

    *timestamp = 0;

    int code_check_res = check_response_code(response_root);
    if (!code_check_res) {

        cJSON *token_obj = cJSON_GetObjectItem(response_root, "token");
        if (!token_obj || !cJSON_IsString(token_obj)) {
            util_log(LOG_ERROR, "Missing/invalid field 'token' in server response!");
            cJSON_Delete(response_root);
            return -4;
        }

        cJSON *timestamp_obj = cJSON_GetObjectItem(response_root, "last_modified");
        if (!timestamp_obj) {
            util_log(LOG_ERROR, "Missing timestamp from server response!");
            cJSON_Delete(response_root);
            return -3;
        }

        *timestamp = hex_to_u64(timestamp_obj->valuestring);
        *token_out = strdup(token_obj->valuestring);
    }
    cJSON_Delete(response_root);

    if (code_check_res < 0)
        return -4;

    return code_check_res;
}

int remote_finish_update_account(int verification_code, char *email, char *old_email, char *token_b64,
                                 time_t *timestamp) {

    int req_len = strlen("DATA") + sizeof(uint32_t) + sizeof(uint16_t) * 2 + strlen(email) + strlen(old_email);

    int      token_len;
    uint8_t *token = b64_decode(token_b64, &token_len);
    if (!token || token_len != TOKEN_LEN) {
        if (token)
            free(token);
        util_log(LOG_ERROR, "Invalid token!");
        return -1;
    }

    uint16_t email_len     = strlen(email);
    uint16_t old_email_len = strlen(old_email);

    uint8_t *request_data     = ec_malloc(req_len);
    int      written_data_len = 0;
    memcpy(request_data + written_data_len, "DATA", strlen("DATA"));
    written_data_len += strlen("DATA");
    memcpy(request_data + written_data_len, token, TOKEN_LEN);
    written_data_len += TOKEN_LEN;
    memcpy(request_data + written_data_len, (uint32_t *)&verification_code, sizeof(uint32_t));
    written_data_len += sizeof(uint32_t);
    memcpy(request_data + written_data_len, &email_len, sizeof(uint16_t));
    written_data_len += sizeof(uint16_t);
    memcpy(request_data + written_data_len, email, email_len);
    written_data_len += email_len;
    memcpy(request_data + written_data_len, &old_email_len, sizeof(uint16_t));
    written_data_len += sizeof(uint16_t);
    memcpy(request_data + written_data_len, old_email, old_email_len);
    written_data_len += old_email_len;

    if (req_len != written_data_len) {
        free(request_data);
        util_log(LOG_ERROR, "Invalid data length! (bug)");
        return -2;
    }

    char response[1024];
    int  sent_len = send_packet(request_data, written_data_len, (uint8_t *)response, sizeof(response));
    free(request_data);

    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send verification code for CREATEACCOUNT request! (send_packet returned %d)",
                 sent_len);
        return -3;
    }

    cJSON *response_root = cJSON_Parse(response);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -4;
    }

    int code_check_res = check_response_code(response_root);
    if (!code_check_res) {

        cJSON *timestamp_obj = cJSON_GetObjectItem(response_root, "last_modified");
        if (!timestamp_obj) {
            util_log(LOG_ERROR, "Missing timestamp from server response!");
            cJSON_Delete(response_root);
            return -5;
        }

        *timestamp = hex_to_u64(timestamp_obj->valuestring);
    }
    cJSON_Delete(response_root);

    if (code_check_res < 0)
        return -6;

    return code_check_res;
}

int remote_auth(char *email, uint8_t *auth_sk, char **token_out) {

    if (!email || !strlen(email) || !auth_sk) {
        util_log(LOG_ERROR, "Missing email/auth_sk");
        return -1;
    }

    cJSON *auth_req_j = cJSON_CreateObject();
    cJSON_AddStringToObject(auth_req_j, "request", "AUTH");
    cJSON_AddStringToObject(auth_req_j, "email", email);

    char *auth_req_str = cJSON_PrintUnformatted(auth_req_j);
    cJSON_Delete(auth_req_j);

    char *auth_req_buf = ec_malloc(strlen("REQUEST") + strlen(auth_req_str));
    sprintf(auth_req_buf, "REQUEST%s", auth_req_str);
    free(auth_req_str);

    char auth_req_response[1024];
    int  sent_len = send_packet((uint8_t *)auth_req_buf, strlen(auth_req_buf), (uint8_t *)auth_req_response,
                                sizeof(auth_req_response));
    free(auth_req_buf);
    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send AUTH request! (send_packet returned %d)", sent_len);
        return -2;
    }

    cJSON *auth_req_response_j = cJSON_Parse(auth_req_response);
    if (!auth_req_response_j) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -3;
    }

    int code_check_res = check_response_code(auth_req_response_j);

    if (code_check_res != DATA_WAIT) {
        cJSON_Delete(auth_req_response_j);
        util_log(LOG_ERROR, "Unexpected response from server!");
        return -4;
    }

    cJSON *challenge_obj = cJSON_GetObjectItem(auth_req_response_j, "challenge");
    char  *challenge_b64 = NULL;
    if (!challenge_obj || !cJSON_IsString(challenge_obj) ||
        !strlen((challenge_b64 = cJSON_GetStringValue(challenge_obj)))) {
        cJSON_Delete(auth_req_response_j);
        util_log(LOG_ERROR, "Server response did not contain challenge!");
        return -5;
    }

    int      challenge_len;
    uint8_t *challenge = b64_decode(challenge_b64, &challenge_len);
    cJSON_Delete(auth_req_response_j);
    if (!challenge || challenge_len != CHALLENGE_LEN) {
        if (challenge)
            free(challenge);
        util_log(LOG_ERROR, "Failed to decode challenge!");
        return -6;
    }

    uint8_t sig[SIGNATURE_LEN];
    crypto_sign_detached(sig, NULL, challenge, CHALLENGE_LEN, auth_sk);

    uint8_t *auth_fin_data = ec_malloc(strlen("DATA") + SIGNATURE_LEN + strlen(email));
    memcpy(auth_fin_data, "DATA", 4);
    memcpy(auth_fin_data + strlen("DATA"), sig, SIGNATURE_LEN);
    memcpy(auth_fin_data + 4 + SIGNATURE_LEN, email, strlen(email));

    char auth_fin_response[1024];
    int  fin_sent_len = send_packet(auth_fin_data, strlen("DATA") + SIGNATURE_LEN + strlen(email),
                                    (uint8_t *)auth_fin_response, sizeof(auth_fin_response));
    free(auth_fin_data);

    if (fin_sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send challenge signature for AUTH request! (send_packet returned %d)",
                 fin_sent_len);
        return -7;
    }

    cJSON *auth_fin_response_j = cJSON_Parse(auth_fin_response);
    if (!auth_fin_response_j) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -8;
    }

    int fin_code_check_res = check_response_code(auth_fin_response_j);

    if (fin_code_check_res) {
        cJSON_Delete(auth_fin_response_j);
        return -9;
    }

    cJSON *token_obj = cJSON_GetObjectItem(auth_fin_response_j, "token");
    if (!token_obj || !cJSON_IsString(token_obj)) {
        cJSON_Delete(auth_fin_response_j);
        util_log(LOG_ERROR, "Server failed to return token!");
        return -10;
    }

    int      token_len;
    uint8_t *token = b64_decode(cJSON_GetStringValue(token_obj), &token_len);
    if (token)
        free(token);
    if (!token || token_len != TOKEN_LEN) {
        cJSON_Delete(auth_fin_response_j);
        util_log(LOG_ERROR, "Server returned invalid token!");
        return -11;
    }

    *token_out = strdup(token_obj->valuestring);
    cJSON_Delete(auth_fin_response_j);

    return 0;
}

int remote_invalidate_tokens(char *email, char *token_b64) {

    if (!email || !token_b64) {
        util_log(LOG_ERROR, "Missing email/token!");
        return -1;
    }

    cJSON *req_j = cJSON_CreateObject();
    cJSON_AddStringToObject(req_j, "request", "INVALIDATETOKENS");
    cJSON_AddStringToObject(req_j, "email", email);
    cJSON_AddStringToObject(req_j, "token", token_b64);

    char *req_str = cJSON_PrintUnformatted(req_j);
    cJSON_Delete(req_j);

    char *req_buf = ec_malloc(strlen("REQUEST") + strlen(req_str));
    sprintf(req_buf, "REQUEST%s", req_str);
    free(req_str);

    char req_response[1024];
    int  sent_len = send_packet((uint8_t *)req_buf, strlen(req_buf), (uint8_t *)req_response, sizeof(req_response));
    free(req_buf);
    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send INVALIDATETOKENS request! (send_packet returned %d)", sent_len);
        return -2;
    }

    cJSON *req_response_j = cJSON_Parse(req_response);
    if (!req_response_j) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -3;
    }

    return check_response_code(req_response_j);
}

int remote_get_account_info(char *email, time_t *last_known_timestamp, uint8_t **salt, uint64_t *auth_seed_id,
                            uint8_t **rk) {

    if (!email) {
        util_log(LOG_ERROR, "Missing email!");
        return -1;
    }

    char timestamp_str[17];
    u64_to_hex(*last_known_timestamp, timestamp_str, sizeof(timestamp_str));

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "request", "GETACCOUNTINFO");
    cJSON_AddStringToObject(request, "email", email);
    cJSON_AddStringToObject(request, "last_known_timestamp", timestamp_str);

    char *request_j = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    uint8_t *request_buf = ec_malloc(strlen(request_j) + strlen("REQUEST") + 1);
    snprintf((char *)request_buf, strlen(request_j) + strlen("REQUEST") + 1, "REQUEST%s", request_j);

    char response_buf[1024];
    int  sent_len =
        send_packet(request_buf, strlen(request_j) + strlen("REQUEST"), (uint8_t *)response_buf, sizeof(response_buf));

    free(request_buf);

    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send GETACCOUNTINFO request! (send_packet returned %d)", sent_len);
        return -2;
    }

    cJSON *response_root = cJSON_Parse(response_buf);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -3;
    }

    char *auth_seed_id_str  = NULL;
    char *salt_b64          = NULL;
    char *rk_b64            = NULL;
    char *last_modified_str = NULL;

    int code_check_res = check_response_code(response_root);
    if (!code_check_res) {

        cJSON *auth_seed_id_obj = cJSON_GetObjectItem(response_root, "auth_seed_id");
        cJSON *salt_obj         = cJSON_GetObjectItem(response_root, "salt");
        cJSON *rk_obj           = cJSON_GetObjectItem(response_root, "recovery_key");
        cJSON *timestamp_obj    = cJSON_GetObjectItem(response_root, "last_modified");

        if (!auth_seed_id_obj || !salt_obj || !rk_obj || !timestamp_obj || !cJSON_IsString(auth_seed_id_obj) ||
            !cJSON_IsString(salt_obj) || !cJSON_IsString(rk_obj) || !cJSON_IsString(timestamp_obj) ||
            strlen(auth_seed_id_obj->valuestring) != 16 || strlen(timestamp_obj->valuestring) != 16) {
            util_log(LOG_ERROR, "Missing/invalid server response fields!");
            cJSON_Delete(response_root);
            return -4;
        }

        auth_seed_id_str  = strdup(auth_seed_id_obj->valuestring);
        rk_b64            = strdup(rk_obj->valuestring);
        salt_b64          = strdup(salt_obj->valuestring);
        last_modified_str = strdup(timestamp_obj->valuestring);
        cJSON_Delete(response_root);

    } else if (code_check_res == E_CLIENTUPTODATE) {
        cJSON_Delete(response_root);
        return code_check_res;
    } else {
        cJSON_Delete(response_root);
        return -5;
    }

    if (auth_seed_id)
        *auth_seed_id = hex_to_u64(auth_seed_id_str);
    free(auth_seed_id_str);

    if (last_known_timestamp)
        *(time_t *)last_known_timestamp = hex_to_u64(last_modified_str);
    free(last_modified_str);

    int salt_len;
    if (salt)
        *salt = b64_decode(salt_b64, &salt_len);
    free(salt_b64);

    int rk_len;
    if (rk)
        *rk = b64_decode(rk_b64, &rk_len);
    free(rk_b64);

    if (salt && (!*salt || salt_len != SALT_LEN)) {
        if (salt && *salt)
            free(*salt);
        if (rk && *rk)
            free(*rk);
        util_log(LOG_ERROR, "Invalid salt length!");
        return -5;
    }

    if (rk && (!*rk || rk_len != RK_LEN)) {
        if (salt) {
            free(*salt);
            *salt = NULL;
        }
        if (rk && *rk)
            free(*rk);
        util_log(LOG_ERROR, "Invalid recovery key length!");
        return -5;
    }

    return 0;
}

int remote_import_account(char *email, char *token_b64, time_t *last_known_timestamp, uint8_t **uname_hash,
                          uint8_t **passwd_hash, uint8_t **salt, uint8_t **auth_pk, uint64_t *auth_seed_id) {
    if (!email || !token_b64 || !last_known_timestamp || !uname_hash || !passwd_hash || !salt || !auth_pk ||
        !auth_seed_id) {
        util_log(LOG_ERROR, "Missing data inputs/outputs!");
        return -1;
    }

    char timestamp_str[17];
    u64_to_hex(*last_known_timestamp, timestamp_str, sizeof(timestamp_str));

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "request", "IMPORTACCOUNT");
    cJSON_AddStringToObject(request, "email", email);
    cJSON_AddStringToObject(request, "token", token_b64);
    cJSON_AddStringToObject(request, "last_known_timestamp", timestamp_str);

    char *request_j = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    uint8_t *request_buf = ec_malloc(strlen(request_j) + strlen("REQUEST") + 1);
    snprintf((char *)request_buf, strlen(request_j) + strlen("REQUEST") + 1, "REQUEST%s", request_j);

    char response_buf[1024];
    int  sent_len =
        send_packet(request_buf, strlen(request_j) + strlen("REQUEST"), (uint8_t *)response_buf, sizeof(response_buf));

    free(request_buf);

    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send IMPORTACCOUNT request! (send_packet returned %d)", sent_len);
        return -2;
    }

    cJSON *response_root = cJSON_Parse(response_buf);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -3;
    }

    char *uname_hash_b64    = NULL;
    char *passwd_hash_b64   = NULL;
    char *auth_pk_b64       = NULL;
    char *auth_seed_id_str  = NULL;
    char *salt_b64          = NULL;
    char *last_modified_str = NULL;

    int code_check_res = check_response_code(response_root);
    if (!code_check_res) {
        cJSON *uname_hash_obj   = cJSON_GetObjectItem(response_root, "uname_hash");
        cJSON *passwd_hash_obj  = cJSON_GetObjectItem(response_root, "passwd_hash");
        cJSON *auth_pk_obj      = cJSON_GetObjectItem(response_root, "auth_pk");
        cJSON *auth_seed_id_obj = cJSON_GetObjectItem(response_root, "auth_seed_id");
        cJSON *salt_obj         = cJSON_GetObjectItem(response_root, "salt");
        cJSON *timestamp_obj    = cJSON_GetObjectItem(response_root, "last_modified");

        if (!uname_hash_obj || !passwd_hash_obj || !auth_pk_obj || !auth_seed_id_obj || !salt_obj || !timestamp_obj ||
            !cJSON_IsString(uname_hash_obj) || !cJSON_IsString(passwd_hash_obj) || !cJSON_IsString(auth_pk_obj) ||
            !cJSON_IsString(auth_seed_id_obj) || !cJSON_IsString(salt_obj) || !cJSON_IsString(timestamp_obj) ||
            strlen(auth_seed_id_obj->valuestring) != 16 || strlen(timestamp_obj->valuestring) != 16) {
            util_log(LOG_ERROR, "Missing/invalid server response fields!");
            cJSON_Delete(response_root);
            return -4;
        }

        uname_hash_b64    = strdup(uname_hash_obj->valuestring);
        passwd_hash_b64   = strdup(passwd_hash_obj->valuestring);
        auth_pk_b64       = strdup(auth_pk_obj->valuestring);
        auth_seed_id_str  = strdup(auth_seed_id_obj->valuestring);
        salt_b64          = strdup(salt_obj->valuestring);
        last_modified_str = strdup(timestamp_obj->valuestring);
        cJSON_Delete(response_root);
    } else if (code_check_res == E_CLIENTUPTODATE) {
        cJSON_Delete(response_root);
        return code_check_res;
    } else {
        cJSON_Delete(response_root);
        return -5;
    }

    *auth_seed_id = hex_to_u64(auth_seed_id_str);
    free(auth_seed_id_str);

    *(time_t *)last_known_timestamp = hex_to_u64(last_modified_str);
    free(last_modified_str);

    int uname_hash_len = 0;

    *uname_hash = b64_decode(uname_hash_b64, &uname_hash_len);
    free(uname_hash_b64);

    if (!*uname_hash || uname_hash_len != UNAME_HASH_LEN) {
        if (*uname_hash)
            free(*uname_hash);
        util_log(LOG_ERROR, "Invalid uname_hash length!");
        return -5;
    }

    int passwd_hash_len = 0;

    *passwd_hash = b64_decode(passwd_hash_b64, &passwd_hash_len);
    free(passwd_hash_b64);

    if (!*passwd_hash || passwd_hash_len != PASSWD_HASH_LEN) {
        free(*uname_hash);
        if (*passwd_hash)
            free(*passwd_hash);
        util_log(LOG_ERROR, "Invalid passwd_hash length!");
        return -5;
    }

    int auth_pk_len = 0;

    *auth_pk = b64_decode(auth_pk_b64, &auth_pk_len);
    free(auth_pk_b64);

    if (!*auth_pk || auth_pk_len != AUTH_PK_LEN) {
        free(*uname_hash);
        free(*passwd_hash);
        if (*auth_pk)
            free(*auth_pk);
        util_log(LOG_ERROR, "Invalid auth_pk length!");
        return -5;
    }

    int salt_len;
    *salt = b64_decode(salt_b64, &salt_len);
    free(salt_b64);

    if (!*salt || salt_len != SALT_LEN) {
        free(*uname_hash);
        free(*passwd_hash);
        free(*auth_pk);
        if (*salt)
            free(*salt);
        util_log(LOG_ERROR, "Invalid salt length!");
        return -5;
    }

    return 0;
}

int remote_delete_account(char *email, char *token_b64) {
    if (!email || !token_b64) {
        util_log(LOG_ERROR, "Missing email/token_b64!");
        return -1;
    }

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "request", "DELETEACCOUNT");
    cJSON_AddStringToObject(request, "email", email);
    cJSON_AddStringToObject(request, "token", token_b64);

    char *request_j = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    uint8_t *request_buf = ec_malloc(strlen(request_j) + strlen("REQUEST") + 1);
    snprintf((char *)request_buf, strlen(request_j) + strlen("REQUEST") + 1, "REQUEST%s", request_j);

    char response_buf[1024];
    int  sent_len =
        send_packet(request_buf, strlen(request_j) + strlen("REQUEST"), (uint8_t *)response_buf, sizeof(response_buf));

    free(request_buf);

    if (sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send DELETEACCOUNT request! (send_packet returned %d)", sent_len);
        return -2;
    }

    cJSON *response_root = cJSON_Parse(response_buf);
    if (!response_root) {
        util_log(LOG_ERROR, "Failed to parse server response!");
        return -3;
    }

    int code_check_res = check_response_code(response_root);
    cJSON_Delete(response_root);
    return code_check_res;
}

static cJSON *process_get_vault(char *request_j, uint8_t **out, int *out_len) {

    if (!ssl) {
        util_log(LOG_ERROR, "TLS not initialized");
        return NULL;
    }

    int sent = SSL_write(ssl, request_j, strlen(request_j));
    if (sent <= 0) {
        int err = SSL_get_error(ssl, sent);
        util_log(LOG_ERROR, "SSL_write failed: %d", err);
        return NULL;
    }
    if (sent != (int)strlen(request_j)) {
        util_log(LOG_WARN, "Partial TLS write: sent %d of %d bytes", sent, (int)strlen(request_j));
        return NULL;
    }

    // enough space for a 4096-byte transfer + metadata
#define MAX_VAULT_RESPONSE_LEN (VAULT_CHUNK_SIZE + 404)

    uint8_t *response_buf = ec_malloc(MAX_VAULT_RESPONSE_LEN);

    int received = SSL_read(ssl, response_buf, MAX_VAULT_RESPONSE_LEN - 1);
    if (received <= 0) {
        int err = SSL_get_error(ssl, received);
        util_log(LOG_ERROR, "SSL_read failed: %d", err);
        return NULL;
    }
    response_buf[received] = 0; // NULL-terminate for cJSON

    cJSON *response = NULL;

    // zero the first 4 bytes so they can be checked for the server response
    // sodium_memzero(response_buf, 4);

    while (!((response = cJSON_Parse((char *)response_buf))) && !memcmp(response_buf, "DATA", 4)) {

        sodium_memzero(response_buf, 4);

        if (received < (int)(strlen("DATA") + sizeof(uint16_t))) {
            util_log(LOG_ERROR, "Response length too short to contain header!");
            break;
        }

        uint16_t data_len = *(uint16_t *)(response_buf + strlen("DATA"));
        if (received != (int)(data_len + strlen("DATA") + sizeof(uint16_t))) {
            util_log(LOG_ERROR, "Invalid data_len while reading vault from network!");
            break;
        }

        if (!*out)
            *out = ec_malloc(((*out_len = data_len)));
        else {
            uint8_t *new_ptr = ec_realloc(*out, ((*out_len += data_len)));
            if (!new_ptr) {
                free(*out);
                *out_len = 0;
                util_log(LOG_ERROR, "Failed to reallocate vault output buffer!");
                break;
            }
            *out = new_ptr;
        }

        memcpy(*out + *out_len - data_len, response_buf + strlen("DATA") + sizeof(uint16_t), data_len);

        received = SSL_read(ssl, response_buf, MAX_VAULT_RESPONSE_LEN - 1);
        if (received <= 0) {
            int err = SSL_get_error(ssl, received);
            util_log(LOG_ERROR, "SSL_read failed: %d", err);
            break;
        }
        response_buf[received] = 0; // NULL-terminate for cJSON
    }

    free(response_buf);
    return response;
}

int remote_get_vault(char *email, char *token_b64, time_t last_known_timestamp, int force, uint8_t **vault_buf,
                     int *vault_len) {

    if (!email || !token_b64) {
        util_log(LOG_ERROR, "Missing email/token_b64!");
        return -1;
    }

    *vault_buf = NULL;
    *vault_len = 0;

    char timestamp_str[17];
    u64_to_hex(last_known_timestamp, timestamp_str, sizeof(timestamp_str));

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "request", "GETVAULT");
    cJSON_AddStringToObject(request, "email", email);
    cJSON_AddStringToObject(request, "token", token_b64);
    // used only to help resolve sync conflicts (where server has client b's vault but client a has also
    // modified its vault, and thus can't get the vault from the server); also ignores hash verification
    cJSON_AddBoolToObject(request, "force", force);
    cJSON_AddStringToObject(request, "last_known_timestamp", timestamp_str);

    char *request_j = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    char *request_buf = ec_malloc(strlen(request_j) + strlen("REQUEST") + 1);
    snprintf(request_buf, strlen(request_j) + strlen("REQUEST") + 1, "REQUEST%s", request_j);
    free(request_j);

    // NULL on client/network error, set on server error/response
    cJSON *response = process_get_vault(request_buf, vault_buf, vault_len);
    free(request_buf);

    if (!response) {
        util_log(LOG_ERROR, "Failed to receive server vault! (client/network error)");
        return -2;
    }

    int code_check = check_response_code(response);
    if (code_check) {
        cJSON_Delete(response);
        return code_check; // caller handles server-side error
    }

    cJSON *bytes_sent_obj = cJSON_GetObjectItem(response, "bytes_sent");
    if (!bytes_sent_obj || !cJSON_IsNumber(bytes_sent_obj)) {
        cJSON_Delete(response);
        util_log(LOG_ERROR, "Missing/invalid response field 'bytes_sent'!");
        return -3;
    }

    if ((int)bytes_sent_obj->valuedouble != *vault_len) {
        cJSON_Delete(response);
        util_log(LOG_WARN, "Server report of bytes sent differs from actual bytes received!");
        return -3;
    }

    cJSON_Delete(response);

    VaultHeader *hdr = (VaultHeader *)*vault_buf;

    if (memcmp(hdr->magic, V_MAGIC, 6)) {
        util_log(LOG_ERROR, "Invalid vault magic!");
        return -4;
    }

    if (hdr->version != V_VERSION) {
        util_log(LOG_ERROR, "Invalid vault version!");
        return -4;
    }

    if (force) // server might return vault with invalid hash, don't bother checking
        return 0;

    uint8_t *hash = sha_256_hash((uint8_t *)hdr + 10 + HASH_LEN, *vault_len - 10 - HASH_LEN);
    if (!hash) {
        util_log(LOG_ERROR, "Failed to hash vault data!");
        return -4;
    }

    if (memcmp(hash, hdr->hash, HASH_LEN)) {
        free(hash);
        util_log(LOG_ERROR, "Verification failed for vault: invalid hash!");
        return -4;
    }

    free(hash);

    return 0; // request ok, vault stored in *vault_buf
}

int remote_push_vault(char *email, char *token_b64, uint8_t *vault_buf, int vault_len) {

    if (!email || !token_b64 || !vault_buf) {
        util_log(LOG_ERROR, "Missing email/token_b64/vault_buf!");
        return -1;
    }

    if (vault_len <= (int)sizeof(VaultHeader)) {
        util_log(LOG_ERROR, "Invalid vault length!");
        return -1;
    }

    VaultHeader *hdr = (VaultHeader *)vault_buf;

    if (memcmp(hdr->magic, V_MAGIC, 6)) {
        util_log(LOG_ERROR, "Invalid vault magic!");
        return -1;
    }

    if (hdr->version != V_VERSION) {
        util_log(LOG_ERROR, "Invalid vault version!");
        return -1;
    }

    uint8_t *hash = sha_256_hash((uint8_t *)hdr + 10 + HASH_LEN, vault_len - 10 - HASH_LEN);
    if (!hash) {
        util_log(LOG_ERROR, "Failed to hash vault data!");
        return -1;
    }

    if (memcmp(hash, hdr->hash, HASH_LEN)) {
        free(hash);
        printf("Verification failed for vault: invalid hash!");
        return -1;
    }

    free(hash);

    time_t vault_timestamp = (time_t)hdr->timestamp;

    char timestamp_str[17];
    u64_to_hex(vault_timestamp, timestamp_str, sizeof(timestamp_str));

    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "request", "PUSHVAULT");
    cJSON_AddStringToObject(request, "email", email);
    cJSON_AddStringToObject(request, "token", token_b64);
    cJSON_AddStringToObject(request, "vault_timestamp", timestamp_str);

    char *request_j = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    uint8_t *request_buf = ec_malloc(strlen(request_j) + strlen("REQUEST") + 1);
    snprintf((char *)request_buf, strlen(request_j) + strlen("REQUEST") + 1, "REQUEST%s", request_j);

    char response_buf[1024];

    int req_sent_len =
        send_packet(request_buf, strlen((char *)request_buf), (uint8_t *)response_buf, sizeof(response_buf));

    free(request_buf);

    if (req_sent_len <= 0) {
        util_log(LOG_ERROR, "Failed to send PUSHVAULT request! (send_packet returned %d)", req_sent_len);
        return -2;
    }

    cJSON *server_response = cJSON_Parse(response_buf);
    if (!server_response) {
        util_log(LOG_ERROR, "Failed to parse initial server response!");
        return -3;
    }

    int server_response_code = check_response_code(server_response);
    if (server_response_code < 0)
        return -4;
    if (server_response_code != DATA_WAIT)
        return server_response_code;

    int bytes_sent = 0;
    int data_sent_len;

    int      token_len;
    uint8_t *raw_token = b64_decode(token_b64, &token_len);
    if (!raw_token || token_len != TOKEN_LEN) {
        if (raw_token)
            free(raw_token);
        util_log(LOG_ERROR, "Failed to convert token to raw!");
        return -5;
    }

    uint8_t *packet =
        ec_malloc(strlen("DATA") + TOKEN_LEN + sizeof(uint16_t) + strlen(email) + sizeof(uint16_t) + VAULT_CHUNK_SIZE);

    int start_packet_bytes_written = 0;
    memcpy(packet, "DATA", 4);
    start_packet_bytes_written += 4;
    memcpy(packet + start_packet_bytes_written, raw_token, TOKEN_LEN);
    start_packet_bytes_written += TOKEN_LEN;
    uint16_t email_len = strlen(email);
    memcpy(packet + start_packet_bytes_written, (uint8_t *)&email_len, sizeof(uint16_t));
    start_packet_bytes_written += sizeof(uint16_t);
    memcpy(packet + start_packet_bytes_written, email, email_len);
    start_packet_bytes_written += email_len;

    while (server_response_code == DATA_WAIT) {

        // packet structure: \"DATA\" + token + (uint16_t)email_len + email + (uint16_t)data_len + partial vault

        uint16_t data_to_send_len = MIN(VAULT_CHUNK_SIZE, vault_len - bytes_sent);

        int packet_bytes_written = start_packet_bytes_written;
        memcpy(packet + packet_bytes_written, (uint8_t *)&data_to_send_len, sizeof(uint16_t));
        packet_bytes_written += sizeof(uint16_t);
        memcpy(packet + packet_bytes_written, vault_buf + bytes_sent, data_to_send_len);
        packet_bytes_written += data_to_send_len;

        data_sent_len = send_packet(packet, packet_bytes_written, (uint8_t *)response_buf, sizeof(response_buf));
        if (data_sent_len < 0) {
            free(raw_token);
            free(packet);
            util_log(LOG_ERROR, "Failed to send data packet for vault transfer!");
            return -6;
        }
        bytes_sent += data_to_send_len;

        cJSON_Delete(server_response);
        server_response = cJSON_Parse(response_buf);
        if (!server_response) {
            free(raw_token);
            free(packet);
            util_log(LOG_ERROR, "Failed to parse server response to data!");
            return -7;
        }

        server_response_code = check_response_code(server_response);
        if (server_response_code < 0) {
            free(raw_token);
            free(packet);
            util_log(LOG_ERROR, "check_response_code returned < 0!");
            return -8;
        }
    }

    free(raw_token);
    free(packet);

    if (server_response_code) // caller must handle server-side error
        return server_response_code;

    cJSON *bytes_written_obj = cJSON_GetObjectItem(server_response, "bytes_written");
    if (!bytes_written_obj || !cJSON_IsNumber(bytes_written_obj)) {
        cJSON_Delete(server_response);
        util_log(LOG_ERROR, "Missing/invalid bytes_written field in server response!");
        return -9;
    }

    if ((int)bytes_written_obj->valuedouble != vault_len) {
        cJSON_Delete(server_response);
        util_log(LOG_ERROR, "Server received a different number of bytes than client sent!");
        return -10;
    }

    cJSON_Delete(server_response);

    return 0; // response ok, server successfully received and stored new vault
}

int remote_sync_account(Account *acc, KeyRing *keys, char *passwd) {
    if (!try_pwmngr_connect()) {
        util_log(LOG_ERROR, "Failed to connect to remote server!");
        return -1;
    }

    uint8_t *salt         = NULL;
    uint64_t auth_seed_id = 0;
    uint8_t *rk           = NULL;

    int64_t tmp = acc->server_last_modified;

    int acc_info_res = remote_get_account_info(acc->email, &tmp, &salt, &auth_seed_id, &rk);

    acc->server_last_modified = tmp;

    if (!acc_info_res) {
        // TODO: make sure vault is synced (with new salt), then
        if (token_b64) {
            free(token_b64);
            token_b64 = NULL;
        }
        memcpy(acc->salt, salt, SALT_LEN);
        acc->auth_seed_id = auth_seed_id;
        memcpy(acc->recovery_key, rk, RK_LEN);
        if (load_keys(keys, passwd, acc->salt, acc->auth_seed_id)) {
            util_log(LOG_ERROR, "Failed to reload keys!");
            return -2;
        }
    } else if (acc_info_res != E_CLIENTUPTODATE) {
        util_log(LOG_ERROR, "Failed to get remote account info! (call returned %d)", acc_info_res);
        pwmngr_disconnect();
        return -3;
    } // else: already up to date

    if (!token_b64) { // authentication required

        int auth_res = remote_auth(acc->email, keys->auth_sk, &token_b64);
        if (auth_res) {
            util_log(LOG_ERROR, "Failed to authenticate with server! (code %d)", auth_res);
            pwmngr_disconnect();
            return -4;
        }

        if (!save_token()) {
            util_log(LOG_ERROR, "Failed to save new token!");
            pwmngr_disconnect();
            return -5;
        }
    }

    time_t   tmstamp          = 0;
    uint8_t *uname_hash       = NULL;
    uint8_t *passwd_hash      = NULL;
    uint8_t *imp_salt         = NULL;
    uint8_t *auth_pk          = NULL;
    uint64_t imp_auth_seed_id = 0;

    int import_res = remote_import_account(acc->email, token_b64, &tmstamp, &uname_hash, &passwd_hash, &imp_salt,
                                           &auth_pk, &imp_auth_seed_id);
    if (import_res == E_INTERNALERR) {
        util_log(LOG_WARN, "Server returned E_INTERNALERR; retrying request...");
        import_res = remote_import_account(acc->email, token_b64, &tmstamp, &uname_hash, &passwd_hash, &imp_salt,
                                           &auth_pk, &imp_auth_seed_id);
    }

    if (import_res == E_INVALIDTOKEN || import_res == E_EXPIREDTOKEN) {

        free(token_b64);
        token_b64    = NULL;
        int auth_res = remote_auth(acc->email, keys->auth_sk, &token_b64);
        if (auth_res) {
            util_log(LOG_ERROR, "Failed to authenticate with server! (code %d)", auth_res);
            pwmngr_disconnect();
            return -4;
        }

        if (!save_token()) {
            util_log(LOG_ERROR, "Failed to save new token!");
            pwmngr_disconnect();
            return -5;
        }

        import_res = remote_import_account(acc->email, token_b64, &tmstamp, &uname_hash, &passwd_hash, &imp_salt,
                                           &auth_pk, &imp_auth_seed_id);
        if (import_res == E_INTERNALERR) {
            util_log(LOG_WARN, "Server returned E_INTERNALERR; retrying request...");
            import_res = remote_import_account(acc->email, token_b64, &tmstamp, &uname_hash, &passwd_hash, &imp_salt,
                                               &auth_pk, &imp_auth_seed_id);
        }
    }

    pwmngr_disconnect();

    if (import_res < 0) {
        util_log(LOG_ERROR, "Failed to import remote account! (call returned %d)", import_res);
        return -5;
    }

    if (import_res) { // at this point, all recoverable errors have been handled
        util_log(LOG_ERROR, "Failed to import remote account! (code %d)", import_res);
        return -6;
    }

    // success

    acc->server_last_modified = tmstamp;
    memcpy(acc->uname_hash, uname_hash, UNAME_HASH_LEN);
    memcpy(acc->passwd_hash, passwd_hash, PASSWD_HASH_LEN);

    return 0;
}

void remote_add_job(SyncJob *job) {

    pthread_mutex_lock(&jq_mut);
    int next_tail = (jq_tail + 1) % MAX_JOBS;
    if (next_tail == jq_head) {
        util_log(LOG_ERROR, "Job queue full, dropping job");
        pthread_mutex_unlock(&jq_mut);
        if (job->on_completion)
            job->on_completion(-1, "Job queue full", NULL);
        return;
    }

    job_queue[jq_tail] = *job;
    jq_tail            = next_tail;

    pthread_cond_signal(&jq_cond);
    pthread_mutex_unlock(&jq_mut);
}

void *cloud_thread(void *arg) {

    (void)(arg);

    while (1) {
        pthread_mutex_lock(&jq_mut);

        while (jq_head == jq_tail)
            pthread_cond_wait(&jq_cond, &jq_mut);

        SyncJob job = job_queue[jq_head];
        jq_head     = (jq_head + 1) % MAX_JOBS;

        pthread_mutex_unlock(&jq_mut);

        switch (job.type) {
        case SYNC_ACCOUNT_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case CREATE_ACC_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case FIN_CREATE_ACC_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case UPDATE_ACC_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case FIN_UPDATE_ACC_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case DELETE_ACC_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case REQ_CODE_RESEND_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        case PUSH_VAULT_JOB:
            if (job.on_completion)
                job.on_completion(-1, "Unimplemented", NULL);
            break;
        default:
            if (job.on_completion)
                job.on_completion(-1, "Invalid job!", NULL);
            break;
        }
    }

    return NULL;
}

#endif