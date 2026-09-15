#include <stdbool.h>

enum Commands {
    LOGIN_CMD,
    LOGOUT_CMD,
    PORT_CMD,
    MKACCOUNT_CMD,
    RMACCOUNT_CMD,
    CHANGEPASSWORD_CMD,
    CHANGEUSERNAME_CMD,
    SEARCH_CMD,
    VIEWENTRY_CMD,
    DUMPENTRIES_CMD,
    MKENTRY_CMD,
    RMENTRY_CMD,
    GENPASS_CMD,
    IMPORT_CMD,
    EXPORT_CMD,
    INSPECT_CMD,
    RANDOM_CMD,
    MKKEY_CMD,
    HASH_CMD,
    PWHASH_CMD,
    BASE64ENCODE_CMD,
    BASE64DECODE_CMD,
    ENCRYPT_CMD,
    DECRYPT_CMD,
    HELP_CMD,
};

#define REQUIRES_LOGIN                                                                                                 \
    if (!logged_in) {                                                                                                  \
        printf("You must be logged in to run this command!\n");                                                        \
        return -1;                                                                                                     \
    }
#define REQUIRES_LOGOUT                                                                                                \
    if (logged_in) {                                                                                                   \
        printf("You must be logged out to run this command!\n");                                                       \
        return -1;                                                                                                     \
    }

typedef enum ArgumentType {
    STRING,  // cmd arg
    NUMBER,  // cmd 0
    KEYVAL,  // cmd arg="hello, world!"
    FLAG,    // cmd --arg
    FLAGWVAL // cmd --arg val
} ArgumentType;

typedef struct Argument {
    char        *name;
    ArgumentType type;
    int          excl_group; // exclusion group: 0 = optional arg, > 0 = exactly one arg from group is required
    void        *value;
} Argument;

typedef struct Command {
    int       id;
    char     *name;
    int       num_aliases;
    char    **aliases;
    int       num_args;
    Argument *args;
    char     *help_msg;
} Command;

typedef struct SearchResult {
    int   frequency;
    int   id;
    char *service;
} SearchResult;

int  cli_run_shell();
bool cli_init();
