#include <gtk/gtk.h>
#include "util.h"
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

bool util_path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool util_mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;

    snprintf(tmp, sizeof(tmp), "%s", path);

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }

    return mkdir(tmp, 0700) == 0 || errno == EEXIST;
}
