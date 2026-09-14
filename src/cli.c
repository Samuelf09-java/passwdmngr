#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#endif

#include "cli.h"
#include "crypto.h"
#include "linenoise.h"
#include "storage.h"
#include "util.h"
#include <json-glib/json-glib.h>
#include <sodium.h>
#include <sys/stat.h>

static char *cmd_prompt = NULL;
static char *histfile   = NULL;

static Command *commands  = NULL;
static int      num_cmds  = 0;
static bool     logged_in = false;

static int run_logout();
static int run_export(char *path, char *id_list);

static char *load_help_file(const char *cmd) {
    GError *error = NULL;

    char *path = ec_malloc(strlen("/com/samuelf09/passwdmngr/.help.txt") + strlen(cmd) + 1);
    sprintf(path, "/com/samuelf09/passwdmngr/%s.help.txt", cmd);

    GBytes *bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    free(path);

    if (!bytes)
        return NULL;

    gsize       size;
    const char *data = g_bytes_get_data(bytes, &size);

    char *copy = ec_malloc(size + 1);
    memcpy(copy, data, size);
    copy[size] = '\0';

    g_bytes_unref(bytes);
    return copy;
}

static bool read_password(char *buf, size_t buflen) {
#ifdef _WIN32
    // Windows implementation
    DWORD  mode   = 0;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(hStdin, &mode);
    SetConsoleMode(hStdin, mode & ~ENABLE_ECHO_INPUT);

    if (!fgets(buf, buflen, stdin))
        return false;

    SetConsoleMode(hStdin, mode); // restore echo

#elif defined(__unix__) || defined(__APPLE__)
    // POSIX implementation
    struct termios oldt, newt;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;

    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (!fgets(buf, buflen, stdin))
        return false;

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // restore echo
#else
    // Fallback: echoing cannot be disabled
    if (!fgets(buf, buflen, stdin))
        return false;
#endif

    buf[strcspn(buf, "\n")] = 0;
    return true;
}

static inline bool is_number(const char *s) {
    if (!s || !*s)
        return false;
    for (const char *p = s; *p; p++)
        if (!is_digit(*p))
            return false;
    return true;
}

static bool is_duplicate_service(char *s) {
    for (int i = 0; i < num_entries; i++)
        if (!strcmp(entries[i].service, s))
            return true;
    return false;
}

static int compare_search_results(const void *a, const void *b) {

    if (a == NULL && b == NULL)
        return 0;
    if (a == NULL)
        return 1;
    if (b == NULL)
        return -1;

    const SearchResult *sa = a;
    const SearchResult *sb = b;

    if (sa->frequency < sb->frequency)
        return 1;
    if (sa->frequency > sb->frequency)
        return -1;
    if (sa->id > sb->id)
        return 1;
    if (sa->id < sb->id)
        return -1;
    return 0;
}

static bool parse_id_list(char *id_list, int *out, int *num_ids) {
    char numbuf[32];
    int  n = 0;

    for (char *p = id_list;; p++) {

        if (*p == ',' || *p == '\0') {
            if (n == 0) {
                util_log(ERROR, "Failed to parse id_list: empty number!");
                return false;
            }

            numbuf[n]     = '\0';
            out[*num_ids] = atoi(numbuf);
            (*num_ids)++;
            n = 0;

            for (int i = 0; i < *num_ids - 1; i++) {
                if (out[i] == out[*num_ids - 1]) {
                    util_log(ERROR, "Failed to parse id_list: duplicate id!");
                    return false;
                }
            }

            if (*p == '\0')
                break;

            continue;
        }

        if (!is_digit(*p)) {
            util_log(ERROR, "Failed to parse id_list: invalid format!");
            return false;
        }

        if (n < (int)sizeof(numbuf) - 1)
            numbuf[n++] = *p;
        else {
            util_log(ERROR, "Failed to parse id_list: number too long!");
            return false;
        }
    }

    return true;
}

// assumes hex + 1 exists (should always be true since it is called from hex_to_raw after validation)
static uint8_t get_raw_byte(char *hex) {
    uint8_t out = 0;
    for (int i = 0; i < 2; i++) {

        switch (hex[i]) {
        case '0':
            out += 0;
            break;

        case '1':
            out += i ? 1 : 1 * 16;
            break;

        case '2':
            out += i ? 2 : 2 * 16;
            break;

        case '3':
            out += i ? 3 : 3 * 16;
            break;

        case '4':
            out += i ? 4 : 4 * 16;
            break;

        case '5':
            out += i ? 5 : 5 * 16;
            break;

        case '6':
            out += i ? 6 : 6 * 16;
            break;

        case '7':
            out += i ? 7 : 7 * 16;
            break;

        case '8':
            out += i ? 8 : 8 * 16;
            break;

        case '9':
            out += i ? 9 : 9 * 16;
            break;

        case 'a':
        case 'A':
            out += i ? 10 : 10 * 16;
            break;

        case 'b':
        case 'B':
            out += i ? 11 : 11 * 16;
            break;

        case 'c':
        case 'C':
            out += i ? 12 : 12 * 16;
            break;

        case 'd':
        case 'D':
            out += i ? 13 : 13 * 16;
            break;

        case 'e':
        case 'E':
            out += i ? 14 : 14 * 16;
            break;

        case 'f':
        case 'F':
            out += i ? 15 : 15 * 16;
            break;

        default:
            util_log(ERROR, "Invalid hex char! (bug)");
            return 0;
        }
    }

    return out;
}

static uint8_t *hex_to_raw(char *hex_str, int *raw_len) {
    int len = 0;
    for (int r = 0; hex_str[r]; r++)
        if (hex_str[r] != ' ' && hex_str[r] != '\t' && hex_str[r] != '\n' && hex_str[r] != '\r')
            hex_str[len++] = hex_str[r];

    hex_str[len] = '\0';

    for (int i = 0; i < len; i++) {
        if (!is_hex(hex_str[i])) {
            util_log(ERROR, "Invalid hex string!");
            return NULL;
        }
    }

    if (len % 2) {
        util_log(ERROR, "Cannot convert to raw bytes: uneven number of hex digits");
        return NULL;
    }

    *raw_len     = len / 2;
    uint8_t *out = ec_malloc(*raw_len);

    for (int i = 0; i < len; i += 2)
        out[i / 2] = get_raw_byte(hex_str + i);

    return out;
}

static void print_hex(uint8_t *data, int data_len, bool compact) {
    for (int i = 0; i < data_len; i++) {
        printf("%02x", data[i]);

        if (!compact && !((i + 1) % 32) && i < data_len - 1)
            printf("\n");
        else if (!compact && i < data_len - 1)
            printf(" ");
    }
}

static int run_login(char *uname) {

    if (logged_in)
        run_logout();

    if (!uname) {
        uname = ec_calloc(MAX_UNAME_LEN + 1, sizeof(char)); // 24 char uname + \0
        printf("Enter username: ");
        if (!fgets(uname, MAX_UNAME_LEN + 1, stdin))
            return -2;

        uname[strcspn(uname, "\n")] = 0;
    }

    char *passwd = ec_calloc(MAX_PASSWD_LEN + 1, sizeof(char)); // 36 char passwd + \0
    printf("Enter password (will not be echoed): ");
    read_password(passwd, MAX_PASSWD_LEN + 1);
    printf("\n");

    if (verify_account(uname, passwd)) {
        free(cmd_prompt);
        cmd_prompt = ec_malloc(strlen("passwdmngr - ->") + strlen(uname) + 1);
        sprintf(cmd_prompt, "passwdmngr - %s->", uname);
        username   = strdup(uname);
        tmp_passwd = strdup(passwd);

        if (!storage_read_user_vault()) {
            util_log(FATAL, "Failed to read user vault!");
            return -2;
        }

        curr_prefs = get_user_prefs(uname);
        if (!curr_prefs) {
            util_log(FATAL, "Failed to load user preferences!");
            return -3;
        }

        logged_in = true;
        util_log(INFO, "User %s: Login successful", uname);

        free(uname);
        commands[LOGIN_CMD].args[0].value = NULL;
        free(passwd);
    } else {
        util_log(ERROR, "Invalid username or password");

        free(uname);
        commands[LOGIN_CMD].args[0].value = NULL;
        free(passwd);

        return -1;
    }

    return 0;
}

static int run_logout() {

    REQUIRES_LOGIN

    logged_in = false;

    util_log(INFO, "Logging out user %s", username);

    wipe_passwd_entries(entries, num_entries);
    entries     = NULL;
    num_entries = 0;

    if (tmp_passwd) {
        wipe_mem(tmp_passwd, strlen(tmp_passwd));
        free(tmp_passwd);
        tmp_passwd = NULL;
    }

    wipe_mem(aes_key, sizeof(aes_key));
    key_set = false;

    free(username);
    username = NULL;
    free(cmd_prompt);
    cmd_prompt = strdup("passwdmngr->");

    if (curr_prefs) {
        free(curr_prefs);
        curr_prefs = NULL;
    }

    util_log(DEBUG, "Cleared all user-specific globals from storage.c");

    return 0;
}

static int run_port(char *vault) {
    X(vault);

    printf("No port functions registered (nothing to do yet)\n");

    return 0;
}

static int run_mkaccount(char *new_uname, char *new_passwd, bool nologin) {

    REQUIRES_LOGOUT

    if (!new_uname) {
        new_uname = ec_calloc(MAX_UNAME_LEN + 1, sizeof(char));
        printf("Enter new username: ");
        if (!fgets(new_uname, MAX_UNAME_LEN + 1, stdin))
            return -2;
        new_uname[strcspn(new_uname, "\n")] = 0;
    }

    if (!new_passwd) {
        new_passwd = ec_calloc(MAX_PASSWD_LEN + 1, sizeof(char));
        printf("Enter new password (will not be echoed): ");
        read_password(new_passwd, MAX_PASSWD_LEN + 1);
        printf("\n");
    }

    tmp_passwd = strdup(new_passwd);

    if (!create_new_account(new_uname, new_passwd)) {
        free(new_uname);
        wipe_mem(new_passwd, strlen(new_passwd));
        free(new_passwd);
        return -1;
    }

    wipe_mem(new_passwd, strlen(new_passwd));
    free(new_passwd);

    if (nologin) { // return early
        if (tmp_passwd) {
            wipe_mem(tmp_passwd, strlen(tmp_passwd));
            free(tmp_passwd);
            tmp_passwd = NULL;
        }

        wipe_mem(aes_key, sizeof(aes_key));
        key_set = false;

        if (entries) {
            wipe_passwd_entries(entries, num_entries);
            entries = NULL;
        }

        free(username);
        username = NULL;

        if (curr_prefs) {
            free(curr_prefs);
            curr_prefs = NULL;
        }

        free(new_uname);

        return 0;
    }

    if (!storage_read_user_vault()) {
        util_log(FATAL, "Failed to read user vault!");
        return -4;
    }

    curr_prefs = get_user_prefs(new_uname);
    if (!curr_prefs) {
        util_log(FATAL, "Failed to load user preferences!");
        free(new_uname);
        return -3;
    }

    logged_in = true;

    free(new_uname);
    return 0;
}

static int run_rmaccount(bool force) {

    REQUIRES_LOGIN

    if (!force) {
        printf("Are you sure you want to delete your account? This is permanent [Y/n]: ");
        char ans = fgetc(stdin);
        printf("\n");
        if (ans != 'Y') {
            printf("Canceled!\n");
            return 0;
        }

        char c;
        while ((c = fgetc(stdin)) != '\n' && c != EOF)
            ;

        printf("Enter your password to confirm account deletion (will not be echoed): ");
        char buf[MAX_PASSWD_LEN + 1];
        if (!read_password(buf, sizeof(buf))) {
            util_log(ERROR, "Failed to read password from user input!");
            return -3;
        }

        if (!verify_account(username, buf)) {
            util_log(ERROR, "Failed to delete account: Incorrect password");
            return -1;
        }

        printf("\n");
    }

    if (!storage_delete_account(username)) {
        util_log(ERROR, "Failed to delete account");
        return -2;
    }

    return run_logout();
}

static int run_changepassword() {

    REQUIRES_LOGIN

    char curr_pass[MAX_PASSWD_LEN + 1];
    printf("Enter your current password (will not be echoed): ");
    if (!read_password(curr_pass, sizeof(curr_pass))) {
        util_log(ERROR, "Failed to read data from stdin!");
        return -3;
    }
    printf("\n");

    if (!strlen(curr_pass)) {
        util_log(ERROR, "No password provided!");
        return -4;
    }

    if (!verify_account(username, curr_pass)) {
        util_log(ERROR, "Wrong password!");
        return -5;
    }

    char new_pass[MAX_PASSWD_LEN + 1];
    printf("Enter your new password (will not be echoed): ");
    if (!read_password(new_pass, sizeof(new_pass))) {
        util_log(ERROR, "Failed to read data from stdin!");
        return -3;
    }
    printf("\n");

    if (!strlen(new_pass)) {
        util_log(ERROR, "Password cannot be blank!");
        return -6;
    }

    if (!strcmp(curr_pass, new_pass)) {
        util_log(WARN, "New password is the same as old password!");
        return -7;
    }

    char conf_new_pass[MAX_PASSWD_LEN + 1];
    printf("Confirm new password (will not be echoed): ");
    if (!read_password(conf_new_pass, sizeof(conf_new_pass))) {
        util_log(ERROR, "Failed to read data from stdin!");
        return -3;
    }
    printf("\n");

    if (strcmp(new_pass, conf_new_pass)) {
        util_log(ERROR, "Passwords do not match!");
        return -8;
    }

    if (!storage_change_passwd(username, new_pass)) {
        util_log(FATAL, "Failed to change password!");
        return -9;
    }

    util_log(INFO, "Successfully changed password");
    return 0;
}

static int run_changeusername(char *new_uname) {

    REQUIRES_LOGIN

    printf("\nWARNING: This operation will change your\n"
           "username, which will also change its hash,\n"
           "which is used as the name of your vault.\n"
           "As a result, any references you have to your\n"
           "vault will become invalid.\n\n"
           "Do you want to proceed? [Y/n]: ");

    char buf[3];
    buf[0] = buf[1] = 0;
    ec_fgets(buf, sizeof(buf), stdin);
    buf[strcspn(buf, "\n")] = 0;

    if (buf[0] != 'Y' || buf[1]) {
        printf("Canceled!\n");
        return 0;
    }

    char *uname = NULL;

    if (!new_uname) {
        uname = ec_calloc(MAX_UNAME_LEN + 1, sizeof(char));
        printf("Enter new username: ");
        ec_fgets(uname, MAX_UNAME_LEN + 1, stdin);
        uname[strcspn(uname, "\n")] = 0;
    } else
        uname = strdup(new_uname);

    if (!uname[0]) {
        printf("Invalid username! (length 0)\n");
        free(uname);
        return -2;
    }

    char passwd_buf[MAX_PASSWD_LEN + 1];
    printf("Enter your password to confirm change (will not be echoed): ");
    if (!read_password(passwd_buf, sizeof(passwd_buf))) {
        util_log(ERROR, "Failed to read password from stdin!");
        free(uname);
        return -3;
    }
    printf("\n");

    if (!verify_account(username, passwd_buf)) {
        util_log(ERROR, "Failed to change username: incorrect password!");
        free(uname);
        return -4;
    }

    uint8_t *old_hash = sha_256_hash((uint8_t *)username, strlen(username));
    uint8_t *new_hash = sha_256_hash((uint8_t *)uname, strlen(uname));

    Account *acc = NULL;
    for (int i = 0; i < num_accounts; i++) {
        if (!memcmp(accounts[i].uname_hash, new_hash, HASH_LEN)) {
            util_log(ERROR, "username '%s' already exists!", uname);
            free(old_hash);
            free(new_hash);
            free(uname);
            return -5;
        }

        if (!acc && !memcmp(accounts[i].uname_hash, old_hash, HASH_LEN))
            acc = &accounts[i];
    }

    free(old_hash);

    if (!acc) {
        util_log(ERROR, "Could not find old username!");
        free(new_hash);
        return -6;
    }

    save_accounts();

    char *old_path = storage_get_user_vault_path(username);
    char *new_path = storage_get_user_vault_path(uname);

    if (!old_path || !new_path) {
        util_log(ERROR, "Failed to derive old/new vault paths!");
        free(new_hash);
        return -7;
    }

    /*
     * windows will fail if new_path exists, but this would only happen if
     * there was a hash collision or malicious modification of $appdir/vaults/
     * (in which case there are worse things that could happen anyway)
     */
    if (rename(old_path, new_path)) {
        util_log(ERROR, "Failed to rename vault!");
        free(new_hash);
        free(old_path);
        free(new_path);
        return -8;
    }

    free(old_path);
    free(new_path);

    char *old_hash_str = hash_uname(username);
    char *new_hash_str = hash_uname(uname);

    memcpy(acc->uname_hash, new_hash, HASH_LEN);
    free(new_hash);
    util_log(INFO, "Changed username from %s to %s (%s to %s)", username, uname, old_hash_str, new_hash_str);
    free(old_hash_str);
    free(new_hash_str);
    free(username);
    username = uname;
    free(cmd_prompt);
    cmd_prompt = ec_malloc(strlen("passwdmngr - ->") + strlen(uname) + 1);
    sprintf(cmd_prompt, "passwdmngr - %s->", uname);

    return 0;
}

/*
 * NOTE:
 * I currently use simple case-insensitive searching to search entries,
 * but it would be nice to include regex search functionality in the
 * future, either through an external library or embedded parser
 */
static int run_search(char *search_text, bool deep) {

    REQUIRES_LOGIN

    util_assert(search_text != NULL, "search_text is null!");

    SearchResult *results = ec_calloc(num_entries, sizeof(SearchResult));

    for (int i = 0; i < num_entries; i++) {
        int hits = 0;
        hits += count_substrings(entries[i].service, search_text);
        if (deep) {
            hits += count_substrings(entries[i].username, search_text);
            hits += count_substrings(entries[i].password, search_text);
            hits += count_substrings(entries[i].notes, search_text);
        }

        if (hits) {
            results[i].frequency = hits;
            results[i].id        = entries[i].id;
            results[i].service   = entries[i].service;
        }
    }

    qsort(results, num_entries, sizeof(SearchResult), compare_search_results);

    printf("\nSearch results:\n\n");

    for (int i = 0; i < num_entries; i++) {
        if (!results[i].service)
            break;

        printf("[%d]: Service %s, id %d (%d matches)\n", i, results[i].service, results[i].id, results[i].frequency);
    }

    printf("\n");

    return 0;
}

/*
 * NOTE:
 * if there are entries with numerical service names, viewentry [service]
 * will be interpreted as an id, not a service; however, this is unlikely
 * enough that it is not currently handled in any way (the user can just
 * provide the id of the entry instead if necessary)
 */
static int run_viewentry(char *service, int id) {

    REQUIRES_LOGIN

    PasswdEntry *e = NULL;
    if (!service)
        e = storage_get_entry(id);
    else // lookup entry by name
        for (int i = 0; i < num_entries; i++)
            if (!strcmp(entries[i].service, service)) {
                e = entries + i;
                break;
            }

    if (!e) {
        printf("No entry found for ");
        if (service)
            printf("service '%s'\n", service);
        else
            printf("id '%d'\n", id);

        return -1;
    }

    printf("Found entry with id '%d'\n\n"
           " Service: %s\n"
           "Username: %s\n"
           "Password: %s\n\n",
           e->id, e->service, e->username, e->password);

    if (e->notes && strlen(e->notes))
        printf("Notes:\n%s\n\n", e->notes);

    return 0;
}

static int run_dumpentries(char *vault, bool pretty_print) {

    bool vault_mem_owned = false;

    if (!vault) { // try current user's vault
        if (!logged_in) {
            printf("You are not logged in, and no vault is specified!\n");
            return -1;
        }

        vault           = storage_get_user_vault_path(username);
        vault_mem_owned = true;
    }

    char *out;
    if (logged_in) { // try current user's key
        int res1 = storage_dump_json(vault, &out, NULL, pretty_print);
        if (res1 < 0) {
            if (res1 == -7) { // decryption failed, possibly invalid key
                printf("Failed to decrypt vault with your password!\n");
                goto try_with_passwd;
            } else {
                util_log(ERROR, "Failed to read vault from provided path!");
                if (vault_mem_owned)
                    free(vault);
                return res1;
            }
        }
        goto print_dump; // succeeded
    }

try_with_passwd:
    printf("Enter the password the vault was encrypted with (will not be echoed): ");
    char passwd_buf[MAX_PASSWD_LEN + 1];
    read_password(passwd_buf, sizeof(passwd_buf));
    printf("\n");

    uint8_t key[HASH_LEN];
    FILE   *fp = fopen(vault, "rb");
    if (!fp) {
        util_log(ERROR, "Failed to open vault");
        if (vault_mem_owned)
            free(vault);
        return -4;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= (int64_t)HEADER_LEN) {
        util_log(ERROR, "Vault is too small to contain any data!");
        if (vault_mem_owned)
            free(vault);
        return -5;
    }

    VaultHeader *hdr = ec_malloc(HEADER_LEN);
    ec_fread(hdr, 1, HEADER_LEN, fp);
    fclose(fp);

    if (!derive_vault_key(passwd_buf, hdr->salt, key, sizeof(key))) {
        util_log(ERROR, "Failed to derive key from provided password and salt!");
        free(hdr);
        if (vault_mem_owned)
            free(vault);
        return -3;
    }

    free(hdr);

    int res2 = storage_dump_json(vault, &out, key, pretty_print);

    if (res2 < 0) {
        util_log(ERROR, "Failed to read vault '%s'!", vault);
        if (vault_mem_owned)
            free(vault);
        return res2;
    }

print_dump:
    if (vault_mem_owned)
        free(vault);

    printf("Entries dump:\n%s\n\n", out);

    if (pretty_print)
        g_free(out);
    else
        free(out);

    return 0;
}

static int run_mkentry(char *service, char *username, char *password, char *notes, bool no_prompt) {

    REQUIRES_LOGIN

    // service is required, it cannot be null
    util_assert(service != NULL, "Service is null!");

    bool username_owned, password_owned, notes_owned;
    username_owned = password_owned = notes_owned = false;

    if (!no_prompt) {
        if (!username) {
            username       = ec_calloc(257, sizeof(char));
            username_owned = true;
            printf("Enter (optional) data for entry's username field: ");
            if (!fgets(username, 257, stdin))
                return -2;
            username[strcspn(username, "\n")] = 0;
        }

        if (!password) {
            password       = ec_calloc(257, sizeof(char));
            password_owned = true;
            printf("Enter (optional) data for entry's password field: ");
            if (!fgets(password, 257, stdin))
                return -2;
            password[strcspn(password, "\n")] = 0;
        }

        if (!notes) {
            notes       = ec_calloc(513, sizeof(char));
            notes_owned = true;
            printf("Enter (optional) data for entry's notes field: ");
            if (!fgets(notes, 513, stdin))
                return -2;
            printf("\n");
            notes[strcspn(notes, "\n")] = 0;
        }
    } else {
        if (!username) {
            username       = ec_malloc(1);
            username_owned = true;
            *username      = 0;
        }
        if (!password) {
            password       = ec_malloc(1);
            password_owned = true;
            *password      = 0;
        }
        if (!notes) {
            notes       = ec_malloc(1);
            notes_owned = true;
            *notes      = 0;
        }
    }

    PasswdEntry *e = ec_calloc(1, sizeof(PasswdEntry));

    e->id       = storage_get_next_id();
    e->service  = service;
    e->username = username;
    e->password = password;
    e->notes    = notes;

    if (!add_entry(e)) {
        if (username_owned)
            free(username);
        if (password_owned)
            free(password);
        if (notes_owned)
            free(notes);
        free(e);
        return -1;
    }

    printf("Successfully added new entry '%s' with id '%d'\n", service, e->id);

    if (username_owned)
        free(username);
    if (password_owned)
        free(password);
    if (notes_owned)
        free(notes);
    free(e);

    return 0;
}

static int run_rmentry(char *service, int id, bool force) {

    REQUIRES_LOGIN

    PasswdEntry *e = NULL;
    if (!service)
        e = storage_get_entry(id);
    else // lookup entry by name
        for (int i = 0; i < num_entries; i++)
            if (!strcmp(entries[i].service, service)) {
                e = entries + i;
                break;
            }

    if (!e) {
        printf("No entry found for ");
        if (service)
            printf("service '%s'\n", service);
        else
            printf("id '%d'\n", id);

        return -2;
    }

    if (!force) {
        printf("Are you sure you want to delete entry '%s' with id '%d'? [Y/n]: ", e->service, e->id);
        char ans = fgetc(stdin);

        char c;
        while ((c = fgetc(stdin)) != '\n' && c != EOF)
            ;

        if (ans != 'Y') {
            printf("Canceled!\n");
            return 0;
        }
    }

    int eid = e->id;
    delete_entry(e->id);
    printf("Deleted entry with id '%d'\n", eid);

    return 0;
}

static int run_import(char *path, char *id_list, int mode) {

    REQUIRES_LOGIN

    // detect invalid path early
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("Invalid path!\n");
        return -2;
    }
    fclose(fp);

    int *import_ids = NULL;
    int  num_ids    = 0;

    if (id_list && strcmp(id_list, "*") && strcmp(id_list, "all")) {
        int max_ids = 1;
        for (char *p = id_list; *p; p++)
            if (*p == ',')
                max_ids++;

        import_ids = ec_calloc(max_ids, sizeof(int));
    }

    if (import_ids && !parse_id_list(id_list, import_ids, &num_ids)) {
        util_log(ERROR, "Failed to parse id_list");
        return -17;
    }

    PasswdEntry *import_entries = NULL;
    VaultHeader *hdr            = NULL;

    int num_import_entries = storage_read_vault(path, &import_entries, &hdr);

    if (num_import_entries == -7) { // wrong key
        printf("Failed to decrypt vault with your password!\n"
               "Please enter the password used to encrypt this\n"
               "vault (will not be echoed): ");

        char passwd_buf[MAX_PASSWD_LEN + 1];
        if (!read_password(passwd_buf, sizeof(passwd_buf))) {
            util_log(ERROR, "\nFailed to read password from command line!\n");
            if (hdr)
                free(hdr);
            if (import_ids)
                free(import_ids);
            return -10;
        }

        printf("\n");

        uint8_t key[32];
        if (!derive_vault_key(passwd_buf, hdr->salt, key, sizeof(key))) {
            util_log(ERROR, "Failed to derive key from user-provided password!");
            free(hdr);
            wipe_mem(passwd_buf, sizeof(passwd_buf));
            if (import_ids)
                free(import_ids);
            return -11;
        }

        wipe_mem(passwd_buf, sizeof(passwd_buf));

        num_import_entries = storage_read_vault_with_key(path, key, &import_entries, &hdr);
        wipe_mem(key, sizeof(key));
    }

    if (hdr)
        free(hdr);

    if (num_import_entries < 0) {
        util_log(ERROR, "Failed to read vault at %s", path);
        if (import_ids)
            free(import_ids);
        return num_import_entries;
    }

    if (num_import_entries == 0) {
        printf("Vault has no entries!\n");
        if (import_ids)
            free(import_ids);
        return -12;
    }

    int num_imports = 0;

    /*
     * yes, we could just swap the files, but then we would have to reencrypt the
     * imported entries anyway if they were encrypted with a different password
     */
    if (mode == REPLACE) {
        char      *appdir    = util_get_app_dir();
        time_t     now       = time(NULL);
        struct tm *curr_time = localtime(&now);
        char       backup_file[34];
        strftime(backup_file, sizeof(backup_file), "backup-%m-%d-%Y_%H-%M-%S.pwmngr", curr_time);
        char *backup_path = ec_malloc(strlen(appdir) + strlen(backup_file) + 1);
        sprintf(backup_path, "%s%s", appdir, backup_file);
        free(appdir);
        if (!run_export(backup_path, "*")) {
            util_log(ERROR, "Failed to export backup of current entries!");
            free(backup_path);
            if (import_ids)
                free(import_ids);
            return -13;
        }

        free(backup_path);

        wipe_passwd_entries(entries, num_entries);
        for (int i = 0; i < num_import_entries; i++) {
            if (import_ids) {
                for (int j = 0; j < num_ids; j++)
                    if (import_entries[i].id == import_ids[j]) {
                        add_entry(import_entries + i);
                        break;
                    }
            } else
                add_entry(import_entries + i);

            num_imports++;
        }

        if (!storage_write_user_vault()) {
            util_log(ERROR, "Failed to write updated vault!");
            if (import_ids)
                free(import_ids);
            return -14;
        }

        wipe_passwd_entries(import_entries, num_import_entries);
        if (import_ids)
            free(import_ids);

        util_log(INFO, "Imported %d entries from %s", num_imports, path);

        return 0;
    }

    // mode == OVERWRITE || PROMPT

    bool dupe;
    for (int i = 0; i < num_import_entries; i++) {

        bool include = !import_ids;

        if (import_ids) {
            include = false;
            for (int j = 0; j < num_ids; j++)
                if (import_entries[i].id == import_ids[j])
                    include = true;
        }

        if (!include)
            continue;

        dupe = false;

        for (int j = 0; j < num_entries; j++) {
            if (!strcmp(import_entries[i].service, entries[j].service)) { // duplicate service

                dupe = true;

                if (mode == OVERWRITE) {

                    import_entries[i].id = entries[j].id;
                    update_entry(entries[j].id, import_entries + i);
                    num_imports++;
                } else { // PROMPT
                    printf("Duplicate entry detected! (%s)\n"
                           "Choose one option:\n"
                           "[1] Keep existing entry\n"
                           "[2] Replace existing entry\n"
                           "[3] Rename new entry\n"
                           "[4] Cancel (quit import)\n"
                           "->",
                           import_entries[i].service);

                    char buf[7];
                    ec_fgets(buf, sizeof(buf), stdin);
                    buf[strcspn(buf, "\n")] = 0;

                    while (!buf[0] || buf[1] || (buf[0] != '1' && buf[0] != '2' && buf[0] != '3' && buf[0] != '4')) {
                        printf("Invalid input!\nPlease try again ->");
                        ec_fgets(buf, sizeof(buf), stdin);
                        buf[strcspn(buf, "\n")] = 0;
                    }

                    switch (buf[0]) {
                    case '1':
                        // keep (default; do nothing)
                        break;

                    case '2':
                        // replace
                        import_entries[i].id = entries[j].id;
                        update_entry(entries[j].id, import_entries + i);
                        num_imports++;
                        break;

                    case '3':
                        // rename
                        printf("Enter new name for imported entry: ");
                        char name_buf[129];
                        ec_fgets(name_buf, sizeof(name_buf), stdin);
                        name_buf[strcspn(name_buf, "\n")] = 0;
                        while (!name_buf[0] || is_duplicate_service(name_buf)) {
                            printf("Invalid name! (cannot be blank or duplicate)\nPlease try again: ");
                            ec_fgets(name_buf, sizeof(name_buf), stdin);
                            name_buf[strcspn(name_buf, "\n")] = 0;
                        }

                        free(import_entries[i].service);
                        import_entries[i].service = strdup(name_buf);
                        import_entries[i].id      = storage_get_next_id();

                        if (!add_entry(import_entries + i)) {
                            util_log(ERROR, "Failed to add renamed entry to array!");
                            wipe_passwd_entries(import_entries, num_import_entries);
                            if (import_ids)
                                free(import_ids);
                            return -16;
                        }
                        num_imports++;

                        break;

                    case '4':
                        // quit
                        printf("Import canceled!\n");

                        wipe_passwd_entries(import_entries, num_import_entries);
                        if (import_ids)
                            free(import_ids);

                        return 0; // not an error, just canceled

                    default:
                        printf("Invalid option! (bug)\n");
                        break;
                    }
                }

                break;
            }
        }

        if (!dupe) { // dupe logic is handled earlier
            import_entries[i].id = storage_get_next_id();
            add_entry(import_entries + i);
            num_imports++;
        }
    }

    wipe_passwd_entries(import_entries, num_import_entries);
    if (import_ids)
        free(import_ids);

    if (!storage_write_user_vault()) {
        util_log(ERROR, "Failed to write updated vault!");
        return -14;
    }

    util_log(INFO, "Imported %d entries from %s", num_imports, path);

    return 0;
}

static int run_export(char *path, char *id_list) {

    REQUIRES_LOGIN

    char *export_path = NULL;

    time_t     now       = time(NULL);
    struct tm *curr_time = localtime(&now);
    char       default_file[34];
    strftime(default_file, sizeof(default_file), "export-%m-%d-%Y_%H-%M-%S.pwmngr", curr_time);

    if (!path) {
        char *appdir = util_get_app_dir();
        export_path  = ec_malloc(strlen(appdir) + strlen(default_file) + 1);
        sprintf(export_path, "%s%s", appdir, default_file);
        free(appdir);
    } else if (path[strlen(path)] == '/') {
        export_path = ec_malloc(strlen(path) + strlen(default_file) + 1);
        sprintf(export_path, "%s%s", path, default_file);
    } else
        export_path = strdup(path);

    FILE *fp = fopen(export_path, "rb");
    if (fp) {
        fclose(fp);
        printf("File at '%s' already exists!\n", export_path);
        free(export_path);
        return -2;
    }

    int *export_ids = NULL;
    int  num_ids    = 0;

    if (id_list && strcmp(id_list, "*") && strcmp(id_list, "all")) {
        int max_ids = 1;
        for (char *p = id_list; *p; p++)
            if (*p == ',')
                max_ids++;

        export_ids = ec_calloc(max_ids, sizeof(int));
    }

    if (export_ids && !parse_id_list(id_list, export_ids, &num_ids)) {
        util_log(ERROR, "Failed to parse id_list");
        free(export_path);
        return -5;
    }

    if (!export_ids)
        num_ids = num_entries;

    PasswdEntry *export_entries = ec_malloc(sizeof(PasswdEntry) * num_ids);

    for (int i = 0; i < num_ids; i++) {
        PasswdEntry *e1 = export_ids ? storage_get_entry(export_ids[i]) : &entries[i];
        PasswdEntry *e2 = &export_entries[i];
        e2->id          = e1->id;
        e2->service     = strdup(e1->service);
        e2->username    = strdup(e1->username);
        e2->password    = strdup(e1->password);
        e2->notes       = strdup(e1->notes);
    }

    uint8_t *salt = get_user_salt();
    if (!util_check_ptr(salt, "Failed to get user salt for export")) {
        wipe_passwd_entries(export_entries, num_ids);
        return -4;
    }

    storage_write_vault(path, export_entries, num_ids, salt);

    util_log(INFO, "Exported %d entries to %s", num_ids, path);

    wipe_passwd_entries(export_entries, num_ids);
    free(salt);

    free(export_path);
    if (export_ids)
        free(export_ids);

    return 0;
}

static int run_inspect(char *path, bool hexdump) {

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("Invalid path/permissions!");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *data_buf = ec_malloc(fsize);

    ec_fread(data_buf, 1, fsize, fp);
    fclose(fp);

    if (fsize < 6) {
        util_log(ERROR, "File is too small to contain magic bytes!");
        return -2;
    }

    // handle accounts.bin-formatted files
    if (!memcmp(data_buf, ACCOUNTS_MAGIC, 6)) {

        util_log(DEBUG, "Handling file as accounts.bin format");

        if ((uint64_t)fsize < sizeof(AccountHeader)) {
            util_log(ERROR, "File is too small to contain accounts header");
            return -3;
        }

        AccountHeader *acc_hdr = (AccountHeader *)data_buf;

        if (hexdump) {

            printf("Header:\n");
            print_hex((uint8_t *)acc_hdr, sizeof(AccountHeader), false);
            printf("\n\n");

            printf("Magic: ");
            print_hex((uint8_t *)acc_hdr, sizeof(acc_hdr->magic), false);
            printf("\n");

            printf("Schema version: ");
            print_hex((uint8_t *)acc_hdr + sizeof(acc_hdr->magic), sizeof(acc_hdr->version), false);
            printf("\n");

            printf("Hash: ");
            print_hex((uint8_t *)acc_hdr + sizeof(acc_hdr->magic) + sizeof(acc_hdr->version), sizeof(acc_hdr->hash),
                      false);
            printf("\n");

            printf("Num_accounts: ");
            print_hex((uint8_t *)acc_hdr + sizeof(acc_hdr->magic) + sizeof(acc_hdr->version) + sizeof(acc_hdr->hash),
                      sizeof(acc_hdr->num_accounts), false);
            printf("\n\n");

            if ((fsize - sizeof(AccountHeader)) % sizeof(Account)) {
                printf("File is corrupted/truncated; printing raw remaining data:\n");
                print_hex(data_buf + sizeof(AccountHeader), fsize - sizeof(AccountHeader), false);
                printf("\n\n");
                free(data_buf);
                return 0;
            }

            int num_data_accounts = (fsize - sizeof(AccountHeader)) / sizeof(Account);
            for (uint8_t *p = data_buf + sizeof(AccountHeader);
                 p < data_buf + sizeof(AccountHeader) + num_data_accounts * sizeof(Account); p += sizeof(Account)) {
                printf("Account %lu:\n", (int)(p - (data_buf + sizeof(AccountHeader))) / sizeof(Account) + 1);
                printf("uname_hash: ");
                print_hex(p, HASH_LEN, false);
                printf("\npasswd_hash: ");
                print_hex(p + HASH_LEN, HASH_LEN + SALT_LEN, false);
                printf("\n\n");
            }

            free(data_buf);

            return 0;
        }

        // regular dump

        printf("Magic: %s (ok)\n", acc_hdr->magic);
        printf("Schema version: %d (", (int)acc_hdr->version);
        if (acc_hdr->version == ACCOUNTS_SCHEMA_VERSION)
            printf("ok)\n");
        else
            printf("invalid, expected %d)\n", ACCOUNTS_SCHEMA_VERSION);

        printf("Hash: ");
        print_hex(acc_hdr->hash, HASH_LEN, true);
        uint8_t *real_hash = sha_256_hash(data_buf + 10 + HASH_LEN, fsize - 10 - HASH_LEN);
        if (!util_check_ptr(real_hash, "Failed to hash accounts data")) {
            free(data_buf);
            return -3;
        }
        if (!memcmp(acc_hdr->hash, real_hash, HASH_LEN))
            printf(" (ok)\n");
        else {
            printf(" (invalid; expected '");
            print_hex(real_hash, HASH_LEN, true);
            printf("')\n");
        }
        free(real_hash);

        printf("Num_accounts: %d (", acc_hdr->num_accounts);
        if ((fsize - sizeof(AccountHeader)) % sizeof(Account)) {
            printf("invalid)\nRemaining part of file is corrupted/truncated;\n"
                   "use 'inspect %s --hexdump' to see raw bytes\n",
                   path);
            free(data_buf);
            return 0;
        }

        int num_data_accounts = (fsize - sizeof(AccountHeader)) / sizeof(Account);
        if ((int)acc_hdr->num_accounts == num_data_accounts)
            printf("ok)\n\n");
        else
            printf("invalid, found %d)\n\n", num_data_accounts);

        for (int i = 0; i < num_data_accounts; i++) {
            printf("Username hash for account %d: ", i + 1);
            print_hex(data_buf + sizeof(AccountHeader) + i * sizeof(Account), HASH_LEN, true);
            printf("\n");
        }

        printf("\n");

        free(data_buf);

        return 0;
    }

    // handle .pwmngr format

    util_log(DEBUG, "Handling file as .pwmngr format");

    if (memcmp(data_buf, VAULT_MAGIC, 6)) {
        util_log(ERROR, "Unrecognized magic bytes!");
        free(data_buf);
        return -4;
    }

    if (fsize < (int)sizeof(VaultHeader)) {
        util_log(ERROR, "File is too small to hold vault header!");
        free(data_buf);
        return -5;
    }

    VaultHeader *vlt_hdr = (VaultHeader *)data_buf;

    if (hexdump) {

        printf("Header:\n");
        print_hex((uint8_t *)vlt_hdr, sizeof(VaultHeader), false);
        printf("\n\n");

        printf("Magic: ");
        print_hex((uint8_t *)vlt_hdr, sizeof(vlt_hdr->magic), false);
        printf("\n");

        printf("Schema version: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic), sizeof(vlt_hdr->version), false);
        printf("\n");

        printf("Hash: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version), sizeof(vlt_hdr->hash), false);
        printf("\n");

        printf("Timestamp: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash),
                  sizeof(vlt_hdr->timestamp), false);
        printf("\n");

        printf("Num_entries: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash) +
                      sizeof(vlt_hdr->timestamp),
                  sizeof(vlt_hdr->num_entries), false);
        printf("\n");

        printf("Ciphertext length: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash) +
                      sizeof(vlt_hdr->timestamp) + sizeof(vlt_hdr->num_entries),
                  sizeof(vlt_hdr->ciphertext_len), false);
        printf("\n");

        printf("Salt: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash) +
                      sizeof(vlt_hdr->timestamp) + sizeof(vlt_hdr->num_entries) + sizeof(vlt_hdr->ciphertext_len),
                  sizeof(vlt_hdr->salt), false);
        printf("\n");

        printf("Nonce: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash) +
                      sizeof(vlt_hdr->timestamp) + sizeof(vlt_hdr->num_entries) + sizeof(vlt_hdr->ciphertext_len) +
                      sizeof(vlt_hdr->salt),
                  sizeof(vlt_hdr->nonce), false);
        printf("\n");

        printf("Tag: ");
        print_hex((uint8_t *)vlt_hdr + sizeof(vlt_hdr->magic) + sizeof(vlt_hdr->version) + sizeof(vlt_hdr->hash) +
                      sizeof(vlt_hdr->timestamp) + sizeof(vlt_hdr->num_entries) + sizeof(vlt_hdr->ciphertext_len) +
                      sizeof(vlt_hdr->salt) + sizeof(vlt_hdr->nonce),
                  sizeof(vlt_hdr->tag), false);
        printf("\n\n");

        printf("Ciphertext:\n");
        print_hex(data_buf + sizeof(VaultHeader), fsize - sizeof(VaultHeader), false);
        printf("\n\n");

        free(data_buf);

        return 0;
    }

    // regular dump

    printf("\n");
    printf("Magic: %s (ok)\n", vlt_hdr->magic);
    printf("Schema version: %u (", vlt_hdr->version);
    if (vlt_hdr->version == VAULT_SCHEMA_VERSION)
        printf("ok)\n");
    else
        printf("invalid, expected %d)\n", VAULT_SCHEMA_VERSION);

    printf("Hash: ");
    print_hex(vlt_hdr->hash, HASH_LEN, true);
    uint8_t *real_hash = sha_256_hash(data_buf + 10 + HASH_LEN, fsize - 10 - HASH_LEN);
    if (!util_check_ptr(real_hash, "Failed to hash vault data")) {
        free(data_buf);
        return -3;
    }
    if (!memcmp(vlt_hdr->hash, real_hash, HASH_LEN))
        printf(" (ok)\n");
    else {
        printf(" (invalid; expected '");
        print_hex(real_hash, HASH_LEN, true);
        printf("')\n");
    }
    free(real_hash);

    time_t     timestmp = (time_t)vlt_hdr->timestamp;
    struct tm *timest   = localtime(&timestmp);
    char       timestr[20];
    strftime(timestr, sizeof(timestr), "%m-%d-%Y %H-%M-%S", timest);
    printf("Timestamp: %lu (%s)\n", vlt_hdr->timestamp, timestr);

    printf("Num_entries: %u\n", vlt_hdr->num_entries);
    printf("Ciphertext length: %u (", vlt_hdr->ciphertext_len);
    if (fsize - (int64_t)sizeof(VaultHeader) == vlt_hdr->ciphertext_len)
        printf("ok)\n");
    else
        printf("invalid; found %ld bytes of ciphertext)\n", fsize - (int64_t)sizeof(VaultHeader));

    char *b64salt  = g_base64_encode(vlt_hdr->salt, SALT_LEN);
    char *b64nonce = g_base64_encode(vlt_hdr->nonce, NONCE_LEN);
    char *b64tag   = g_base64_encode(vlt_hdr->tag, TAG_LEN);

    if (!b64salt || !b64nonce || !b64tag) {
        util_log(ERROR, "Failed to convert salt/nonce/tag to base64");
        if (b64salt)
            g_free(b64salt);
        if (b64nonce)
            g_free(b64nonce);
        if (b64tag)
            g_free(b64tag);
        free(data_buf);
        return -6;
    }

    printf("Salt: %s\n", b64salt);
    printf("Nonce: %s\n", b64nonce);
    printf("Tag: %s\n\n", b64tag);

    g_free(b64salt);
    g_free(b64nonce);
    g_free(b64tag);

    // do something with ciphertext

    free(data_buf);

    return 0;
}

static int run_random(int bytes, bool base64) {

    if (bytes <= 0) {
        printf("Invalid number of bytes!\n");
        return -1;
    }

    uint8_t *rand = ec_malloc(bytes);
    randombytes_buf(rand, bytes);

    printf("Generated %d random bytes", bytes);

    if (base64) {
        char *b64 = g_base64_encode(rand, bytes);
        printf(" encoded as base64:\n\n%s\n\n", b64);
        g_free(b64);
        return 0;
    }

    printf("\n\n");

    print_hex(rand, bytes, false);

    printf("\n\n");

    return 0;
}

static int run_mkkey(char *passwd, char *salt, bool hex_out) {

    char *owned_passwd = passwd ? strdup(passwd) : NULL;
    char *owned_salt   = salt ? strdup(salt) : NULL;

    if (!passwd) {
        char *buf = ec_calloc(MAX_PASSWD_LEN + 1, sizeof(char));
        printf("Enter password for the key (will not be echoed): ");
        if (!read_password(buf, MAX_PASSWD_LEN + 1)) {
            util_log(ERROR, "Failed to read password from stdin");
            return -1;
        }
        printf("\n");
        owned_passwd = buf;
    }

    if (!salt) {
        char *buf = ec_calloc(26, sizeof(char));
        printf("Enter 16-byte base64 salt: ");
        if (!fgets(buf, 26, stdin)) {
            util_log(ERROR, "Failed to read salt from stdin");
            return -1;
        }
        owned_salt = buf;
    }

    if (!owned_passwd[0] || !owned_salt[0]) {
        util_log(ERROR, "Password or salt is NULL!");
        free(owned_passwd);
        free(owned_salt);
        return -6;
    }

    gsize    salt_len;
    uint8_t *raw_salt = g_base64_decode(owned_salt, &salt_len);
    free(owned_salt);
    if (!raw_salt) {
        util_log(ERROR, "Failed to decode base64 salt!");
        free(owned_passwd);
        return -2;
    }

    if (salt_len != 16) {
        util_log(ERROR, "Invalid salt length!");
        g_free(raw_salt);
        free(owned_passwd);
        return -3;
    }

    uint8_t key[32];
    if (!derive_vault_key(owned_passwd, raw_salt, key, 32)) {
        util_log(ERROR, "Failed to derive vault key from provided information!");
        free(owned_passwd);
        g_free(raw_salt);
        return -4;
    }

    free(owned_passwd);
    g_free(raw_salt);

    printf("Generated new key from provided password and salt:\n\n");

    if (hex_out) {
        for (int i = 0; i < 32; i++) {
            printf("%02x", key[i]);

            if (i < 31)
                printf(" ");
        }

        printf("\n\n");

        return 0;
    }

    char *b64 = g_base64_encode(key, 32);
    if (!b64) {
        util_log(ERROR, "Failed to encode key as base64!");
        return -5;
    }

    printf("%s\n\n", b64);

    g_free(b64);

    return 0;
}

static int run_hash(char *text, char *hex, bool hex_out, bool hex_out_no_space) {

    uint8_t *hash = NULL;

    if (text)
        hash = sha_256_hash((uint8_t *)text, strlen(text));
    else if (hex) {
        int      raw_len = 0;
        uint8_t *raw     = hex_to_raw(hex, &raw_len);
        if (!raw) {
            util_log(ERROR, "Failed to convert hex to raw bytes!");
            return -1;
        }
        hash = sha_256_hash(raw, raw_len);
    }

    if (!hash) {
        util_log(ERROR, "Failed to hash data!");
        return -2;
    }

    printf("Successfully hashed data:\n\n");

    if (hex_out || hex_out_no_space) {
        print_hex(hash, HASH_LEN, hex_out_no_space);
        free(hash);

        printf("\n\n");

        return 0;
    }

    char *b64 = g_base64_encode(hash, HASH_LEN);
    free(hash);
    if (!b64) {
        util_log(ERROR, "Failed to encode hash as base64!");
        return -5;
    }

    printf("%s\n\n", b64);

    g_free(b64);

    return 0;
}

static int run_pwhash(char *passwd, char *salt, bool hex_out) {

    char *owned_passwd = passwd ? strdup(passwd) : NULL;
    char *owned_salt   = salt ? strdup(salt) : NULL;

    if (!passwd) {
        char *buf = ec_calloc(MAX_PASSWD_LEN + 1, sizeof(char));
        printf("Enter password to hash (will not be echoed): ");
        if (!read_password(buf, MAX_PASSWD_LEN + 1)) {
            util_log(ERROR, "Failed to read password from stdin");
            return -1;
        }
        printf("\n");
        owned_passwd = buf;
    }

    if (!salt) {
        char *buf = ec_calloc(26, sizeof(char));
        printf("Enter 16-byte base64 salt: ");
        if (!fgets(buf, 26, stdin)) {
            util_log(ERROR, "Failed to read salt from stdin");
            return -1;
        }
        owned_salt = buf;
    }

    if (!owned_passwd[0] || !owned_salt[0]) {
        util_log(ERROR, "Password or salt is NULL!");
        free(owned_passwd);
        free(owned_salt);
        return -6;
    }

    gsize    salt_len;
    uint8_t *raw_salt = g_base64_decode(owned_salt, &salt_len);
    free(owned_salt);
    if (!raw_salt) {
        util_log(ERROR, "Failed to decode base64 salt!");
        free(owned_passwd);
        return -2;
    }

    if (salt_len != 16) {
        util_log(ERROR, "Invalid salt length!");
        g_free(raw_salt);
        free(owned_passwd);
        return -3;
    }

    uint8_t pwhash[HASH_LEN + SALT_LEN];
    if (!hash_pw_with_salt(passwd, pwhash, sizeof(pwhash), raw_salt)) {
        util_log(ERROR, "Failed to derive vault key from provided information!");
        free(owned_passwd);
        g_free(raw_salt);
        return -4;
    }

    free(owned_passwd);
    g_free(raw_salt);

    printf("Successfully hashed password with provided salt:\n\n");

    if (hex_out) {
        print_hex(pwhash, HASH_LEN + SALT_LEN, false);
        printf("\n\n");

        return 0;
    }

    char *b64 = g_base64_encode(pwhash, HASH_LEN + SALT_LEN);
    if (!b64) {
        util_log(ERROR, "Failed to encode hash as base64!");
        return -5;
    }

    printf("%s\n\n", b64);

    g_free(b64);

    return 0;
}

static int run_base64encode(char *hex) {
    int      raw_len;
    uint8_t *raw = hex_to_raw(hex, &raw_len);
    if (!raw) {
        util_log(ERROR, "Failed to convert hex string to raw bytes!");
        return -1;
    }

    char *b64 = g_base64_encode(raw, raw_len);
    if (!b64) {
        util_log(ERROR, "Failed to convert raw bytes to base64!");
        free(raw);
        return -2;
    }

    printf("Successfully encoded %d hex bytes as base64\n\n%s\n\n", raw_len, b64);

    free(raw);
    g_free(b64);

    return 0;
}

static int run_base64decode(char *base64) {

    gsize    bytes = 0;
    uint8_t *raw   = g_base64_decode(base64, &bytes);

    if (!raw || !bytes) {
        printf("Failed to decode base64!\n");
        return -1;
    }

    printf("\n");
    print_hex(raw, bytes, false);
    printf("\n\n");

    return 0;
}

static int run_encrypt(char *text, char *hex, char *file, char *binfile, char *key, char *nonce, bool hex_out) {

    uint8_t *data     = NULL;
    int      data_len = 0;

    if (text) {
        data     = (uint8_t *)strdup(text);
        data_len = strlen(text);
    } else if (hex) {
        data_len = 0;

        data = hex_to_raw(hex, &data_len);
        if (!data) {
            util_log(ERROR, "Failed to convert hex to raw bytes!");
            return -1;
        }
    } else if (file) {
        FILE *fp = fopen(file, "rb");
        if (!fp) {
            util_log(ERROR, "Invalid input file path/permissions!");
            return -7;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (!fsize) {
            util_log(ERROR, "Input file has no data!");
            return -8;
        }

        uint8_t *hex_buf = ec_malloc(fsize);
        ec_fread(hex_buf, 1, fsize, fp);
        fclose(fp);

        data = hex_to_raw((char *)hex_buf, &data_len);
        free(hex_buf);
        if (!data) {
            util_log(ERROR, "Failed to convert hex to raw bytes!");
            return -1;
        }

    } else if (binfile) {
        FILE *fp = fopen(binfile, "rb");
        if (!fp) {
            util_log(ERROR, "Invalid input file path/permissions!");
            return -7;
        }

        fseek(fp, 0, SEEK_END);
        data_len = (int)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (!data_len) {
            util_log(ERROR, "Input file has no data!");
            return -8;
        }

        data = ec_malloc(data_len);
        ec_fread(data, 1, data_len, fp);
        fclose(fp);
    }

    if (!data) {
        util_log(ERROR, "No data found to encrypt! (bug)");
        return -2;
    }

    gsize    raw_key_len;
    gsize    raw_nonce_len;
    uint8_t *raw_key   = g_base64_decode(key, &raw_key_len);
    uint8_t *raw_nonce = g_base64_decode(nonce, &raw_nonce_len);

    if (!raw_key || !raw_nonce) {
        util_log(ERROR, "Failed to decode base64 key/nonce!");
        free(data);
        return -3;
    }

    if (raw_key_len != 32 || raw_nonce_len != NONCE_LEN) {
        util_log(ERROR, "Invalid length for base64 key/nonce!");
        free(data);
        return -4;
    }

    uint8_t *ciphertext = ec_malloc(data_len + 16);
    uint8_t *tag        = ec_malloc(TAG_LEN);

    int ciphertext_len = aes_gcm_encrypt(data, data_len, raw_key, raw_nonce, NONCE_LEN, ciphertext, tag);
    free(data);

    if (ciphertext_len <= 0) {
        util_log(ERROR, "Failed to encrypt data! (encryption returned length <= 0)");
        return -5;
    }

    printf("Successfully encrypted %d bytes of data into %d bytes of ciphertext:\nciphertext:\n", data_len,
           ciphertext_len);

    if (hex_out) {
        print_hex(ciphertext, ciphertext_len, false);
        printf("\ntag: ");
        print_hex(tag, TAG_LEN, false);
        printf("\n\n");

        free(ciphertext);
        free(tag);
        return 0;
    }

    char *b64ct = g_base64_encode(ciphertext, ciphertext_len);
    char *b64t  = g_base64_encode(tag, TAG_LEN);
    free(ciphertext);
    free(tag);
    if (!b64ct || !b64t) {
        util_log(ERROR, "Failed to convert raw bytes to base64!");
        return -6;
    }

    printf("%s\ntag: %s\n\n", b64ct, b64t);
    g_free(b64ct);
    g_free(b64t);

    return 0;
}

static int run_decrypt(char *b64, char *hex, char *file, char *binfile, char *key, char *nonce, char *tag,
                       bool hex_out) {

    uint8_t *ciphertext     = NULL;
    gsize    ciphertext_len = 0;

    if (b64) {
        ciphertext = g_base64_decode(b64, &ciphertext_len);
        if (!ciphertext) {
            util_log(ERROR, "Failed to decode base64 to decrypt!");
            return -1;
        }
    } else if (hex) {
        ciphertext = hex_to_raw(hex, (int *)&ciphertext_len);
        if (!ciphertext) {
            util_log(ERROR, "Failed to convert hex to raw bytes!");
            return -1;
        }
    } else if (file) {
        FILE *fp = fopen(file, "rb");
        if (!fp) {
            util_log(ERROR, "Invalid input file path/permissions!");
            return -7;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (!fsize) {
            util_log(ERROR, "Input file has no data!");
            return -8;
        }

        uint8_t *tmp_buf = ec_malloc(fsize + 1);
        ec_fread(tmp_buf, 1, fsize, fp);
        fclose(fp);
        tmp_buf[fsize] = 0;

        ciphertext = hex_to_raw((char *)tmp_buf, (int *)&ciphertext_len);

        if (!ciphertext || ciphertext_len <= 0) {
            ciphertext = g_base64_decode((char *)tmp_buf, &ciphertext_len);
            if (!ciphertext) {
                util_log(ERROR, "Failed to retrieve data from file as hex or base64!");
                return -9;
            }
        }
    } else if (binfile) {
        FILE *fp = fopen(binfile, "rb");
        if (!fp) {
            util_log(ERROR, "Invalid input file path/permissions!");
            return -7;
        }

        fseek(fp, 0, SEEK_END);
        ciphertext_len = (int)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (!ciphertext_len) {
            util_log(ERROR, "Input file has no ciphertext!");
            return -8;
        }

        ciphertext = ec_malloc(ciphertext_len);
        ec_fread(ciphertext, 1, ciphertext_len, fp);
        fclose(fp);
    }

    if (!ciphertext) {
        util_log(ERROR, "No ciphertext found to decrypt! (bug)");
        return -2;
    }

    gsize    raw_key_len;
    gsize    raw_nonce_len;
    gsize    raw_tag_len;
    uint8_t *raw_key   = g_base64_decode(key, &raw_key_len);
    uint8_t *raw_nonce = g_base64_decode(nonce, &raw_nonce_len);
    uint8_t *raw_tag   = g_base64_decode(tag, &raw_tag_len);

    if (!raw_key || !raw_nonce || !raw_tag) {
        util_log(ERROR, "Failed to decode base64 key/nonce/tag!");
        free(ciphertext);
        return -3;
    }

    if (raw_key_len != 32 || raw_nonce_len != NONCE_LEN || raw_tag_len != TAG_LEN) {
        util_log(ERROR, "Invalid length for base64 key/nonce/tag!");
        free(ciphertext);
        return -4;
    }

    uint8_t *plaintext = ec_malloc(ciphertext_len + 1);
    int plaintext_len  = aes_gcm_decrypt(ciphertext, ciphertext_len, raw_key, raw_nonce, NONCE_LEN, raw_tag, plaintext);

    if (plaintext_len <= 0) {
        util_log(ERROR, "Failed to decrypt ciphertext! (decryption returned length <= 0)");
        return -5;
    }

    plaintext[plaintext_len] = '\0';
    free(ciphertext);

    printf("Successfully decrypted %d bytes of ciphertext into %d bytes of data:\n\n", (int)ciphertext_len,
           plaintext_len);

    if (hex_out) {
        print_hex(plaintext, plaintext_len, false);
        free(plaintext);

        printf("\n\n");
        return 0;
    }

    printf("%s\n\n", (char *)plaintext);

    free(plaintext);
    return 0;
}

static int run_help(char *cmd) {

    if (!cmd) { // print default help
        printf("\nPassword Manager CLI - help menu\n"
               "Try 'help [cmd]' for more detailed output about a specific command\n"
               "'exit' and 'clear' are special commands handled directly by the shell\n\n"
               "Command list: ('<>' denotes required argument, '[]' denotes optional argument)\n");

        for (int i = 0; i < num_cmds; i++) {
            printf("%2d: %s", i + 1, commands[i].name);

            for (int j = 0; j < commands[i].num_args; j++) {
                Argument *a = &commands[i].args[j];

                switch (a->type) {
                case STRING:
                case NUMBER:
                    if (a->excl_group)
                        printf(" <%s>", a->name);
                    else
                        printf(" [%s]", a->name);
                    break;

                case KEYVAL:
                    if (a->excl_group)
                        printf(" <%s=val>", a->name);
                    else
                        printf(" [%s=val]", a->name);
                    break;

                case FLAG:
                    printf(" --%s", a->name);
                    break;

                case FLAGWVAL:
                    printf(" --%s val", a->name);
                    break;

                default:
                    util_log(FATAL, "Invalid arg type in run_help (bug)");
                    return -1;
                }
            }
            printf("\n");
        }

        printf("\n");

        return 0;
    }

    if (!strcmp(cmd, "exit")) {
        printf("Help entry for 'exit':\n\n"
               "Break out of the command-processing loop\n"
               "  and gracefully terminate the app\n\n");
        return 0;
    }

    if (!strcmp(cmd, "clear")) {
        printf("Help entry for 'clear':\n\n"
               "Clear the terminal\n\n");
        return 0;
    }

    Command *target_cmd = NULL;

    bool found;
    for (int i = 0; i < num_cmds; i++) {

        found = false;

        if (!strcmp(commands[i].name, cmd)) {
            target_cmd = &commands[i];
            break;
        }

        for (int j = 0; j < commands[i].num_aliases; j++)
            if (!strcmp(commands[i].aliases[j], cmd)) {
                found      = true;
                target_cmd = &commands[i];
                break;
            }

        if (found)
            break;
    }

    if (!target_cmd) {
        util_log(ERROR, "Failed to show command-specific help: unrecognized name");
        return -2;
    }

    printf("\nHelp entry for '%s'\n\n", cmd);

    printf("%s", target_cmd->name);

    for (int i = 0; i < target_cmd->num_args; i++) {
        Argument *a = &target_cmd->args[i];

        switch (a->type) {
        case STRING:
        case NUMBER:
            if (a->excl_group)
                printf(" <%s>", a->name);
            else
                printf(" [%s]", a->name);
            break;

        case KEYVAL:
            if (a->excl_group)
                printf(" <%s=val>", a->name);
            else
                printf(" [%s=val]", a->name);
            break;

        case FLAG:
            printf(" --%s", a->name);
            break;

        case FLAGWVAL:
            printf(" --%s val", a->name);
            break;

        default:
            util_log(FATAL, "Invalid arg type in run_help (bug)");
            return -1;
        }
    }

    printf("\n\n");

    if (!target_cmd->help_msg)
        printf("Command has no command-specific help message\n\n");
    else
        printf("%s\n\n", target_cmd->help_msg);

    return 0;
}

/*
 * non-pointer args for run_cmd() functions are handled as '<type>_nullables[index]';
 * defaults to false (bool) or -1 (int) if args[index].value = NULL
 */

static int handle_cmd(Command *cmd) {

    bool bool_nullables[10];
    int  int_nullables[10];

    switch (cmd->id) {
    case LOGIN_CMD:
        return run_login((char *)cmd->args[0].value);

    case LOGOUT_CMD:
        return run_logout();

    case PORT_CMD:
        return run_port((char *)cmd->args[0].value);

    case MKACCOUNT_CMD:
        bool_nullables[2] = cmd->args[2].value ? *(bool *)cmd->args[2].value : false;
        return run_mkaccount((char *)cmd->args[0].value, (char *)cmd->args[1].value, bool_nullables[2]);

    case RMACCOUNT_CMD:
        bool_nullables[0] = cmd->args[0].value ? *(bool *)cmd->args[0].value : false;
        return run_rmaccount(bool_nullables[0]);

    case CHANGEPASSWORD_CMD:
        return run_changepassword();

    case CHANGEUSERNAME_CMD:
        return run_changeusername((char *)cmd->args[0].value);

    case SEARCH_CMD:
        bool_nullables[1] = cmd->args[1].value ? *(bool *)cmd->args[1].value : false;
        return run_search((char *)cmd->args[0].value, bool_nullables[1]);

    case VIEWENTRY_CMD:
        int_nullables[1] = cmd->args[1].value ? *(int *)cmd->args[1].value : -1;
        return run_viewentry((char *)cmd->args[0].value, int_nullables[1]);

    case DUMPENTRIES_CMD:
        bool_nullables[1] = cmd->args[1].value ? *(bool *)cmd->args[1].value : false;
        return run_dumpentries((char *)cmd->args[0].value, bool_nullables[1]);

    case MKENTRY_CMD:
        bool_nullables[4] = cmd->args[4].value ? *(bool *)cmd->args[4].value : false;
        return run_mkentry((char *)cmd->args[0].value, (char *)cmd->args[1].value, (char *)cmd->args[2].value,
                           (char *)cmd->args[3].value, bool_nullables[4]);

    case RMENTRY_CMD:
        int_nullables[1]  = cmd->args[1].value ? *(int *)cmd->args[1].value : -1;
        bool_nullables[2] = cmd->args[2].value ? *(bool *)cmd->args[2].value : false;
        return run_rmentry((char *)cmd->args[0].value, int_nullables[1], bool_nullables[2]);

    case IMPORT_CMD:
        int_nullables[2] = cmd->args[2].value ? *(int *)cmd->args[2].value : 2;
        return run_import((char *)cmd->args[0].value, (char *)cmd->args[1].value, int_nullables[2]);

    case EXPORT_CMD:
        return run_export((char *)cmd->args[0].value, (char *)cmd->args[1].value);

    case INSPECT_CMD:
        bool_nullables[1] = cmd->args[1].value ? *(bool *)cmd->args[1].value : false;
        return run_inspect((char *)cmd->args[0].value, bool_nullables[1]);

    case RANDOM_CMD:
        int_nullables[0]  = cmd->args[0].value ? *(int *)cmd->args[0].value : -1;
        bool_nullables[1] = cmd->args[1].value ? *(bool *)cmd->args[1].value : false;
        return run_random(int_nullables[0], bool_nullables[1]);

    case MKKEY_CMD:
        bool_nullables[2] = cmd->args[2].value ? *(bool *)cmd->args[2].value : false;
        return run_mkkey((char *)cmd->args[0].value, (char *)cmd->args[1].value, bool_nullables[2]);

    case HASH_CMD:
        bool_nullables[2] = cmd->args[2].value ? *(bool *)cmd->args[2].value : false;
        bool_nullables[3] = cmd->args[3].value ? *(bool *)cmd->args[3].value : false;
        return run_hash((char *)cmd->args[0].value, (char *)cmd->args[1].value, bool_nullables[2], bool_nullables[3]);

    case PWHASH_CMD:
        bool_nullables[2] = cmd->args[2].value ? *(bool *)cmd->args[2].value : false;
        return run_pwhash((char *)cmd->args[0].value, (char *)cmd->args[1].value, bool_nullables[2]);

    case BASE64ENCODE_CMD:
        return run_base64encode((char *)cmd->args[0].value);

    case BASE64DECODE_CMD:
        return run_base64decode((char *)cmd->args[0].value);

    case ENCRYPT_CMD:
        bool_nullables[6] = cmd->args[6].value ? *(bool *)cmd->args[6].value : false;
        return run_encrypt((char *)cmd->args[0].value, (char *)cmd->args[1].value, (char *)cmd->args[2].value,
                           (char *)cmd->args[3].value, (char *)cmd->args[4].value, (char *)cmd->args[5].value,
                           bool_nullables[6]);

    case DECRYPT_CMD:
        bool_nullables[7] = cmd->args[7].value ? *(bool *)cmd->args[7].value : false;
        return run_decrypt((char *)cmd->args[0].value, (char *)cmd->args[1].value, (char *)cmd->args[2].value,
                           (char *)cmd->args[3].value, (char *)cmd->args[4].value, (char *)cmd->args[5].value,
                           (char *)cmd->args[6].value, bool_nullables[7]);

    case HELP_CMD:
        return run_help((char *)cmd->args[0].value);

    default:
        util_log(ERROR, "Invalid command id passed to handle_cmd!");
        return -1;
    }
}

// TODO: clean up tokenization logic
static Command *parse_cmd(char *cmd_buf) {

    char **words        = ec_calloc(5, sizeof(char *)); // can be grown if necessary
    int    num_words    = 5;
    int    current_word = 0;
    char  *word_buf     = ec_malloc(256);
    int    idx          = 0;
    bool   quote_open   = false;

    char c;
    for (int cmd_pos = 0; (c = cmd_buf[cmd_pos]) != '\0'; cmd_pos++) {
        if (c != ' ') {
            if (c == '"') {
                if (!cmd_pos)
                    return NULL; // if this is the first char, just return; it can't be a valid command anyway

                quote_open = !quote_open;
                continue;
            }

            if (c == '\\') {

                if (cmd_buf[++cmd_pos] == '\0')
                    break;

                switch (cmd_buf[cmd_pos]) {
                case '\\':
                    c = '\\';
                    break;

                case 'n':
                    c = '\n';
                    break;

                case 't':
                    c = '\t';
                    break;

                case ' ':
                    c = ' ';
                    break;

                case '\'': // escaping ' is supported but not required;
                    c = '\'';
                    break;

                case '"':
                    c = '"';
                    break;

                default:
                    util_log(ERROR, "Failed to parse command: invalid escape sequence");
                    return NULL;
                }

                // let normal write take care of the char decided on
            }

            word_buf[idx] = c;
            idx++;
        } else {
            if (!cmd_pos || cmd_buf[cmd_pos - 1] == ' ') // skip beginning spaces and multiple spaces
                continue;

            if (quote_open) { // allow spaces
                word_buf[idx] = c;
                idx++;
                continue;
            }

            word_buf[idx] = 0;
            idx           = 0;

            if (current_word == num_words) // more space needed
                words = ec_realloc(words, sizeof(char *) * (num_words += 3));

            words[current_word++] = strdup(word_buf);
        }
    }

    if (quote_open) {
        util_log(ERROR, "Failed to parse command: unclosed quote");
        return NULL;
    }

    if (idx > 0) {
        word_buf[idx] = 0;
        if (current_word == num_words)
            words = ec_realloc(words, sizeof(char *) * ++num_words);
        words[current_word++] = strdup(word_buf);
    }

    if (!words[0]) {
        util_log(ERROR, "No command found in token buffer (bug)");
        return NULL;
    }

    Command *target_cmd = NULL;
    bool     found      = false;
    for (int i = 0; i < num_cmds; i++) {
        if (!strcmp(commands[i].name, words[0])) {
            target_cmd = &commands[i];
            break;
        }

        for (int j = 0; j < commands[i].num_aliases; j++) {
            if (!strcmp(commands[i].aliases[j], words[0])) {
                target_cmd = &commands[i];
                found      = true;
                break;
            }
        }

        if (found)
            break;
    }

    if (!target_cmd)
        return NULL; // invalid command

    // Reset all argument values
    for (int i = 0; i < target_cmd->num_args; i++) {
        Argument *a = &target_cmd->args[i];
        if (a->value) {
            free(a->value);
            a->value = NULL;
        }
    }

    // proccess tokens
    for (int w = 1; w < current_word; w++) {
        char *tok = words[w];

        // KEYVAL
        char *eq = strchr(tok, '=');
        if (eq) {
            int sep = eq - tok;

            char *key = strndup(tok, sep);

            bool      is_known    = false;
            Argument *matched_arg = NULL;

            for (int i = 0; i < target_cmd->num_args; i++) {
                Argument *a = &target_cmd->args[i];
                if (a->type == KEYVAL && strcmp(a->name, key) == 0) {
                    is_known    = true;
                    matched_arg = a;
                    break;
                }
            }

            if (!is_known) { // could be base64, so don't warn
                util_log(DEBUG, "Skipping unknown key '%s'", key);
                free(key);
            } else {
                char *val = strdup(tok + sep + 1);

                if (matched_arg->value)
                    free(matched_arg->value);

                matched_arg->value = val;

                free(key);
                continue;
            }
        }

        // FLAG
        if (tok[0] == '-') {
            bool matched = false;

            for (int i = 0; i < target_cmd->num_args; i++) {
                Argument *a = &target_cmd->args[i];
                if (a->type != FLAG)
                    continue;

                char flagbuf[128];
                snprintf(flagbuf, sizeof(flagbuf), "--%s", a->name);

                if (strcmp(tok, flagbuf) == 0) {
                    a->value          = ec_calloc(1, sizeof(bool));
                    *(bool *)a->value = true;
                    matched           = true;
                    break;
                }
            }

            if (!matched) {
                util_log(ERROR, "Unknown flag '%s' for command '%s'", tok, target_cmd->name);
                goto invalid_args;
            }

            continue;
        }

        // STRING/NUMBER
        Argument *posarg = NULL;

        if (is_number(tok)) {
            for (int i = 0; i < target_cmd->num_args; i++) {
                Argument *a = &target_cmd->args[i];
                if (a->type == NUMBER && a->value == NULL) {
                    posarg = a;
                    break;
                }
            }
        }

        if (!posarg) {
            for (int i = 0; i < target_cmd->num_args; i++) {
                Argument *a = &target_cmd->args[i];
                if (a->type == STRING && a->value == NULL) {
                    posarg = a;
                    break;
                }
            }
        }

        if (!posarg) {
            util_log(ERROR, "Too many positional arguments");
            goto invalid_args;
        }

        if (posarg->type == NUMBER) {
            posarg->value         = ec_calloc(1, sizeof(int));
            *(int *)posarg->value = atoi(tok);
        } else
            posarg->value = strdup(tok);
    }

    int max_group = 0;
    for (int i = 0; i < target_cmd->num_args; i++)
        if (target_cmd->args[i].excl_group > 0)
            max_group = MAX(max_group, target_cmd->args[i].excl_group);

    int *used = ec_calloc(max_group + 1, sizeof(int));

    for (int i = 0; i < target_cmd->num_args; i++) {
        Argument *a = &target_cmd->args[i];
        if (a->excl_group > 0 && a->value != NULL)
            used[a->excl_group]++;
    }

    for (int g = 1; g <= max_group; g++) {
        if (used[g] == 0) {
            util_log(ERROR, "Missing required argument from exclusion group %d", g);
            free(used);
            goto invalid_args;
        }

        if (used[g] > 1) {
            util_log(ERROR, "Multiple arguments provided from exclusion group %d", g);
            free(used);
            goto invalid_args;
        }
    }

    free(used);
    return target_cmd;

invalid_args: // return empty cmd to signal invalid args (to suppress 'unrecognized command' warning)
    printf("Invalid/unrecognized argument for command '%s'; try 'help %s' for usage\n", target_cmd->name,
           target_cmd->name);
    return ec_calloc(1, sizeof(Command));
}

int cli_run_shell() {

    printf("\nWelcome to the (very creatively named) Password\n"
           "Manager CLI! Type 'help' for a list of commands.\n\n");

    while (1) {
        char *cmd_buf = linenoise(cmd_prompt);

        // ctrl-d/c
        if (!cmd_buf)
            break;

        if (strlen(cmd_buf) > 2000) {
            util_log(WARN, "Input over 2000 characters long; ignoring");
            continue;
        }

        if (!strlen(cmd_buf)) // empty buffer
            continue;

        linenoiseHistoryAdd(cmd_buf);
        linenoiseHistorySave(histfile);

        if (!strcmp(cmd_buf, "exit")) {
            util_log(INFO, "exit command called; exiting...");
            break;
        }

        if (!strcmp(cmd_buf, "clear")) {
            linenoiseClearScreen();
            free(cmd_buf);
            continue;
        }

        Command *cmd = parse_cmd(cmd_buf);
        free(cmd_buf);

        if (!cmd) {
            printf("Unrecognized command; try 'help' for a list of commands\n");
            continue;
        }

        if (!cmd->name) { // parser returns empty cmd on invalid args but NULL on unrecognized cmd
            free(cmd);    // ec_calloc'd only as a flag; safe to free
            continue;
        }

        int retval = handle_cmd(cmd);

        if (retval)
            printf("Command returned non-zero exit value: %d\n", retval);
    }

    return 0;
}

static void register_help_info(char *cmd_name, char *help_msg) {
    for (int i = 0; i < num_cmds; i++)
        if (!strcmp(commands[i].name, cmd_name)) {
            if (commands[i].help_msg) {
                util_log(ERROR, "Command %s already has help info registered! (bug)", commands[i].name);
                return;
            }
            commands[i].help_msg = strdup(help_msg);
            return;
        }

    util_log(ERROR, "Failed to fetch command to register help message!");
}

static void register_arg(char *cmd_name, char *arg_name, ArgumentType arg_type, int excl_group) {

    Command *cmd = NULL;
    for (int i = 0; i < num_cmds; i++)
        if (!strcmp(commands[i].name, cmd_name)) {
            cmd = commands + i;
            break;
        }

    if (!util_check_ptr(cmd, "Failed to fetch command to register arg!"))
        return;

    Argument *arg   = ec_malloc(sizeof(Argument));
    arg->name       = strdup(arg_name);
    arg->type       = arg_type;
    arg->excl_group = excl_group;
    arg->value      = NULL;

    cmd->num_args++;
    if (!cmd->args)
        cmd->args = ec_malloc(sizeof(Argument));
    else
        cmd->args = ec_realloc(cmd->args, sizeof(Argument) * cmd->num_args);

    memcpy(&cmd->args[cmd->num_args - 1], arg, sizeof(Argument));
    free(arg);
}

static void register_alias(char *cmd_name, char *alias) {

    Command *cmd = NULL;
    for (int i = 0; i < num_cmds; i++) {
        if (!strcmp(commands[i].name, alias)) {
            util_log(FATAL, "Invalid command alias registered! (duplicate of existing command)");
            return;
        }

        if (!strcmp(commands[i].name, cmd_name))
            cmd = commands + i;

        for (int j = 0; j < commands[i].num_aliases; j++) {
            if (!strcmp(commands[i].aliases[j], alias)) {
                util_log(FATAL, "Invalid command alias registered! (duplicate of existing alias)");
                return;
            }
        }
    }

    if (!cmd) {
        util_log(FATAL, "Failed to fetch command to add alias (bug)");
        return;
    }

    if (!cmd->aliases)
        cmd->aliases = ec_malloc(sizeof(char *));
    else
        cmd->aliases = ec_realloc(cmd->aliases, sizeof(char *) * (cmd->num_aliases + 1));

    cmd->aliases[cmd->num_aliases] = strdup(alias);
    cmd->num_aliases++;
}

static void register_cmd(char *name) {

    for (int i = 0; i < num_cmds; i++)
        if (!strcmp(commands[i].name, name)) {
            util_log(FATAL, "Duplicate command registered! (bug)");
            return;
        }

    if (!num_cmds)
        commands = ec_malloc(sizeof(Command) * ++num_cmds);
    else
        commands = ec_realloc(commands, sizeof(Command) * ++num_cmds);

    commands[num_cmds - 1].id          = num_cmds - 1;
    commands[num_cmds - 1].name        = strdup(name);
    commands[num_cmds - 1].num_aliases = 0;
    commands[num_cmds - 1].aliases     = NULL;
    commands[num_cmds - 1].num_args    = 0;
    commands[num_cmds - 1].args        = NULL;
    commands[num_cmds - 1].help_msg    = NULL;
}

// TODO: full rewrite of my sloppy file completion code (hey, it works, what can I say?)
static void completion_cb(const char *token, const char *full, linenoiseCompletions *lc) {
    char tmp[2048];
    strncpy(tmp, full, sizeof(tmp));
    tmp[sizeof(tmp) - 1] = '\0';

    char *first = tmp;
    char *space = strchr(tmp, ' ');
    if (space)
        *space = '\0';
    char *last_space = strrchr(full, ' ');

    // complete cmd
    if (!last_space || !strcmp(first, "help")) {

        if (!strncmp("exit", token, strlen(token)))
            linenoiseAddCompletion(lc, "exit");
        else if (!strncmp("clear", token, strlen(token)))
            linenoiseAddCompletion(lc, "clear");

        for (int i = 0; i < num_cmds; i++) {
            if (!strncmp(commands[i].name, token, strlen(token)))
                linenoiseAddCompletion(lc, commands[i].name);
            for (int j = 0; j < commands[i].num_aliases; j++)
                if (!strncmp(commands[i].aliases[j], token, strlen(token)))
                    linenoiseAddCompletion(lc, commands[i].aliases[j]);
        }
        return;
    }

    // get cmd
    Command *cmd = NULL;
    for (int i = 0; i < num_cmds; i++) {
        if (!strcmp(commands[i].name, first)) {
            cmd = &commands[i];
            break;
        }
        bool break2 = false;
        for (int j = 0; j < commands[i].num_aliases; j++) {
            if (!strcmp(commands[i].aliases[j], first)) {
                cmd    = &commands[i];
                break2 = true;
                break;
            }
        }
        if (break2)
            break;
    }

    if (!cmd)
        return;

    // complete keys & flags
    bool check_files = false;

    // flags
    if (token[0] == '-') {
        for (int i = 0; i < cmd->num_args; i++) {
            if (cmd->args[i].type == FLAG) {
                char flag_comp[256];
                sprintf(flag_comp, "--%s ", cmd->args[i].name);
                if (!strncmp(flag_comp, token, strlen(token)))
                    linenoiseAddCompletion(lc, flag_comp);
            }
        }
        return;
    }

    // keyval
    char *eq = strchr(token, '=');

    if (eq)
        check_files = true;
    else {

        for (int i = 0; i < cmd->num_args; i++) {
            if (cmd->args[i].type == KEYVAL) {
                if (!strncmp(cmd->args[i].name, token, strlen(token)))
                    linenoiseAddCompletion(lc, cmd->args[i].name);
            }
        }

        check_files = true;
    }

    if (!check_files)
        return;

    // file paths
    DIR *d;

    char *dir_path_s1   = strrchr(token, '=') ? strdup(strrchr(token, '=') + 1) : strdup((char *)token);
    char *keyval_prefix = strrchr(token, '=') && strlen(dir_path_s1) ? strdup(token) : NULL;
    if (keyval_prefix)
        *(strrchr(keyval_prefix, '=') + 1) = 0;
    char *dir_path       = NULL;
    bool  preserve_root  = false;
    bool  check_preserve = false;
#ifdef _WIN32
    if (!(strlen(dir_path_s1) >= 3 && is_alpha(dir_path_s1[0]) && !strncmp(dir_path_s1 + 1, ":\\", 2))) {
        dir_path = ec_malloc(strlen(dir_path_s1) + 2);
        sprintf(dir_path, ".\\%s", dir_path_s1);
#else
    if (!(strlen(dir_path_s1) && dir_path_s1[0] == '/')) {
        dir_path = ec_malloc(strlen(dir_path_s1) + 2);
        sprintf(dir_path, "./%s", dir_path_s1);
#endif
    } else { // absolute path
        dir_path       = dir_path_s1;
        check_preserve = true;
    }

    char *last_slash;
#ifdef _WIN32
    last_slash = strrchr(dir_path, '\\');
#else
    last_slash = strrchr(dir_path, '/');
#endif
    char *complete_path = last_slash + 1;
    if (last_slash)
        *last_slash = 0;

    if (!strlen(dir_path) && last_slash) {
        if (check_preserve)
            preserve_root = true;
#ifdef _WIN32
        d = opendir("C:\\");
#else
        d = opendir("/");
#endif
    } else
        d = opendir(dir_path);

    if (!d) {
        free(dir_path_s1);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;
        if (!strncmp(ent->d_name, complete_path, strlen(complete_path))) {

            char *tmp_dpath = dir_path;
            while (strlen(tmp_dpath) >= 2 && tmp_dpath[0] == '.' && tmp_dpath[1] == PATH_SEPARATOR)
                tmp_dpath += 2;
            if (!strcmp(tmp_dpath, "."))
                tmp_dpath[0] = 0;
            char *comp =
                ec_calloc((keyval_prefix ? strlen(keyval_prefix) : 0) + strlen(tmp_dpath) + 1 + strlen(ent->d_name) + 2,
                          sizeof(char));
            char *sep            = tmp_dpath[0] ? (PATH_SEPARATOR == '\\' ? "\\" : "/") : "";
            char *preserved_root = "";
            if (preserve_root) // root was removed
#ifdef _WIN32
                preserved_root = "C:\\";
#else
                preserved_root = "/";
#endif
            sprintf(comp, "%s%s%s%s%s", keyval_prefix ? keyval_prefix : "", preserved_root, tmp_dpath, sep,
                    ent->d_name);

            bool is_dir = false;
            if (ent->d_type == DT_DIR) {
                is_dir = true;
            } else if (ent->d_type == DT_UNKNOWN) {
                char fullpath[PATH_MAX];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, ent->d_name);
#ifdef _WIN32
                struct _stat st;
                if (_stat(fullpath, &st) == 0 && (st.st_mode & _S_IFDIR))
                    is_dir = true;
#else
                struct stat st;
                if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
                    is_dir = true;
#endif
            }
            comp[strlen(comp)] = is_dir ? PATH_SEPARATOR : ' ';

            if (strlen(comp) >= 2 && comp[0] == '.' && comp[1] == PATH_SEPARATOR)
                linenoiseAddCompletion(lc, comp + 2);
            else
                linenoiseAddCompletion(lc, comp);
            free(comp);
        }
    }
    closedir(d);

    if (keyval_prefix)
        free(keyval_prefix);
    free(dir_path_s1);
}

bool cli_init() {

    char *basedir = util_get_app_dir();
    if (util_check_ptr(basedir, "Failed to find base app directory!")) {
        histfile = ec_malloc(strlen(basedir) + strlen("passwdmngr.hist") + 1);
        sprintf(histfile, "%spasswdmngr.hist", basedir);
        free(basedir);

        FILE *fp = fopen(histfile, "a");
        if (!fp) {
            util_log(ERROR, "Failed to open/create history file!");
            return false;
        }
        fclose(fp);

        linenoiseHistorySetMaxLen(250);
        if (linenoiseHistoryLoad(histfile)) {
            util_log(FATAL, "Failed to load command history file!");
            return false;
        }
    }

    linenoiseSetCompletionCallback(completion_cb);

    cmd_prompt = strdup("passwdmngr->");

    GError *error = NULL;

    GBytes *bytes =
        g_resources_lookup_data("/com/samuelf09/passwdmngr/commands.json", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!bytes) {
        util_log(ERROR, "Failed to load commands.json: %s", error->message);
        g_clear_error(&error);
        return false;
    }

    gsize size;

    const char *data = g_bytes_get_data(bytes, &size);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, size, &error)) {
        util_log(ERROR, "Failed to parse commands.json: %s", error->message);
        g_clear_error(&error);
        g_object_unref(parser);
        g_bytes_unref(bytes);
        return false;
    }

    g_bytes_unref(bytes);

    JsonNode   *root          = json_parser_get_root(parser);
    JsonObject *root_obj      = json_node_get_object(root);
    JsonArray  *json_commands = json_object_get_array_member(root_obj, "commands");

    guint cmd_count = json_array_get_length(json_commands);

    for (guint i = 0; i < cmd_count; i++) {
        JsonObject *cmd = json_array_get_object_element(json_commands, i);

        const char *name = json_object_get_string_member(cmd, "name");
        register_cmd((char *)name);

        JsonArray *aliases = json_object_get_array_member(cmd, "aliases");
        for (guint j = 0; j < json_array_get_length(aliases); j++) {
            const char *alias = json_array_get_string_element(aliases, j);
            register_alias((char *)name, (char *)alias);
        }

        JsonArray *args = json_object_get_array_member(cmd, "args");
        for (guint j = 0; j < json_array_get_length(args); j++) {
            JsonObject *arg = json_array_get_object_element(args, j);

            const char *arg_name = json_object_get_string_member(arg, "name");

            const char *type_str = json_object_get_string_member(arg, "type");

            int excl_group = json_object_get_int_member(arg, "excl_group");

            ArgumentType type;
            if (!strcmp(type_str, "STRING"))
                type = STRING;
            else if (!strcmp(type_str, "NUMBER"))
                type = NUMBER;
            else if (!strcmp(type_str, "KEYVAL"))
                type = KEYVAL;
            else {
                util_log(WARN, "Unknown arg type '%s' for command '%s'", type_str, name);
                continue;
            }

            register_arg((char *)name, (char *)arg_name, type, excl_group);
        }

        JsonArray *flags = json_object_get_array_member(cmd, "flags");
        for (guint j = 0; j < json_array_get_length(flags); j++) {
            const char *flag = json_array_get_string_element(flags, j);
            register_arg((char *)name, (char *)flag, FLAG, 0);
        }

        char *help_text = load_help_file(name);

        if (!help_text) {
            util_log(WARN, "No help file for command '%s'", name);
        } else {
            register_help_info((char *)name, strdup(help_text));
            g_free(help_text);
        }
    }

    g_object_unref(parser);

    return true;
}
