#ifndef NO_CLOUD

#include "storage.h" // for Account & KeyRing
#include <stdint.h>
#include <time.h>

typedef enum JobType {
    SYNC_ACCOUNT_JOB,
    CREATE_ACC_JOB,
    FIN_CREATE_ACC_JOB,
    UPDATE_ACC_JOB,
    FIN_UPDATE_ACC_JOB,
    DELETE_ACC_JOB,
    REQ_CODE_RESEND_JOB,
    PUSH_VAULT_JOB,
} JobType;

typedef void(sync_job_completion_cb)(const int, const char *, void *);

typedef struct SyncJob {
    JobType                 type;
    void                   *args;
    sync_job_completion_cb *on_completion;
} SyncJob;

int  pwmngr_connect(const char *server_ip, uint16_t port);
void pwmngr_disconnect();

int remote_get_server_info(char **version, char **info, int *smtp);
int remote_create_account(char *email, uint8_t *username_hash, uint8_t *password_hash, uint8_t *auth_pk, uint8_t *rk,
                          uint8_t *salt, uint64_t auth_seed_id, time_t *timestamp, char **token_out);
int remote_update_account(char *current_email, char *token_b64, char *email, uint8_t *username_hash,
                          uint8_t *password_hash, uint8_t *auth_pk, uint8_t *recovery_key, uint8_t *salt,
                          uint64_t auth_seed_id);
int remote_request_code_resend(char *email, int mode, char *old_email, char *token_b64);
int remote_finish_create_account(int verification_code, char *email, time_t *timestamp, char **token_out);
int remote_finish_update_account(int verification_code, char *email, char *old_email, char *token_b64,
                                 time_t *timestamp);
int remote_auth(char *email, uint8_t *auth_sk, char **token_out);
int remote_invalidate_tokens(char *email, char *token_b64);
int remote_get_account_info(char *email, time_t *last_known_timestamp, uint8_t **salt, uint64_t *auth_seed_id,
                            uint8_t **rk);
int remote_import_account(char *email, char *token_b64, time_t *last_known_timestamp, uint8_t **uname_hash,
                          uint8_t **passwd_hash, uint8_t **salt, uint8_t **auth_pk, uint64_t *auth_seed_id);
int remote_delete_account(char *email, char *token_b64);
int remote_get_vault(char *email, char *token_b64, time_t last_known_timestamp, int force, uint8_t **vault_buf,
                     int *vault_len);
int remote_push_vault(char *email, char *token_b64, uint8_t *vault_buf, int vault_len);

int remote_sync_account(Account *acc, KeyRing *keys, char *passwd);

void remote_add_job(SyncJob *job);

void *cloud_thread(void *arg);

#endif